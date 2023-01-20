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

#ifndef _MSGQUEUE_SMTPSESSIONPOOL_H_
#define _MSGQUEUE_SMTPSESSIONPOOL_H_

#include <core/core.h>
#include <msgqueue/smtpsession.h>
#include <list>
#include <pthread.h>

class SMTPSessionPool
{
public:
    SMTPSessionPool();
    ~SMTPSessionPool();

public:
    SMTPSession *getSMTPSession(const string &domain, const string &host, int port = 25);
    void putBackSMTPSession(SMTPSession *sess);
    void cleanUp();

private:
    SMTPSession *createSMTPSession(const string &domain, const string &host, int port = 25);

private:
    pthread_mutex_t lock;
    list<SMTPSession *> pool;

    SMTPSessionPool(const SMTPSessionPool &);
    SMTPSessionPool &operator=(const SMTPSessionPool &);
};

#endif
