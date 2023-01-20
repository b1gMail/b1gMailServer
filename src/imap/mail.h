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

#ifndef _IMAP_MAIL_H
#define _IMAP_MAIL_H

#include <core/core.h>
#include <pluginsdk/bmsplugin.h>

#include <map>

using namespace std;

class MailHeader
{
public:
    MailHeader(const string &key, const string &value, bool bDecode);
    MailHeader()
        : bDecode(false)
    {}

    void Add(const string &str);
    void DecodeValue();

    string strKey;
    string strValue;
    string strRawValue;
    bool bDecode;
};

class Mail : public b1gMailServer::Mail
{
private:
    map<string, MailHeader> headers;

public:
    Mail();

    const char *GetHeader(const char *key) const;
    const char *GetRawHeader(const char *key) const;
    vector<string> GetHeaderFields() const
    {
        vector<string> res;
        for(map<string, MailHeader>::const_iterator it = headers.begin(); it != headers.end(); ++it)
        {
            res.push_back(it->first);
        }
        return res;
    }

    void Parse(const char *line);
    void ParseInfo();
    void ExtractMailAddresses(const char *in, vector<char*> *to);
    static char *ExtractMailAddress(const char *in);

    bool bHasAttachment;
    bool bDecode;
    string strBoundary;
    string strPriority;

private:
    string lastHeaderField;
};

#endif
