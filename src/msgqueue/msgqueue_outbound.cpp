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

#include <msgqueue/msgqueue.h>
#include <core/dns.h>
#include <core/process.h>

#include <algorithm>
#include <sstream>

/*
 * Process outbound item
 */
void MSGQueue::ProcessOutbound(MSGQueueItem *item, MSGQueueResult *result)
{
    // if destination is one of our local domains and the message is outbound,
    // bounce it
    string rcptDomain = item->domain;
    transform(rcptDomain.begin(), rcptDomain.end(), rcptDomain.begin(), ::tolower);
    if(!rcptDomain.empty())
    {
        if(IsLocalDomain(rcptDomain))
        {
            result->apPointType         = BMAP_SEND_RECP_LOCAL_INVALID;
            result->apPointComment      = string("Mailbox ") + item->to + string(" does not exist at local domain ") + rcptDomain;

            throw DeliveryException(item->to,
                string("Mailbox does not exist at local domain ") + item->domain + string("."),
                QUEUE_STATUS_FATAL_ERROR,
                "5.1.1");
        }
    }

    // deliver according to configured method
    RelayServerInfo rs;
    int outboundTarget = atoi(cfg->Get("outbound_target"));
    switch(outboundTarget)
    {
    case OUTBOUND_TARGET_SENDMAIL:
        this->DeliverOutboundToSendmail(item, result, cfg->Get("outbound_sendmail_path"));
        break;

    case OUTBOUND_TARGET_SMTPRELAY:
        rs.host         = cfg->Get("outbound_smtp_relay_host");
        rs.user         = cfg->Get("outbound_smtp_relay_user");
        rs.pass         = cfg->Get("outbound_smtp_relay_pass");
        rs.requiresAuth = *cfg->Get("outbound_smtp_relay_auth") == '1';
        rs.port         = atoi(cfg->Get("outbound_smtp_relay_port"));
        this->DeliverOutboundToSMTPRelay(item, result, rs);
        break;

    case OUTBOUND_TARGET_DELIVER_SELF:
        this->DeliverOutbound(item, result);
        break;

    default:
        throw DeliveryException("ProcessOutbound",
            "Unknown outbound target.",
            QUEUE_STATUS_TEMPORARY_ERROR,
            "4.3.5");
        break;
    };
}

void MSGQueue::DeliverOutboundToSendmail(MSGQueueItem *item, MSGQueueResult *result, const char *sendmailPath)
{
    char szBuffer[MAIL_LINEMAX+2];

    // build pipe command
#ifdef WIN32
    string strPipeScript = "\"\"";
#else
    string strPipeScript = "\"";
#endif
    strPipeScript.append(sendmailPath);
    strPipeScript.append("\" -f \"");
    strPipeScript.append(item->from);
    strPipeScript.append("\" -- \"");
    strPipeScript.append(item->to);
#ifdef WIN32
    strPipeScript.append("\"\"");
#else
    strPipeScript.append("\"");
#endif

    // open message
    FILE *stream = fopen(this->QueueFileNameStr(item->id).c_str(), "rb");
    if(stream == NULL)
    {
        throw DeliveryException("DeliverOutboundToSendmail",
            "Failed to open queue message file.",
            QUEUE_STATUS_TEMPORARY_ERROR,
            "4.3.0");
    }

    // create process
    Process *proc = new Process(strPipeScript);
    if(proc->Open())
    {
        FILE *fpPipeIn = proc->GetInputFP(),
             *fpPipeOut = proc->GetOutputFP();

        if(fpPipeIn != NULL && fpPipeOut != NULL)
        {
            // pass through message
            while(!feof(stream))
            {
                if(fgets(szBuffer, MAIL_LINEMAX, stream) == NULL)
                    break;
                size_t iLen = strlen(szBuffer);

                while(iLen > 0 && (szBuffer[iLen-1] == '\n' || szBuffer[iLen-1] == '\r'))
                    szBuffer[--iLen] = '\0';

                iLen = strlen(szBuffer);
                szBuffer[iLen]   = '\n';
                szBuffer[iLen+1] = '\0';

                size_t iWrittenBytes = fwrite(szBuffer, 1, strlen(szBuffer), fpPipeIn);
                if(iWrittenBytes != strlen(szBuffer))
                    break;
            }

            // close process
            int quitSignal = -1;
            int iPipeResult = proc->Close(&quitSignal);
#ifndef WIN32
            if(quitSignal != -1)
            {
                delete proc;
                fclose(stream);

                string sigName = "?";
                if(quitSignal == SIGTERM)
                    sigName = "SIGTERM";
                else if(quitSignal == SIGKILL)
                    sigName = "SIGKILL";

                throw DeliveryException("DeliverOutboundToSendmail",
                    string("Timeout while waiting for sendmail to finish - sent ") + sigName + string("."),
                    QUEUE_STATUS_TEMPORARY_ERROR,
                    "4.3.0");
            }
            else if(!WIFEXITED(iPipeResult))
            {
                delete proc;
                fclose(stream);

                throw DeliveryException("DeliverOutboundToSendmail",
                    "Sendmail exited abnormally.",
                    QUEUE_STATUS_TEMPORARY_ERROR,
                    "4.3.0");
            }
            else if(WEXITSTATUS(iPipeResult) != 0)
            {
                delete proc;
                fclose(stream);

                throw DeliveryException("DeliverOutboundToSendmail",
                    "Got error exit code from sendmail.",
                    QUEUE_STATUS_TEMPORARY_ERROR,
                    "4.3.0");
            }
#else
            if(iPipeResult != 0)
            {
                delete proc;
                fclose(stream);

                throw DeliveryException("DeliverOutboundToSendmail",
                    "Got error exit code from sendmail.",
                    QUEUE_STATUS_TEMPORARY_ERROR,
                    "4.3.0");
            }
#endif

            // ok!
            result->status = QUEUE_STATUS_SUCCESS;
            result->statusInfo = "Delivered to sendmail.";
            result->deliveredTo = "sendmail";
        }
        else
        {
            delete proc;
            fclose(stream);

            throw DeliveryException("DeliverOutboundToSendmail",
                "Sendmail FPs are NULL (this is impossible).",
                QUEUE_STATUS_TEMPORARY_ERROR,
                "4.3.0");
        }
    }
    else
    {
        delete proc;
        fclose(stream);

        throw DeliveryException("DeliverOutboundToSendmail",
            "Failed to create sendmail process.",
            QUEUE_STATUS_TEMPORARY_ERROR,
            "4.3.0");
    }

    // close process
    delete proc;

    // close message
    fclose(stream);
}

