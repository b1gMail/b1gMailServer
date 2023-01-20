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

#include "config.h"
#include "utils.h"

#include <stdexcept>
#include <iostream>
#include <fstream>
#include <unistd.h>

using namespace std;

Config *g_Config = NULL;

Config::Config(string Filename) : map<string, string>()
{
    (*this)["smtp_host"]    = "localhost";
    (*this)["smtp_port"]    = "25";
    (*this)["smtp_auth"]    = "0";

    char szHostname[256];
    if(gethostname(szHostname, sizeof(szHostname)-1) == 0)
        (*this)["hostname"] = szHostname;

    this->ReadConfigFile(Filename);
}

void Config::ReadConfigFile(string Filename)
{
    string Line;
    ifstream fp(Filename.c_str());
    if(!fp)
        throw runtime_error("fatal: failed to open config file: " + Filename);
    while(getline(fp, Line))
    {
        if(Line.length() < 3)
            continue;
        if(Line[0] == '#')
            continue;

        size_t eqPos = Line.find('=');
        if(eqPos == string::npos)
            continue;

        string Key = StrToLower(TrimString(Line.substr(0, eqPos))),
                Value = TrimString(Line.substr(eqPos+1));

        (*this)[Key] = Value;
    }
    fp.close();
}
