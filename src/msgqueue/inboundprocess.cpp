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

#include <ctype.h>
#include <string.h>

#define INBOUND_BUFFER_SIZE         4096
#define INBOUND_PIPE_NOISE_MAX      32
#define INBOUND_PIPE_QUIT_TIMEOUT   2

InboundProcess::InboundProcess(bool keepAlive)
{
    this->keepAlive = keepAlive;
    this->proc = NULL;
    this->sessionActive = false;
    this->failed = false;
    this->deliveryCount = 0;
    this->lastUse = time(NULL);
}

InboundProcess::~InboundProcess()
{
    this->endSession();

    if(this->proc != NULL)
        delete this->proc;
}

int InboundProcess::getCommandTimeout() const
{
    int t = atoi(cfg->Get("queue_timeout"));
    if(t <= 0)
        t = 30;
    return(t);
}

bool InboundProcess::shouldDiscard() const
{
    if(!this->keepAlive)
        return(false);
    if(this->failed)
        return(true);
    if(this->deliveryCount >= INBOUND_PROCESS_MAX_DELIVERIES)
        return(true);
    if(this->sessionActive && (this->proc == NULL || !this->proc->IsRunning()))
        return(true);
    if(this->sessionActive && this->lastUse < time(NULL) - INBOUND_PROCESS_HOLDTIME)
        return(true);
    return(false);
}

#ifdef __GNUC__
__attribute__((noreturn))
#endif
void InboundProcess::failPipe(const string &msg)
{
    this->failed = true;
    throw DeliveryException("Inbound pipe",
        msg,
        QUEUE_STATUS_TEMPORARY_ERROR,
        "4.3.0");
}

static bool isSMTPStatusLine(const char *line)
{
    if(line == NULL || strlen(line) < 3)
        return(false);
    if(!isdigit((unsigned char)line[0])
        || !isdigit((unsigned char)line[1])
        || !isdigit((unsigned char)line[2]))
        return(false);
    return(line[3] == ' ' || line[3] == '-' || line[3] == '\0');
}

static string clipPipeLine(const char *line)
{
    string s = (line != NULL) ? line : "";
    if(s.length() > 180)
    {
        s.erase(180);
        s.append("...");
    }
    return(s);
}

void InboundProcess::writePipe(const char *data, size_t len, const char *what)
{
    if(this->proc == NULL || !this->proc->WriteFully(data, len, this->getCommandTimeout()))
    {
        this->failPipe(string("Timeout/error while writing ") + what
            + " to b1gMail pipe (keep-alive: 1).");
    }
}

void InboundProcess::expectSMTP(const char *expectedCode, const char *what)
{
    char szBuffer[INBOUND_BUFFER_SIZE];
    int noise = 0;
    int timeout = this->getCommandTimeout();
    time_t deadline = time(NULL) + timeout;
    string lastNoise;

    while(noise <= INBOUND_PIPE_NOISE_MAX)
    {
        int remain = (int)(deadline - time(NULL));
        if(remain <= 0 || this->proc == NULL || !this->proc->IsRunning())
        {
            string msg = string("Timeout/EOF while waiting for ") + what
                + " from b1gMail pipe (keep-alive: 1)";
            if(!lastNoise.empty())
                msg += string("; last output: \"") + lastNoise + "\"";
            else if(this->proc != NULL && !this->proc->IsRunning())
                msg = string("b1gMail pipe process died while waiting for ")
                    + what + " (keep-alive: 1)";
            this->failPipe(msg + ".");
        }

        if(!this->proc->ReadLine(szBuffer, sizeof(szBuffer), remain))
        {
            string msg = string("Timeout/EOF while waiting for ") + what
                + " from b1gMail pipe (keep-alive: 1)";
            if(!lastNoise.empty())
                msg += string("; last output: \"") + lastNoise + "\"";
            this->failPipe(msg + ".");
        }

        if(!isSMTPStatusLine(szBuffer))
        {
            lastNoise = clipPipeLine(szBuffer);
            db->Log(CMP_MSGQUEUE, PRIO_WARNING, utils->PrintF(
                "Inbound keep-alive pipe produced non-protocol output during %s: %s",
                what,
                lastNoise.c_str()));
            noise++;
            continue;
        }

        if(strncmp(szBuffer, expectedCode, 3) != 0)
        {
            this->failPipe(string("b1gMail pipe ") + what
                + " command failed (keep-alive: 1): got \""
                + clipPipeLine(szBuffer) + "\".");
        }

        if(szBuffer[3] == '-')
            continue;

        return;
    }

    this->failPipe(string("b1gMail pipe ") + what
        + " produced too much non-protocol output (keep-alive: 1).");
}

void InboundProcess::beginSession()
{
    if(this->shouldDiscard() || (this->proc != NULL && !this->proc->IsRunning()))
        this->endSession();

    if(this->sessionActive && this->proc != NULL && this->proc->IsRunning())
        return;

    this->failed = false;
    this->deliveryCount = 0;

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
            this->failed = true;

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
    {
        if(this->keepAlive && this->proc->IsOpen() && this->proc->IsRunning() && !this->failed)
            this->proc->WriteFully("QUIT\r\n", 6, INBOUND_PIPE_QUIT_TIMEOUT);

        delete proc;
    }

    this->proc = NULL;
    this->sessionActive = false;
}

void InboundProcess::deliver(const string &from, const string &to, FILE *stream)
{
    char szBuffer[INBOUND_BUFFER_SIZE];

    if(this->keepAlive)
    {
        if(this->proc == NULL || !this->proc->IsOpen() || !this->proc->IsRunning())
        {
            this->failPipe("b1gMail pipe process is not running (keep-alive: 1).");
        }

        string rcptCmd = "RCPT TO:<" + to + ">\r\n";
        this->writePipe(rcptCmd.c_str(), rcptCmd.length(), "RCPT TO");
        this->expectSMTP("250", "RCPT TO");

        this->writePipe("DATA\r\n", 6, "DATA");
        this->expectSMTP("354", "DATA");

        int timeout = this->getCommandTimeout();
        time_t deadline = time(NULL) + timeout;
        while(!feof(stream))
        {
            if(fgets(szBuffer, sizeof(szBuffer), stream) == NULL)
                break;

            int remain = (int)(deadline - time(NULL));
            if(remain <= 0
                || this->proc == NULL
                || !this->proc->IsRunning())
            {
                this->failPipe("Timeout/error while writing message data to b1gMail pipe (keep-alive: 1).");
            }

            if(*szBuffer == '.')
            {
                if(!this->proc->WriteFully(".", 1, remain))
                    this->failPipe("Timeout/error while writing message data to b1gMail pipe (keep-alive: 1).");
            }

            size_t iReadBytes = strlen(szBuffer);
            if(!this->proc->WriteFully(szBuffer, iReadBytes, remain))
                this->failPipe("Timeout/error while writing message data to b1gMail pipe (keep-alive: 1).");
        }

        this->writePipe("\r\n.\r\n", 5, "end-of-data");
        this->expectSMTP("250", "message data");

        this->deliveryCount++;
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
