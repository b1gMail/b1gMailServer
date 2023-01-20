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

#define PCRE_STATIC
#include <pcre.h>
#include <core/exception.h>

#define PATTERN_COUNT       4

pcre *pMailPattern, *pIPPattern, *pHeaderPattern, *pMsgIdPattern;

struct cPCREPattern
{
    pcre **pPointer;
    const char *szPattern;
};

cPCREPattern cPatterns[] = {
    { &pMailPattern,    "[a-zA-Z0-9&'\\.\\-_\\+=]+@[a-zA-Z0-9.-]+\\.+[a-zA-Z]{2,12}" },
    { &pIPPattern,      "([0-9]){1,3}\\.([0-9]){1,3}\\.([0-9]){1,3}\\.([0-9]){1,3}" },
    { &pHeaderPattern,  "(=\\?([^?]+)\\?(Q|B)\\?([^?]*)\\?=)" },
    { &pMsgIdPattern,   "<([^>]+)>" }
};

void CompilePatterns()
{
    const char *szError;
    int iErrorOffset;

    for(int i=0; i<PATTERN_COUNT; i++)
    {
        cPCREPattern *entry = &cPatterns[i];

        *(entry->pPointer) = pcre_compile(entry->szPattern,
            0,
            &szError,
            &iErrorOffset,
            NULL);

        if(*(entry->pPointer) == NULL)
            throw Core::Exception("PCRE pattern compilation failed", szError);
    }
}

void FreePatterns()
{
    for(int i=0; i<PATTERN_COUNT; i++)
    {
        cPCREPattern *entry = &cPatterns[i];
        pcre_free(*(entry->pPointer));
    }
}
