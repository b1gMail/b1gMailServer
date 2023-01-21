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
#include <msgqueue/inboundprocess.h>

#define INBOUND_BUFFER_SIZE 4096

InboundProcess::InboundProcess(bool keepAlive)
{
    this->keepAlive = keepAlive;
    this->proc = NULL;
    this->sessionActive = false;
    this->lastUse = time(NULL);
}

InboundProcess::~InboundProcess()
{
    this->endSession();

    if(this->proc != NULL)
        delete this->proc;
}

void InboundProcess::beginSession()
{
    if(this->proc != NULL && !this->proc->IsRunning())
        this->endSession();

    if(this->sessionActive && this->proc != NULL && this->proc->IsRunning())
        return;

    if(this->keepAlive)
    {
        // build pipe script command
        string strPipeScript = "\"";
        strPipeScript.append(cfg->Get("php_path"));
        strPipeScript.append("\" \"");
        strPipeScript.append(cfg->Get("selffolder"));
        strPipeScript.append("interface");
        strPipeScript.append(1, PATH_SEP);
        strPipeScript.append("pipe.php\" --timeout=");
        strPipeScript.append(cfg->Get("queue_timeout"));
        strPipeScript.append(" --keep-alive");

        // open process
        this->proc = new Process(strPipeScript);
        this->proc->SetCloseTimeout(5);

        if(!this->proc->Open())
        {
            delete this->proc;
            this->proc = NULL;
            this->sessionActive = false;

            throw DeliveryException("Inbound pipe",
                "Failed to create pipe process (keep-alive: 1).",
                QUEUE_STATUS_TEMPORARY_ERROR,
                "4.3.0");
        }
    }

    this->sessionActive = true;
}

void InboundProcess::endSession()
{
    if(this->proc != NULL)
        delete proc;

    this->proc = NULL;
    this->sessionActive = false;
}

