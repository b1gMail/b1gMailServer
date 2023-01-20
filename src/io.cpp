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

#include <core/core.h>

#ifdef WIN32
extern bool bInterruptFGets;
#endif

size_t iTrafficIn = 0, iTrafficOut = 0;
bool bIO_SSL = false;
SSL *ssl;

#undef fwrite
#undef fgets
#undef fread
#undef printf

/*
 * fgets-wrapper (counts traffic if fp is stdin)
 */
char *my_fgets(char *buf, int s, FILE *fp)
{
    char *result;

#ifndef WIN32
    if(bIO_SSL && fp == stdin)
    {
        int iReadBytes = 0, iStepReadBytes = 0;
        memset(buf, 0, s);

        while(true)
        {
            iStepReadBytes = SSL_read(ssl, buf+iReadBytes, 1/*s-iReadBytes*/);
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
            HANDLE StdHandle = GetStdHandle(STD_INPUT_HANDLE);
            if(StdHandle == INVALID_HANDLE_VALUE || StdHandle == NULL)
                return(NULL);

            SOCKET sSocket = (SOCKET)StdHandle;

            unsigned long ul = 1;
            char *pointer = buf;
            int i = 0;
            bool bStop = false;

            if(ioctlsocket(sSocket, FIONBIO, &ul) != 0)
                return(NULL);

            fd_set fdSet;
            while(i < s && !bStop)
            {
                FD_ZERO(&fdSet);
                FD_SET(sSocket, &fdSet);

                struct timeval timeout;
                timeout.tv_sec = 0;
                timeout.tv_usec = 50000;

                bool sslPending = bIO_SSL && SSL_pending(ssl) > 0;

                if(!sslPending && select(sSocket+1, &fdSet, NULL, NULL, &timeout) < 0)
                    break;

                if(bInterruptFGets)
                    bStop = true;

                if(sslPending || FD_ISSET(sSocket, &fdSet))
                {
                    char cBuff;

                    while(i < s
                        && !bStop
                        && !bInterruptFGets)
                    {
                        int len;

                        if(bIO_SSL)
                        {
                            len = SSL_read(ssl, &cBuff, 1);

                            if(len == 0)
                            {
                                bStop = true;
                            }
                            else if(len < 0)
                            {
                                int errorCode = SSL_get_error(ssl, len);
                                if(errorCode != SSL_ERROR_WANT_READ && errorCode != SSL_ERROR_WANT_WRITE)
                                    bStop = true;
                            }
                        }
                        else
                        {
                            len = recv(sSocket, &cBuff, 1, 0);
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
            if(ioctlsocket(sSocket, FIONBIO, &ul) != 0)
                return(NULL);

            if(i == 0 || bInterruptFGets)
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
        iTrafficIn += strlen(result);

    return(result);
}

/*
 * fread-wrapper (counts traffic if fp is stdin)
 */
size_t my_fread(void *buf, size_t s1, size_t s2, FILE *fp)
{
    if(s1 == 0 || s2 == 0)
        return(0);

    size_t result = 0;

    if(bIO_SSL && fp == stdin)
    {
        int readBytes;
        int bytesToRead = s1*s2;

        while(bytesToRead > 0)
        {
            readBytes = SSL_read(ssl, (char *)buf+result, bytesToRead);

            if(readBytes <= 0)
                break;

            result += readBytes;
            bytesToRead -= readBytes;
        }
        result /= s1;
    }
#ifdef WIN32
    else if(fp == stdin)
    {
        int readBytes;
        int bytesToRead = s1*s2;

        while(bytesToRead > 0 && !bInterruptFGets)
        {
            // TODO: recv timeout
            readBytes = recv((SOCKET)GetStdHandle(STD_INPUT_HANDLE), (char *)buf+result, bytesToRead, 0);

            if(readBytes <= 0)
                break;

            result += readBytes;
            bytesToRead -= readBytes;
        }
        result /= s1;
    }
#endif
    else
    {
        result = fread(buf, s1, s2, fp);
    }
    if(fp == stdin && result > 0)
        iTrafficIn += result * s1;

    return(result);
}

/*
 * fwrite-wrapper (counts traffic if fp is stdout)
 */
size_t my_fwrite(const void *buf, size_t s1, size_t s2, FILE *fp)
{
    if(s1 == 0 || s2 == 0)
        return(0);

    size_t result;

    if(bIO_SSL && fp == stdout)
    {
        int SSLResult = SSL_write(ssl, buf, (int)(s1*s2));

        if(SSLResult > 0)
            result = (size_t)SSLResult / s1;
        else
            result = 0;
    }
#ifdef WIN32
    else if(fp == stdout)
    {
        result = send((SOCKET)GetStdHandle(STD_OUTPUT_HANDLE), (const char *)buf, s1*s2, 0);
        result /= s1;
    }
#endif
    else
    {
        result = fwrite(buf, s1, s2, fp);
    }
    if(fp == stdout && result > 0)
        iTrafficOut += result * s1;
    return(result);
}

/*
 * vprintf-wrapper (counts traffic)
 */
int my_vprintf(const char *str, va_list list)
{
    int result = 0;

#ifndef WIN32
    if(bIO_SSL)
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
        if(bIO_SSL)
        {
#endif
            result = SSL_write(ssl, szSSLString, result);
#ifdef WIN32
        }
        else
        {
            result = send((SOCKET)GetStdHandle(STD_OUTPUT_HANDLE), szSSLString, result, 0);
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
        iTrafficOut += (size_t)result;

    return(result);
}

/*
 * printf-wrapper (counts traffic)
 */
int my_printf(const char *str, ...)
{
    va_list list;
    va_start(list, str);

    int Result = my_vprintf(str, list);

    va_end(list);

    return(Result);
}

