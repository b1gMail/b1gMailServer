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

#ifndef _CORE_MYSQL_H_
#define _CORE_MYSQL_H_

#define PRIO_NOTE       1
#define PRIO_WARNING    2
#define PRIO_ERROR      4
#define PRIO_DEBUG      8

#define CMP_CORE        1
#define CMP_POP3        2
#define CMP_IMAP        4
#define CMP_HTTP        8
#define CMP_FTP         9
#define CMP_SMTP        16
#define CMP_MSGQUEUE    32
#define CMP_PLUGIN      64

#define MYSQL_MAX_CONNECTION_ATTEMPTS   10
#define MYSQL_CONNECTION_ATTEMPT_DELAY  500     // ms

#include <core/core.h>
#include <plugin/plugin.h>
#include <mysql.h>
#include <string>
#include <time.h>

namespace Core
{
    class MySQL_Result : public b1gMailServer::MySQL_Result
    {
    public:
        MySQL_Result(MYSQL_RES *res);
        virtual ~MySQL_Result();

    public:
        char **FetchRow();
        MYSQL_FIELD *FetchFields();
        unsigned long NumRows();
        unsigned long NumFields();
        unsigned long *FetchLengths();

    private:
        MYSQL_RES *result;

        MySQL_Result(const MySQL_Result &);
        MySQL_Result &operator=(const MySQL_Result &);
    };

    class MySQL_DB : public b1gMailServer::MySQL_DB
    {
    public:
        MySQL_DB(const char *strHost,
            const char *strUser,
            const char *strPass,
            const char *strDB,
            const char *strSocket = NULL,
            bool fromThread = false);
        virtual ~MySQL_DB();

    public:
        MySQL_Result *Query(const char *strQuery, ...);
        unsigned long InsertId();
        unsigned long AffectedRows();
        void Log(int iComponent, int iSeverity, char *szEntry);
        void TempClose();
        std::string Escape(std::string in);

    private:
        void Connect();

    private:
        MYSQL *handle;
        std::string strHost, strUser, strPass, strDB, strSocket;
        bool bTempClosed;
        time_t lastQuery;
        bool fromThread;

        MySQL_DB(const MySQL_DB &);
        MySQL_DB &operator=(const MySQL_DB &);
    };
};

extern Core::MySQL_DB *db, *mydb;

#endif
