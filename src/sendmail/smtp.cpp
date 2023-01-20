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

#include "smtp.h"
#include "config.h"
#include "socket.h"
#include "utils.h"

#include <string.h>
#include <unistd.h>
#include <stdexcept>
#include <iostream>

#define SMTP_TIMEOUT    60
#define MAIL_LINEMAX    (1000+1)

using namespace std;

SMTP::SMTP(SendmailContext &ctx)
{
    this->ctx = &ctx;
}

bool SMTP::ReadResponse(string &strResponse)
{
    char szLineBuffer[512+1];           // rfc2821 / 4.5.3.1
    strResponse.clear();

    do
    {
        if(socket->ReadLine(szLineBuffer, sizeof(szLineBuffer)) == NULL)
            break;

        strResponse.append(szLineBuffer);
    }
    while(strlen(szLineBuffer) > 3 && szLineBuffer[3] == '-');

    return(strResponse.length() > 0);
}

void SMTP::Deliver()
{
    bool bResult = false;

    try
    {
        char szBuffer[MAIL_LINEMAX+10];
        int iSMTPPort;
        bool bSMTPAuth = cfg["smtp_auth"] == "1";
        string Hostname, strStatusInfo = "unknown error";

        if(sscanf(cfg["smtp_port"].c_str(), "%d", &iSMTPPort) != 1 || iSMTPPort == 0)
            iSMTPPort = 25;

        if((Hostname = cfg["hostname"]) == "")
            Hostname = "unknown";

        socket = new Socket(cfg["smtp_host"].c_str(), iSMTPPort, SMTP_TIMEOUT);

        string strResponse;
        if(ReadResponse(strResponse))
        {
            if(strResponse.find("220") == 0)
            {
                socket->PrintF("%s %s\r\n", bSMTPAuth ? "EHLO" : "HELO", Hostname.c_str());

                if(this->ReadResponse(strResponse))
                {
                    if(strResponse.find("250") == 0)
                    {
                        // auth?
                        if(bSMTPAuth)
                        {
                            socket->PrintF("AUTH LOGIN\r\n");
                            if(this->ReadResponse(strResponse)
                                && strResponse.find("334") == 0)
                            {
                                string EncodedUser = Base64Encode(cfg["smtp_user"]),
                                        EncodedPass = Base64Encode(cfg["smtp_pass"]);

                                socket->PrintF("%s\r\n", EncodedUser.c_str());

                                if(this->ReadResponse(strResponse)
                                       && strResponse.find("334") == 0)
                                {
                                    socket->PrintF("%s\r\n", EncodedPass.c_str());

                                    if(this->ReadResponse(strResponse)
                                       && strResponse.find("235") == 0)
                                    {
                                        bSMTPAuth = false;
                                    }
                                }
                            }
                        }

                        // auth success?
                        if(!bSMTPAuth)
                        {
                            socket->PrintF("MAIL FROM:<%s>\r\n", ctx->Sender.c_str());

                            if(this->ReadResponse(strResponse))
                            {
                                if(strResponse.find("250") == 0)
                                {
                                    bool bRecpOK = true;

                                    for(unsigned int i=0; i<ctx->Recipients.size(); i++)
                                    {
                                        string Recipient = ctx->Recipients.at(i);

                                        socket->PrintF("RCPT TO:<%s>\r\n", Recipient.c_str());
                                        if(!this->ReadResponse(strResponse)
                                            || strResponse.find("250") != 0)
                                        {
                                            bRecpOK = false;
                                            break;
                                        }
                                    }

                                    if(bRecpOK)
                                    {
                                        socket->PrintF("DATA\r\n");

                                        if(this->ReadResponse(strResponse))
                                        {
                                            if(strResponse.find("354") == 0)
                                            {
                                                // pass through message
                                                while(!feof(ctx->tmpFile))
                                                {
                                                    if(fgets(szBuffer, MAIL_LINEMAX, ctx->tmpFile) == NULL)
                                                        break;
                                                    size_t iReadBytes = strlen(szBuffer);
                                                    if(iReadBytes <= 0)
                                                        break;

                                                    // fix line endings
                                                    size_t iLen = strlen(szBuffer);
                                                    while(iLen > 0 && (szBuffer[iLen-1] == '\n' || szBuffer[iLen-1] == '\r'))
                                                        szBuffer[--iLen] = '\0';
                                                    iLen = strlen(szBuffer);
                                                    szBuffer[iLen]   = '\r';
                                                    szBuffer[iLen+1] = '\n';
                                                    szBuffer[iLen+2] = '\0';
                                                    iReadBytes = strlen(szBuffer);

                                                    if(*szBuffer == '.')
                                                        socket->Write(".", 1);
                                                    size_t iWrittenBytes = socket->Write(szBuffer, iReadBytes);
                                                    if(iWrittenBytes != iReadBytes)
                                                        break;
                                                }

                                                // termination sequence
                                                socket->PrintF("\r\n.\r\n");

                                                // read response
                                                if(this->ReadResponse(strResponse))
                                                {
                                                    if(strResponse.find("250") == 0)
                                                    {
                                                        bResult = true;
                                                    }
                                                    else
                                                        if(strResponse[0] == '4')
                                                        {
                                                            strStatusInfo = "Temporary relay server error (6)\r\nServer answered: \"";
                                                            strStatusInfo += TrimString(strResponse);
                                                            strStatusInfo += "\"";
                                                        }
                                                        else if(strResponse[0] == '5')
                                                        {
                                                            strStatusInfo = "Relay server rejected message acceptance (6)\r\nServer answered: \"";
                                                            strStatusInfo += TrimString(strResponse);
                                                            strStatusInfo += "\"";
                                                        }
                                                        else
                                                        {
                                                            strStatusInfo = "Unexpected relay server response (6)\r\nServer answered: \"";
                                                            strStatusInfo += TrimString(strResponse);
                                                            strStatusInfo += "\"";
                                                        }
                                                }
                                                else
                                                {
                                                    strStatusInfo = "Failed to read relay server answer (6)";
                                                }
                                            }
                                            else
                                                if(strResponse[0] == '4')
                                                {
                                                    strStatusInfo = "Temporary relay server error (5)\r\nServer answered: \"";
                                                    strStatusInfo += TrimString(strResponse);
                                                    strStatusInfo += "\"";
                                                }
                                                else if(strResponse[0] == '5')
                                                {
                                                    strStatusInfo = "Relay server rejected data transfer initialization (5)\r\nServer answered: \"";
                                                    strStatusInfo += TrimString(strResponse);
                                                    strStatusInfo += "\"";
                                                }
                                                else
                                                {
                                                    strStatusInfo = "Unexpected relay server response (5)\r\nServer answered: \"";
                                                    strStatusInfo += TrimString(strResponse);
                                                    strStatusInfo += "\"";
                                                }
                                        }
                                        else
                                        {
                                            strStatusInfo = "Failed to read relay server answer (5)";
                                        }
                                    }
                                    else
                                    {
                                        strStatusInfo = "Relay server rejected one or more recipients";
                                    }
                                }
                                else
                                    if(strResponse[0] == '4')
                                    {
                                        strStatusInfo = "Temporary relay server error (3)\r\nServer answered: \"";
                                        strStatusInfo += TrimString(strResponse);
                                        strStatusInfo += "\"";
                                    }
                                    else if(strResponse[0] == '5')
                                    {
                                        strStatusInfo = "Relay server rejected return path (3)\r\nServer answered: \"";
                                        strStatusInfo += TrimString(strResponse);
                                        strStatusInfo += "\"";
                                    }
                                    else
                                    {
                                        strStatusInfo = "Unexpected relay server response (3)\r\nServer answered: \"";
                                        strStatusInfo += TrimString(strResponse);
                                        strStatusInfo += "\"";
                                    }
                            }
                            else
                            {
                                strStatusInfo = "Failed to read relay server answer (3)";
                            }
                        }
                        else
                        {
                            strStatusInfo = "Relay server authentication failed - configuration error?";
                        }
                    }
                    else
                        if(strResponse[0] == '4')
                        {
                            strStatusInfo = "Temporary relay server (2)\r\nServer answered: \"";
                            strStatusInfo += TrimString(strResponse);
                            strStatusInfo += "\"";
                        }
                        else if(strResponse[0] == '5')
                        {
                            strStatusInfo = "Fatal relay server error (2)\r\nServer answered: \"";
                            strStatusInfo += TrimString(strResponse);
                            strStatusInfo += "\"";
                        }
                        else
                        {
                            strStatusInfo = "Unexpected relay server response (2)\r\nServer answered: \"";
                            strStatusInfo += TrimString(strResponse);
                            strStatusInfo += "\"";
                        }
                }
                else
                {
                    strStatusInfo = "Failed to read relay server answer (2)";
                }
            }

            else
                if(strResponse[0] == '4')
                {
                    strStatusInfo = "Temporary relay server error (1)\r\nServer answered: \"";
                    strStatusInfo += TrimString(strResponse);
                    strStatusInfo += "\"";
                }
                else if(strResponse[0] == '5')
                {
                    strStatusInfo = "Fatal relay server error (1)\r\nServer answered: \"";
                    strStatusInfo += TrimString(strResponse);
                    strStatusInfo += "\"";
                }
                else
                {
                    strStatusInfo = "Unexpected relay server response (1)\r\nServer answered: \"";
                    strStatusInfo += TrimString(strResponse);
                    strStatusInfo += "\"";
                }

            // sign off
            socket->PrintF("QUIT\r\n");
            this->ReadResponse(strResponse);
        }
        else
        {
            strStatusInfo = "Failed to read relay server greeting (1)";
        }

        if(!bResult)
            throw runtime_error(strStatusInfo);
    }
    catch(const runtime_error &ex)
    {
        throw runtime_error("fatal: error during SMTP session: " + string(ex.what()));
    }
}
