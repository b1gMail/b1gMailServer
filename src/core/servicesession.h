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

#ifndef _CORE_SERVICESESSION_H_
#define _CORE_SERVICESESSION_H_

#include <core/core.h>

#undef fgets
#undef fread
#undef fwrite
#undef vprintf
#undef printf

namespace Core
{
    class MySQL_DB;

    class ServiceSession
    {
    public:
        ServiceSession(SOCKET sIn = (SOCKET)NULL, SOCKET sOut = (SOCKET)NULL);
        virtual ~ServiceSession();

    public:
        virtual void Run() = 0;

    protected:
        char *fgets(char *buf, int s, FILE *fp);
        size_t fread(void *buf, size_t s1, size_t s2, FILE *fp);
        size_t fwrite(const void *buf, size_t s1, size_t s2, FILE *fp);
        int vprintf(const char *str, va_list list);
        int printf(const char *str, ...);

    protected:
        MySQL_DB *db;
        SOCKET sIn, sOut;

    public:
        int trafficIn, trafficOut;
        bool ioSSL;
        SSL *ssl;
        bool quitService;

        ServiceSession(const ServiceSession &);
        ServiceSession &operator=(const ServiceSession &);
    };
};

#endif
