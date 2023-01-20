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

#ifndef _CORE_CONFIG_H_
#define _CORE_CONFIG_H_

#include <core/core.h>
#include <plugin/plugin.h>
#include <pthread.h>

#include <map>

namespace Core
{
    class Config : public b1gMailServer::Config
    {
    public:
        Config();
        ~Config();

    public:
        void ReadDBConfig();
        const char *Get(const char *szKey);
        void CheckRequiredValues();
        void CheckDBRequiredValues();
        void Dump();

    private:
        map<string, string> items;
        pthread_mutex_t mutex;

        Config(const Config &);
        Config &operator=(const Config &);
    };
};

extern Core::Config *cfg;

#endif
