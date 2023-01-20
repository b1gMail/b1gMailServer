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

#ifndef _MYSQL_H_
#define _MYSQL_H_

#include <stdlib.h>
#include <string.h>
#include <mysql.h>

class MySQL_Result
{
public:
    MySQL_Result(MYSQL_RES *res);
    ~MySQL_Result();

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

class MySQL_DB
{
public:
    MySQL_DB(const char *strHost,
        const char *strUser,
        const char *strPass,
        const char *strDB,
        const char *strSocket = NULL);
    ~MySQL_DB();

    static void LibraryInit();
    static void LibraryEnd();

public:
    MySQL_Result *Query(const char *strQuery, ...);
    unsigned long InsertId();
    unsigned long AffectedRows();

private:
    MYSQL *handle;

    MySQL_DB(const MySQL_DB &);
    MySQL_DB &operator=(const MySQL_DB &);
};

#endif
