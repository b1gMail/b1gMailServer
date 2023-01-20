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

#ifndef _SPF_H_
#define _SPF_H_

#include <core/core.h>

enum SPFResult
{
    SPF_RESULT_NONE,
    SPF_RESULT_NEUTRAL,
    SPF_RESULT_PASS,
    SPF_RESULT_FAIL,
    SPF_RESULT_SOFTFAIL,
    SPF_RESULT_TEMPERROR,
    SPF_RESULT_PERMERROR
};

class SPF
{
public:
    SPF(const string &heloDomain, const string &clientIP, const string &clientHost);

public:
    SPFResult CheckHost(const string &ip, const string &domain, string sender, string &explanation, bool initial = true);

private:
    bool LookupRecord(const string &domain, string &result);
    bool EntryMatches(const string &entry, const string &domain, const string &ip, const string &sender, SPFResult &error);
    string ExpandMacro(const string &in, const string &domain, const string &ip, const string &sender, bool exp, bool &syntaxError);

private:
    string heloDomain;
    string clientIP;
    string clientHost;
    string myDomain;
    vector<string> checkedHosts;
    int lookupCounter;
};

#endif
