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

#include <core/socket.h>
#include <core/dns.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sstream>

#include <openssl/x509v3.h>

#ifndef WIN32
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <sys/un.h>
#endif

#ifdef WIN32
#define in_addr_t unsigned long
#endif

/*
 * Constructor
 */
Socket::Socket(const char *szHost, int iPort, int iTimeout, bool bUnixSocket)
{
    this->logFP = NULL;
    this->ssl = NULL;
    this->ssl_ctx = NULL;
    this->bIsUnixSocket = bUnixSocket;

#ifndef WIN32
    if(bUnixSocket)
    {
        this->InAddr = inet_addr("127.0.0.1");

        struct sockaddr_un cAddr = { 0 };
        cAddr.sun_family = AF_LOCAL;
        if(strlen(szHost) < sizeof(cAddr.sun_path))
            strcpy(cAddr.sun_path, szHost);
        else
            throw Core::Exception("Unix socket path too long", szHost);

        if((this->iSocket = socket(AF_LOCAL, SOCK_STREAM, 0)) < 0)
            throw(Core::Exception("Failed to create Unix socket"));

        fcntl(this->iSocket, F_SETFD, fcntl(this->iSocket, F_GETFD) | FD_CLOEXEC);

        // connect
        if(connect(this->iSocket, (struct sockaddr *)&cAddr, sizeof(cAddr)) != 0)
            throw(Core::Exception("Failed to connect to Unix socket", szHost));

        return;
    }
#endif
    struct sockaddr_in cServerAddr = { 0 };
    bool success = false;
    string lastErrorMsg = "", lastErrorArg = "";

    // get ip address
    vector<in_addr_t> hostIPs;
    if(DNS::ALookup(szHost, hostIPs) < 1)
        throw(Core::Exception("A lookup failed", szHost));

    // fill cServerAddr
    cServerAddr.sin_family = AF_INET;
    cServerAddr.sin_port = htons(iPort);

    for(vector<in_addr_t>::iterator it = hostIPs.begin(); it != hostIPs.end(); ++it)
    {
        cServerAddr.sin_addr.s_addr = *it;

        // set InAddr
        this->InAddr = cServerAddr.sin_addr.s_addr;

        // create socket
        if((this->iSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
            throw(Core::Exception("Failed to create socket"));

#ifndef WIN32
        fcntl(this->iSocket, F_SETFD, fcntl(this->iSocket, F_GETFD) | FD_CLOEXEC);
#endif

        // bind it?
        const char *szClientAddr = cfg->Get("client_addr");
        if(szClientAddr != NULL && strcmp(szClientAddr, "") != 0)
        {
            in_addr_t iClientAddr = inet_addr(szClientAddr);
            if(iClientAddr != (in_addr_t)(-1) && iClientAddr != INADDR_NONE)
            {
                struct sockaddr_in cClientAddr = { 0 };
                memset(&cClientAddr, 0, sizeof(cClientAddr));
                cClientAddr.sin_family = AF_INET;
                cClientAddr.sin_addr.s_addr = iClientAddr;

                if(bind(this->iSocket, (struct sockaddr *)&cClientAddr, sizeof(cClientAddr)) < 0)
                {
                    db->Log(CMP_CORE, PRIO_WARNING, utils->PrintF("Failed to bind client socket to address specified in client_addr (%s)",
                                                                  szClientAddr));
                }
            }
            else
            {
                db->Log(CMP_CORE, PRIO_WARNING, utils->PrintF("Invalid address specified in client_addr (%s)",
                                                              szClientAddr));
            }
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

        // set non-blocking
#ifdef WIN32
        unsigned long ul = 1;
        ioctlsocket(this->iSocket, FIONBIO, &ul);
#else
        fcntl(this->iSocket, F_SETFL, fcntl(this->iSocket, F_GETFL) | O_NONBLOCK);
#endif

        // connect
        connect(this->iSocket, (struct sockaddr *)&cServerAddr, sizeof(cServerAddr));

        // wait
        fd_set fdSet;
        FD_ZERO(&fdSet);
        FD_SET(this->iSocket, &fdSet);
        struct timeval tv;
        tv.tv_sec = iTimeout;
        tv.tv_usec = 0;

        if(select(this->iSocket+1, NULL, &fdSet, NULL, iTimeout>0 ? &tv : NULL) == -1)
        {
#ifdef WIN32
            closesocket(this->iSocket);
#else
            close(this->iSocket);
#endif

            throw Core::Exception("select() failed while connecting socket", szHost);
        }
        else
        {
            if(FD_ISSET(this->iSocket, &fdSet))
            {
                int sockError;
                socklen_t sockLen = sizeof(sockError);

                if(getsockopt(this->iSocket, SOL_SOCKET, SO_ERROR, (char *)&sockError, &sockLen) == 0)
                {
                    if(sockError == 0)
                    {
                        success = true;
                    }
                    else
                    {
#ifdef WIN32
                        if(sockError == WSAETIMEDOUT)
#else
                        if(sockError == ETIMEDOUT)
#endif
                        {
                            lastErrorMsg = "Timeout while connecting socket (2)";
                            lastErrorArg = szHost;
                        }
                        else
                        {
                            lastErrorMsg = "Failed to connect socket";
                            lastErrorArg = szHost;
                        }
                    }
                }
                else
                {
                    lastErrorMsg = "getsockopt() failed while connecting socket";
                    lastErrorArg = szHost;
                }
            }
            else
            {
                lastErrorMsg = "Timeout while connecting socket (1)";
                lastErrorArg = szHost;
            }
        }

        // set blocking
#ifdef WIN32
        ul = 0;
        ioctlsocket(this->iSocket, FIONBIO, &ul);
#else
        fcntl(this->iSocket, F_SETFL, fcntl(this->iSocket, F_GETFL) & ~(O_NONBLOCK));
#endif

        // ok?
        if(success)
        {
            break;
        }
        else
        {
#ifdef WIN32
            closesocket(this->iSocket);
#else
            close(this->iSocket);
#endif
        }
    }

    if(!success)
        throw(Core::Exception(lastErrorMsg.c_str(), lastErrorArg.c_str()));
}

Socket::Socket(int socket, int iTimeout)
{
    this->logFP = NULL;
    this->ssl = NULL;
    this->ssl_ctx = NULL;

    this->iSocket = socket;

    // set timeout?
    if(iTimeout > 0)
    {
#ifdef WIN32
        int tvTimeout;
#else
        struct timeval tvTimeout;
#endif

#ifdef WIN32
        tvTimeout = iTimeout * 1000;
#else
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
}

/*
 * Destructor
 */
Socket::~Socket()
{
    if(this->ssl != NULL)
    {
        SSL_free(this->ssl);
        this->ssl = NULL;
    }

    if(this->iSocket != 0)
    {
#ifdef WIN32
        closesocket(this->iSocket);
#else
        close(this->iSocket);
#endif
    }

    if(this->ssl_ctx != NULL)
    {
        SSL_CTX_free(this->ssl_ctx);
        this->ssl_ctx = NULL;
    }

    if(this->logFP != NULL)
    {
        fclose(this->logFP);
        this->logFP = NULL;
    }
}

void Socket::EnableLogging(const char *fileName)
{
    if(this->logFP != NULL)
    {
        fclose(this->logFP);
        this->logFP = NULL;
    }

    this->logFP = fopen(fileName, "wb");
}

/**
 * Enable/disable TCP_NODELAY
 */
void Socket::SetNoDelay(bool noDelay)
{
    int flag = noDelay ? 1 : 0;

    setsockopt(this->iSocket, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));
}

/*
 * Write
 */
int Socket::Write(const char *szBuff, int iLen)
{
    int iResult;

    if(this->ssl != NULL)
        iResult = SSL_write(this->ssl, szBuff, iLen == -1 ? strlen(szBuff) : iLen);
    else
        iResult = send(this->iSocket, szBuff, iLen == -1 ? (int)strlen(szBuff) : iLen, 0);

#ifdef WIN32
    if(iResult == SOCKET_ERROR && (WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAETIMEDOUT))
#else
    if(iResult == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
#endif
    {
        if(this->logFP != NULL) fprintf(this->logFP, "ERROR: Socket write timeout\n");
        throw(Core::Exception("Socket write timeout"));
    }

    if(this->logFP != NULL)
    {
        fprintf(this->logFP, ">w \"");
        fwrite(szBuff, iLen, 1, this->logFP);
        fprintf(this->logFP, "\" = %d\n", iResult);
    }

    return(iResult);
}

/*
 * Read
 */
int Socket::Read(char *szBuff, int iLen)
{
    memset(szBuff, 0, iLen);

    int iResult;

    if(this->ssl != NULL)
        iResult = SSL_read(this->ssl, szBuff, iLen);
    else
        iResult = recv(this->iSocket, szBuff, iLen, 0);

#ifdef WIN32
    if(iResult == SOCKET_ERROR && (WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAETIMEDOUT))
#else
    if(iResult == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
#endif
    {
        if(this->logFP != NULL) fprintf(this->logFP, "ERROR: Socket read timeout\n");
        throw(Core::Exception("Socket read timeout"));
    }

    if(this->logFP != NULL)
    {
        fprintf(this->logFP, "<r \"");
        if(iResult > 0)
            fwrite(szBuff, iResult, 1, this->logFP);
        fprintf(this->logFP, "\"\n");
    }

    return(iResult);
}

/*
 * PrintF
 */
int Socket::PrintF(const char *szFormat, ...)
{
    va_list arglist;
    va_start(arglist, szFormat);

    char *szStr = utils->VPrintF(szFormat, arglist);
    if(szStr == NULL)
        return(false);

    int iResult = this->Write(szStr, strlen(szStr));
    free(szStr);

    return(iResult);
}

/*
 * Read line
 */
char *Socket::ReadLine(char *szBuffer, int iBufferLength)
{
    memset(szBuffer, 0, iBufferLength);

    char *c = szBuffer;
    int iResult;

    do
    {
        if(this->ssl != NULL)
            iResult = SSL_read(this->ssl, c, 1);
        else
            iResult = recv(this->iSocket, c, 1, 0);

#ifdef WIN32
        if(iResult == SOCKET_ERROR && (WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAETIMEDOUT))
#else
        if(iResult == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
#endif
        {
            if(this->logFP != NULL) fprintf(this->logFP, "ERROR: Socket read timeout\n");
            throw(Core::Exception("Socket read timeout"));
        }

        if(iResult < 1)
            break;
    }
    while((c-szBuffer) < iBufferLength-1
          && *c != '\n'
          && c++);

    if(c == szBuffer)
    {
        if(this->logFP != NULL) fprintf(this->logFP, "<l (no line read)\n");
        return(NULL);
    }
    else
    {
        if(this->logFP != NULL) fprintf(this->logFP, "<l \"%s\"\n", szBuffer);
        return(szBuffer);
    }
}

bool Socket::StartTLS(string &errorOut, const std::vector<TLSARecord> *tlsaRecords, const string &daneDomain, const string &nextHopDomain, DANEResult *daneResult)
{
    bool enableDANE = (tlsaRecords != NULL && daneResult != NULL && !tlsaRecords->empty());

    if(this->logFP != NULL) fprintf(this->logFP, "StartTLS (enableDANE = %d, |tlsaRecords| = %lu): ", enableDANE ? 1 : 0, enableDANE ? tlsaRecords->size() : 0);

    if((this->ssl_ctx = SSL_CTX_new(SSLv23_client_method())) == 0)
    {
        errorOut = "SSL error while creating CTX";
        if(this->logFP != NULL) fprintf(this->logFP, "%s\n", errorOut.c_str());
        return(false);
    }

    if(enableDANE)
    {
        if(SSL_CTX_dane_enable(this->ssl_ctx) <= 0)
        {
            errorOut = "SSL error while enabling DANE on CTX";
            if(this->logFP != NULL) fprintf(this->logFP, "%s\n", errorOut.c_str());
            return(false);
        }
    }

    SSL_CTX_set_options(this->ssl_ctx, (SSL_OP_ALL & ~SSL_OP_TLSEXT_PADDING) | SSL_OP_NO_SSLv3 | SSL_OP_NO_SSLv2);

    if((this->ssl = SSL_new(this->ssl_ctx)) == 0)
    {
        SSL_CTX_free(this->ssl_ctx);

        this->ssl = NULL;
        this->ssl_ctx = NULL;

        errorOut = "SSL error while creating layer";
        if(this->logFP != NULL) fprintf(this->logFP, "%s\n", errorOut.c_str());
        return(false);
    }

    if(enableDANE)
    {
        if(SSL_dane_enable(this->ssl, daneDomain.c_str()) <= 0)
        {
            errorOut = "SSL error while enabling DANE on SSL";
            if(this->logFP != NULL) fprintf(this->logFP, "%s\n", errorOut.c_str());
            return(false);
        }

        SSL_dane_set_flags(this->ssl, DANE_FLAG_NO_DANE_EE_NAMECHECKS);

        if(!nextHopDomain.empty() && nextHopDomain != daneDomain)
        {
            SSL_add1_host(this->ssl, nextHopDomain.c_str());
        }

        SSL_set_hostflags(this->ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);

        daneResult->usableRecords = 0;
        for(vector<TLSARecord>::const_iterator it = tlsaRecords->begin();
            it != tlsaRecords->end();
            ++it)
        {
            if((it->usage != TLSARecord::TLSA_TRUST_ANCHOR_ASSERTION && it->usage != TLSARecord::TLSA_DOMAIN_ISSUED_CERT)
                || it->value.empty())
            {
                // not suitable
                continue;
            }

            int addRet = SSL_dane_tlsa_add(this->ssl,
                static_cast<uint8_t>(it->usage),
                static_cast<uint8_t>(it->selector),
                static_cast<uint8_t>(it->matchingType),
                const_cast<unsigned char *>(reinterpret_cast<const unsigned char *>(it->value.c_str())), //!< @todo Remove const_cast once fixed in OpenSSL
                it->value.size());
            if(addRet > 0)
            {
                ++daneResult->usableRecords;
            }
            else
            {
                if(this->logFP != NULL) fprintf(this->logFP, "(TLSA record not usable: %d %d %d %lu) ", it->usage, it->selector, it->matchingType, it->value.size());
            }
        }

        if(this->logFP != NULL) fprintf(this->logFP, "(usable TLSA records: %d) ", daneResult->usableRecords);

        if(daneResult->usableRecords > 0)
        {
            SSL_set_verify(this->ssl, SSL_VERIFY_PEER, NULL);
            daneResult->usingDANE = true;

            if(this->logFP != NULL) fprintf(this->logFP, "(SSL_VERIFY_PEER set) ");
        }
        else
        {
            SSL_set_verify(this->ssl, SSL_VERIFY_NONE, NULL);
            daneResult->usingDANE = false;

            if(this->logFP != NULL) fprintf(this->logFP, "(SSL_VERIFY_NONE set) ");
        }
    }

    SSL_clear(this->ssl);
    SSL_set_fd(this->ssl, this->iSocket);

    int res = SSL_connect(this->ssl);
    bool sessionReused = (res == 1 && SSL_session_reused(this->ssl));
    bool reusedSessionAuthenticated = (sessionReused && SSL_get_verify_result(this->ssl) == X509_V_OK);

    if(res != 1 || (sessionReused && !reusedSessionAuthenticated))
    {
        const char *errText = "unknown error";
        int errNo = -1, additionalCode = 0;

        if (res != 1)
        {
            errNo = SSL_get_error(this->ssl, res);

            switch(errNo)
            {
            case SSL_ERROR_NONE:
                errText = "SSL_ERROR_NONE";
                break;
            case SSL_ERROR_ZERO_RETURN:
                errText = "SSL_ERROR_ZERO_RETURN";
                break;
            case SSL_ERROR_WANT_READ:
                errText = "SSL_ERROR_WANT_READ";
                break;
            case SSL_ERROR_WANT_WRITE:
                errText = "SSL_ERROR_WANT_WRITE";
                break;
            case SSL_ERROR_WANT_CONNECT:
                errText = "SSL_ERROR_WANT_CONNECT";
                break;
            case SSL_ERROR_WANT_ACCEPT:
                errText = "SSL_ERROR_WANT_ACCEPT";
                break;
            case SSL_ERROR_WANT_X509_LOOKUP:
                errText = "SSL_ERROR_WANT_X509_LOOKUP";
                break;
            case SSL_ERROR_SYSCALL:
                errText = "SSL_ERROR_SYSCALL";
                break;
            case SSL_ERROR_SSL:
                if(enableDANE && (additionalCode = SSL_get_verify_result(this->ssl)) != X509_V_OK)
                    errText = "DANE verification failed";
                else
                    errText = "SSL_ERROR_SSL";
                break;
            default:
                errText = "unknown error";
                break;
            };
        }
        else if(sessionReused && !reusedSessionAuthenticated)
        {
            errText = "Reused session not authenticated";
        }

        stringstream ss;
        ss << "SSL error while connecting: " << errText << " (" << res << ", " << errNo << ", " << additionalCode << ")";

        SSL_free(this->ssl);
        SSL_CTX_free(this->ssl_ctx);

        this->ssl = NULL;
        this->ssl_ctx = NULL;

        errorOut = ss.str();

        if(this->logFP != NULL) fprintf(this->logFP, "%s\n", errorOut.c_str());
        return(false);
    }

    if(enableDANE)
    {
        daneResult->verified = SSL_get_verify_result(this->ssl) == X509_V_OK;

        EVP_PKEY *mspki = NULL;
        daneResult->depth = SSL_get0_dane_authority(this->ssl, NULL, &mspki);
        if(daneResult->depth >= 0)
        {
            SSL_get0_dane_tlsa(this->ssl,
                &daneResult->verifiedUsage,
                &daneResult->verifiedSelector,
                &daneResult->verifiedMType,
                NULL, NULL);
        }
    }

    SSL_CTX_set_mode(this->ssl_ctx, SSL_MODE_AUTO_RETRY);

    if(this->logFP != NULL) fprintf(this->logFP, "OK\n");
    return(true);
}
