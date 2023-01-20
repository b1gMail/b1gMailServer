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

#include <msgqueue/smtpsessionpool.h>

#define SMTP_SESSION_HOLDTIME       10 /* seconds */

SMTPSessionPool::SMTPSessionPool()
{
    pthread_mutex_init(&this->lock, NULL);
}

SMTPSessionPool::~SMTPSessionPool()
{
    list<SMTPSession *>::iterator it;

    for(it = this->pool.begin(); it != this->pool.end(); ++it)
    {
        delete *it;
    }

    this->pool.clear();

    pthread_mutex_destroy(&this->lock);
}

SMTPSession *SMTPSessionPool::getSMTPSession(const string &domain, const string &host, int port)
{
    SMTPSession *sess = NULL;

    pthread_mutex_lock(&this->lock);
    for(list<SMTPSession *>::iterator it = this->pool.begin(); it != this->pool.end(); ++it)
    {
        if((*it)->domain == domain)
        {
            sess = *it;
            this->pool.erase(it);
            break;
        }
    }
    pthread_mutex_unlock(&this->lock);

    if(sess == NULL)
    {
        sess = this->createSMTPSession(domain, host, port);
    }

    return(sess);
}

SMTPSession *SMTPSessionPool::createSMTPSession(const string &domain, const string &host, int port)
{
    SMTPSession *sess = new SMTPSession(domain, host, port);
    return(sess);
}

void SMTPSessionPool::putBackSMTPSession(SMTPSession *sess)
{
    pthread_mutex_lock(&this->lock);
    this->pool.push_back(sess);
    pthread_mutex_unlock(&this->lock);
}

void SMTPSessionPool::cleanUp()
{
    time_t delTime = time(NULL) - SMTP_SESSION_HOLDTIME;

    pthread_mutex_lock(&this->lock);
    list<SMTPSession *>::iterator it;
    for(it = this->pool.begin(); it != this->pool.end(); )
    {
        SMTPSession *sess = *it;

        if(sess->lastUse < delTime)
        {
            it = this->pool.erase(it);
            delete sess;
        }
        else
        {
            ++it;
        }
    }
    pthread_mutex_unlock(&this->lock);
}
