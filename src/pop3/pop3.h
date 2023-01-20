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

#ifndef _POP3_POP3_H
#define _POP3_POP3_H

#include <core/core.h>

enum ePOP3State
{
    POP3_STATE_AUTHORIZATION,
    POP3_STATE_TRANSACTION
};

struct POP3Msg
{
    int iNr;
    int iID;
    size_t iSize;
    string strUID;
    bool bFile;
    bool bDele;
    int iBlobStorage;
};

class POP3
{
public:
    POP3();
    ~POP3();

public:
    void Run();

private:
    bool ProcessLine(char *szLine);
    void IncGeneration(int iGeneration, int iStructureGeneration);
    void Redirect(int iPort, const string &strUser);
    bool User(char *szLine);
    void Pass(char *szLine);
    void List(char *szLine);
    void Uidl(char *szLine);
    void Dele(char *szLine);
    void Retr(char *szLine);
    void Top(char *szLine);
    void Noop();
    void Rset();
    void Stat();
    void Capa();
    void STLS();
    void Quit();

public:
    int iUserID;
    bool bTimeout;
    bool bQuit;
    bool bTLSMode;
    bool bBanned;
    SSL_CTX *ssl_ctx;

private:
    string strPeer;
    std::string strUser;
    int iState;
    int iCommands;
    int iFetched;
    int iPartFetched;
    int iBadTries;
    int iDele;
    my_ulonglong iDropSize;
    my_ulonglong iDeleSize;
    vector<POP3Msg> vMessages;

    POP3(const POP3 &);
    POP3 &operator=(const POP3 &);
};

extern POP3 *cPOP3_Instance;

#endif
