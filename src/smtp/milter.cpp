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

#include "milter.h"

#include <iostream>

class MilterRejected {};

struct MilterData_OptNeg
{
    uint32_t version;
    uint32_t actions;
    uint32_t protocol;
};

void MilterPacket::writeToSocket(Socket *sock)
{
    uint32_t len = 1 + this->data.size();
    uint32_t nLen = htonl(len);
    int packetLen = sizeof(uint32_t) + len;

    char *packet = new char[packetLen];
    memcpy(packet, &nLen, sizeof(nLen));
    packet[4] = this->cmd;
    memcpy(packet+5, this->data.c_str(), this->data.size());

    try
    {
        if(sock->Write(packet, packetLen) != packetLen)
            throw Core::Exception("Failed to write data to milter socket!");
    }
    catch(...)
    {
        delete[] packet;
        throw;
    }

    delete[] packet;
}

MilterPacket MilterPacket::readFromSocket(Socket *sock)
{
    MilterPacket result;
    char *data = NULL;

    try
    {
        char cmd;
        uint32_t len;

        do
        {
            uint32_t nLen = 0;
            if(sock->Read((char *)&nLen, sizeof(nLen)) != sizeof(nLen))
                throw Core::Exception("Failed to read length from milter socket!");

            len = ntohl(nLen);
            if(len < 1)
                throw Core::Exception("Invalid milter packet length!");
            if(len > MILTER_MAX_PACKET)
                throw Core::Exception("Milter packet too long!");

            if(sock->Read((char *)&cmd, sizeof(cmd)) != sizeof(cmd))
                throw Core::Exception("Failed to read command from milter socket!");
        }
        while(cmd == SMFIR_PROGRESS);

        result.cmd = cmd;

        if(len > 1)
        {
            data = new char[len-1];
            if(static_cast<uint32_t>(sock->Read(data, len-1)) != len - 1)
                throw Core::Exception("Failed to read data from milter socket!");
            result.data = string(data, len-1);
            delete[] data;
            data = NULL;
        }
    }
    catch(...)
    {
        if(data != NULL)
        {
            delete[] data;
            data = NULL;
        }
        throw;
    }

    return result;
}

Milter::Milter(const string &host, const int port, const bool isUnixSocket, const MilterResponse defaultAct)
    : milterHost(host), milterPort(port), milterIsUnixSocket(isUnixSocket),
      sock(NULL), negAction(~0), negProtocol(0),
      lastAcceptRejectAction(SMFIR_ACCEPT),
      replBodyReceived(false),
      defaultAction(defaultAct),
      doQuarantine(false)
{

}

Milter::~Milter()
{
    if(this->sock != NULL)
    {
        delete this->sock;
        this->sock = NULL;
    }
}

void Milter::connect()
{
    if(this->sock != NULL)
    {
        delete this->sock;
        this->sock = NULL;
    }

    this->sock = new Socket((char *)this->milterHost.c_str(),
        this->milterPort,
        MILTER_TIMEOUT,
        this->milterIsUnixSocket);
    this->sock->SetNoDelay(true);
}

MilterResponse Milter::process()
{
    try
    {
        connect();

        negotiateOptions();

        if((negProtocol & SMFIP_NOCONNECT) == 0)
            sendConnect();

        if((negProtocol & SMFIP_NOHELO) == 0)
            sendHelo();

        if((negProtocol & SMFIP_NOMAIL) == 0)
            sendMail();

        if((negProtocol & SMFIP_NORCPT) == 0)
            sendRcpt();

        if((negProtocol & SMFIP_NOHDRS) == 0)
            sendHeaders();

        if((negProtocol & SMFIP_NOBODY) == 0)
            sendBody();

        sendEndOfBody();

        bool isAcceptRejectAction = false;
        do
        {
            checkResponse(&isAcceptRejectAction);
        }
        while(!isAcceptRejectAction);
    }
    catch(const MilterRejected &)
    {
    }
    catch(const Core::Exception &ex)
    {
        db->Log(CMP_SMTP, PRIO_WARNING, utils->PrintF("Exception while checking mail via milter: %s %s",
            ex.strPart.c_str(),
            ex.strError.c_str()));
        lastAcceptRejectAction = defaultAction;
    }
    catch(...)
    {
        db->Log(CMP_SMTP, PRIO_WARNING, utils->PrintF("Unknown exception while checking mail via milter"));
        lastAcceptRejectAction = defaultAction;
    }

    return lastAcceptRejectAction;
}

