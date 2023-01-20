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

#include "HTTPServer.h"
#include "Utils.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <iostream>

#include <stdexcept>

using namespace std;

const char *HTTPParseErrorMessages[] = {
    "No error (valid request)",
    "Invalid HTTP request",
    "Invalid/unsupported protocol in HTTP request",
    "Invalid/unsupported request method"
};

HTTPServer::HTTPServer(int port, in_addr_t addr)
{
    this->port      = port;
    this->addr      = addr;
    this->backlog   = 5;
}

HTTPServer::~HTTPServer()
{

}

void HTTPServer::run()
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0)
        throw runtime_error("Failed to create HTTP server socket");

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family       = AF_INET;
    serverAddr.sin_addr.s_addr  = this->addr;
    serverAddr.sin_port         = htons(this->port);

    int optVal = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(optVal));

    if(bind(sock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
        throw runtime_error("Failed to bind HTTP server socket");

    if(listen(sock, this->backlog) != 0)
        throw runtime_error("Failed to put HTTP server socket in listen mode");

    bool running = true;
    while(running)
    {
        struct sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);

        int clientSock = accept(sock, (struct sockaddr *)&clientAddr, &clientAddrLen);
        if(clientSock < 0)
            break;

        running = this->processConnection(clientSock, &clientAddr);

        close(clientSock);
    }

    close(sock);
}

bool HTTPServer::processConnection(int sock, sockaddr_in* clientAddr)
{
    bool result = true;

    FILE *fp = fdopen(sock, "r+");
    if(fp == NULL)
        throw runtime_error("Failed to fdopen() socket");

    int lineNum = 0;
    HTTPRequest request(fp);

    // receive request
    while(!feof(fp))
    {
        char lineBuffer[512];

        if(fgets(lineBuffer, sizeof(lineBuffer), fp) == NULL)
            break;

        if(!this->processRequestLine(lineBuffer, lineNum++, request))
            break;
    }

    // request OK?
    if(request.parseStatus == PARSE_OK)
    {
        if(request.requestMethod == REQUEST_POST
            && request.headerFields["content-type"].length() >= 33
            && Utils::strToLower(request.headerFields["content-type"].substr(0, 33)) == "application/x-www-form-urlencoded"
            && request.headerFields["content-length"].length() > 0)
        {
            std::size_t length = strtoul(request.headerFields["content-length"].c_str(), NULL, 10);

            if(length < 1024*100)
            {
                char formDataBuffer[ length+1 ];
                memset(formDataBuffer, 0, length+1);

                if(fread(formDataBuffer, 1, length, fp) == length)
                {
                    char *pch = strtok(formDataBuffer, "&");
                    while(pch != NULL)
                    {
                        string fieldData = pch;

                        size_t eqPos = fieldData.find('=');
                        if(eqPos != string::npos)
                        {
                            string key = Utils::trim(fieldData.substr(0, eqPos)),
                                    value = Utils::trim(fieldData.substr(eqPos+1));

                            request.postFields[Utils::quotedPrintableDecode(key)] = Utils::quotedPrintableDecode(value);
                        }

                        pch = strtok(NULL, "&");
                    }
                }
            }
        }

        if(!this->processRequest(request))
            result = false;
    }
    else
    {
        request.errorPage(ERROR_BAD_REQUEST, HTTPParseErrorMessages[request.parseStatus]);
    }

    fclose(fp);
    return(result);
}

