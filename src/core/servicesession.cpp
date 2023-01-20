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

#include <core/servicesession.h>

ServiceSession::ServiceSession(SOCKET sIn, SOCKET sOut)
{
    if(sIn == (SOCKET)NULL)
    {
#ifdef WIN32
        this->sIn = (SOCKET)GetStdHandle(STD_INPUT_HANDLE);
#else
        this->sIn = fileno(stdin);
#endif
    }
    else
        this->sIn = sIn;

    if(sOut == (SOCKET)NULL)
    {
#ifdef WIN32
        this->sOut = (SOCKET)GetStdHandle(STD_OUTPUT_HANDLE);
#else
        this->sOut = fileno(stdout);
#endif
    }
    else
        this->sOut = sOut;

    this->db = NULL;    // TODO: connect

    this->trafficIn = 0;
    this->trafficOut = 0;

    this->ioSSL = false;
    this->ssl = NULL;

    this->quitService = false;
}

ServiceSession::~ServiceSession()
{
    if(this->db != NULL)
        delete db;
}

/*
 * fgets-wrapper (counts traffic if fp is stdin)
 */
char *ServiceSession::fgets(char *buf, int s, FILE *fp)
{
    char *result;

#ifndef WIN32
    if(this->ioSSL && fp == stdin)
    {
        int iReadBytes = 0, iStepReadBytes = 0;
        memset(buf, 0, s);

        while(true)
        {
            iStepReadBytes = SSL_read(this->ssl, buf+iReadBytes, 1/*s-iReadBytes*/);
            if(iStepReadBytes <= 0)
                return(NULL);
            if(*(buf+iReadBytes) == '\n')
                break;
            iReadBytes += iStepReadBytes;
            if(iReadBytes >= s-1)
                break;
        }
        result = buf;
    }
    else
    {
#else
    {
#endif
#ifdef WIN32
        if(fp == stdin)
        {
            unsigned long ul = 1;
            char *pointer = buf;
            int i = 0;
            bool bStop = false;

            if(ioctlsocket(this->sIn, FIONBIO, &ul) != 0)
                return(NULL);

            fd_set fdSet;
            while(i < s && !bStop)
            {
                FD_ZERO(&fdSet);
                FD_SET(this->sIn, &fdSet);

                struct timeval timeout;
                timeout.tv_sec = 0;
                timeout.tv_usec = 50000;

                bool sslPending = this->ioSSL && SSL_pending(this->ssl) > 0;

                if(!sslPending && select(this->sIn+1, &fdSet, NULL, NULL, &timeout) < 0)
                    break;

                if(this->quitService)
                    bStop = true;

                if(sslPending || FD_ISSET(this->sIn, &fdSet))
                {
                    char cBuff;

                    while(i < s
                        && !bStop
                        && !this->quitService)
                    {
                        int len;

                        if(this->ioSSL)
                        {
                            len = SSL_read(this->ssl, &cBuff, 1);

                            if(len == 0)
                            {
                                bStop = true;
                            }
                            else if(len < 0)
                            {
                                int errorCode = SSL_get_error(this->ssl, len);
                                if(errorCode != SSL_ERROR_WANT_READ && errorCode != SSL_ERROR_WANT_WRITE)
                                    bStop = true;
                            }
                        }
                        else
                        {
                            len = recv(this->sIn, &cBuff, 1, 0);
                            if(len == 0 || (len == -1 && WSAGetLastError() != WSAEWOULDBLOCK))
                                bStop = true;
                        }
                        if(len != 1)
                            break;
                        *pointer++ = cBuff;
                        i++;
                        if(cBuff == '\n')
                            bStop = true;
                    }
                }
            }

            *pointer = 0;

            ul = 0;
            if(ioctlsocket(this->sIn, FIONBIO, &ul) != 0)
                return(NULL);

            if(i == 0 || this->quitService)
                result = NULL;
            else
                result = buf;
        }
        else
#endif
        {
            result = fgets(buf, s, fp);
        }
    }

    if(fp == stdin && result != NULL)
        this->trafficIn += strlen(result);

    return(result);
}

/*
 * fread-wrapper (counts traffic if fp is stdin)
 */
size_t ServiceSession::fread(void *buf, size_t s1, size_t s2, FILE *fp)
{
    if(s1 == 0 || s2 == 0)
        return(0);

    size_t result = 0;

    if(this->ioSSL && fp == stdin)
    {
        int readBytes;
        int bytesToRead = s1*s2;

        while(bytesToRead > 0)
        {
            readBytes = SSL_read(this->ssl, (char *)buf+result, bytesToRead);

            if(readBytes <= 0)
                break;

            result += readBytes;
            bytesToRead -= readBytes;
        }
        result /= s1;
    }
    else if(fp == stdin)
    {
        result = recv(this->sIn, (char *)buf, s1*s2, 0);
        result /= s1;
    }
    else
    {
        result = fread(buf, s1, s2, fp);
    }
    if(fp == stdin && result > 0)
        this->trafficIn += result * s1;

    return(result);
}

/*
 * fwrite-wrapper (counts traffic if fp is stdout)
 */
size_t ServiceSession::fwrite(const void *buf, size_t s1, size_t s2, FILE *fp)
{
    if(s1 == 0 || s2 == 0)
        return(0);

    size_t result;

    if(this->ioSSL && fp == stdout)
    {
        int SSLResult = SSL_write(this->ssl, buf, (int)(s1*s2));

        if(SSLResult > 0)
            result = (size_t)SSLResult / s1;
        else
            result = 0;
    }
    else if(fp == stdout)
    {
        result = send(this->sOut, (const char *)buf, s1*s2, 0);
        result /= s1;
    }
    else
    {
        result = fwrite(buf, s1, s2, fp);
    }
    if(fp == stdout && result > 0)
        this->trafficOut += result * s1;
    return(result);
}

/*
 * vprintf-wrapper (counts traffic)
 */
int ServiceSession::vprintf(const char *str, va_list list)
{
    int result = 0;

#ifndef WIN32
    if(this->ioSSL)
#endif
    {
#ifndef WIN32
        char *szSSLString = NULL;
        result = vasprintf(&szSSLString, str, list);
#else
        int result = _vscprintf(str, list);
        char *szSSLString = (char *)malloc(result+1);
        result = vsprintf_s(szSSLString, result+1, str, list);
#endif

#ifdef WIN32
        if(this->ioSSL)
        {
#endif
            result = SSL_write(this->ssl, szSSLString, result);
#ifdef WIN32
        }
        else
        {
            result = send(this->sOut, szSSLString, result, 0);
        }
#endif
        free(szSSLString);
    }
#ifndef WIN32
    else
    {
        result = vprintf(str, list);

        // autoflush
        if(str[strlen(str)-1] == '\n')
            fflush(stdout);
    }
#endif

    if(result > 0)
        this->trafficOut += (size_t)result;

    return(result);
}

/*
 * printf-wrapper (counts traffic)
 */
int ServiceSession::printf(const char *str, ...)
{
    va_list list;
    va_start(list, str);

    int Result = my_vprintf(str, list);

    va_end(list);

    return(Result);
}
