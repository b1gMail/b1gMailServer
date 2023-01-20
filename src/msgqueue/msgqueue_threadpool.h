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

#ifndef _MSGQUEUE_MSGQUEUE_THREADPOOL_H
#define _MSGQUEUE_MSGQUEUE_THREADPOOL_H

#include <core/core.h>
#include <core/threadpool.h>

class MSGQueue_ThreadPool : public ThreadPool
{
public:
    MSGQueue_ThreadPool(unsigned int threadCount, unsigned int maxGrow, void (*workProcessor)(void *));
    ~MSGQueue_ThreadPool();
};

#endif
