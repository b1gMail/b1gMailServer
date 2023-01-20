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

#ifndef _CORE_THREADPOOL_H_
#define _CORE_THREADPOOL_H_

#include <pthread.h>
#include <vector>
#include <list>
#include <queue>

namespace Core
{
    class ThreadPool;

    class ThreadInfo
    {
    public:
        ThreadPool *pool;
        bool isDynamic;
        bool isBusy;
        unsigned int id;
    };

    class ThreadPool
    {
    public:
        ThreadPool(unsigned int threadCount, unsigned int maxGrow, void (*workProcessor)(void *));
        virtual ~ThreadPool();

    public:
        void start();
        void stop();
        void addToQueue(void *item);
        void lockQueue();
        void unlockQueue();
        static void *threadEntry(void *arg);
        void *threadMain(ThreadInfo *ti);
        void shrink();

    protected:
        void *popJob(ThreadInfo *ti, bool skipWait = false);
        void grow();

    public:
        unsigned int threadCount;
        unsigned int maxGrow;
        unsigned int createdThreads;
        unsigned int busyThreads;
        unsigned int threadTimeout;
        std::list<pthread_t> threads;
        std::queue<pthread_t> finishedDynamicThreads;
        bool queueLocked;
        pthread_mutex_t queueLock;
        pthread_mutex_t finishedDynamicThreadsLock;
        std::list<void *> queue;        // we do not use a std::queue here as we may want to skip entries

    protected:
        bool run;
        pthread_cond_t workAvailable;
        void (*workProcessor)(void *);

    private:
        ThreadPool(const ThreadPool &);
        ThreadPool &operator=(const ThreadPool &);
    };
};

#endif
