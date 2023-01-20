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

#ifndef _SOCKET_H
#define _SOCKET_H

class Socket
{
public:
    Socket(const char *szHost, int iPort, int iTimeout = 0);
    ~Socket();

public:
    int Write(const char *szBuff, int iLen = -1);
    int Read(char *szBuff, int iLen);
    int PrintF(const char *szFormat, ...);
    char *ReadLine(char *szBuffer, int iBufferLength);

public:
    int iSocket;
    int InAddr;
};

#endif
