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

#include <core/exception.h>

using namespace Core;

Exception::Exception(const char *szError)
{
    this->strPart = "";
    this->strError = szError == NULL ? "" : szError;
}

Exception::Exception(const char *szPart, const char *szError)
{
    this->strPart = szPart == NULL ? "" : szPart;
    this->strError = szError == NULL ? "" : szError;
}

void Exception::Output()
{
    printf("Error while running b1gMailServer:\n\t%s%s%s\n\n",
        this->strPart.c_str(),
        this->strPart == "" ? "" : ": ",
        this->strError.c_str());
}

void Exception::SMTPOutput(bool details)
{
    printf("421-The service encountered an internal error and is temporarily\r\n");
    printf("421%cunavailable.\r\n", details ? '-' : ' ');

    if(details)
    {
        printf("421-Error details: %s%c\r\n",
            this->strPart.c_str(),
            this->strPart.empty() ? ' ' : ':');
        printf("421 \t%s\r\n",
            this->strError.c_str());
    }
}

void Exception::POP3Output(bool details)
{
    printf("-ERR The service is temporarily unavailable.");

    if(details)
    {
        printf(" %s%s%s",
            this->strPart.c_str(),
            this->strPart == "" ? "" : ": ",
            this->strError.c_str());
    }

    printf("\r\n");
}

void Exception::IMAPOutput(bool details)
{
    printf("* BYE The service is temporarily unavailable.");

    if(details)
    {
        printf(" %s%s%s",
            this->strPart.c_str(),
            this->strPart == "" ? "" : ": ",
            this->strError.c_str());
    }

    printf("\r\n");
}
