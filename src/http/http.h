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

#ifndef _HTTP_HTTP_H
#define _HTTP_HTTP_H

#include <core/core.h>

#define HTTP_LINEMAX            512

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

class WebdiskFileInfo
{
public:
    int fileID;
    int fileSize;
    string mimeType;
    string fileName;
    int blobStorage;
};

class HTTPRequest
{
public:
    HTTPRequestMethod requestMethod;
    HTTPParseStatus parseStatus;
    string requestURL;
    string requestHost;
    string requestProtocol;
    string userAgent;
};

class HTTP
{
public:
    HTTP();
    ~HTTP();

public:
    void Run();

private:
    bool ProcessLine(char *szLine);
    void ProcessRequest();
    WebdiskFileInfo *FindWebdiskFile(string strURL, int iUserID);
    void SendFile(WebdiskFileInfo *file);
    void ErrorPage(HTTPError error, const char *szInfo);
    void PrintHeaders(int statusCode, const char *statusInfo, int contentLength, const char *contentType);

private:
    string strPeer;
    int lineNum;
    int iTrafficAllowed;
    int iTrafficUsed;
    int iBandwithLimit;
    int iUser;
    string adSig;

public:
    HTTPRequest request;

    HTTP(const HTTP &);
    HTTP &operator=(const HTTP &);
};

#endif
