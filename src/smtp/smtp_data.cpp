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

#include <smtp/smtp.h>
#include <smtp/milter.h>

#include <sstream>

/*
 * DATA command
 */
void SMTP::Data()
{
    db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] DATA",
        this->strPeer.c_str()));

    // check state
    if(this->iState != SMTP_STATE_MAIL
        || this->vRecipients.size() < 1)
    {
        this->Error(503, "5.5.1 Please send valid MAIL/RCPT commands first");
        return;
    }

    // enter data state
    this->iHopCounter = 0;
    this->bValidFromHeader = false;
    this->bSizeLimitExceeded = false;
    this->bDataPassedHeaders = false;
    this->bWriteMessageIDHeader = this->bAuthenticated || this->iPeerOrigin == SMTP_PEER_ORIGIN_TRUSTED;
    this->bWriteDateHeader = this->bAuthenticated || this->iPeerOrigin == SMTP_PEER_ORIGIN_TRUSTED;
    this->iHeaderBytes = 0;
    this->strBody.clear();
    this->vHeaders.clear();
    this->iState = SMTP_STATE_DATA;
    printf("354 Send data (max %d bytes), end with \\r\\n.\\r\\n\r\n",
        atoi(cfg->Get("smtp_size_limit")));
}

/*
 * Process message
 */
