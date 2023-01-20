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

#ifndef _MSGQUEUE_SMTPSESSION_H_
#define _MSGQUEUE_SMTPSESSION_H_

#include <core/core.h>
#include <core/socket.h>
#include <core/dns.h>

class SMTPSession
{
public:
    SMTPSession(const string &domain, const string &host, int port = 25);
    ~SMTPSession();

public:
    void beginSession(bool ehlo = false);
    void endSession();
    void auth(const string &user, const string &pass);
    void rset();
    void mailFrom(const string &returnPath, int mailSize);
    void rcptTo(const string &recipient);
    void data(FILE *stream);
    void startTLS();

private:
    string readResponse();
    void throwError(string response, const string &tempMsg, const string &fatalMsg, const string &elseMsg,
        const string &tempStatusCode, const string &fatalStatusCode, const string &elseStatusCode);
    void addToGreylist();
    void lookupTLSARecords();

public:
    string domain;
    string host;
    int port;
    time_t lastUse;
    bool sessionActive;
    bool addedToGreylist;
    bool authenticated;
    bool isMXConnection;
    bool tlsSupported;
    bool useTLS;
    bool useTLSA;
    bool mxAuthenticated;
    bool tlsActive;
    bool enableLogging;
    bool isDirty;
    bool sizeSupported;
    vector<TLSARecord> tlsaRecords;
    DANEResult daneResult;

private:
    Socket *sock;

    SMTPSession(const SMTPSession &);
    SMTPSession &operator=(const SMTPSession &);
};

#endif