void MSGQueue::DeliverOutboundToSMTPRelay(MSGQueueItem *item, MSGQueueResult *result, const RelayServerInfo &relayServer)
{
    string relayServerFakeDomain = string("smtpRelay@") + relayServer.host; // ID for the SMTP session pool
    bool deliveredUsingTLS = false;

    // connect to relay server
    SMTPSession *sess = NULL;
    try
    {
        sess = this->smtpPool->getSMTPSession(relayServerFakeDomain,
            relayServer.host,
            relayServer.port);
        sess->isMXConnection = false;
        sess->useTLS = *cfg->Get("outbound_smtp_usetls") == '1';
        sess->useTLSA = false;
        sess->beginSession(relayServer.requiresAuth);
    }
    catch(DeliveryException &ex)
    {
        if(sess != NULL)
        {
            delete sess;
            sess = NULL;
        }

        if(ex.errorCode == QUEUE_STATUS_FATAL_ERROR)
        {
            throw ex;
        }
    }

    // session established?
    if(sess != NULL)
    {
        // open message
        FILE *stream = fopen(this->QueueFileNameStr(item->id).c_str(), "rb");
        if(stream == NULL)
        {
            this->smtpPool->putBackSMTPSession(sess);

            throw DeliveryException("DeliverOutboundToSMTPRelay",
                "Failed to open queue message file.",
                QUEUE_STATUS_TEMPORARY_ERROR,
                "4.3.0");
        }

        // deliver
        try
        {
            if(relayServer.requiresAuth)
                sess->auth(relayServer.user, relayServer.pass); // auth() will detect if we're already authenticated from a prior delivery and prevent double auth
            sess->rset();
            sess->mailFrom(item->from, item->size);
            sess->rcptTo(item->to);
            sess->data(stream);

            deliveredUsingTLS = sess->tlsActive;
        }
        catch(DeliveryException &ex)
        {
            if(ex.errorCode == QUEUE_STATUS_FATAL_ERROR)
            {
                delete sess;
            }
            else
            {
                this->smtpPool->putBackSMTPSession(sess);
                sess = NULL;
            }

            sess = NULL;
            fclose(stream);

            throw ex;
        }

        // close message
        fclose(stream);
        stream = NULL;

        // put back connection
        string deliveredTo = sess->host;
        this->smtpPool->putBackSMTPSession(sess);

        // ok!
        result->status = QUEUE_STATUS_SUCCESS;
        result->statusInfo = string("Delivered to relay server ")
            + deliveredTo
            + (deliveredUsingTLS ? string(" (using TLS)") : string(""))
            + string(".");
        result->deliveredTo = "relay server";
    }
}

