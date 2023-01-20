/*
 * b1gMailServer
 * Copyright (c) 2002-2022
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include <imap/imap.h>
#include <imap/mail.h>

/*
 * MailHeader constructor
 */
MailHeader::MailHeader(const string &key, const string &value, bool bDecode)
{
    this->strKey = key;
    this->strValue = value;
    this->strRawValue = value;
    this->bDecode = bDecode;
    if(bDecode)
        this->DecodeValue();
}

/*
 * Decodes a header value
 */
void MailHeader::DecodeValue()
{
    int rc, ovector[30], match_len, offset = 0;
    const char *match;
    ovector[1] = 0;

    string sdec = strValue;

    // match all
    for(;;)
    {
        offset = ovector[1];
        rc = pcre_exec(
            pHeaderPattern,             // pattern compiled in main()
            NULL,                       // no studied data
            this->strValue.c_str(),             // input
            this->strValue.size(),  // input length
            offset,                     // offset in input
            0,                          // no special options
            ovector,                    // output vector
            30                          // output vector size
        );
        if(rc < 0)
            break;
        if(rc == 0)
            rc = 10;

        string strFund;
        char cEncoding = ' ';
        string strEncoded;

        // store matches in vector
        for(int i=0; i<rc; i++)
        {
            match = this->strValue.c_str() + ovector[2*i];
            match_len = ovector[2*i+1] - ovector[2*i];

            if(i == 0)
            {
                strFund.assign(match, match_len);
            }
            else if(i == 3)
            {
                cEncoding = match[0];
            }
            else if(i == 4)
            {
                strEncoded.assign(match, match_len);
            }
        }

        string dec;
        switch(::toupper(cEncoding))
        {
        case 'Q':
            dec = IMAPHelper::QuotedPrintableDecode(strEncoded.c_str());
            break;
        case 'B': {
            char *szTemp = utils->Base64Decode(strEncoded.c_str());
            dec = szTemp;
            free(szTemp);
        } break;
        default:
            dec = strEncoded;
            break;
        }

        IMAPHelper::StringReplace(sdec, strFund.c_str(), dec.c_str());
    }

    this->strValue = sdec;
}

/*
 * Appends a value to header
 */
void MailHeader::Add(const string &str)
{
    strRawValue.append(" ");
    strRawValue.append(str);

    strValue = strRawValue;
    if(this->bDecode)
        this->DecodeValue();
}

/*
 * Extracts mail addresses from a string using pcre lib
 */
void Mail::ExtractMailAddresses(const char *in, vector<char*> *to)
{
    int rc, ovector[30], match_len, offset = 0;
    const char *match;
    char *recp;
    ovector[1] = 0;

    // match all
    for(;;)
    {
        offset = ovector[1];
        rc = pcre_exec(
            pMailPattern,               // pattern compiled in main()
            NULL,                       // no studied data
            in,                         // input
            (int)strlen(in),            // input length
            offset,                     // offset in input
            0,                          // no special options
            ovector,                    // output vector
            30                          // output vector size
        );
        if(rc < 0)
            break;
        if(rc == 0)
            rc = 10;

        // store matches in vector
        for(int i=0; i<rc; i++)
        {
            match = in + ovector[2*i];
            match_len = ovector[2*i+1] - ovector[2*i];

            recp = (char *)mmalloc(match_len+1);
            strncpy(recp, match, match_len);
            recp[match_len] = '\0';
            to->push_back(recp);
        }
    }
}

/*
 * As above, but extract only one mail address
 */
char *Mail::ExtractMailAddress(const char *in)
{
    vector<char*> res;

    {
        Mail mail;
        mail.ExtractMailAddresses(in, &res);
    }

    if(res.size() > 1)
    {
        for(int i=1; i<(int)res.size(); i++)
        {
            free(res[i]);
        }
    }

    if(res.size() == 0)
        return(NULL);

    return(res[0]);
}

/*
 * Parses a mail header line
 */
void Mail::Parse(const char *line_)
{
    string line = utils->RTrim(line_);
    std::size_t dotPos = line.find(':');
    std::size_t spacePos = line.find_first_of("\t ");

    // New field?
    if((spacePos == string::npos && dotPos != string::npos) || ((spacePos != string::npos && dotPos != string::npos) && (spacePos > dotPos)))
    {
        string key = utils->StrToLower(line.substr(0, dotPos));
        string val = utils->Trim(line.substr(dotPos + 1));

        this->lastHeaderField = key;

        // if header field already exists, append to it
        map<string, MailHeader>::iterator it = headers.find(key);
        if(it != headers.end())
        {
            it->second.Add(val);
        }
        else
        {
            headers[key] = MailHeader(key, val, this->bDecode);
        }
    }
    else
    {
        // append string to existing field
        if(this->lastHeaderField.size() > 0)
        {
            map<string, MailHeader>::iterator it = headers.find(this->lastHeaderField);
            if(it != headers.end())
            {
                it->second.Add(line);
            }
        }
    }
}

/*
 * Get a header value by key
 */
const char *Mail::GetHeader(const char *key) const
{
    map<string, MailHeader>::const_iterator it = headers.find(key);
    if(it != headers.end())
    {
        return it->second.strValue.c_str();
    }
    return(NULL);
}

/*
 * Get a raw header value by key
 */
const char *Mail::GetRawHeader(const char *key) const
{
    map<string, MailHeader>::const_iterator it = headers.find(key);
    if(it != headers.end())
    {
        return it->second.strRawValue.c_str();
    }
    return(NULL);
}

/*
 * Parse some mail info
 */
void Mail::ParseInfo()
{
    const char *tmp;

    // attachment in mail?
    if((tmp = this->GetHeader("content-type")) != NULL)
    {
        if(strcasestr(tmp, "multipart/mixed") == NULL)
        {
            this->bHasAttachment = false;
        }
        else
        {
            this->bHasAttachment = true;
        }
    }
    else
    {
        this->bHasAttachment = false;
    }

    // priority
    if((tmp = this->GetHeader("x-priority")) != NULL)
    {
        int iPriority = atoi(tmp);
        switch(iPriority)
        {
        case 5:
            this->strPriority = "low";
            break;
        case 1:
            this->strPriority = "high";
            break;
        default:
            this->strPriority = "normal";
            break;
        }
    }
    else
    {
        this->strPriority = "normal";
    }
}

/*
 * Class constructor
 */
Mail::Mail()
{
    this->bHasAttachment = false;
    this->bDecode = false;
}
