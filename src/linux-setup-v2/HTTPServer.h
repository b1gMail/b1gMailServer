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

#ifndef _HTTPSERVER_H_
#define _HTTPSERVER_H_

#include <sys/types.h>
#include <netinet/in.h>
#include <map>
#include <string>

enum HTTPError
{
    ERROR_BAD_REQUEST = 400,
    ERROR_UNAUTHORIZED = 401,
    ERROR_PAGE_NOT_FOUND = 404,
    ERROR_INTERNAL_SERVER_ERROR = 500,
    ERROR_BANDWITH_EXCEEDED = 503
};

enum HTTPRequestMethod
{
    REQUEST_GET,
    REQUEST_POST
};

enum HTTPParseStatus
{
    PARSE_OK = 0,
    PARSE_INVALID_REQUEST,
    PARSE_INVALID_PROTOCOL,
    PARSE_INVALID_REQUEST_METHOD
};

class HTTPRequest
{
public:
    HTTPRequest(FILE *fp)
    {
        this->fp = fp;
    }

public:
    void errorPage(const HTTPError error, const std::string msg);
    void sendHeaders(int statusCode, const std::string statusDesc, const int contentLength, const std::string contentType,
                        const std::string additional = "");

public:
    HTTPRequestMethod requestMethod;
    HTTPParseStatus parseStatus;
    std::string requestURI;
    std::string requestHost;
    std::string requestProtocol;
    std::string userAgent;
    std::map< std::string, std::string > headerFields;
    std::map< std::string, std::string > postFields;
    FILE *fp;

    HTTPRequest(const HTTPRequest &);
    HTTPRequest &operator=(const HTTPRequest &);
};

class HTTPServer
{
public:
    HTTPServer(int port, in_addr_t addr = INADDR_ANY);
    ~HTTPServer();

public:
    void run();

private:
    bool processConnection(int sock, struct sockaddr_in *clientAddr);
    bool processRequestLine(std::string line, int lineNum, HTTPRequest &request);
    virtual bool processRequest(HTTPRequest &request);

private:
    int port;
    in_addr_t addr;
    int backlog;
};

#endif
