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

#include <pthread.h>
#include <sys/timeb.h>
#include <errno.h>

DWORD _pthread_tls = TLS_OUT_OF_INDEXES;

int pthread_mutex_init(pthread_mutex_t *m, pthread_mutexattr_t *a)
{
    InitializeCriticalSection(m);
    return(0);
}

int pthread_mutex_lock(pthread_mutex_t *m)
{
    EnterCriticalSection(m);
    return(0);
}

int pthread_mutex_unlock(pthread_mutex_t *m)
{
    LeaveCriticalSection(m);
    return(0);
}

int pthread_mutex_destroy(pthread_mutex_t *m)
{
    DeleteCriticalSection(m);
    return(0);
}

int pthread_cond_init(pthread_cond_t *c, pthread_condattr_t *a)
{
    InitializeConditionVariable(c);
    return(0);
}

int pthread_cond_destroy(pthread_cond_t *c)
{
    return(0);
}

int pthread_cond_signal(pthread_cond_t *c)
{
    WakeConditionVariable(c);
    return(0);
}

int pthread_cond_broadcast(pthread_cond_t *c)
{
    WakeAllConditionVariable(c);
    return(0);
}

int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m)
{
    SleepConditionVariableCS(c, m, INFINITE);
    return(0);
}

static unsigned long long _pthread_timespec_to_rel_ms(struct timespec *t)
{
    struct __timeb64 tb;
    _ftime64(&tb);

    unsigned long long t1 = t->tv_sec * 1000 + t->tv_nsec / 1000000;
    unsigned long long t2 = tb.time * 1000 + tb.millitm;

    if(t1 < t2) return(t1);
    return(t1 - t2);
}

int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, struct timespec *t)
{
    unsigned long long tm = _pthread_timespec_to_rel_ms(t);

    if(!SleepConditionVariableCS(c, m, (DWORD)tm)) return(ETIMEDOUT);
    if(!_pthread_timespec_to_rel_ms(t)) return(ETIMEDOUT);

    return(0);
}

int pthread_exit(void *res)
{
    return(0);
}

pthread_t pthread_self()
{
    pthread_v *tv = (pthread_v *)TlsGetValue(_pthread_tls);
    return(tv);
}

static DWORD WINAPI _pthread_thread_entry(LPVOID arg)
{
    pthread_v *tv = (pthread_v *)arg;
    TlsSetValue(_pthread_tls, (LPVOID)tv);
    tv->res = tv->func(tv->arg);
    return(0);
}

int pthread_create(pthread_t *t, pthread_attr_t *attr, void *(* func)(void *), void *arg)
{
    if(_pthread_tls == TLS_OUT_OF_INDEXES)
    {
        _pthread_tls = TlsAlloc();

        if(_pthread_tls == TLS_OUT_OF_INDEXES)
            return(1);
    }

    pthread_v *tv = new pthread_v;
    *t = tv;
    tv->func = func;
    tv->arg = arg;

    DWORD dwStackSize = 0;
    if(attr != NULL)
    {
        dwStackSize = attr->s_size;
    }

    tv->hThread = CreateThread(NULL, dwStackSize, _pthread_thread_entry, tv, 0, &tv->dwThreadID);
    if(tv->hThread == NULL)
    {
        delete tv;
        return(1);
    }

    return(0);
}

int pthread_join(pthread_t t, void **res)
{
    WaitForSingleObject(t->hThread, INFINITE);
    CloseHandle(t->hThread);
    delete t;

    return(0);
}
