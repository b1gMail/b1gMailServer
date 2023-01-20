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
#include <msgqueue/smtpsession.h>

#include <sstream>

SMTPSession::SMTPSession(const string &domain, const string &host, int port)
{
    this->domain = domain;
    this->host = host;
    this->port = port;
    this->sock = NULL;
    this->sessionActive = false;
    this->addedToGreylist = false;
    this->authenticated = false;
    this->isMXConnection = false;
    this->tlsSupported = false;
    this->useTLS = false;
    this->useTLSA = false;
    this->mxAuthenticated = false;
    this->tlsActive = false;
    this->lastUse = time(NULL);
    this->enableLogging = false;
    this->isDirty = true;
    this->sizeSupported = false;

    const char *loggingSetting = cfg->Get("log_smtp_sessions");
    if(loggingSetting != NULL)
        this->enableLogging = true;
}

SMTPSession::~SMTPSession()
{
    this->endSession();

    if(this->sock != NULL)
        delete this->sock;
}

void SMTPSession::rset()
{
    if(!this->isDirty)
        return;

    this->sock->PrintF("RSET\r\n");
    string response = this->readResponse();
    if(response.find("250") != 0)
    {
        throw DeliveryException(this->host, "SMTP session reset failed.", QUEUE_STATUS_TEMPORARY_ERROR, "4.5.0", string("smtp; ") + response);
    }

    this->isDirty = false;
}

void SMTPSession::startTLS()
{
    this->sock->PrintF("STARTTLS\r\n");
    string response = this->readResponse();
    if(response.find("220") == 0)
    {
        string tlsError;
        if(!this->sock->StartTLS(tlsError, &tlsaRecords, this->host, this->domain, &this->daneResult))
        {
            throw DeliveryException(this->host, string("TLS initiation failed: ") + tlsError, QUEUE_STATUS_TEMPORARY_ERROR, "4.4.2");
        }
        else
        {
            // re-issue EHLO
            this->sock->PrintF("EHLO %s\r\n", cfg->Get("b1gmta_host"));
            response = this->readResponse();

            if(response.find("250") != 0)
            {
                throw DeliveryException(this->host, string("EHLO after STARTTLS failed"), QUEUE_STATUS_TEMPORARY_ERROR, "4.5.0", string("smtp; ") + response);
            }
            else
            {
                this->isDirty = false;
                this->tlsActive = true;
            }
        }
    }
}

void SMTPSession::auth(const string &user, const string &pass)
{
    if(this->authenticated)
        return;

    this->sock->PrintF("AUTH LOGIN\r\n");
    string response = this->readResponse();
    if(response.find("334") != 0)
    {
        this->throwError(response,
            "Server temporarily rejected AUTH LOGIN command.",
            "Server rejected AUTH LOGIN command.",
            "Unexpected server response (AUTH LOGIN).",
            "4.3.5", "5.3.5", "4.5.0");
    }

    char *szEncoded = utils->Base64Encode(user.c_str());
    if(szEncoded == NULL)
    {
        throw DeliveryException(this->host,
            "Failed to base64-encode SMTP username.",
            QUEUE_STATUS_TEMPORARY_ERROR,
            "4.3.0");
    }

    this->sock->PrintF("%s\r\n", szEncoded);
    free(szEncoded);
    response = this->readResponse();
    if(response.find("334") != 0)
    {
        this->throwError(response,
            "Server temporarily rejected AUTH LOGIN command.",
            "Server rejected AUTH LOGIN command.",
            "Unexpected server response (AUTH LOGIN).",
            "4.3.5", "5.3.5", "4.5.0");
    }

    szEncoded = utils->Base64Encode(pass.c_str());
    if(szEncoded == NULL)
    {
        throw DeliveryException(this->host,
            "Failed to base64-encode SMTP password.",
            QUEUE_STATUS_TEMPORARY_ERROR,
            "4.3.0");
    }

    this->sock->PrintF("%s\r\n", szEncoded);
    free(szEncoded);
    response = this->readResponse();
    if(response.find("235") != 0)
    {
        this->throwError(response,
            "Server temporarily rejected credentials.",
            "Server rejected credentials.",
            "Unexpected server response (AUTH LOGIN after credentials submission).",
            "4.3.0", "5.3.0", "4.3.0");
    }

    this->authenticated = true;
}

void SMTPSession::mailFrom(const string &returnPath, int mailSize)
{
    this->isDirty = true;
    if(this->sizeSupported && mailSize > 0)
    {
        this->sock->PrintF("MAIL FROM:<%s> SIZE=%d\r\n", returnPath.c_str(), mailSize);
    }
    else
    {
        this->sock->PrintF("MAIL FROM:<%s>\r\n", returnPath.c_str());
    }

    string response = this->readResponse();
    if(response.find("250") != 0)
    {
        this->throwError(response,
            "Server temporarily rejected return path.",
            "Server rejected return path.",
            "Unexpected server response (MAIL FROM).",
            "4.0.0", "5.0.0.", "4.0.0");
    }
}