void MSGQueue::DeliverOutbound(MSGQueueItem *item, MSGQueueResult *result)
{
    bool useDNSSEC = *cfg->Get("outbound_smtp_usednssec") == '1';
    bool deliveredUsingTLS = false;
    DANEResult daneResult;

    // extract domain
    string domain;
    size_t atPos = item->to.find('@');
    if(atPos == string::npos)
    {
        throw DeliveryException(item->to,
            "Invalid mail recipient.",
            QUEUE_STATUS_FATAL_ERROR,
            "5.1.3");
    }
    domain = item->to.substr(atPos+1);

    // determine MX servers
    vector<MXServer> mxServers;
    if(DNS::MXLookup(domain, mxServers, true, useDNSSEC) < 1)
    {
        result->apPointType         = BMAP_SEND_RECP_DOMAIN_INVALID;
        result->apPointComment      = string("No MX server found for domain ") + domain;

        throw DeliveryException(domain,
            "No MX server found for domain.",
            QUEUE_STATUS_FATAL_ERROR,
            "5.1.2");
    }

    // connect to MX server
    DeliveryException lastError;
    SMTPSession *sess = NULL;
    vector<MXServer>::iterator it;
    for(it = mxServers.begin(); it != mxServers.end(); ++it)
    {
        try
        {
            sess = this->smtpPool->getSMTPSession(domain, it->Hostname, 25);
            sess->isMXConnection = true;
            sess->useTLS = *cfg->Get("outbound_smtp_usetls") == '1';
            sess->useTLSA = sess->useTLS && *cfg->Get("outbound_smtp_usedane") == '1';
            sess->mxAuthenticated = it->Authenticated;
            sess->beginSession();
            daneResult = sess->daneResult;
            break;
        }
        catch(DeliveryException &ex)
        {
            if(sess != NULL)
            {
                delete sess;
                sess = NULL;
            }

            if(ex.errorCode == QUEUE_STATUS_FATAL_ERROR || this->bQuit)
            {
                throw ex;
                break;
            }
            else
            {
                lastError = ex;
                continue;
            }
        }
    }

    // SMTP session established?
    if(sess != NULL)
    {
        // open message
        FILE *stream = fopen(this->QueueFileNameStr(item->id).c_str(), "rb");
        if(stream == NULL)
        {
            this->smtpPool->putBackSMTPSession(sess);

            throw DeliveryException("DeliverOutbound",
                "Failed to open queue message file.",
                QUEUE_STATUS_TEMPORARY_ERROR,
                "4.3.0");
        }

        // deliver
        try
        {
            sess->rset();
            sess->mailFrom(item->from, item->size);

            try
            {
                sess->rcptTo(item->to);
            }
            catch(DeliveryException &ex)
            {
                if(ex.errorCode == QUEUE_STATUS_FATAL_ERROR)
                {
                    result->apPointType         = BMAP_SEND_RECP_LOCAL_INVALID;
                    result->apPointComment      = string("MX server rejected recipient address ") + item->to;
                }
                throw ex;
            }

            sess->data(stream);

            deliveredUsingTLS = sess->tlsActive;
        }
        catch(DeliveryException &ex)
        {
            if(ex.errorCode == QUEUE_STATUS_FATAL_ERROR)
            {
                delete sess;
            }
            else
            {
                this->smtpPool->putBackSMTPSession(sess);
                sess = NULL;
            }

            sess = NULL;
            fclose(stream);

            throw ex;
        }

        // close message
        fclose(stream);
        stream = NULL;

        // put back connection
        string deliveredTo = sess->host;
        this->smtpPool->putBackSMTPSession(sess);

        // ok!
        stringstream tlsStatus;
        if(deliveredUsingTLS)
        {
            tlsStatus << " (using TLS";
            if(daneResult.usingDANE && daneResult.verified)
            {
                tlsStatus << "; DANE validated: ";
                tlsStatus << daneResult.depth;
                tlsStatus << ", " << static_cast<unsigned int>(daneResult.verifiedUsage);
                tlsStatus << ", " << static_cast<unsigned int>(daneResult.verifiedSelector);
                tlsStatus << ", " << static_cast<unsigned int>(daneResult.verifiedMType);
            }
            else if(sess->useTLSA)
            {
                tlsStatus << "; not DANE validated";
                tlsStatus << ", usable TLSA records: " << daneResult.usableRecords;
            }
            tlsStatus << ")";
        }

        result->status = QUEUE_STATUS_SUCCESS;
        result->statusInfo = string("Delivered to ")
            + deliveredTo
            + tlsStatus.str()
            + string(".");
        result->deliveredTo = deliveredTo;
    }
    else
    {
        if(lastError.errorCode != QUEUE_STATUS_UNKNOWN_ERROR)
            throw lastError;

        throw DeliveryException(domain,
            "Could not connect to a responsible MX server.",
            QUEUE_STATUS_TEMPORARY_ERROR,
            "4.4.1");
    }
}
