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

#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
#include <string.h>

#include "config.h"
#include "utils.h"
#include "context.h"
#include "smtp.h"

using namespace std;

int main(int argc, char **argv)
{
    g_Config = NULL;
    int Result = 1;

    try
    {
        // read config file
        g_Config = new Config("/etc/bms-sendmail.conf");

        // context
        SendmailContext ctx;

        // parse cmd line args
        for(int i=1; i<argc; i++)
        {
            char *szArg = argv[i];
            bool bMoreArgs = i < argc-1;

            // ignore
            if(strcmp(szArg, "-Am") == 0
               || strcmp(szArg, "-Ac") == 0
               || strcmp(szArg, "-bh") == 0
               || strcmp(szArg, "-bH") == 0
               || strcmp(szArg, "-bm") == 0
               || strcmp(szArg, "-m") == 0
               || strcmp(szArg, "-o7") == 0
               || strcmp(szArg, "-o8") == 0
               || strcmp(szArg, "-om") == 0
               || strcmp(szArg, "-U") == 0
               || strcmp(szArg, "--") == 0)
            {
                continue;
            }

            // -oi/-i no dot exit
            else if(strcmp(szArg, "-oi") == 0
                    || strcmp(szArg, "-i") == 0)
            {
                ctx.Flags &= ~FLAG_DOTEXIT;
            }

            // -t extract recipients
            else if(strcmp(szArg, "-t") == 0)
            {
                ctx.Flags |= FLAG_EXTRACTRECIPIENTS;
            }

            // -B body type
            else if(strcmp(szArg, "-B") == 0)
            {
                if(bMoreArgs && (strcasecmp(argv[i+1], "7BIT") == 0 || strcasecmp(argv[i+1], "8BITMIME") == 0))
                    continue;
                else
                    throw runtime_error("fatal: -B option needs 8BITMIME or 7BIT");
            }

            // -h hop count
            else if(strcmp(szArg, "-h") == 0)
            {
                if(bMoreArgs)
                {
                    i++;
                    continue;
                }
                else
                    throw runtime_error("fatal: -h needs hop count");
            }

            // -R return limit
            else if(strcmp(szArg, "-h") == 0)
            {
                if(bMoreArgs)
                {
                    i++;
                    continue;
                }
                else
                    throw runtime_error("fatal: -R needs return limit");
            }

            // -F sender name
            else if(strcmp(szArg, "-F") == 0)
            {
                if(bMoreArgs)
                {
                    ctx.Sender = argv[++i];
                    continue;
                }
                else
                    throw runtime_error("fatal: -F needs sender name");
            }

            // -f/-r return path
            else if(strcmp(szArg, "-f") == 0 || strcmp(szArg, "-r") == 0)
            {
                if(bMoreArgs)
                {
                    ctx.ReturnPath = argv[++i];
                    continue;
                }
                else
                    throw runtime_error(string("fatal: ") + string(szArg) + string(" needs return path"));
            }

            // invalid arg
            else if(*szArg == '-')
            {
                cout << argv[0] << ": illegal option -- " << szArg << endl;
                throw runtime_error(string("fatal: usage ") + string(argv[0]) + string(" [options]"));
            }

            // recipient
            else
            {
                bool bRecipientExists = false;

                for(unsigned int j=0; j<ctx.Recipients.size(); j++)
                    if(strcasecmp(ctx.Recipients.at(j).c_str(), szArg) == 0)
                    {
                        bRecipientExists = true;
                        break;
                    }

                if(!bRecipientExists)
                    ctx.Recipients.push_back(szArg);
            }
        }

        // create temp msg file
        ctx.tmpFile = tmpfile();
        if(ctx.tmpFile == NULL)
            throw runtime_error("fatal: failed to create temporary message file");

        // write received header
        ctx.WriteReceivedHeader();

        // read message
        string Line;
        bool bPassedHeaders = false;
        while(getline(cin, Line))
        {
            Line = RTrimString(Line) + string("\r\n");

            // dot exit?
            if((ctx.Flags & FLAG_DOTEXIT) != 0
                && Line == ".\r\n")
            {
                break;
            }

            // passed headers?
            if(!bPassedHeaders
                && Line == "\r\n")
            {
                // add from header
                if(ctx.Headers["from"] == "")
                    ctx.WriteFromHeader();

                bPassedHeaders = true;
            }

            // parse header?
            if(!bPassedHeaders)
            {
                if(!ctx.ParseHeaderLine(Line))
                    continue;
            }

            // append to temp file
            fwrite(Line.c_str(), 1, Line.length(), ctx.tmpFile);
        }
        fseek(ctx.tmpFile, 0, SEEK_SET);

        // extract recipients
        ctx.ExtractRecipients();

        // recipients specified?
        if(ctx.Recipients.size() == 0)
            throw runtime_error("fatal: no recipients specified");

        // pass to SMTP server
        SMTP smtp(ctx);
        smtp.Deliver();

        Result = 0;
    }
    catch(const runtime_error &ex)
    {
        cout << argv[0] << ": " << ex.what() << endl;
        Result = 1;
    }

    // close config file
    if(g_Config != NULL)
        delete g_Config;

    return(Result);
}
