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

#include <core/utils.h>
#include <core/process.h>

#include <errno.h>
#include <string.h>
#include <pthread.h>
#ifndef WIN32
#include <sys/select.h>
#include <unistd.h>
#endif

pthread_mutex_t g_popenLock;

using namespace std;
using namespace Core;

void Process::Init()
{
    pthread_mutex_init(&g_popenLock, NULL);
}

void Process::UnInit()
{
    pthread_mutex_destroy(&g_popenLock);
}

Process::Process(const string &commandLine)
{
    this->closeTimeout = 0;
    this->open = false;
#ifndef WIN32
    this->pid = 0;
#else
    this->pid = INVALID_HANDLE_VALUE;
#endif
    this->fdIn = 0;
    this->fdOut = 0;
    this->fpIn = NULL;
    this->fpOut = NULL;
    this->commandLine = commandLine;
}

Process::~Process()
{
    if(this->IsOpen())
        this->Close();
}

FILE *Process::GetInputFP()
{
    if(!this->IsOpen())
        return(NULL);

    return(this->fpIn);
}

FILE *Process::GetOutputFP()
{
    if(!this->IsOpen())
        return(NULL);

    return(this->fpOut);
}

void Process::SetCloseTimeout(int val)
{
    this->closeTimeout = val;
}

bool Process::IsOpen()
{
    return(this->open);
}

bool Process::Open()
{
    if(this->IsOpen())
        return(false);

    this->readBuf.clear();

    pthread_mutex_lock(&g_popenLock);
    this->pid = utils->POpen(this->commandLine.c_str(), &this->fdIn, &this->fdOut);
    pthread_mutex_unlock(&g_popenLock);

#ifndef WIN32
    if(this->pid > 0)
#else
    if(this->pid != INVALID_HANDLE_VALUE)
#endif
    {
        this->fpIn = fdopen(this->fdIn, "w");
        if(this->fpIn != NULL)
        {
            this->fpOut = fdopen(this->fdOut, "r");
            if(this->fpOut != NULL)
            {
                setvbuf(this->fpIn, NULL, _IONBF, 0);
                setvbuf(this->fpOut, NULL, _IONBF, 0);
                this->open = true;
            }
            else
            {
                close(this->fdOut);
                fclose(this->fpIn);
                this->EndProcess();
            }
        }
        else
        {
            close(this->fdOut);
            close(this->fdIn);
            this->EndProcess();
        }
    }

    return(this->IsOpen());
}

int Process::Close(int *quitSignal)
{
    if(!this->IsOpen())
        return(-1);

    fclose(this->fpIn);

    open = false;
    this->readBuf.clear();
    int result = this->EndProcess(quitSignal);

    fclose(this->fpOut);

    return(result);
}

#ifdef WIN32

int Process::EndProcess(int *quitSignal)
{
    DWORD result = 255;
    time_t timeoutTime = (this->closeTimeout > 0) ? time(NULL) + (time_t)this->closeTimeout : 0;
    bool timeout = true;
    if(quitSignal != NULL) *quitSignal = -1;

    // wait for process to finish
    while(timeoutTime == 0 || time(NULL) < timeoutTime)
    {
        DWORD waitResult = WaitForSingleObject(this->pid, 100);

        if(waitResult != WAIT_TIMEOUT)
        {
            GetExitCodeProcess(this->pid, &result);
            timeout = false;
            break;
        }
    }

    // timeout?
    if(timeout)
    {
        *quitSignal = SIGKILL;
        TerminateProcess(this->pid, 1);
        WaitForSingleObject(this->pid, INFINITE);
    }

    return((int)result);
}

bool Process::IsRunning()
{
    return(WaitForSingleObject(this->pid, 0) == WAIT_TIMEOUT);
}

#else

int Process::EndProcess(int *quitSignal)
{
    int result = 255;
    time_t timeoutTime = (this->closeTimeout > 0) ? time(NULL) + (time_t)this->closeTimeout : 0;
    bool timeout = true;
    if(quitSignal != NULL) *quitSignal = -1;

    // wait for process to finish
    while(timeoutTime == 0 || time(NULL) < timeoutTime)
    {
        pid_t waitResult = waitpid(this->pid, &result, WNOHANG);

        if(waitResult > 0)
        {
            timeout = false;
            break;
        }

        utils->MilliSleep(100);
    }

    // timeout?
    if(timeout)
    {
        // ask politely to quit
        if(quitSignal != NULL) *quitSignal = SIGTERM;
        kill(this->pid, SIGTERM);

        // wait 5 seconds for exit
        timeout = true;
        timeoutTime = time(NULL) + 5;
        while(time(NULL) <= timeoutTime)
        {
            waitpid(this->pid, &result, WNOHANG);

            if(kill(this->pid, 0) == -1)
            {
                timeout = false;
                break;
            }

            utils->MilliSleep(100);
        }

        // still alive? => kill
        if(timeout)
        {
            if(quitSignal != NULL) *quitSignal = SIGKILL;
            kill(this->pid, SIGKILL);
        }

        // avoid zombies
        waitpid(this->pid, &result, 0);
    }

    return(result);
}

bool Process::IsRunning()
{
    if(kill(this->pid, 0) == -1)
        return(false);

    int iResult;
    if(waitpid(this->pid, &iResult, WNOHANG) == this->pid)
        return(false);

    return(true);
}

#endif

