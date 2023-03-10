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

#ifndef _MSGQUEUE_INBOUNDPROCESSPOOL_H_
#define _MSGQUEUE_INBOUNDPROCESSPOOL_H_

#include <core/core.h>
#include <msgqueue/inboundprocess.h>
#include <pthread.h>
#include <queue>

class InboundProcessPool
{
public:
    InboundProcessPool();
    ~InboundProcessPool();

public:
    InboundProcess *getInboundProcess();
    void putBackInboundProcess(InboundProcess *proc);

private:
    InboundProcess *createInboundProcess();

private:
    pthread_mutex_t lock;
    queue<InboundProcess *> pool;

    InboundProcessPool(const InboundProcessPool &);
    InboundProcessPool &operator=(const InboundProcessPool &);
};

#endif