bool HTTPServer::processRequestLine(string line, int lineNum, HTTPRequest &request)
{
    if(line.compare("") == 0 || line.compare("\n") == 0 || line.compare("\r\n") == 0)
        return(false);

    line = Utils::trim(line);

    if(lineNum == 0)
    {
        // extract request command
        size_t spacePos = line.find(' ');
        if(spacePos == string::npos)
        {
            request.parseStatus = PARSE_INVALID_REQUEST;
            return(false);
        }

        string requestMethod = line.substr(0, spacePos);
        if(strcasecmp(requestMethod.c_str(), "get") == 0)
        {
            request.requestMethod = REQUEST_GET;
        }
        else if(strcasecmp(requestMethod.c_str(), "post") == 0)
        {
            request.requestMethod = REQUEST_POST;
        }
        else
        {
            request.parseStatus = PARSE_INVALID_REQUEST_METHOD;
            return(false);
        }

        // extract request URI
        size_t spacePos2 = line.find(' ', spacePos+1);
        if(spacePos == string::npos)
        {
            request.parseStatus = PARSE_INVALID_REQUEST;
            return(false);
        }

        request.requestURI      = line.substr(spacePos+1, spacePos2-spacePos-1);
        request.requestProtocol = line.substr(spacePos2+1);

        if(strcasecmp(request.requestProtocol.c_str(), "http/1.0") != 0 && strcasecmp(request.requestProtocol.c_str(), "http/1.1") != 0)
        {
            request.parseStatus = PARSE_INVALID_PROTOCOL;
            return(false);
        }

        request.parseStatus     = PARSE_OK;
    }
    else
    {
        size_t colonPos = line.find(':');
        if(colonPos != string::npos)
        {
            string key = Utils::trim(line.substr(0, colonPos)),
                    val = Utils::trim(line.substr(colonPos+1));

            if(strcasecmp(key.c_str(), "host") == 0)
            {
                request.requestHost = val;
            }
            else if(strcasecmp(key.c_str(), "user-agent") == 0)
            {
                request.userAgent   = val;
            }

            request.headerFields[Utils::strToLower(key)] = val;
        }
    }

    return(true);
}

void HTTPRequest::sendHeaders(int statusCode, const string statusDesc, const int contentLength, const string contentType,
                                const string additional)
{
    fprintf(fp, "%s %03d %s\r\n",
         this->requestProtocol.c_str(),
         statusCode,
         statusDesc.c_str());
    fprintf(fp, "Server: b1gMailServerSetup/2.0\r\n");
    fprintf(fp, "Connection: close\r\n");
    fprintf(fp, "Content-Type: %s\r\n", contentType.c_str());
    fprintf(fp, "Content-Length: %d\r\n", contentLength);
    fprintf(fp, "%s", additional.c_str());
    fprintf(fp, "\r\n");
}

void HTTPRequest::errorPage(const HTTPError error, const string msg)
{
    // build error strings
    const char *szError = "Unknown error",
        *szDesc = "An unknown error occured.";
    switch(error)
    {
    case ERROR_BAD_REQUEST:
        szError = "Bad Request";
        szDesc = "Your HTTP client made a request this server cannot understand. Please try again with an other HTTP "
                    "client or contact the server\'s administator.";
        break;

    case ERROR_PAGE_NOT_FOUND:
        szError = "Page Not Found";
        szDesc = "The requested page does not exist. Please check the URL and try again or contact the server\'s "
                    "administrator.";
        break;

    case ERROR_UNAUTHORIZED:
        szError = "Unauthorized";
        szDesc = "You are not allowed to access this page. Please try again or contact the servers\'s administator, "
                    "if you think this is an error of the server.";
        break;

    case ERROR_INTERNAL_SERVER_ERROR:
        szError = "Internal Server Error";
        szDesc = "This server cannot process your request now because of internal technical problems. Please try again "
                    "later or contact the server\'s administrator.";
        break;

    case ERROR_BANDWITH_EXCEEDED:
        szError = "Bandwith Limit Exceeded";
        szDesc = "The bandwith limit of this host has exceeded for this month. Please try again next month.";
        break;
    };

    // build page
    string strPage = "<html>\r\n"
                        "<head>\r\n"
                            "<title>";
    strPage += szError;
    strPage +=              "</title>\r\n"
                        "</head>\r\n"
                        "<body>\r\n"
                            "<h1>";
    strPage += szError;
    strPage +=              "</h1>\r\n"
                            "<p>\r\n";
    strPage += szDesc;
    strPage +=              "</p>\r\n"
                            "<p><em>\r\n";
    strPage += msg.length() == 0 ? "There is no further information about this error available." : msg;
    strPage +=              "</em></p>\r\n"
                            "<hr />\r\n"
                            "<address>"
                                "b1gMailServerSetup/2.0"
                            "</address>"
                        "</body>\r\n"
                    "</html>\r\n";

    this->sendHeaders((int)error, szError, strPage.length(), "text/html");
    fprintf(fp, "%s", strPage.c_str());
}

bool HTTPServer::processRequest(HTTPRequest &request)
{
    string response = "Not implemented.";

    request.sendHeaders(200, "OK", response.length(), "text/html");
    fprintf(request.fp, "%s", response.c_str());

    return(true);
}