void Milter::addDelRcpt(const MilterPacket &p)
{
    string rcpt = string(p.data.c_str());

    size_t pos;
    while((pos = rcpt.find_first_of("<>")) != string::npos)
        rcpt.erase(rcpt.begin()+pos);

    bool exists = false;
    for(vector<SMTPRecipient>::iterator it = rcptTo.begin();
        it != rcptTo.end(); )
    {
        if(strcasecmp(it->strAddress.c_str(), rcpt.c_str()) == 0)
        {
            exists = true;

            if(p.cmd == SMFIR_DELRCPT)
            {
                it = rcptTo.erase(it);
                continue;
            }
        }

        ++it;
    }

    if(p.cmd == SMFIR_ADDRCPT && !exists)
    {
        SMTPRecipient newRcpt;
        newRcpt.iLocalRecipient = utils->LookupUser(rcpt.c_str(), false);
        newRcpt.iDeliveryStatusID = 0;
        newRcpt.strAddress = rcpt;
        rcptTo.push_back(newRcpt);
    }
}

void Milter::replBody(const MilterPacket &p)
{
    if(!replBodyReceived)
    {
        body.clear();
        replBodyReceived = true;
    }

    body += string(p.data.c_str());
}

void Milter::chgFrom(const MilterPacket &p)
{
    mailFrom = string(p.data.c_str());

    size_t pos;
    while((pos = mailFrom.find_first_of("<>")) != string::npos)
        mailFrom.erase(mailFrom.begin()+pos);
}

void Milter::processReplyCode(const MilterPacket &p)
{
    string code = string(p.data.c_str());
    if(code.length() < 3)
        throw Core::Exception("Reply code received from milter is too short");

    string codeNum = code.substr(0, 3);
    string codeText;

    if(code.length() > 4)
        codeText = code.substr(4);

    replyCodeNum = atoi(codeNum.c_str());
    replyCodeText = codeText;
}

void Milter::checkResponse(bool *isAcceptRejectAction)
{
    bool acceptReject = false, doThrow = false;
    MilterPacket r = MilterPacket::readFromSocket(sock);

    switch(r.cmd)
    {
    case SMFIR_ADDRCPT:
        addDelRcpt(r);
        break;

    case SMFIR_DELRCPT:
        addDelRcpt(r);
        break;

    case SMFIR_SHUTDOWN:
        acceptReject = true;
        doThrow = true;
        break;

    case SMFIR_ACCEPT:
        acceptReject = true;
        doThrow = true;
        break;

    case SMFIR_REPLBODY:
        replBody(r);
        break;

    case SMFIR_CONTINUE:
        acceptReject = true;
        break;

    case SMFIR_DISCARD:
        acceptReject = true;
        doThrow = true;
        break;

    case SMFIR_CHGFROM:
        chgFrom(r);
        break;

    case SMFIR_CONN_FAIL:
        acceptReject = true;
        doThrow = true;
        break;

    case SMFIR_ADDHEADER:
        insertHeader(r);
        break;

    case SMFIR_INSHEADER:
        insertHeader(r);
        break;

    case SMFIR_SETSYMLIST:
        // not supported
        break;

    case SMFIR_CHGHEADER:
        insertHeader(r);
        break;

    case SMFIR_QUARANTINE:
        doQuarantine = true;
        break;

    case SMFIR_REJECT:
        acceptReject = true;
        doThrow = true;
        break;

    case SMFIR_SKIP:
        // not supported
        break;

    case SMFIR_TEMPFAIL:
        acceptReject = true;
        doThrow = true;
        break;

    case SMFIR_REPLYCODE:
        acceptReject = true;
        doThrow = true;
        processReplyCode(r);
        break;

    default:
        break;
    }

    if(isAcceptRejectAction != NULL)
        *isAcceptRejectAction = acceptReject;

    if(acceptReject && (r.cmd != SMFIR_ACCEPT || lastAcceptRejectAction != SMFIR_DISCARD))
        lastAcceptRejectAction = static_cast<MilterResponse>(r.cmd);

    if(doThrow)
        throw MilterRejected();
}

void Milter::insertHeader(const MilterPacket &p)
{
    uint32_t index = 0;
    std::size_t offset = sizeof(uint32_t);

    if(p.cmd == SMFIR_ADDHEADER)
    {
        index = headers.size() - 1;
        offset = 0;
    }
    else if(p.cmd == SMFIR_INSHEADER
        || p.cmd == SMFIR_CHGHEADER)
    {
        memcpy(&index, p.data.c_str(), sizeof(index));
        index = ntohl(index);
    }

    if(p.data.size() < offset + 3)
        throw Core::Exception("Invalid length of milter header package");

    string key, value;
    bool keyComplete = false;
    for(string::const_iterator it = p.data.begin()+offset;
        it != p.data.end();
        ++it)
    {
        char c = *it;
        if(c == '\0')
        {
            if(keyComplete) break;
            keyComplete = true;
            continue;
        }

        if(keyComplete)
            value.append(1, c);
        else
            key.append(1, c);
    }

    if(p.cmd == SMFIR_INSHEADER || p.cmd == SMFIR_ADDHEADER)
    {
        if(index > headers.size()-1)
            index = headers.size()-1;
        headers.insert(headers.begin()+index, pair<string, string>(key, value));
    }
    else if(p.cmd == SMFIR_CHGHEADER)
    {
        uint32_t counter = 0;
        for(vector<pair<string, string> >::iterator it = headers.begin();
            it != headers.end();
            ++it)
        {
            if(strcasecmp(it->first.c_str(), key.c_str()) == 0)
            {
                if(++counter == index)
                {
                    if(value.empty())
                        headers.erase(it);
                    else
                        it->second = value;
                    break;
                }
            }
        }
    }
}

