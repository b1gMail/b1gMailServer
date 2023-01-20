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

#include "mysql.h"
#include <string>
#include <stdexcept>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace std;

const char *g_mysqlSocketSearchPaths[] = {
    "/var/lib/mysql/mysql.sock",
    "/var/run/mysqld/mysqld.sock",
    "/tmp/mysql.sock",
    "/var/lib/mysqld/mysqld.sock",
    "/var/lib/mysql/mysqld.sock",
    "/var/lib/mysqld/mysql.sock",
    "/var/run/mysql/mysql.sock",
    "/var/run/mysqld/mysql.sock",
    "/var/run/mysql/mysqld.sock",
    "/tmp/mysqld.sock",
    NULL
};

MySQL_DB::MySQL_DB(const char *strHost,
    const char *strUser,
    const char *strPass,
    const char *strDB,
    const char *strSocket)
{
    if((this->handle = mysql_init(NULL)) == NULL)
        throw runtime_error("MySQL error: library initialization failed");

    if(strSocket != NULL)
        strHost = "localhost";

    // try to find socket if not specified
    const char *theSocket = strSocket;
    if(strcmp(strHost, "localhost") == 0 && (theSocket == NULL || *theSocket == '\0'))
    {
        struct stat st;

        for(int i=0; g_mysqlSocketSearchPaths[i] != NULL; i++)
        {
            if(stat(g_mysqlSocketSearchPaths[i], &st) == 0 && S_ISSOCK(st.st_mode))
            {
                theSocket = g_mysqlSocketSearchPaths[i];
                break;
            }
        }
    }

    if(mysql_real_connect(this->handle,
        strHost,
        strUser,
        strPass,
        strDB,
        0,
        theSocket,
        0) != this->handle)
    throw runtime_error(string("MySQL error: ") + string(mysql_error(this->handle)));

    try
    {
        this->Query("SET sql_mode=''");
    }
    catch(...) { }
}

MySQL_DB::~MySQL_DB()
{
    if(this->handle != NULL)
        mysql_close(this->handle);
}

void MySQL_DB::LibraryInit()
{
    if(mysql_library_init(0, NULL, NULL) != 0)
        throw runtime_error("Failed to initialize MySQL library");
}

void MySQL_DB::LibraryEnd()
{
    mysql_library_end();
}

MySQL_Result *MySQL_DB::Query(const char *szQuery, ...)
{
    char szBuff[255], *szBuff2, *szArg;
    MySQL_Result *res = NULL;
    string strQuery;
    va_list arglist;

    // prepare query
    va_start(arglist, szQuery);
    std::size_t queryLength = strlen(szQuery);
    for(std::size_t i=0; i < queryLength; i++)
    {
        char c = szQuery[i],
            c2 = szQuery[i+1];
        if(c == '%')
        {
            switch(c2)
            {
            case '%':
                strQuery += '%';
                break;
            case 's':
                strQuery.append(va_arg(arglist, char *));
                break;
            case 'd':
                snprintf(szBuff, 255, "%d", va_arg(arglist, int));
                strQuery.append(szBuff);
                break;
            case 'f':
                snprintf(szBuff, 255, "%f", va_arg(arglist, double));
                strQuery.append(szBuff);
                break;
            case 'l':
                snprintf(szBuff, 255, "%li", va_arg(arglist, long int));
                strQuery.append(szBuff);
                break;
            case 'u':
                snprintf(szBuff, 255, "%lu", va_arg(arglist, unsigned long));
                strQuery.append(szBuff);
                break;
            case 'q':
                szArg = va_arg(arglist, char *);
                szBuff2 = new char[strlen(szArg)*2+1];
                mysql_real_escape_string(this->handle, szBuff2, szArg, (unsigned long)strlen(szArg));
                strQuery.append(szBuff2);
                delete[] szBuff2;
                break;
            };
            i++;
        }
        else
        {
            strQuery += c;
        }
    }
    va_end(arglist);

    // execute query
    if(mysql_real_query(this->handle, strQuery.c_str(), (unsigned long)strQuery.length()) == 0)
    {
        MYSQL_RES *result = mysql_store_result(this->handle);
        if(result != NULL)
            res = new MySQL_Result(result);
    }
    else
    {
        throw runtime_error(string("MySQL error: ") + string(mysql_error(this->handle)));
    }

    return(res);
}

unsigned long MySQL_DB::InsertId()
{
    return((unsigned long)mysql_insert_id(this->handle));
}

unsigned long MySQL_DB::AffectedRows()
{
    return((unsigned long)mysql_affected_rows(this->handle));
}
