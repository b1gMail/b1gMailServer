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

#include <core/mysql.h>

#ifndef WIN32
#   include <sys/types.h>
#   include <sys/stat.h>
#   include <unistd.h>

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
#endif

using namespace Core;

MySQL_DB::MySQL_DB(const char *strHost,
    const char *strUser,
    const char *strPass,
    const char *strDB,
    const char *strSocket,
    bool fromThread)
{
    const char *theSocket = strSocket;

#ifndef WIN32
    // try to find socket if not specified
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
#endif
    if(theSocket == NULL)
        theSocket = "";

    this->strHost = (strHost == NULL) ? "localhost" : strHost;
    this->strUser = strUser;
    this->strPass = strPass;
    this->strDB = strDB;
    this->strSocket = theSocket;
    this->bTempClosed = true;
    this->lastQuery = 0;
    this->fromThread = fromThread;

    this->handle = NULL;
    this->Connect();
}

MySQL_DB::~MySQL_DB()
{
    if(this->handle != NULL)
        mysql_close(this->handle);

    if(this->fromThread)
        mysql_thread_end();
    else
        mysql_library_end();
}

void MySQL_DB::Connect()
{
    if(this->handle != NULL)
        mysql_close(this->handle);

    if((this->handle = mysql_init(NULL)) == NULL)
        throw Core::Exception("MySQL driver initialization failed");

    int iAttempts = 0;
    bool bEstablished = false;

    while(!bEstablished && iAttempts < MYSQL_MAX_CONNECTION_ATTEMPTS)
    {
        iAttempts++;

        if(mysql_real_connect(this->handle,
            this->strHost.c_str(),
            this->strUser.c_str(),
            this->strPass.c_str(),
            this->strDB.c_str(),
            0,
            this->strSocket.length() == 0 ? NULL : this->strSocket.c_str(),
            0) != this->handle)
        {
            // too many connections?
            if(mysql_errno(this->handle) == 1203)
            {
                // delay for MYSQL_CONNECTION_ATTEMPT_DELAY ms to allow connection slots to free up
                usleep(MYSQL_CONNECTION_ATTEMPT_DELAY * 1000);
                continue;
            }

            // other error => fail
            else
            {
                break;
            }
        }
        else
        {
            bEstablished = true;
        }
    }

    if(!bEstablished)
        throw Core::Exception("MySQL", (char *)mysql_error(this->handle));

    this->lastQuery = time(NULL);
    this->bTempClosed = false;

    try
    {
        this->Query("SET sql_mode=''");
    }
    catch(...) { }
}

void MySQL_DB::Log(int iComponent, int iSeverity, char *szEntry)
{
    int iLogLevel = atoi(cfg->Get("loglevel"));
    if((iLogLevel & iSeverity) != 0)
    {
        if(fpBMSLog == NULL)
        {
            this->Query("INSERT INTO bm60_bms_logs(iComponent,iSeverity,iDate,szEntry) VALUES('%d','%d',UNIX_TIMESTAMP(),'#%d - %q')",
                    iComponent,
                    iSeverity,
#ifdef WIN32
                    (int)GetCurrentProcessId(),
#else
                    (int)getpid(),
#endif
                    szEntry);
        }
        else
        {
            char szDate[32] = { 0 };
            const char *szComponent = "Unknown";
            time_t iTime;

            // get time
            iTime = time(NULL);
#ifdef WIN32
            strcpy(szDate, ctime(&iTime));
#else
            ctime_r(&iTime, szDate);
#endif

            // component name
            if(iComponent == CMP_CORE)
                szComponent = "Core";
            else if(iComponent == CMP_POP3)
                szComponent = "POP3";
            else if(iComponent == CMP_IMAP)
                szComponent = "IMAP";
            else if(iComponent == CMP_HTTP)
                szComponent = "HTTP";
            else if(iComponent == CMP_SMTP)
                szComponent = "SMTP";
            else if(iComponent == CMP_MSGQUEUE)
                szComponent = "MSGQueue";

            // log to file
            fprintf(fpBMSLog, "[%s] - %s - %d - #%d - %s\n",
                    szDate,
                    szComponent,
                    iSeverity,
#ifdef WIN32
                    (int)GetCurrentProcessId(),
#else
                    (int)getpid(),
#endif
                    szEntry);
        }
    }
    free(szEntry);
}

MySQL_Result *MySQL_DB::Query(const char *szQuery, ...)
{
    if(this->bTempClosed || this->handle == NULL)
        this->Connect();
    else if(this->lastQuery < time(NULL)-10)
        mysql_ping(this->handle);

    char szBuff[255], *szArg;
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
                strQuery.append(Escape(szArg));
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
    this->lastQuery = time(NULL);
    int iAttempts = 0;

tryQuery:
    if(mysql_real_query(this->handle, strQuery.c_str(), (unsigned long)strQuery.length()) == 0)
    {
        MYSQL_RES *result = mysql_store_result(this->handle);
        if(result != NULL)
            res = new MySQL_Result(result);
    }
    else
    {
        // handling for timed out connections (mysql server gone away): attempt reconnect
        if(iAttempts == 0 && mysql_errno(this->handle) == 2006)
        {
            this->Connect();
            iAttempts++;
            goto tryQuery;
        }
        else
        {
            throw Core::Exception("MySQL", (char *)mysql_error(this->handle));
        }
    }

    return(res);
}

unsigned long MySQL_DB::InsertId()
{
    if(this->bTempClosed || this->handle == NULL)
        return(0);

    return((unsigned long)mysql_insert_id(this->handle));
}

unsigned long MySQL_DB::AffectedRows()
{
    if(this->bTempClosed || this->handle == NULL)
        return(0);

    return((unsigned long)mysql_affected_rows(this->handle));
}

void MySQL_DB::TempClose()
{
    if(this->bTempClosed)
        return;

    if(this->handle != NULL)
    {
        mysql_close(this->handle);
        this->handle = NULL;
    }

    this->bTempClosed = true;
}

string MySQL_DB::Escape(string in)
{
    char *szSQLValBuff = new char[ in.length()*2+1 ];
    mysql_real_escape_string(this->handle, szSQLValBuff, in.c_str(), (unsigned long)in.length());
    string result = szSQLValBuff;
    delete[] szSQLValBuff;

    return(result);
}
