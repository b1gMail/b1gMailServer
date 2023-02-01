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
#include <imap/mailparser.h>
#include <imap/imaphelper.h>

#include <algorithm>

/*
 * Parse *(; key=value)
 */
void MailParser::ParseKeyVal(const string &strText, string &strStruct)
{
    vector<string> items;
    utils->Explode(strText, items, ';');
    for(vector<string>::const_iterator it = items.begin(); it != items.end(); ++it)
    {
        std::size_t eqPos = it->find('=');
        if(eqPos != string::npos)
        {
            string key = utils->Trim(it->substr(0, eqPos));
            string val = utils->Trim(it->substr(eqPos + 1));

            strStruct.append("\"");
            strStruct.append(IMAPHelper::Escape(key.c_str()));
            strStruct.append("\" \"");

            if(!val.empty() && val.at(0) == '"')
            {
                val.erase(val.begin());
                if(!val.empty() && val.at(val.size() - 1) == '"')
                {
                    val.erase(val.end() - 1);
                }
            }
            strStruct.append(IMAPHelper::Escape(val.c_str()));

            strStruct.append("\" ");
        }
    }

    if(strStruct.at(strStruct.length()-1) == ' ')
        strStruct.erase(strStruct.length()-1);
}

/*
 * Build body structure
 */