void SMTPSession::rcptTo(const string &recipient)
{
    this->isDirty = true;
    this->sock->PrintF("RCPT TO:<%s>\r\n", recipient.c_str());
    string response = this->readResponse();
    if(response.find("250") != 0)
    {
        this->throwError(response,
            "Server temporarily rejected recipient.",
            "Server rejected recipient.",
            "Unexpected server response (RCPT TO).",
            "4.0.0", "5.0.0", "4.5.0");
    }
}

void SMTPSession::data(FILE *stream)
{
    this->isDirty = true;
    this->sock->PrintF("DATA\r\n");

    string response = this->readResponse();
    if(response.find("354") == 0)
    {
        char szBuffer[MAIL_LINEMAX+3];

        // nagle is useful here as we do not want every line in its own TCP packet
        this->sock->SetNoDelay(false);

        // pass through message
        while(!feof(stream))
        {
            if(fgets(szBuffer, MAIL_LINEMAX, stream) == NULL)
                break;
            size_t iReadBytes = strlen(szBuffer);
            if(iReadBytes <= 0)
                break;

            // fix line endings
            size_t iLen = strlen(szBuffer);
            while(iLen > 0 && (szBuffer[iLen-1] == '\n' || szBuffer[iLen-1] == '\r'))
                szBuffer[--iLen] = '\0';
            iLen = strlen(szBuffer);
            szBuffer[iLen]   = '\r';
            szBuffer[iLen+1] = '\n';
            szBuffer[iLen+2] = '\0';
            iReadBytes = strlen(szBuffer);

            if(*szBuffer == '.')
                this->sock->Write(".", 1);
            size_t iWrittenBytes = this->sock->Write(szBuffer, iReadBytes);
            if(iWrittenBytes != iReadBytes)
                break;
        }

        // send termination sequence, turn off nagle (flush nagle buffer)
        this->sock->SetNoDelay(true);
        this->sock->PrintF("\r\n.\r\n");

        response = this->readResponse();
        if(response.find("250") != 0)
        {
            this->throwError(response,
                "Server temporarily rejected message.",
                "Server rejected message.",
                "Unexpected server response (message).",
                "4.0.0", "5.0.0", "4.5.0");
        }

        this->lastUse = time(NULL);
    }
    else
    {
        this->throwError(response,
            "Server temporarily rejected DATA command.",
            "Server rejected DATA command.",
            "Unexpected server response (DATA).",
            "4.0.0", "5.0.0", "4.5.0");
    }
}

void SMTPSession::lookupTLSARecords()
{
    tlsaRecords.clear();
    if(this->mxAuthenticated)
        DNS::TLSALookup(host, port, tlsaRecords);
}

void SMTPSession::beginSession(bool ehlo)
{
    if(this->sessionActive)
        return;

    try
    {
        this->daneResult = DANEResult();

        if(this->useTLSA)
        {
            lookupTLSARecords();
        }
        else
        {
            tlsaRecords.clear();
        }

        this->sock = new Socket((char *)this->host.c_str(), this->port, atoi(cfg->Get("queue_timeout")));
        this->sock->SetNoDelay(true);

        if(this->enableLogging)
        {
            stringstream ss;
            ss  << cfg->Get("log_smtp_sessions") << "smtpsession-"
                << time(NULL) << "-"
                << this->host << "-"
                << this->port << ".log";
            this->sock->EnableLogging(ss.str().c_str());
        }

        string response = this->readResponse();
        if(response.find("220") == 0)
        {
            this->sock->PrintF("EHLO %s\r\n", cfg->Get("b1gmta_host"));

            response = this->readResponse();
            if(response.find("500") == 0 || response.find("502") == 0 || response.find("503") == 0)
            {
                if(ehlo)
                {
                    // ehlo not implemented
                    this->throwError(response,
                        "Server seems to temporarily not support the EHLO command.",
                        "Server seems not to support the EHLO command.",
                        "Unexpected SMTP response (EHLO).",
                        "4.0.0", "5.0.0", "4.5.0");
                }

                this->sock->PrintF("HELO %s\r\n", cfg->Get("b1gmta_host"));
                response = this->readResponse();
            }

            if(response.find("250") == 0)
            {
                // extract remote server host name
                string remoteHostName = "";
                if(response.length() >= 6)
                {
                    remoteHostName = response.substr(4);
                    size_t spacePos = remoteHostName.find_first_of(' ');

                    if(spacePos != string::npos)
                        remoteHostName = remoteHostName.substr(0, spacePos);
                }

                if(string(cfg->Get("b1gmta_host")) == remoteHostName)
                {
                    // loop
                    throw DeliveryException(this->host, "Mail loops back to myself.", QUEUE_STATUS_FATAL_ERROR, "5.4.6");
                }

                this->tlsSupported = response.find("-STARTTLS\r\n") != string::npos
                                        || response.find(" STARTTLS\r\n") != string::npos;

                this->sizeSupported = response.find("-SIZE") != string::npos
                                        || response.find(" SIZE") != string::npos;

                if(this->useTLSA && !this->tlsaRecords.empty() && !this->tlsSupported)
                {
                    // TLSA records published but no TLS supported!
                    throw DeliveryException(this->host, "STARTTLS not supported despite published TLSA records!", QUEUE_STATUS_TEMPORARY_ERROR, "4.3.0");
                }

                if((this->useTLSA && !this->tlsaRecords.empty()) || (this->tlsSupported && this->useTLS))
                    this->startTLS();

                this->sessionActive = true;
                this->isDirty = false;
                if(this->isMXConnection)
                    this->addToGreylist();
            }
            else
            {
                // helo rejected
                this->throwError(response,
                    "Server temporarily rejected our HELO hostname.",
                    "Server rejected our HELO hostname.",
                    "Unexpected SMTP response (HELO).",
                    "4.0.0", "5.0.0", "4.5.0");
            }
        }
        else
        {
            // greeting not 220
            this->throwError(response,
                "Temporary server error (init).",
                "Fatal server error (init).",
                "Unexpected server response (init).",
                "4.0.0", "5.0.0", "4.5.0");
        }
    }
    catch(Core::Exception &e)
    {
        string statusInfo = "Unknown exception";

        if(e.strPart != "")
            statusInfo = e.strPart;
        else if(e.strError != "")
            statusInfo = e.strError;

        throw DeliveryException(this->host,
            statusInfo,
            QUEUE_STATUS_TEMPORARY_ERROR,
            "4.0.0");
    }
}