void SMTP::ProcessMessage()
{
    int iHopLimit = atoi(cfg->Get("smtp_hop_limit"));
    bool bHaveSendStats = strcmp(cfg->Get("enable_sendstats"), "1") == 0;

    // size limit exceeded?
    if(this->bSizeLimitExceeded)
    {
        this->Error(552, "5.3.4 Message size limit exceeded");
    }

    // from header error?
    else if(this->bAuthenticated
         && this->iSenderCheckMode == SMTP_SENDERCHECK_FULL
         && !this->bValidFromHeader)
    {
        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] Message data rejected (no valid From header found)",
            this->strPeer.c_str()));
        this->Error(554, "5.7.1 Please supply 'From' header field disclosing one of your account's email addresses");
    }

    // hop count limit exceeded?
    else if(iHopLimit > 0 && this->iHopCounter >= iHopLimit)
    {
        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] Message rejected (hop count %d exceeds limit of %d hops)",
            this->strPeer.c_str(),
            this->iHopCounter,
            iHopLimit));
        this->Error(554, "5.4.0 Too many hops");
    }

    // no => enqueue!
    else
    {
        bool bLimitError = false;
        int iRecipientCount = 0;

        // check group smtp limit again (otherwise it could be circumvented by creating many parallel sessions)
        // discovered in analysis of #1623
        if(this->bAuthenticated
            && this->iSMTPLimitCount > 0
            && this->iSMTPLimitTime > 0)
        {
            if(bHaveSendStats)
            {
                bLimitError = !utils->MaySendMail(this->iUserID,
                    (int)this->vRecipients.size(),
                    this->iSMTPLimitCount,
                    this->iSMTPLimitTime,
                    &iRecipientCount);
            }
            else
            {
                MYSQL_ROW row;
                MySQL_Result *res = db->Query("SELECT SUM(`recipients`) FROM bm60_bms_smtpstats WHERE `userid`=%d AND `time`>=%d",
                                             this->iUserID,
                                             (int)time(NULL)-this->iSMTPLimitTime*60);
                if(res->NumRows() == 1)
                {
                    row = res->FetchRow();

                    if(row[0] != NULL && ((iRecipientCount = atoi(row[0])) + (int)this->vRecipients.size()) >= this->iSMTPLimitCount)
                        bLimitError = true;
                }
                delete res;
            }
        }

        if(bLimitError)
        {
            utils->AddAbusePoint(this->iUserID, BMAP_SEND_FREQ_LIMIT, "SMTP, %d recipients in the last %d minutes",
                iRecipientCount,
                this->iSMTPLimitTime);
            this->Error(450, "4.5.3 Mail limit reached. Please try again later.");
            db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("Recipient rejected (group SMTP limit for user %d reached)",
                                                        this->iUserID));
        }
        else
        {
            unsigned long iStatInsertID = 0;

            if(this->bAuthenticated)
            {
                if(bHaveSendStats)
                {
                    iStatInsertID = utils->AddSendStat(this->iUserID, (int)this->vRecipients.size());
                }
                else
                {
                    db->Query("INSERT INTO bm60_bms_smtpstats(`userid`,`recipients`,`time`) VALUES(%d,%d,%d)",
                             this->iUserID,
                             (int)this->vRecipients.size(),
                             (int)time(NULL));
                    iStatInsertID = db->InsertId();
                }
            }

            bool bOK = true;

            // build additional headers
            if(!this->bAuthenticated
                && this->iPeerOrigin != SMTP_PEER_ORIGIN_TRUSTED
                && strcmp(cfg->Get("spf_enable"), "1") == 0
                && strcmp(cfg->Get("spf_inject_header"), "1") == 0)
            {
                string addHeader;

                switch(this->spfResult)
                {
                case SPF_RESULT_NONE:
                    addHeader += string("None");
                    break;

                case SPF_RESULT_NEUTRAL:
                    addHeader += string("Neutral");
                    break;

                case SPF_RESULT_PASS:
                    addHeader += string("Pass");
                    break;

                case SPF_RESULT_FAIL:
                    addHeader += string("Fail");
                    break;

                case SPF_RESULT_SOFTFAIL:
                    addHeader += string("SoftFail");
                    break;

                case SPF_RESULT_TEMPERROR:
                    addHeader += string("TempError");
                    break;

                case SPF_RESULT_PERMERROR:
                    addHeader += string("PermError");
                    break;

                default:
                    addHeader += string("Unknown");
                    break;
                };

                addHeader += string("\r\n\tidentity=") + this->strReturnPath
                            + string("; client-ip=") + this->strPeer
                            + string(";\r\n\thelo=") + this->strHeloHost;

                this->vHeaders.insert(this->vHeaders.begin(),
                    pair<string, string>("Received-SPF", addHeader));
            }

            // prepare milter connection info
            MilterConnectionInformation connectionInfo;
            connectionInfo.hostName = (strPeerHost == "(unknown)" ? "localhost" : strPeerHost);
            if(connectionInfo.hostName == strRealPeer)
                connectionInfo.hostName = "[" + connectionInfo.hostName + "]";
            connectionInfo.heloHostName = this->strHeloHost;
            if(this->strPeer.empty() || this->strPeer == "(unknown)")
            {
                connectionInfo.family = SMFIA_UNKNOWN;
            }
            else
            {
                connectionInfo.family = (IPAddress(this->strPeer).isIPv6 ? SMFIA_INET6 : SMFIA_INET);
                connectionInfo.ipAddress = this->strPeer;
                connectionInfo.port = utils->GetPeerPort();
            }
            connectionInfo.isAuthenticated = this->bAuthenticated;
            connectionInfo.authMethod = this->strAuthMethod;

            // process milters
            bool doRejectFromMilter = false, doShutdownFromMilter = false, doQuarantineFromMilter = false;
            MYSQL_ROW row;
            MySQL_Result *res = db->Query("SELECT `milterid`,`title`,`hostname`,`port`,`flags`,`default_action` FROM bm60_bms_milters ORDER BY `pos` ASC");
            while((row = res->FetchRow()))
            {
                const std::string host(row[2]);
                int port = atoi(row[3]);
                int flags = atoi(row[4]);
                MilterResponse defaultAction = static_cast<MilterResponse>(atoi(row[5]));

                if(((flags & MILTERFLAG_NONAUTH) && !this->bAuthenticated)
                    || ((flags & MILTERFLAG_AUTH) && this->bAuthenticated))
                {
                    db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("Checking mail using milter #%s",
                        row[0]));

                    bool isUnixSocket = (flags & MILTERFLAG_UNIXSOCKET) != 0;

                    Milter m(host, port, isUnixSocket, defaultAction);
                    m.setConnectionInformation(connectionInfo);
                    m.setMailFrom(this->strReturnPath);
                    m.setRcptTo(this->vRecipients);
                    m.setHeaders(this->vHeaders);
                    m.setBody(this->strBody);
                    MilterResponse mr = m.process();

                    db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("Result of milter #%s: %d",
                        row[0],
                        static_cast<int>(mr)));

                    if(mr == SMFIR_ACCEPT || mr == SMFIR_CONTINUE)
                    {
                        this->vHeaders          = m.getHeaders();
                        this->strBody           = m.getBody();
                        this->vRecipients       = m.getRcptTo();
                        this->strReturnPath     = m.getMailFrom();
                        doQuarantineFromMilter  = m.getDoQuarantine();
                    }

                    switch(mr)
                    {
                    case SMFIR_ACCEPT:
                    case SMFIR_CONTINUE:
                        break;

                    case SMFIR_DISCARD:
                    case SMFIR_REJECT:
                        doRejectFromMilter = true;
                        this->Error(554, "5.7.1 Mail rejected by content filter");
                        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("Mail discarded/rejected from milter %s",
                            row[0]));
                        break;

                    case SMFIR_CONN_FAIL:
                        doRejectFromMilter = true;
                        doShutdownFromMilter = true;
                        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("Connection failure requested from milter %s",
                            row[0]));
                        break;

                    case SMFIR_TEMPFAIL:
                        doRejectFromMilter = true;
                        this->Error(451, "4.7.0 Please try again later");
                        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("Mail rejected temporarily from milter %s",
                            row[0]));
                        break;

                    case SMFIR_REPLYCODE:
                        doRejectFromMilter = true;
                        this->Error(m.getReplyCode().first,
                            m.getReplyCode().second.c_str());
                        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("Mail rejected from milter %s: %d %s",
                            row[0],
                            m.getReplyCode().first,
                            m.getReplyCode().second.c_str()));
                        break;

                    case SMFIR_SHUTDOWN:
                        doRejectFromMilter = true;
                        doShutdownFromMilter = true;
                        this->Error(421, "Shutting down connection");
                        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("Connection shutdown upon request from milter %d",
                            row[0]));
                        break;

                    default:
                        db->Log(CMP_SMTP, PRIO_WARNING, utils->PrintF("Unhandled milter result from milter %s: %d",
                            row[0],
                            static_cast<int>(mr)));
                        break;
                    }

                    if(doRejectFromMilter)
                        break;
                }
            }
            delete res;

            if(doRejectFromMilter)
            {
                if(doShutdownFromMilter)
                    this->iState = SMTP_STATE_QUIT;
            }
            else
            {
                // enqueue message for each recipient
                MSGQueue *mq = new MSGQueue();
                vector<int> queueIDs;
                for(unsigned int i=0; i<this->vRecipients.size(); i++)
                {
                    int currentID;

                    if((currentID = mq->EnqueueMessage(this->vHeaders,
                        this->strBody,
                        this->vRecipients.at(i).iLocalRecipient == 0 ? MESSAGE_OUTBOUND : MESSAGE_INBOUND,
                        this->strReturnPath.c_str(),
                        this->vRecipients.at(i).strAddress.c_str(),
                        this->iUserID,
                        true,
                        this->bWriteMessageIDHeader,
                        this->bWriteDateHeader,
                        this->strHeloHost.c_str(),
                        this->strPeer.c_str(),
                        this->strPeerHost.c_str(),
                        this->iUserID != 0 ? this->iUserID : this->ib1gMailUserID,
                        false,
                        this->bTLSMode,
                        this->vRecipients.at(i).iDeliveryStatusID,
                        doQuarantineFromMilter ? MSGQUEUEFLAG_IS_SPAM : 0)) <= 0)
                    {
                        bOK = false;
                        break;
                    }
                    else
                    {
                        queueIDs.push_back(currentID);
                        this->vRecipients.at(i).iLocalRecipient == 0 ? this->iOutboundMessages++ : this->iInboundMessages++;
                    }
                }

                // report status
                if(bOK)
                {
                    // everything ok => release
                    mq->ReleaseMessages(queueIDs);

                    this->iErrorCounter = 0;

                    stringstream IDList;
                    for(unsigned int i=0; i<queueIDs.size(); i++)
                    {
                        if(i > 0)
                            IDList << ", ";

                        char szHexID[16];
                        snprintf(szHexID, 16, "%08X", queueIDs.at(i));

                        IDList << szHexID;
                    }

                    db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] Message enqueued for %d recipient(s), queue ID(s): %s",
                        this->strPeer.c_str(),
                        (int)this->vRecipients.size(),
                        IDList.str().c_str()));

                    printf("250 2.0.0 Message enqueued for delivery\r\n");
                }
                else
                {
                    // at least one message couldn't be enqueued => delete all message instances from queue and fail
                    if(queueIDs.size() > 0)
                        mq->DeleteMessages(queueIDs);

                    // remove send stats
                    if(iStatInsertID > 0)
                    {
                        if(bHaveSendStats)
                        {
                            db->Query("DELETE FROM bm60_sendstats WHERE `sendstatid`=%d",
                                (int)iStatInsertID);
                        }
                        else
                        {
                            db->Query("DELETE FROM bm60_bms_smtpstats WHERE `statid`=%d",
                                (int)iStatInsertID);
                        }
                    }

                    this->Error(451, "4.3.0 Internal error while enqueueing message for one or more recipient(s)");
                }

                delete mq;
            }
        }
    }

    // silent rset
    this->Rset(true);
}

