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

#ifndef _WINPTHREAD_PTHREAD_H_
#define _WINPTHREAD_PTHREAD_H_

#include <windows.h>
#include <time.h>

#ifndef ETIMEDOUT
#   define ETIMEDOUT            110
#endif

#define PTHREAD_MUTEX_INITIALIZER   {(void*)-1,-1,0,0,0,0}

typedef CRITICAL_SECTION pthread_mutex_t;
typedef CONDITION_VARIABLE pthread_cond_t;
typedef unsigned int pthread_mutexattr_t;
typedef unsigned int pthread_condattr_t;

typedef struct
{
    HANDLE hThread;
    DWORD dwThreadID;
    void *(* func)(void *);
    void *arg;
    void *res;
}
*pthread_t, pthread_v;

typedef struct
{
    unsigned int p_state;
    void *stack;
    size_t s_size;
}
pthread_attr_t;

int pthread_mutex_init(pthread_mutex_t *m, pthread_mutexattr_t *a);
int pthread_mutex_lock(pthread_mutex_t *m);
int pthread_mutex_unlock(pthread_mutex_t *m);
int pthread_mutex_destroy(pthread_mutex_t *m);

int pthread_cond_init(pthread_cond_t *c, pthread_condattr_t *a);
int pthread_cond_signal(pthread_cond_t *c);
int pthread_cond_broadcast(pthread_cond_t *c);
int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m);
int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, struct timespec *t);
int pthread_cond_destroy(pthread_cond_t *c);

pthread_t pthread_self();
int pthread_exit(void *res);

int pthread_create(pthread_t *t, pthread_attr_t *attr, void *(* func)(void *), void *arg);
int pthread_join(pthread_t t, void **res);

#endif
