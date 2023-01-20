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

#include <core/core.h>
#include <core/threadpool.h>
#ifndef WIN32
#include <sys/time.h>
#else
#include <sys/timeb.h>
#endif
#include <utility>

using namespace std;

ThreadPool::ThreadPool(unsigned int threadCount, unsigned int maxGrow, void (*workProcessor)(void *))
{
    if(threadCount < 1)
        threadCount = 1;
    if(maxGrow < threadCount)
        maxGrow = threadCount;

    this->threadCount       = threadCount;
    this->maxGrow           = maxGrow;
    this->run               = false;
    this->workProcessor     = workProcessor;
    this->busyThreads       = 0;
    this->createdThreads    = 0;
    this->threadTimeout     = 30;

    pthread_mutex_init(&this->finishedDynamicThreadsLock, NULL);
    pthread_mutex_init(&this->queueLock, NULL);
    pthread_cond_init(&this->workAvailable, NULL);
}

ThreadPool::~ThreadPool()
{
    this->stop();

    pthread_mutex_destroy(&this->finishedDynamicThreadsLock);
    pthread_mutex_destroy(&this->queueLock);
    pthread_cond_destroy(&this->workAvailable);
}

void ThreadPool::lockQueue()
{
    pthread_mutex_lock(&this->queueLock);
    this->queueLocked = true;
}

void ThreadPool::unlockQueue()
{
    if(!this->queueLocked)
        return;

    pthread_mutex_unlock(&this->queueLock);
    this->queueLocked = false;
}

void ThreadPool::grow()
{
    if(this->createdThreads >= this->maxGrow)
        return;

    ThreadInfo *ti = new ThreadInfo;
    ti->pool = this;
    ti->isDynamic = true;
    ti->isBusy = false;
    ti->id = this->threads.size();

    int result;
    pthread_t thread;
    result = pthread_create(&thread,
            NULL,
            &ThreadPool::threadEntry,
            ti);

    if(result != 0)
    {
        delete ti;
        //FATAL("pthread_create() failed for worker thread %u with status %d", i, result);
        throw new Exception("Failed to grow thread pool");
    }
    else
    {
        //DEBUG("Worker thread %u created", i);
        this->threads.push_back(thread);
        ++this->createdThreads;
    }
}

void ThreadPool::shrink()
{
    pthread_mutex_lock(&this->finishedDynamicThreadsLock);
    while(!this->finishedDynamicThreads.empty())
    {
        pthread_t thread = this->finishedDynamicThreads.front();

        pthread_join(thread, NULL);

        for(list<pthread_t>::iterator it = this->threads.begin(); it != this->threads.end(); ++it)
        {
            if(*it == thread)
            {
                it = this->threads.erase(it);
                break;
            }
        }

        this->finishedDynamicThreads.pop();
        --this->createdThreads;
    }
    pthread_mutex_unlock(&this->finishedDynamicThreadsLock);
}

void ThreadPool::addToQueue(void *item)
{
    if(!this->queueLocked)
        pthread_mutex_lock(&this->queueLock);

    this->queue.push_back(item);

    if((this->busyThreads >= this->createdThreads || this->queue.size() > this->createdThreads)
        && (this->createdThreads < this->maxGrow))
    {
        this->grow();
    }

    pthread_cond_signal(&this->workAvailable);

    if(!this->queueLocked)
        pthread_mutex_unlock(&this->queueLock);
}

void ThreadPool::start()
{
    int result;

    this->run = true;

    for(unsigned int i=0; i<threadCount; i++)
    {
        ThreadInfo *ti = new ThreadInfo;
        ti->pool = this;
        ti->isDynamic = false;
        ti->isBusy = false;
        ti->id = i;

        pthread_t thread;
        result = pthread_create(&thread,
                NULL,
                &ThreadPool::threadEntry,
                ti);

        if(result != 0)
        {
            delete ti;
            //FATAL("pthread_create() failed for worker thread %u with status %d", i, result);
            throw new Exception("Failed to create worker thread");
        }
        else
        {
            //DEBUG("Worker thread %u created", i);
            this->threads.push_back(thread);
            ++this->createdThreads;
        }
    }
}

