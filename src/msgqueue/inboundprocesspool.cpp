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

#include <msgqueue/inboundprocesspool.h>

InboundProcessPool::InboundProcessPool()
{
    pthread_mutex_init(&this->lock, NULL);
}

InboundProcessPool::~InboundProcessPool()
{
    while(!this->pool.empty())
    {
        InboundProcess *proc = this->pool.front();
        this->pool.pop();

        delete proc;
    }

    pthread_mutex_destroy(&this->lock);
}

InboundProcess *InboundProcessPool::getInboundProcess()
{
    InboundProcess *proc = NULL;

    pthread_mutex_lock(&this->lock);
    if(!this->pool.empty())
    {
        proc = this->pool.front();
        this->pool.pop();
    }
    pthread_mutex_unlock(&this->lock);

    if(proc == NULL)
    {
        proc = this->createInboundProcess();
    }

    return(proc);
}

InboundProcess *InboundProcessPool::createInboundProcess()
{
    InboundProcess *proc = new InboundProcess(strcmp(cfg->Get("inbound_reuse_process"), "1") == 0);
    return(proc);
}

void InboundProcessPool::putBackInboundProcess(InboundProcess *proc)
{
    pthread_mutex_lock(&this->lock);
    this->pool.push(proc);
    pthread_mutex_unlock(&this->lock);
}