void MailParser::BodyStructure(bool bExtensionData, const MailStructItem &i, string &strStruct)
{
    strStruct.append("(");

    string strBodyType, strBodySubType, strBodyID, strBodyDescription, strContentDisposition,
        strContentDispositionParams, strBodyEncoding, strContentType, strContentTypeParams;

    char szBuffer[255];

    {
        const char *szTemp;

        if((szTemp = i.cHeaders.GetHeader("content-id")) != NULL)
            strBodyID = szTemp;

        if((szTemp = i.cHeaders.GetHeader("content-description")) != NULL)
            strBodyDescription = szTemp;

        if((szTemp = i.cHeaders.GetHeader("content-transfer-encoding")) != NULL)
            strBodyEncoding = szTemp;

        if((szTemp = i.cHeaders.GetHeader("content-type")) != NULL)
            strContentType = szTemp;

        if((szTemp = i.cHeaders.GetHeader("content-disposition")) != NULL)
            strContentDisposition = szTemp;
    }

    // split body type and body subtype
    if(!strContentType.empty())
    {
        std::size_t slashPos = strContentType.find('/');
        if(slashPos != string::npos)
        {
            strBodyType = strContentType.substr(0, slashPos);
            strBodySubType = strContentType.substr(slashPos + 1);
        }

        std::size_t semiPos = strBodySubType.find(';');
        if(semiPos != string::npos)
        {
            strContentTypeParams = strBodySubType.substr(semiPos + 1);
            strBodySubType = strBodySubType.substr(0, semiPos);
        }
        else
        {
            semiPos = strBodyType.find(';');
            if(semiPos != string::npos)
            {
                strContentTypeParams = strBodyType.substr(semiPos + 1);
                strBodySubType = strBodyType.substr(0, semiPos);
            }
        }
    }

    if(!strContentDisposition.empty())
    {
        std::size_t semiPos = strContentDisposition.find(';');
        if(semiPos != string::npos)
        {
            strContentDispositionParams = strContentDisposition.substr(semiPos + 1);
            strContentDisposition = strContentDisposition.substr(0, semiPos);
        }
    }

    if(i.vChildItems.size() > 0)
    {
        for(std::size_t j=0; j<i.vChildItems.size(); j++)
            MailParser::BodyStructure(bExtensionData, i.vChildItems.at(j), strStruct);

        // print body subtype
        if(strBodySubType.empty())
        {
            strStruct.append(" NIL");
        }
        else
        {
            strStruct.append(" \"");
            strStruct.append(IMAPHelper::Escape(strBodySubType.c_str()));
            strStruct.append("\"");
        }

        if(bExtensionData)
        {
            // print extension data
            if(strContentTypeParams.empty())
            {
                strStruct.append(" NIL");
            }
            else
            {
                strStruct.append(" (");
                MailParser::ParseKeyVal(strContentTypeParams, strStruct);
                strStruct.append(")");
            }

            if(strContentDisposition.empty() &&
                strContentDispositionParams.empty())
            {
                strStruct.append(" NIL");
            }
            else
            {
                strStruct.append(" (");

                if(strContentDisposition.empty())
                {
                    strStruct.append("NIL");
                }
                else
                {
                    strStruct.append("\"");
                    strStruct.append(IMAPHelper::Escape(strContentDisposition.c_str()));
                    strStruct.append("\"");
                }

                if(strContentDispositionParams.empty())
                {
                    strStruct.append(" NIL");
                }
                else
                {
                    strStruct.append(" (");
                    MailParser::ParseKeyVal(strContentDispositionParams, strStruct);
                    strStruct.append(")");
                }

                strStruct.append(")");
            }

            // body language
            strStruct.append(" NIL");

            // body location
            strStruct.append(" NIL");
        }
    }
    else
    {
        // print body type
        if(strBodyType.empty())
        {
            strStruct.append("\"TEXT\"");
        }
        else
        {
            strStruct.append("\"");
            strStruct.append(IMAPHelper::Escape(strBodyType.c_str()));
            strStruct.append("\"");
        }

        // print body subtype
        if(strBodySubType.empty())
        {
            strStruct.append(" \"PLAIN\"");
        }
        else
        {
            strStruct.append(" \"");
            strStruct.append(IMAPHelper::Escape(strBodySubType.c_str()));
            strStruct.append("\"");
        }

        // body parameters
        if(strContentTypeParams.empty())
        {
            strStruct.append(" (\"CHARSET\" \"us-ascii\")");
            //strStruct.append(" NIL");
        }
        else
        {
            strStruct.append(" (");
            MailParser::ParseKeyVal(strContentTypeParams, strStruct);
            strStruct.append(")");
        }

        // body id
        if(strBodyID.empty())
        {
            strStruct.append(" NIL");
        }
        else
        {
            strStruct.append(" \"");
            strStruct.append(IMAPHelper::Escape(strBodyID.c_str()));
            strStruct.append("\"");
        }

        // body description
        if(strBodyDescription.empty())
        {
            strStruct.append(" NIL");
        }
        else
        {
            strStruct.append(" \"");
            strStruct.append(IMAPHelper::Escape(strBodyDescription.c_str()));
            strStruct.append("\"");
        }

        // body encoding
        if(strBodyEncoding.empty())
        {
            strStruct.append(" NIL");
        }
        else
        {
            strStruct.append(" \"");
            strStruct.append(IMAPHelper::Escape(strBodyEncoding.c_str()));
            strStruct.append("\"");
        }

        // print body size
        snprintf(szBuffer, 255, " %zu", i.strText.size());
        strStruct.append(szBuffer);

        // print text size in lines
        if(strBodyType.empty() || strcasecmp(strBodyType.c_str(), "text") == 0)
        {
            std::size_t iLines = 1 + std::count(i.strText.begin(), i.strText.end(), '\n');

            // print text size in lines
            snprintf(szBuffer, 255, " %zu", iLines);
            strStruct.append(szBuffer);
        }

        // print extension data
        if(bExtensionData)
        {
            if(i.cHeaders.GetHeader("content-md5") == NULL)
            {
                strStruct.append(" NIL");       // no md5
            }
            else
            {
                strStruct.append(" \"");
                strStruct.append(IMAPHelper::Escape(i.cHeaders.GetHeader("content-md5")));
                strStruct.append("\"");
            }

            if(strContentDisposition.empty() &&
                strContentDispositionParams.empty())
            {
                strStruct.append(" NIL");
            }
            else
            {
                strStruct.append(" (");

                if(strContentDisposition.empty())
                {
                    strStruct.append("NIL");
                }
                else
                {
                    strStruct.append("\"");
                    strStruct.append(IMAPHelper::Escape(strContentDisposition.c_str()));
                    strStruct.append("\"");
                }

                if(strContentDispositionParams.empty())
                {
                    strStruct.append(" NIL");
                }
                else
                {
                    strStruct.append(" (");
                    MailParser::ParseKeyVal(strContentDispositionParams, strStruct);
                    strStruct.append(")");
                }

                strStruct.append(")");
            }

            if(i.cHeaders.GetHeader("content-language") == NULL)
            {
                strStruct.append(" NIL");       // no language
            }
            else
            {
                strStruct.append(" \"");
                strStruct.append(IMAPHelper::Escape(i.cHeaders.GetHeader("content-language")));
                strStruct.append("\"");
            }

            if(i.cHeaders.GetHeader("content-location") == NULL)
            {
                strStruct.append(" NIL");       // no location
            }
            else
            {
                strStruct.append(" \"");
                strStruct.append(IMAPHelper::Escape(i.cHeaders.GetHeader("content-location")));
                strStruct.append("\"");
            }
        }
    }

    strStruct.append(")");
}

