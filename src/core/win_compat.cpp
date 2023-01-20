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

#ifdef WIN32

#include <core/core.h>

char *strtok_r(char *str, const char *delim, char **saveptr)
{
    return(strtok(str, delim));
}

/*
 * strcasestr() implementation
 */
char *strcasestr(const char *s1, const char *s2)
{
    char *p, *startn = NULL, *np = NULL;

    for(p = (char *)s1; *p != NULL; p++)
    {
        if(np)
        {
            if(toupper(*p) == toupper(*np))
            {
                if(*++np == NULL)
                    return(startn);
            }
            else
            {
                np = 0;
            }
        }
        else if(toupper(*p) == toupper(*s2))
        {
            np = (char *)s2 + 1;
            startn = p;
        }
    }

    return(NULL);
}

/*
 * alarm implementation
 */
HANDLE hAlarmThread = NULL;
bool bQuitThread = false, bInterruptFGets = false, bAlarmActive = false;
void (*AlarmFunction)(int);
unsigned int AlarmSeconds;
CRITICAL_SECTION alarmLock;

void InterruptFGets()
{
    bInterruptFGets = true;
}

void SetAlarmSignalCallback(void (*func)(int))
{
    AlarmFunction = func;
}

DWORD WINAPI AlarmThread(void *param)
{
    unsigned int MilliSeconds = 0;
    bool bRunFunction = false;

    while(!bQuitThread)
    {
        Sleep(10);
        MilliSeconds += 10;

        if(MilliSeconds % 1000 == 0)
        {
            EnterCriticalSection(&alarmLock);
            {
                if(bAlarmActive)
                {
                    if(AlarmSeconds > 0)
                    {
                        if(--AlarmSeconds == 0)
                        {
                            bRunFunction = true;
                            bAlarmActive = false;
                        }
                    }
                }
            }
            LeaveCriticalSection(&alarmLock);
        }

        if(bRunFunction)
        {
            AlarmFunction(SIGALRM);
            bRunFunction = false;
        }
    }

    return(0);
}

unsigned int alarm(unsigned int sec)
{
    unsigned int iResult = 0;
    static bool firstRun = true;

    if(firstRun)
    {
        firstRun = false;

        AlarmSeconds = 0;
        bAlarmActive = true;
        InitializeCriticalSection(&alarmLock);
        hAlarmThread = CreateThread(NULL, 0, AlarmThread, NULL, 0, NULL);
        if(hAlarmThread != NULL)
            CloseHandle(hAlarmThread);
    }

    EnterCriticalSection(&alarmLock);
    {
        if(sec == 0)
            iResult = AlarmSeconds;

        AlarmSeconds = sec;
        bAlarmActive = (sec > 0);
    }
    LeaveCriticalSection(&alarmLock);

    return(iResult);
}

#endif