void SMTPSession::endSession()
{
    if(this->sock != NULL)
    {
        try
        {
            this->sock->PrintF("QUIT\r\n");
            this->readResponse();
        }
        catch(...)
        {
        }
    }

    delete this->sock;
    this->sock = NULL;
    this->sessionActive = false;
}

string SMTPSession::readResponse()
{
    char szLineBuffer[512+1];           // rfc2821 4.5.3.1
    string response;

    do
    {
        try
        {
            if(this->sock->ReadLine(szLineBuffer, sizeof(szLineBuffer)) == NULL)
            {
                throw DeliveryException(this->host, "Failed to read SMTP response.", QUEUE_STATUS_TEMPORARY_ERROR, "4.4.2");
                break;
            }
        }
        catch(const Core::Exception &e)
        {
            string statusInfo;
            if(e.strPart != "")
                statusInfo = e.strPart;
            else if(e.strError != "")
                statusInfo = e.strError;
            throw DeliveryException(this->host, "Failed to read SMTP response: " + statusInfo, QUEUE_STATUS_TEMPORARY_ERROR, "4.4.2");
            break;
        }

        response.append(szLineBuffer);
    }
    while(strlen(szLineBuffer) > 3 && szLineBuffer[3] == '-');

    if(response.length() == 0)
    {
        throw DeliveryException(this->host, "Empty SMTP response.", QUEUE_STATUS_TEMPORARY_ERROR, "4.5.0");
    }

    return(response);
}

void SMTPSession::throwError(string response, const string &tempMsg, const string &fatalMsg, const string &elseMsg,
    const string &tempStatusCode, const string &fatalStatusCode, const string &elseStatusCode)
{
    if(response[0] == '4')
    {
        throw DeliveryException(this->host,
            tempMsg + string("\r\nServer answered: \"")
            + utils->Trim(response)
            + string("\""),
            QUEUE_STATUS_TEMPORARY_ERROR,
            tempStatusCode,
            string("smtp; ") + response);
    }
    else if(response[0] == '5')
    {
        throw DeliveryException(this->host,
            fatalMsg + string("\r\nServer answered: \"")
            + utils->Trim(response)
            + string("\""),
            QUEUE_STATUS_FATAL_ERROR,
            fatalStatusCode,
            string("smtp; ") + response);
    }
    else
    {
        throw DeliveryException(this->host,
            elseMsg + string("\r\nServer answered: \"")
            + utils->Trim(response)
            + string("\""),
            QUEUE_STATUS_TEMPORARY_ERROR,
            elseStatusCode,
            string("smtp; ") + response);
    }
}

void SMTPSession::addToGreylist()
{
    if(strcmp(cfg->Get("grey_enabled"), "1") != 0)
        return;

    if(this->addedToGreylist)
        return;

    if(cMSGQueue_Instance != NULL)
        cMSGQueue_Instance->AddToGreylist(this->sock->InAddr);

    this->addedToGreylist = true;
}