/*
 * Process a data line
 */
bool SMTP::ProcessDataLine(char *szLine)
{
    // termination line?
    if(strcmp(szLine, ".") == 0
        || strcmp(szLine, ".\r\n") == 0
        || strcmp(szLine, ".\n") == 0)
    {
        // process message!
        this->ProcessMessage();
    }

    // no, normal data
    else if(!this->bSizeLimitExceeded)
    {
        // would this line exceed the size limit?
        if(this->iMessageSizeLimit != 0
            && (int)this->strBody.size() + this->iHeaderBytes + (int)strlen(szLine) > this->iMessageSizeLimit)
        {
            this->bSizeLimitExceeded = true;
        }

        // no, write data
        else
        {
            if(!this->bDataPassedHeaders)
            {
                if(strcmp(szLine, "\n") == 0
                   || strcmp(szLine, "\r\n") == 0
                   || strcmp(szLine, "") == 0)
                {
                    this->bDataPassedHeaders = true;
                    return(true);
                }
                else
                {
                    string strLine(szLine);
                    this->iHeaderBytes += strLine.length();

                    strLine = utils->RTrim(strLine, "\r\n");
                    size_t colonPos = strLine.find_first_of(':');
                    size_t spacePos = strLine.find_first_of(' ');
                    size_t tabPos   = strLine.find_first_of('\t');

                    if(spacePos == string::npos || (tabPos != string::npos && tabPos < spacePos))
                        spacePos = tabPos;

                    if(colonPos != string::npos && (spacePos == string::npos || colonPos < spacePos))
                    {
                        string fieldKey = strLine.substr(0, colonPos);
                        fieldKey = utils->Trim(fieldKey);

                        string fieldVal = (strLine.size() > colonPos+1) ? strLine.substr(colonPos+1) : "";
                        fieldVal = utils->Trim(fieldVal);

                        pair<string, string> header;
                        header.first = fieldKey;
                        header.second = fieldVal;

                        vHeaders.push_back(header);
                    }
                    else if((spacePos == 0) && !vHeaders.empty())
                    {
                        vHeaders.back().second.append("\r\n");
                        vHeaders.back().second.append(strLine);
                    }

                    if(strlen(szLine) > sizeof("date:")
                       && strncasecmp(szLine, "date:", 5) == 0)
                    {
                        this->bWriteDateHeader = false;
                    }
                    else if(strlen(szLine) > sizeof("message-id:")
                            && strncasecmp(szLine, "message-id:", 11) == 0)
                    {
                        this->bWriteMessageIDHeader = false;
                    }
                    else if(strlen(szLine) > sizeof("from:")
                            && strncasecmp(szLine, "from:", 5) == 0)
                    {
                        if(this->bAuthenticated)
                        {
                            char *szFromAddress = Mail::ExtractMailAddress(szLine + 5);
                            if(szFromAddress != NULL)
                            {
                                if(utils->IsValidSenderAddressForUser(this->iUserID, szFromAddress))
                                    this->bValidFromHeader = true;

                                free(szFromAddress);
                            }
                        }
                    }
                    else if(strlen(szLine) > sizeof("received:")
                        && strncasecmp(szLine, "received:", 9) == 0)
                    {
                        ++iHopCounter;
                    }
                }
            }

            if(this->bDataPassedHeaders)
            {
                size_t iLen = strlen(szLine);
                while(iLen > 0 && (szLine[iLen-1] == '\n' || szLine[iLen-1] == '\r'))
                    szLine[--iLen] = '\0';

                if(*szLine == '.')
                {
                    if(strlen(szLine) > 1)
                        this->strBody.append(szLine+1, strlen(szLine)-1);
                }
                else
                {
                    this->strBody.append(szLine, strlen(szLine));
                }

                this->strBody.append("\r\n");
            }
        }
    }

    return(true);
}