void Milter::negotiateOptions()
{
    MilterData_OptNeg onp;
    onp.version         = htonl(MILTER_VERSION);
    onp.actions         = htonl(
          SMFIF_ADDHDRS
        | SMFIF_CHGBODY
        | SMFIF_ADDRCPT
        | SMFIF_DELRCPT
        | SMFIF_CHGHDRS
        | SMFIF_QUARANTINE
    );
    onp.protocol        = 0;

    MilterPacket p;
    p.cmd   = SMFIC_OPTNEG;
    p.data  = string((const char *)&onp, sizeof(onp));

    p.writeToSocket(sock);

    MilterPacket r  = MilterPacket::readFromSocket(sock);
    if(r.data.size() != sizeof(MilterData_OptNeg))
        throw Core::Exception("Invalid length of milter OptNeg package");

    MilterData_OptNeg onr;
    memcpy(&onr, r.data.c_str(), sizeof(onr));

    onr.version     = ntohl(onr.version);
    onr.actions     = ntohl(onr.actions);
    onr.protocol    = ntohl(onr.protocol);

    if(onr.version != MILTER_VERSION)
        throw Core::Exception("Unsupported milter version!");

    this->negAction     = onr.actions;
    this->negProtocol   = onr.protocol;
}

void Milter::sendConnect()
{
    uint16_t port = htons(connectionInformation.port);

    string data;
    data  = connectionInformation.hostName;
    data.append(1, '\0');
    data.append(1, static_cast<char>(connectionInformation.family));
    data.append(reinterpret_cast<const char *>(&port), sizeof(port));
    data.append(connectionInformation.ipAddress);
    data.append(1, '\0');

    MilterPacket p;
    p.cmd       = SMFIC_CONNECT;
    p.data      = data;

    p.writeToSocket(sock);

    checkResponse();
}

void Milter::sendHelo()
{
    string data = connectionInformation.heloHostName;
    data.append(1, '\0');

    MilterPacket p;
    p.cmd       = SMFIC_HELO;
    p.data      = data;

    p.writeToSocket(sock);

    checkResponse();
}

void Milter::sendMail()
{
    {
        string macroData;
        macroData.append(1, SMFIC_MAIL);
        macroData.append("{auth_type}");
        macroData.append(1, '\0');
        macroData.append(connectionInformation.isAuthenticated ? connectionInformation.authMethod : "");
        macroData.append(1, '\0');

        MilterPacket macro;
        macro.cmd   = SMFIC_MACRO;
        macro.data  = macroData;

        macro.writeToSocket(sock);
    }

    string data = mailFrom;
    data.append(1, '\0');

    MilterPacket p;
    p.cmd       = SMFIC_MAIL;
    p.data      = data;

    p.writeToSocket(sock);

    checkResponse();
}

void Milter::sendRcpt()
{
    for(vector<SMTPRecipient>::iterator it = rcptTo.begin(); it != rcptTo.end(); ++it)
    {
        string data = it->strAddress;
        data.append(1, '\0');

        MilterPacket p;
        p.cmd   = SMFIC_RCPT;
        p.data  = data;

        p.writeToSocket(sock);

        checkResponse();
    }
}

void Milter::sendHeaders()
{
    for(vector<pair<string, string> >::iterator it = headers.begin(); it != headers.end(); ++it)
    {
        string data = it->first;
        data.append(1, '\0');
        data.append(it->second);
        data.append(1, '\0');

        MilterPacket p;
        p.cmd   = SMFIC_HEADER;
        p.data  = data;

        p.writeToSocket(sock);

        checkResponse();
    }

    if((negProtocol & SMFIP_NOEOH) == 0)
    {
        MilterPacket p;
        p.cmd       = SMFIC_EOH;
        p.data      = string();
        p.writeToSocket(sock);
        checkResponse();
    }
}

void Milter::sendBody()
{
    try
    {
        for(size_t pos = 0; pos < body.size(); pos += MILTER_CHUNK_SIZE)
        {
            MilterPacket p;
            p.cmd   = SMFIC_BODY;
            p.data  = body.substr(pos, MILTER_CHUNK_SIZE);

            p.writeToSocket(sock);

            checkResponse();
        }
    }
    catch(...)
    {
        throw;
    }
}

void Milter::sendEndOfBody()
{
    MilterPacket p;
    p.cmd       = SMFIC_BODYEOB;
    p.data      = string();

    p.writeToSocket(sock);

    checkResponse();
}
