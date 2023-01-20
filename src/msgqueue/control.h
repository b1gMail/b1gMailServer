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

#ifndef _MSGQUEUE_CONTROL_H
#define _MSGQUEUE_CONTROL_H

#include <core/core.h>
#include <core/socket.h>

#define MSGQUEUE_CONTROL_PORT_MIN   50000
#define MSGQUEUE_CONTROL_PORT_MAX   65000
#define MSGQUEUE_CONTROL_TIMEOUT    5

class APNSDispatcher;

class MSGQueue_Control_Connection
{
public:
    MSGQueue_Control_Connection(Socket *sock, const char *secret, MySQL_DB *db, APNSDispatcher *apnsDispatcher);
    ~MSGQueue_Control_Connection();

public:
    void Process();

private:
    bool ProcessRequestLine(const char *lineBuffer);

public:
    Socket *sock;
    bool quit;

private:
    const char *secret;
    bool authenticated;
    MySQL_DB *db;
    APNSDispatcher *apnsDispatcher;

    MSGQueue_Control_Connection(const MSGQueue_Control_Connection &);
    MSGQueue_Control_Connection &operator=(const MSGQueue_Control_Connection &);
};

class MSGQueue_Control
{
public:
    MSGQueue_Control(APNSDispatcher *apnsDispatcher);
    ~MSGQueue_Control();

public:
    void Run();

private:
    void DeterminePort();
    bool CheckPortStatus(const int port);
    void ProcessConnection(int sock, sockaddr_in* clientAddr);

public:
    bool bQuit;

public:
    int sock;

private:
    int port;
    in_addr_t addr;
    int backlog;
    char secret[33];
    MySQL_DB *db;
    APNSDispatcher *apnsDispatcher;

    MSGQueue_Control(const MSGQueue_Control &);
    MSGQueue_Control &operator=(const MSGQueue_Control &);
};

extern MSGQueue_Control *cMSGQueue_Control_Instance;
extern MSGQueue_Control_Connection *cMSGQueue_Control_Connection_Instance;

#endif
