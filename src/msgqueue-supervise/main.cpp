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

#include "main.h"
#include <syslog.h>
#include <time.h>

pid_t pQueue = 0;
bool bStop = false;

void SignalHandler(int iSignal)
{
    if(iSignal == SIGINT || iSignal == SIGTERM)
    {
        bStop = true;
        if(pQueue != 0)
            kill(pQueue, SIGINT);
    }
    else if(iSignal == SIGHUP)
    {
        bStop = false;
        if(pQueue != 0)
            kill(pQueue, SIGINT);
    }
}

int main(int argc, char *argv[])
{
    FILE *fp;
    struct stat st;
    int iStatus;
    int iPID;

    // already running?
    if(stat(QUEUE_PID_FILE, &st) == 0)
    {
        fp = fopen(QUEUE_PID_FILE, "r");
        if(fscanf(fp, "%d", &iPID) == 1)
        {
            fclose(fp);
            if(kill((pid_t)iPID, 0) != -1)
            {
                fprintf(stderr, "MSGQueue already running (PID %d)\n",
                    iPID);
                return(1);
            }
            else
                unlink(QUEUE_PID_FILE);
        }
        else
            fclose(fp);
    }

    // fork
    if(fork() != 0)
        return(0);

    // close fds
    /*fclose(stdin);
    fclose(stdout);
    fclose(stderr);*/

    // set signal handlers
    signal(SIGINT, SignalHandler);
    signal(SIGHUP, SignalHandler);
    signal(SIGTERM, SignalHandler);

    // store pid
    fp = fopen(QUEUE_PID_FILE, "w");
    fprintf(fp, "%d", (int)getpid());
    fclose(fp);

    // launch it!
    bool FirstStart = true, FirstStartSucceeded = false;
    unsigned int FailCounter = 0;
    int ResultCode = 0;
    openlog("b1gMailServer-MSGQueue", LOG_PID, LOG_MAIL);
    while(!bStop)
    {
        if((pQueue = fork()) == 0)
        {
            execl(BIN_DIR "b1gmailserver", BIN_DIR "b1gmailserver", "--msgqueue", (char *)NULL);
            return(LAUNCH_ERROR_CODE);
        }

        time_t BeforeWait = time(NULL);
        if(waitpid(pQueue, &iStatus, 0) <= 0)
        {
            syslog(LOG_ERR, "waitpid() failed");
            unlink(QUEUE_PID_FILE);
            ResultCode = 1;
            break;
        }
        else if(WIFEXITED(iStatus) && WEXITSTATUS(iStatus) == LAUNCH_ERROR_CODE)
        {
            syslog(LOG_ERR, "Failed to launch MSGQueue (%d = %d)", WEXITSTATUS(iStatus), LAUNCH_ERROR_CODE);
            unlink(QUEUE_PID_FILE);
            ResultCode = 1;
            break;
        }
        else if(WIFSIGNALED(iStatus) && WTERMSIG(iStatus) == SIGINT)
        {
            break;
        }
        else if(WIFSIGNALED(iStatus) && WTERMSIG(iStatus) == SIGSEGV)
        {
            syslog(LOG_NOTICE, "MSGQueue SIGSEGV - restarting");
        }

        if(time(NULL)-BeforeWait < 5)
        {
            FailCounter++;
            sleep(5);
        }
        else
        {
            if(FirstStart)
                FirstStartSucceeded = true;
            FailCounter = 0;
        }

        FirstStart = false;

        if(FailCounter >= 6 && !FirstStartSucceeded)
        {
            syslog(LOG_ERR, "Exiting after 6 subsequent failed MSGQueue launch attempts (run time < 5 seconds)");
            ResultCode = 1;
            break;
        }
    }
    closelog();

    // return
    unlink(QUEUE_PID_FILE);
    return(ResultCode);
}