void ThreadPool::stop()
{
    pthread_mutex_lock(&this->queueLock);
    this->run = false;
    pthread_cond_broadcast(&this->workAvailable);
    pthread_mutex_unlock(&this->queueLock);

    while(!this->threads.empty())
    {
        pthread_t threadID = this->threads.back();
        this->threads.pop_back();

#ifndef WIN32
        pthread_kill(threadID, SIGINT);
#endif
        pthread_join(threadID, NULL);
    }
}

void *ThreadPool::threadEntry(void *arg)
{
    ThreadInfo *ti = (ThreadInfo *)arg;
    void *result = NULL;

    if(ti != NULL)
    {
        result = ti->pool->threadMain(ti);
        delete ti;
    }

    pthread_exit(result);
    return(result);
}

void *ThreadPool::popJob(ThreadInfo *ti, bool skipWait)
{
    void *job = NULL;

    pthread_mutex_lock(&this->queueLock);
    if(this->run)
    {
        if(!skipWait)
        {
            if(ti->isBusy)
            {
                --this->busyThreads;
                ti->isBusy = false;
            }

            if(ti->isDynamic)
            {
                struct timespec waitTime;

#ifndef WIN32
                struct timeval sysTime;

                if(gettimeofday(&sysTime, NULL) == 0)
                {
                    waitTime.tv_sec     = sysTime.tv_sec + this->threadTimeout;
                    waitTime.tv_nsec    = sysTime.tv_usec * 1000;
                    pthread_cond_timedwait(&this->workAvailable, &this->queueLock, &waitTime);
                }
                else
                {
                    pthread_cond_wait(&this->workAvailable, &this->queueLock);
                }
#else
                struct __timeb64 tb;
                _ftime64(&tb);

                waitTime.tv_sec     = tb.time + this->threadTimeout;
                waitTime.tv_nsec    = tb.millitm * 1000;
                pthread_cond_timedwait(&this->workAvailable, &this->queueLock, &waitTime);
#endif
            }
            else
            {
                pthread_cond_wait(&this->workAvailable, &this->queueLock);
            }
        }
        if(this->queue.size() > 0)
        {
            job = this->queue.front();
            this->queue.pop_front();
        }
        if(!skipWait && job != NULL)
        {
            if(!ti->isBusy)
            {
                ++this->busyThreads;
                ti->isBusy = true;
            }
        }
        if(this->queue.size() > 0)
            pthread_cond_signal(&this->workAvailable);
    }
    pthread_mutex_unlock(&this->queueLock);

    return(job);
}

void *ThreadPool::threadMain(ThreadInfo *ti)
{
    //DEBUG("Worker thread %u entered", threadID);

    void *result = NULL;
    bool skipWait = true;
    bool doRun = true;

    while(doRun)
    {
        void *job = this->popJob(ti, skipWait);

        // process job
        if(job != NULL)
        {
            //DEBUG("Worker thread %u starting job", threadID);
            this->workProcessor(job);
            //DEBUG("Worker thread %u done", threadID);
        }
        else
        {
            // timed out while waiting for a job in a dynamic thread => end thread
            if(ti->isDynamic)
                break;
        }

        // look for jobs which were signalled while all threads were working
        pthread_mutex_lock(&this->queueLock);
        skipWait = this->queue.size() != 0;
        doRun = this->run;
        pthread_mutex_unlock(&this->queueLock);
    }

    if(ti->isDynamic)
    {
        pthread_mutex_lock(&this->finishedDynamicThreadsLock);
        this->finishedDynamicThreads.push(pthread_self());
        pthread_mutex_unlock(&this->finishedDynamicThreadsLock);
    }

    return(result);
}