/*
 * Parser
 */
MailStructItem MailParser::Parse(const string &strMsg)
{
    int iNr = 0;

    MailStructItem cItem;

    // root item has nr 0
    cItem.iNr = 0;

    // replace \r\n\r\n with \0
    std::size_t breakPos = strMsg.find("\r\n\r\n");
    std::size_t breakLength = 4;
    if(breakPos == string::npos)
    {
        breakPos = strMsg.find("\n\n");
        breakLength = 2;
    }

    if(breakPos == string::npos)
    {
        cItem.strText = strMsg;
        cItem.strHeader = strMsg;
        cItem.strHeader2 = "";
    }
    else
    {
        cItem.strText = strMsg.substr(breakPos + breakLength);
        cItem.strHeader = strMsg.substr(0, breakPos);
        cItem.strHeader2 = cItem.strHeader + "\r\n\r\n";
    }

    // parse headers
    vector<string> headerLines;
    utils->Explode(cItem.strHeader, headerLines, '\n');
    for (vector<string>::const_iterator it = headerLines.begin(); it != headerLines.end(); ++it)
    {
        cItem.cHeaders.Parse(it->c_str());
    }

    // multipart?
    const char *szContentType = cItem.cHeaders.GetHeader("content-type");
    if(szContentType != NULL
        && (strcasestr(szContentType, "multipart/") != NULL))
    {
        // yes, extract boundary
        char *szBoundary = mstrdup(szContentType);

        char *szBoundaryStr = strcasestr(szBoundary, "boundary=");
        if(szBoundaryStr != NULL)
        {
            szBoundaryStr = szBoundaryStr + 9;
            if(*szBoundaryStr == '"')
                szBoundaryStr++;
            char *szQuote = strchr(szBoundaryStr, '"');
            if(szQuote == NULL)
                szQuote = strchr(szBoundaryStr, ' ');
            if(szQuote == NULL)
                szQuote = strchr(szBoundaryStr, '\n');
            if(szQuote != NULL)
                *szQuote = '\0';
            if((szQuote = strchr(szBoundaryStr, ';')) != NULL)
                *szQuote = '\0';

            // ok, boundary in szBoundaryStr, split by boundary
            string strToken = string("\r\n--") + string(szBoundaryStr) + string("\r\n");
            string strFinToken = string("--") + string(szBoundaryStr) + string("--");

            char *szCopy = mstrdup((string("\r\n") + cItem.strText).c_str());

            char *pch = szCopy;
            vector<const char *> vParts;
            while((pch = strstr(pch, strToken.c_str())) != NULL)
            {
                char *t = strstr(pch, strFinToken.c_str());
                if(t != NULL)
                    *t = '\0';

                *pch++ = '\0';
                vParts.push_back(pch + strToken.size() - 1);
            }

            for(std::size_t i=0; i < vParts.size(); i++)
            {
                MailStructItem cPart = MailParser::Parse(vParts.at(i));
                cPart.iNr = ++iNr;
                cItem.vChildItems.push_back(cPart);
            }

            free(szCopy);
        }
        else
        {
            // no boundary given, threat as non-multipart message
            cItem.vChildItems.clear();
        }

        free(szBoundary);
    }
    else if(szContentType != NULL
            && strcasestr(szContentType, "message/rfc822") != NULL)
    {
        MailStructItem cItem2 = MailParser::Parse(cItem.strText);

        cItem = cItem2;
    }

    return(cItem);
}