void InboundProcess::deliver(const string &from, const string &to, int flags, FILE *stream)
{
    char szBuffer[INBOUND_BUFFER_SIZE];

    if(this->keepAlive)
    {
        // use existing proc
        FILE *fpPipeIn = this->proc->GetInputFP(),
             *fpPipeOut = this->proc->GetOutputFP();

        if(fpPipeIn != NULL && fpPipeOut != NULL)
        {
            // RCPT TO
            fprintf(fpPipeIn, "RCPT TO:<%s>\r\n", to.c_str());
            fflush(fpPipeIn);
            if(fgets(szBuffer, sizeof(szBuffer), fpPipeOut) == NULL
               || strlen(szBuffer) < 3
               || strncmp(szBuffer, "250", 3) != 0)
            {
                throw DeliveryException("Inbound pipe",
                    "b1gMail pipe RCPT TO command failed (keep-alive: 1).",
                    QUEUE_STATUS_TEMPORARY_ERROR,
                    "4.3.0");
            }

            if(flags != 0)
            {
                // FLAGS
                fprintf(fpPipeIn, "FLAGS %d\r\n", flags);
                fflush(fpPipeIn);
                if(fgets(szBuffer, sizeof(szBuffer), fpPipeOut) == NULL
                    || strlen(szBuffer) < 3
                    || strncmp(szBuffer, "250", 3) != 0)
                {
                    // Since b1gMail versions < 7.4 do not support this command,
                    // silently ignore any error response.
                }
            }

            // DATA
            fprintf(fpPipeIn, "DATA\r\n");
            fflush(fpPipeIn);
            if(fgets(szBuffer, sizeof(szBuffer), fpPipeOut) == NULL
               || strlen(szBuffer) < 3
               || strncmp(szBuffer, "354", 3) != 0)
            {
                throw DeliveryException("Inbound pipe",
                    "b1gMail pipe DATA command failed (keep-alive: 1).",
                    QUEUE_STATUS_TEMPORARY_ERROR,
                    "4.3.0");
            }

            // message data
            while(!feof(stream))
            {
                if(fgets(szBuffer, sizeof(szBuffer), stream) == NULL)
                    break;

                if(*szBuffer == '.')
                    fputc('.', fpPipeIn);

                size_t iReadBytes = strlen(szBuffer);
                size_t iWrittenBytes = fwrite(szBuffer, 1, iReadBytes, fpPipeIn);
                if(iWrittenBytes != iReadBytes)
                    break;
            }

            // finish
            fprintf(fpPipeIn, "\r\n.\r\n");
            fflush(fpPipeIn);
            if(fgets(szBuffer, sizeof(szBuffer), fpPipeOut) == NULL
               || strlen(szBuffer) < 3
               || strncmp(szBuffer, "250", 3) != 0)
            {
                throw DeliveryException("Inbound pipe",
                    "b1gMail pipe rejected message data (keep-alive: 1).",
                    QUEUE_STATUS_TEMPORARY_ERROR,
                    "4.3.0");
            }
        }
        else
        {
            throw DeliveryException("Inbound pipe",
                "Delivery FPs are NULL (this is impossible; keep-alive: 1).",
                QUEUE_STATUS_TEMPORARY_ERROR,
                "4.3.0");
        }
    }
    else
    {
        // create proc for single use
        string strPipeScript = "\"";
        strPipeScript.append(cfg->Get("php_path"));
        strPipeScript.append("\" \"");
        strPipeScript.append(cfg->Get("selffolder"));
        strPipeScript.append("interface");
        strPipeScript.append(1, PATH_SEP);
        strPipeScript.append("pipe.php\" --timeout=");
        strPipeScript.append(cfg->Get("queue_timeout"));
        strPipeScript.append(" \"");
        strPipeScript.append(from);
        strPipeScript.append("\" -- \"");
        strPipeScript.append(to);
        strPipeScript.append("\"");

        int iTimeout = atoi(cfg->Get("queue_timeout"));
        if(iTimeout > 0)
            iTimeout = iTimeout + 1;
        else if(iTimeout < 0)
            iTimeout = 30;

        Process *proc = new Process(strPipeScript);
        proc->SetCloseTimeout(iTimeout);
        if(proc->Open())
        {
            FILE *fpPipeIn = proc->GetInputFP(),
                 *fpPipeOut = proc->GetOutputFP();

            if(fpPipeIn != NULL && fpPipeOut != NULL)
            {
                // message data
                while(!feof(stream))
                {
                    if(fgets(szBuffer, sizeof(szBuffer), stream) == NULL)
                        break;

                    size_t iReadBytes = strlen(szBuffer);
                    size_t iWrittenBytes = fwrite(szBuffer, 1, iReadBytes, fpPipeIn);
                    if(iWrittenBytes != iReadBytes)
                        break;
                }
                fflush(fpPipeIn);

                // finish
                int quitSignal = -1;
                int iPipeResult = proc->Close(&quitSignal);

                if(quitSignal != -1)
                {
                    delete proc;

                    string sigName;
                    if(quitSignal == SIGTERM)
                        sigName += "SIGTERM";
                    else if(quitSignal == SIGKILL)
                        sigName += "SIGKILL";
                    else
                        sigName += "?";

                    throw DeliveryException("Inbound pipe",
                        string("Timeout while waiting for b1gMail pipe to finish - sent ") + sigName + string("."),
                        QUEUE_STATUS_TEMPORARY_ERROR,
                        "4.3.0");
                }
#ifndef WIN32
                else if(!WIFEXITED(iPipeResult))
                {
                    delete proc;
                    throw DeliveryException("Inbound pipe",
                        string("b1gMail pipe exited abnormally."),
                        QUEUE_STATUS_TEMPORARY_ERROR,
                        "4.3.0");
                }
                else if(WEXITSTATUS(iPipeResult) != 0)
                {
                    delete proc;
                    throw DeliveryException("Inbound pipe",
                        string("Got error exit code from b1gMail pipe."),
                        QUEUE_STATUS_TEMPORARY_ERROR,
                        "4.3.0");
                }
#else
                else if(iPipeResult != 0)
                {
                    delete proc;
                    throw DeliveryException("Inbound pipe",
                        string("Got error exit code from b1gMail pipe."),
                        QUEUE_STATUS_TEMPORARY_ERROR,
                        "4.3.0");
                }
#endif
            }
            else
            {
                delete proc;
                throw DeliveryException("Inbound pipe",
                    "Delivery FPs are NULL (this is impossible; keep-alive: 0).",
                    QUEUE_STATUS_TEMPORARY_ERROR,
                    "4.3.0");
            }
        }
        else
        {
            delete proc;
            throw DeliveryException("Inbound pipe",
                "Failed to create pipe process (keep-alive: 0).",
                QUEUE_STATUS_TEMPORARY_ERROR,
                "4.3.0");
        }

        delete proc;
    }

    this->lastUse = time(NULL);
}
