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

#include "socket.h"
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <stdarg.h>
#include <string>
#include <stdexcept>
#include <stdlib.h>

using namespace std;

Socket::Socket(const char *szHost, int iPort, int iTimeout)
{
    struct hostent *cHostent;
    struct sockaddr_in cServerAddr = { 0 };

    // get ip address
    if((cHostent = gethostbyname(szHost)) == NULL)
        throw runtime_error("GetHostByName() failed: " + string(szHost));

    // fill cServerAddr
    cServerAddr.sin_family = cHostent->h_addrtype;
    memcpy((char *)&cServerAddr.sin_addr.s_addr, cHostent->h_addr_list[0], cHostent->h_length);
    cServerAddr.sin_port = htons(iPort);

    // set InAddr
    this->InAddr = (int)cServerAddr.sin_addr.s_addr;

    // create socket
    if((this->iSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        throw runtime_error("Failed to create socket");

    // bind it?
    string ClientAddr = cfg["client_addr"];
    if(ClientAddr.length() > 1)
    {
        in_addr_t iClientAddr = inet_addr(ClientAddr.c_str());
        if(iClientAddr != (in_addr_t)(-1) && iClientAddr != INADDR_NONE)
        {
            struct sockaddr_in cClientAddr = { 0 };
            memset(&cClientAddr, 0, sizeof(cClientAddr));
            cClientAddr.sin_family = AF_INET;
            cClientAddr.sin_addr.s_addr = iClientAddr;

            if(bind(this->iSocket, (struct sockaddr *)&cClientAddr, sizeof(cClientAddr)) < 0)
                throw runtime_error("cannot bind socket to address specified in client_addr: " + ClientAddr);
        }
        else
            throw runtime_error("invalid address specified in client_addr: " + ClientAddr);
    }

    // set timeout?
    if(iTimeout > 0)
    {
#ifdef WIN32
        DWORD tvTimeout = iTimeout * 1000;
#else
        struct timeval tvTimeout;
        tvTimeout.tv_sec = iTimeout;
        tvTimeout.tv_usec = 0;
#endif

        setsockopt(this->iSocket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tvTimeout, sizeof(tvTimeout));

#ifdef WIN32
        tvTimeout = iTimeout * 1000;
#else
        tvTimeout.tv_sec = iTimeout;
        tvTimeout.tv_usec = 0;
#endif

        setsockopt(this->iSocket, SOL_SOCKET, SO_SNDTIMEO, (char *)&tvTimeout, sizeof(tvTimeout));
    }

    // connect
    if(connect(this->iSocket, (struct sockaddr *)&cServerAddr, sizeof(cServerAddr)) < 0)
    {
        if(errno == ETIMEDOUT)
        {
            close(this->iSocket);
            throw runtime_error("timeout while connecting socket: " + string(szHost));
        }
        else
        {
            close(this->iSocket);
            throw runtime_error("failed to connect socket: " + string(szHost));
        }
    }
}

Socket::~Socket()
{
    close(this->iSocket);
}

int Socket::Write(const char *szBuff, int iLen)
{
    int iResult = send(this->iSocket, szBuff, iLen == -1 ? (int)strlen(szBuff) : iLen, 0);

    if(iResult == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
        throw runtime_error("socket write timeout");

    return(iResult);
}

int Socket::Read(char *szBuff, int iLen)
{
    memset(szBuff, 0, iLen);

    int iResult = recv(this->iSocket, szBuff, iLen-1, 0);

    if(iResult == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
        throw runtime_error("socket read timeout");

    return(iResult);
}

int Socket::PrintF(const char *szFormat, ...)
{
    va_list arglist;
    va_start(arglist, szFormat);

    int result;
    char *szStr = NULL;

    result = vasprintf(&szStr, szFormat, arglist);
    if(szStr == NULL)
        return(0);

    int iResult = this->Write(szStr, result);
    free(szStr);

    return(iResult);
}

char *Socket::ReadLine(char *szBuffer, int iBufferLength)
{
    memset(szBuffer, 0, iBufferLength);

    char *c = szBuffer;
    int iResult;

    do
    {
        iResult = recv(this->iSocket, c, 1, 0);

        if(iResult == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
            throw runtime_error("socket read timeout");

        if(iResult < 1)
            break;
    }
    while((c-szBuffer) < iBufferLength-1
          && *c != '\n'
          && c++);

    if(c == szBuffer)
        return(NULL);
    else
        return(szBuffer);
}