#ifdef WIN32
#define PIPE_READ_FN    _read
#define PIPE_WRITE_FN   _write
#else
#define PIPE_READ_FN    read
#define PIPE_WRITE_FN   write
#endif

bool Process::WaitForReadable(int timeoutSeconds)
{
    if(!this->readBuf.empty())
        return(true);

    if(timeoutSeconds <= 0)
        timeoutSeconds = 1;

#ifdef WIN32
    HANDLE hPipe = (HANDLE)_get_osfhandle(this->fdOut);
    time_t deadline = time(NULL) + timeoutSeconds;
    while(time(NULL) < deadline)
    {
        DWORD avail = 0;
        if(!PeekNamedPipe(hPipe, NULL, 0, NULL, &avail, NULL))
            return(false);
        if(avail > 0)
            return(true);
        if(!this->IsRunning())
            return(false);
        Sleep(50);
    }
    return(false);
#else
    time_t deadline = time(NULL) + timeoutSeconds;
    while(true)
    {
        time_t now = time(NULL);
        if(now >= deadline)
            return(false);

        struct timeval tv;
        tv.tv_sec = (time_t)(deadline - now);
        tv.tv_usec = 0;

        fd_set fdSet;
        FD_ZERO(&fdSet);
        FD_SET(this->fdOut, &fdSet);

        int rc = select(this->fdOut + 1, &fdSet, NULL, NULL, &tv);
        if(rc > 0 && FD_ISSET(this->fdOut, &fdSet))
            return(true);
        if(rc == 0)
            return(false);
        if(rc < 0)
        {
            if(errno == EINTR)
                continue;
            return(false);
        }
    }
#endif
}

bool Process::WaitForWritable(int timeoutSeconds)
{
    if(timeoutSeconds <= 0)
        timeoutSeconds = 1;

#ifdef WIN32
    return(this->IsRunning());
#else
    time_t deadline = time(NULL) + timeoutSeconds;
    while(true)
    {
        time_t now = time(NULL);
        if(now >= deadline)
            return(false);

        struct timeval tv;
        tv.tv_sec = (time_t)(deadline - now);
        tv.tv_usec = 0;

        fd_set fdSet;
        FD_ZERO(&fdSet);
        FD_SET(this->fdIn, &fdSet);

        int rc = select(this->fdIn + 1, NULL, &fdSet, NULL, &tv);
        if(rc > 0 && FD_ISSET(this->fdIn, &fdSet))
            return(true);
        if(rc == 0)
            return(false);
        if(rc < 0)
        {
            if(errno == EINTR)
                continue;
            return(false);
        }
    }
#endif
}

bool Process::ReadLine(char *buf, size_t bufSize, int timeoutSeconds)
{
    if(buf == NULL || bufSize < 2 || !this->IsOpen())
        return(false);
    if(timeoutSeconds <= 0)
        timeoutSeconds = 30;

    size_t pos = 0;
    time_t deadline = time(NULL) + timeoutSeconds;

    while(pos < bufSize - 1)
    {
        if(this->readBuf.empty())
        {
            int remain = (int)(deadline - time(NULL));
            if(remain <= 0)
                break;
            if(!this->WaitForReadable(remain))
                break;

            char chunk[512];
            int n = (int)PIPE_READ_FN(this->fdOut, chunk, sizeof(chunk));
            if(n < 0)
            {
#ifdef WIN32
                if(errno == EINTR)
                    continue;
#else
                if(errno == EINTR)
                    continue;
#endif
                break;
            }
            if(n == 0)
                break;
            this->readBuf.append(chunk, (size_t)n);
        }

        size_t nl = this->readBuf.find('\n');
        if(nl == string::npos)
        {
            size_t take = this->readBuf.size();
            if(take > bufSize - 1 - pos)
                take = bufSize - 1 - pos;
            memcpy(buf + pos, this->readBuf.data(), take);
            pos += take;
            this->readBuf.erase(0, take);
            if(pos >= bufSize - 1)
            {
                buf[pos] = '\0';
                return(true);
            }
            continue;
        }

        size_t take = nl;
        if(take > 0 && this->readBuf[take - 1] == '\r')
            take--;
        if(take > bufSize - 1 - pos)
            take = bufSize - 1 - pos;
        memcpy(buf + pos, this->readBuf.data(), take);
        pos += take;
        this->readBuf.erase(0, nl + 1);
        buf[pos] = '\0';
        return(true);
    }

    buf[pos] = '\0';
    return(false);
}

bool Process::WriteFully(const void *data, size_t len, int timeoutSeconds)
{
    if(data == NULL || !this->IsOpen())
        return(false);
    if(len == 0)
        return(true);
    if(timeoutSeconds <= 0)
        timeoutSeconds = 30;

    const char *p = (const char *)data;
    size_t left = len;
    time_t deadline = time(NULL) + timeoutSeconds;

    while(left > 0)
    {
        int remain = (int)(deadline - time(NULL));
        if(remain <= 0)
            return(false);
        if(!this->WaitForWritable(remain))
            return(false);

        int n = (int)PIPE_WRITE_FN(this->fdIn, p, left);
        if(n < 0)
        {
            if(errno == EINTR)
                continue;
#ifndef WIN32
            if(errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
#endif
            return(false);
        }
        if(n == 0)
            return(false);
        p += n;
        left -= (size_t)n;
    }

    return(true);
}
