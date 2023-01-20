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

#include "utils.h"

#include <string>
#include <stdexcept>
#include <time.h>
#include <pcre.h>
#include <string.h>

using namespace std;

unsigned char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

string Base64Encode(string str)
{
    int strPos = 0, i = 0, j = 0, in_len = (int)str.length(), out_len = (int)(str.length()*2+100);
    string Result;
    unsigned char chunk[3], echunk[4];

    while(in_len-- && out_len)
    {
        chunk[i++] = str.at(strPos++);
        if(i == 3)
        {
            echunk[0] = (chunk[0] & 0xfc) >> 2;
            echunk[1] = ((chunk[0] & 0x03) << 4) + ((chunk[1] & 0xf0) >> 4);
            echunk[2] = ((chunk[1] & 0x0f) << 2) + ((chunk[2] & 0xc0) >> 6);
            echunk[3] = chunk[2] & 0x3f;

            for(i = 0; (i < 4) && out_len--; i++)
                Result.append(1, base64_table[echunk[i]]);
            i = 0;
        }
    }

    if(i)
    {
        for(j = i; j < 3; j++)
            chunk[j] = '\0';

        echunk[0] = (chunk[0] & 0xfc) >> 2;
        echunk[1] = ((chunk[0] & 0x03) << 4) + ((chunk[1] & 0xf0) >> 4);
        echunk[2] = ((chunk[1] & 0x0f) << 2) + ((chunk[2] & 0xc0) >> 6);
        echunk[3] = chunk[2] & 0x3f;

        for (j = 0; (j < i + 1) && out_len--; j++)
            Result.append(1, base64_table[echunk[j]]);

        while((i++ < 3) && out_len--)
            Result.append(1, '=');
    }

    return(Result);
}

string TrimString(string str, string TrimChars)
{
    str = str.erase(str.find_last_not_of(TrimChars)+1);
    str = str.erase(0, str.find_first_not_of(TrimChars));
    return(str);
}

string RTrimString(string str, string TrimChars)
{
    str = str.erase(str.find_last_not_of(TrimChars)+1);
    return(str);
}

string LTrimString(string str, string TrimChars)
{
    str = str.erase(0, str.find_first_not_of(TrimChars));
    return(str);
}

string StrToLower(string str)
{
    string result;

    for(unsigned int i=0; i<str.length(); i++)
        result.append(1, (char)tolower(str.at(i)));

    return(result);
}

int GetTimeZone()
{
    time_t TheTime = time(NULL), TimeA = 0, TimeB = 0;
    struct tm *TimeInfo = NULL;

    if((TimeInfo = localtime(&TheTime)) != NULL)
    {
        TimeInfo->tm_isdst = 0;
        TimeA = mktime(TimeInfo);
    }

    if((TimeInfo = gmtime(&TheTime)) != NULL)
    {
        TimeInfo->tm_isdst = 0;
        TimeB = mktime(TimeInfo);
    }

    return((int)(((double)TimeA-(double)TimeB)/(double)36.0));
}

void ExtractMailAddresses(std::string str, std::vector<std::string> &dest)
{
    int rc, ovector[30], match_len, offset = 0, iErrorOffset;
    const char *in = str.c_str();
    const char *match;
    ovector[1] = 0;

    const char *szMailPattern = "[a-zA-Z0-9&'\\.\\-_\\+]+@[a-zA-Z0-9.-]+\\.+[a-zA-Z]{2,12}", *szError;
    pcre *pMailPattern = pcre_compile(szMailPattern, 0, &szError, &iErrorOffset, NULL);

    if(pMailPattern == NULL)
        throw runtime_error("fatal: failed to compile mail address pattern");

    // match all
    for(;;)
    {
        offset = ovector[1];
        rc = pcre_exec(
            pMailPattern,               // pattern
            NULL,                       // no studied data
            in,                         // input
            (int)str.length(),          // input length
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

            string Address;
            Address.append(match, (size_t)match_len);

            bool bExists = false;
            for(unsigned int i=0; i<dest.size(); i++)
            {
                if(strcasecmp(dest.at(i).c_str(), Address.c_str()) == 0)
                {
                    bExists = true;
                    break;
                }
            }

            if(!bExists)
                dest.push_back(Address);
        }
    }

    pcre_free(pMailPattern);
}
