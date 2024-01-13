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

#ifndef _SMTP_SMTP_H
#define _SMTP_SMTP_H

#include <core/core.h>
#include <smtp/spf.h>
#include <msgqueue/msgqueue.h>
#include <imap/imap.h>
#include <imap/mail.h>
#include <fcntl.h>

#define SMTP_LINEMAX    (1000+1)                // rfc2821 / 4.5.3.1

enum eSMTPPeerOrigin
{
    SMTP_PEER_ORIGIN_UNKNOWN        = 0,
    SMTP_PEER_ORIGIN_DEFAULT        = 1,
    SMTP_PEER_ORIGIN_TRUSTED        = 2,
    SMTP_PEER_ORIGIN_DIALUP         = 3,
    SMTP_PEER_ORIGIN_REJECT         = 4,
    SMTP_PEER_ORIGIN_NOGREY         = 5,
    SMTP_PEER_ORIGIN_NOGREYANDBAN   = 6
};

enum eSMTPState
{
    SMTP_STATE_PREHELO,
    SMTP_STATE_HELO,
    SMTP_STATE_EHLO,
    SMTP_STATE_MAIL,
    SMTP_STATE_DATA,
    SMTP_STATE_QUIT
};

enum eSenderCheckMode
{
    SMTP_SENDERCHECK_NO,
    SMTP_SENDERCHECK_MAILFROM,
    SMTP_SENDERCHECK_FULL
};

struct SMTPRecipient
{
    string strAddress;
    int iLocalRecipient;
    int iDeliveryStatusID;
};

class SMTP
{
public:
    SMTP();
    ~SMTP();

public:
    void Run();

private:
    void ClassifyPeer();
    void Error(int iErrNo, const char *szMsg);
    bool ProcessLine(char *szLine);
    bool ProcessDataLine(char *szLine);
    void ProcessMessage();

private:
    void Helo(char *szLine, bool bEhlo = false);
    void Auth(char *szLine);
    void Mail(char *szLine);
    void Rcpt(char *szLine);
    void Data();
    void Rset(bool bSilent = false);
    void Noop();
    void StartTLS();
    void Quit();

public:
    int iUserID;
    int ib1gMailUserID;
    bool bQuit;
    bool bBanned;
    bool bTimeout;
    bool bTLSMode;
    bool bSubmission;
    SSL_CTX *ssl_ctx;

private:
    string strPeer;
    int iPeerOrigin;
    string strPeerHost;
    bool bHasReverseDNS;
    int iState;
    int iInboundMessages;
    int iOutboundMessages;
    int iErrorCounter;
    int iCommands;
    int iRecipientLimit;
    int iMessageSizeLimit;
    int iMessageCountLimit;
    int iSMTPLimitCount;
    int iSMTPLimitTime;
    bool bTodayKeyValid;
    string strHeloHost;
    string strReturnPath;
    string strRealPeer;
    bool bAuthenticated;
    bool bEhlo;
    bool bGreylisted;
    string strAuthUser;
    vector<SMTPRecipient> vRecipients;
    bool bSizeLimitExceeded;
    bool bWriteMessageIDHeader;
    bool bWriteDateHeader;
    bool bPrevLineEndsWithCrLf;
    bool bDataPassedHeaders;
    bool bValidFromHeader;
    eSenderCheckMode iSenderCheckMode;
    SPFResult spfResult;
    string spfExplanation;
    int iLastFetch;
    int iHopCounter;
    int iHeaderBytes;
    vector<pair<string, string> > vHeaders;
    string strBody;
    string strAuthMethod;

    SMTP(const SMTP &);
    SMTP &operator=(const SMTP &);
};

extern SMTP *cSMTP_Instance;

#endif
