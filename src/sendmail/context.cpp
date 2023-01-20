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

#include "context.h"
#include "utils.h"
#include "config.h"

#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <pwd.h>

using namespace std;

SendmailContext::SendmailContext()
{
    this->Flags     = FLAG_DOTEXIT;
    this->tmpFile   = NULL;
}

SendmailContext::~SendmailContext()
{
    if(this->tmpFile != NULL)
        fclose(this->tmpFile);
}

void SendmailContext::WriteFromHeader()
{
    if(this->tmpFile == NULL)
        return;

    string Username = "", From = "";

    struct passwd *pw = getpwuid(getuid());
    if(pw != NULL && pw->pw_name != NULL)
        Username = pw->pw_name;
    else
        Username = "unknown";
    From = Username;
    From += "@";
    From += cfg["hostname"];
    From += " (";
    From += Username;
    From += ")";

    // add header
    fprintf(tmpFile, "From: %s\r\n",
         From.c_str());
    this->Headers["from"] = From;
}

void SendmailContext::WriteReceivedHeader()
{
    if(this->tmpFile == NULL)
        return;

    // create time string
    char szTime[128] = { 0 }, szTimeZoneAbbrev[16] = { 0 };
    time_t iTime = time(NULL);
    struct tm *cTime = localtime(&iTime);
    strftime(szTime, 128, "%a, %d %b %Y %H:%M:%S", cTime);
    strftime(szTimeZoneAbbrev, 16, "%Z", cTime);
    int iTimeZone = GetTimeZone();

    // add header
    fprintf(tmpFile, "Received: from local user (id %d)\r\n"
            "\tby %s (b1gMailServer-sendmail-wrapper);\r\n"
            "\t%s%s%04d (%s)\r\n",
         (int)getuid(),
         cfg["hostname"].c_str(),
        szTime,
        iTimeZone >= 0 ? " +" : " -",
        iTimeZone,
        szTimeZoneAbbrev);
}

bool SendmailContext::ParseHeaderLine(string Line)
{
    size_t dpPos = Line.find(':'),
            wsPos = Line.find_first_of("\t ");

    if(dpPos != string::npos
        && (wsPos == string::npos || dpPos < wsPos))
    {
        string Key = StrToLower(TrimString(Line.substr(0, dpPos))),
                Value = TrimString(Line.substr(dpPos+1));

        if(this->Headers.find(Key) != this->Headers.end())
            this->Headers[Key] = TrimString(this->Headers[Key] + " " + Value);
        else
            this->Headers[Key] = Value;

        this->LastHeaderKey = Key;
    }

    else if(wsPos != string::npos
         && !this->LastHeaderKey.empty())
    {
        this->Headers[this->LastHeaderKey] = TrimString(this->Headers[this->LastHeaderKey] + " " + TrimString(Line));
    }

    else
    {
        this->LastHeaderKey = "";
    }

    return(this->LastHeaderKey != "bcc");
}

void SendmailContext::ExtractRecipients()
{
    string RecipientString = this->Headers["to"] + " "
            + this->Headers["cc"] + " "
            + this->Headers["bcc"];

    ExtractMailAddresses(RecipientString, this->Recipients);

    // also correct "from"
    if(Sender == "")
    {
        struct passwd *pw = getpwuid(getuid());
        if(pw != NULL && pw->pw_name != NULL)
            Sender = pw->pw_name;
        else
            Sender = "unknown";
        Sender += "@";
        Sender += cfg["hostname"];
    }
}
