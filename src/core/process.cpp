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

#include <pthread.h>

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
