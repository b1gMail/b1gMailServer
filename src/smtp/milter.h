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

#ifndef _MILTER_H_
#define _MILTER_H_

#include <core/core.h>

#include <utility>

#include "smtp.h"

#define MILTER_MAX_PACKET       (512*1024)
#define MILTER_VERSION          2
#define MILTER_CHUNK_SIZE       65535
#define MILTER_TIMEOUT          90

enum MilterFlags
{
    MILTERFLAG_UNIXSOCKET       = (1<<0),
    MILTERFLAG_NONAUTH          = (1<<1),
    MILTERFLAG_AUTH             = (1<<2)
};

enum MilterOptNegAction
{
    SMFIF_ADDHDRS               = 0x01,
    SMFIF_CHGBODY               = 0x02,
    SMFIF_ADDRCPT               = 0x04,
    SMFIF_DELRCPT               = 0x08,
    SMFIF_CHGHDRS               = 0x10,
    SMFIF_QUARANTINE            = 0x20
};

enum MilterOptNegProtocol
{
    SMFIP_NOCONNECT             = 0x01,
    SMFIP_NOHELO                = 0x02,
    SMFIP_NOMAIL                = 0x04,
    SMFIP_NORCPT                = 0x08,
    SMFIP_NOBODY                = 0x10,
    SMFIP_NOHDRS                = 0x20,
    SMFIP_NOEOH                 = 0x40
};

enum MilterCommand
{
    SMFIC_ABORT                 = 'A',
    SMFIC_BODY                  = 'B',
    SMFIC_CONNECT               = 'C',
    SMFIC_MACRO                 = 'D',
    SMFIC_HELO                  = 'H',
    SMFIC_MAIL                  = 'M',
    SMFIC_RCPT                  = 'R',
    SMFIC_BODYEOB               = 'E',
    SMFIC_HEADER                = 'L',
    SMFIC_EOH                   = 'N',
    SMFIC_OPTNEG                = 'O',
    SMFIC_QUIT                  = 'Q'
};

enum MilterResponse
{
    SMFIR_ADDRCPT               = '+',
    SMFIR_DELRCPT               = '-',
    SMFIR_SHUTDOWN              = '4',
    SMFIR_ACCEPT                = 'a',
    SMFIR_REPLBODY              = 'b',
    SMFIR_CONTINUE              = 'c',
    SMFIR_DISCARD               = 'd',
    SMFIR_CHGFROM               = 'e',
    SMFIR_CONN_FAIL             = 'f',
    SMFIR_ADDHEADER             = 'h',
    SMFIR_INSHEADER             = 'i',
    SMFIR_SETSYMLIST            = 'l',
    SMFIR_CHGHEADER             = 'm',
    SMFIR_PROGRESS              = 'p',
    SMFIR_QUARANTINE            = 'q',
    SMFIR_REJECT                = 'r',
    SMFIR_SKIP                  = 's',
    SMFIR_TEMPFAIL              = 't',
    SMFIR_REPLYCODE             = 'y'
};

enum MilterProtocolFamily
{
    SMFIA_UNKNOWN               = 'U',
    SMFIA_UNIX                  = 'L',
    SMFIA_INET                  = '4',
    SMFIA_INET6                 = '6'
};

struct MilterPacket
{
    char cmd;
    string data;

    MilterPacket()
        : cmd(0)
    {
    }

    void writeToSocket(Socket *sock);
    static MilterPacket readFromSocket(Socket *sock);
};

struct MilterConnectionInformation
{
    string hostName;
    MilterProtocolFamily family;
    unsigned short port;
    string ipAddress;
    string heloHostName;
    bool isAuthenticated;
    string authMethod;
};

class Milter
{
public:
    Milter(const string &host, const int port, const bool isUnixSocket, const MilterResponse defaultAct);
    ~Milter();

public:
    void setConnectionInformation(const MilterConnectionInformation &info)
    {
        this->connectionInformation = info;
    }

    void setMailFrom(const string &addr)
    {
        this->mailFrom = addr;
    }

    const string &getMailFrom() const
    {
        return this->mailFrom;
    }

    void setRcptTo(const vector<SMTPRecipient> &addrs)
    {
        this->rcptTo = addrs;
    }

    const vector<SMTPRecipient> &getRcptTo() const
    {
        return this->rcptTo;
    }

    void setHeaders(const vector<pair<string, string> > &heads)
    {
        this->headers = heads;
    }

    const vector<pair<string, string> > &getHeaders() const
    {
        return this->headers;
    }

    void setBody(const string &data)
    {
        this->body = data;
    }

    const string &getBody() const
    {
        return this->body;
    }

    MilterResponse process();

    pair<int, string> getReplyCode() const
    {
        return pair<int, string>(replyCodeNum, replyCodeText);
    }

private:
    void connect();
    void checkResponse(bool *isAcceptRejectAction = NULL);
    void negotiateOptions();
    void sendConnect();
    void sendHelo();
    void sendMail();
    void sendRcpt();
    void sendHeaders();
    void sendBody();
    void sendEndOfBody();

    void insertHeader(const MilterPacket &p);
    void addDelRcpt(const MilterPacket &p);
    void replBody(const MilterPacket &p);
    void chgFrom(const MilterPacket &p);
    void processReplyCode(const MilterPacket &p);

private:
    string milterHost;
    int milterPort;
    bool milterIsUnixSocket;
    Socket *sock;
    uint32_t negAction;
    uint32_t negProtocol;
    MilterConnectionInformation connectionInformation;
    string mailFrom;
    vector<SMTPRecipient> rcptTo;
    vector<pair<string, string> > headers;
    string body;
    MilterResponse lastAcceptRejectAction;
    bool replBodyReceived;
    MilterResponse defaultAction;
    int replyCodeNum;
    string replyCodeText;

    Milter(const Milter &);
    Milter &operator=(const Milter &);
};

#endif
