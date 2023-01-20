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

#include "Template.h"
#include "resdata.h"
#include <stdexcept>
#include <iostream>

using namespace std;

Template::Template(Language *lang)
{
    this->lang = lang;
}

void Template::assign(const string &key, const string &val)
{
    this->vars[key] = val;
}

string Template::fetch(const string &name)
{
    int resLength = 0;
    const char *resPtr = getResource(name.c_str(), &resLength);

    if(resPtr == NULL)
        throw runtime_error("Template not found: " + name);

    string tpl;
    tpl.append(resPtr, resLength);

    size_t pos = tpl.find('{');

    while(pos != string::npos)
    {
        size_t nlPos = tpl.find('\n', pos+1), closePos = tpl.find('}', pos+1);

        if(closePos == string::npos || nlPos <= closePos)
            throw runtime_error("Syntax error in template " + name);

        string cmdData = tpl.substr(pos+1, closePos-pos-1);

        if(cmdData.length() > 0)
        {
            string replacement = "";

            // variable?
            if(cmdData.at(0) == '$')
            {
                replacement = this->vars[cmdData.substr(1)];
            }

            // language entry?
            else if(cmdData.length() > 4 && cmdData.substr(0, 4) == "lng:")
            {
                replacement = this->lang->lang[cmdData.substr(4)];
            }

            // include
            else if(cmdData.length() > 8 && cmdData.substr(0, 8) == "include:")
            {
                if(cmdData.at(8) == '$')
                    replacement = this->fetch(this->vars[cmdData.substr(9)]);
                else
                    replacement = this->fetch(cmdData.substr(8));
            }

            // unknown
            else
            {
                throw runtime_error("Unrecognized template command: " + cmdData);
            }

            // replace
            tpl.replace(pos, closePos-pos+1, replacement);
            if(replacement.length() >= closePos-pos)
                pos += replacement.length() - (closePos-pos);
            else
                pos += replacement.length();
        }

        pos = tpl.find('{', pos+1);
    }

    return(tpl);
}
