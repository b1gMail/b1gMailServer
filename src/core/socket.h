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

#ifndef _CORE_SOCKET_H_
#define _CORE_SOCKET_H_

#include <core/core.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

namespace Core
{
    struct TLSARecord;

    struct DANEResult
    {
        DANEResult()
            : usableRecords(0)
            , usingDANE(false)
            , verified(false)
            , depth(-1)
            , verifiedUsage(255)
            , verifiedSelector(255)
            , verifiedMType(255)
        {}

        int usableRecords;
        bool usingDANE;
        bool verified;
        int depth;

        uint8_t verifiedUsage;
        uint8_t verifiedSelector;
        uint8_t verifiedMType;
    };

    class Socket
    {
    public:
        Socket(const char *szHost, int iPort, int iTimeout = 0, bool bUnixSocket = false);
        Socket(int socket, int iTimeout);
        ~Socket();

    public:
        int Write(const char *szBuff, int iLen = -1);
        int Read(char *szBuff, int iLen);
        int PrintF(const char *szFormat, ...);
        char *ReadLine(char *szBuffer, int iBufferLength);
        void SetNoDelay(bool noDelay);
        bool StartTLS(string &errorOut, const std::vector<TLSARecord> *tlsaRecords = NULL,
             const string &daneDomain = "", const string &nextHopDomain = "", DANEResult *daneResult = NULL);
        void EnableLogging(const char *fileName);

    public:
        in_addr_t InAddr;

    private:
        SSL_CTX *ssl_ctx;
        SSL *ssl;
#ifdef WIN32
        SOCKET iSocket;
#else
        int iSocket;
#endif
        FILE *logFP;
        bool bIsUnixSocket;

        Socket(const Socket &);
        Socket &operator=(const Socket &);
    };
};

#endif
