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

#include "Language.h"
#include "Utils.h"
#include "resdata.h"
#include <stdexcept>
#include <iostream>

using namespace std;

Language::Language(const string name)
{
    int langDataLength = 0;
    const char *langDataPtr = getResource((name + ".lang").c_str(), &langDataLength);

    if(langDataPtr == NULL)
        throw runtime_error("Language resource not found: " + name);

    string langData;
    langData.append(langDataPtr, langDataLength);
    size_t nlPos;

    while((nlPos = langData.find('\n')) != string::npos)
    {
        string line = langData.substr(0, nlPos);
        langData.erase(0, nlPos+1);

        size_t eqPos = line.find('=');
        if(eqPos != string::npos)
        {
            string key = Utils::trim(line.substr(0, eqPos)), value = Utils::trim(line.substr(eqPos+1));

            this->lang[key] = value;
        }
    }
}
