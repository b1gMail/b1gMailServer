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

#include "inetserver.h"
#include <stdarg.h>
#include <time.h>

static char *szServiceShortNames[] = { "Unknown", "POP3", "IMAP", "SMTP", "HTTP", "MSGQueue" },
    *szServiceArgs[] = { "", "--pop3", "--imap", "--smtp", "--http", "--msgqueue" },
    *szServiceNames[] =
    {
        "Unknown",
        "b1gMailServer POP3 service",
        "b1gMailServer IMAP service",
        "b1gMailServer SMTP service",
        "b1gMailServer HTTP service",
        "b1gMailServer message queue"
    };
int iDefaultPorts[] = { 0, 110, 143, 25, 11080, 0 },
    iDefaultSSLPorts[] = { 0, 995, 993, 465, 80443, 0 };

struct bms_config_t
{
    bool tcpNoDelay;
    int maxConnections;
    int listenBacklog;
    int logLevel;
    char logFile[MAX_PATH+1];
};

struct thread_data_t
{
    SOCKET sSocket;
    struct sockaddr_in *sAddr;
    int connectionID;
    bool isSSL;
};

struct bms_service_config_t
{
    char dir[MAX_PATH+1];
    char command[MAX_PATH+1];
    char sslCommand[MAX_PATH+1];
    unsigned long listenAddr;
    int port;
    int sslPort;
};

static SERVICE_STATUS_HANDLE hsvcStatus;
static SERVICE_STATUS svcStatus;
static CRITICAL_SECTION consoleLock, logLock;
static int iConnections = 0, iConnectionIDCounter = 0, iMode = MODE_UNKNOWN;
static bool bStopSVC;
static bms_config_t bmsConfig;
static bms_service_config_t bmsServiceConfig;

/**************************************************************************
 * Server code                                                            *
 **************************************************************************/
/*
 * Thread-safe log writer
 */
void WriteLog(int prio, const char *component, const char *format, ...)
{
    static FILE *logFP = NULL;
    static char szDate[32];
    va_list list;

    if((bmsConfig.logLevel & prio) != prio)
        return;

    EnterCriticalSection(&logLock);
    do
    {
        if(logFP == NULL)
            logFP = fopen(bmsConfig.logFile, "ab");

        if(logFP == NULL)
            break;

        const char *szPrio = NULL;
        switch(prio)
        {
        case PRIO_DEBUG:
            szPrio = "DEBUG";
            break;

        case PRIO_NOTE:
            szPrio = "NOTE";
            break;

        case PRIO_WARNING:
            szPrio = "WARNING";
            break;

        case PRIO_ERROR:
            szPrio = "ERROR";
            break;

        default:
            szPrio = "UNKNOWN";
            break;
        };

        time_t theTime = time(NULL);
        strcpy_s(szDate, sizeof(szDate), ctime(&theTime));
        szDate[strlen(szDate)-1] = 0;

        fprintf(logFP, "%s [%s] %s: ",
            szDate,
            component,
            szPrio);

        va_start(list, format);
        vfprintf(logFP, format, list);
        va_end(list);

        fprintf(logFP, "\r\n");
        fflush(logFP);
    }
    while(false);
    LeaveCriticalSection(&logLock);
}

/*
 * Get working dir
 */
static char *GetDir()
{
    HKEY hKey;
    if(RegOpenKeyEx(HKEY_LOCAL_MACHINE,
        PREFS_REG_KEY,
        0,
        KEY_QUERY_VALUE,
        &hKey) != ERROR_SUCCESS)
        return(NULL);

    char *szCommand = (char *)malloc(MAX_PATH+1);
    if(szCommand == NULL)
    {
        RegCloseKey(hKey);
        return(NULL);
    }

    memset(szCommand, 0, MAX_PATH+1);
    DWORD dwBuffLen = MAX_PATH;

    if(RegQueryValueEx(hKey, "Path", NULL, NULL, (LPBYTE)szCommand, &dwBuffLen) != ERROR_SUCCESS)
    {
        RegCloseKey(hKey);
        free(szCommand);
        return(NULL);
    }

    RegCloseKey(hKey);

    return(szCommand);
}

/*
 * Get path to inet app
 */
static char *GetCommand()
{
    char *szCommand = (char *)malloc(MAX_PATH*2+1),
        *szDir = GetDir();

    if(szDir != NULL && szCommand != NULL)
    {
        sprintf_s(szCommand, MAX_PATH*2+1, "\"%s\\b1gmailserver.exe\" %s",
            szDir,
            szServiceArgs[iMode]);

        free(szDir);

        return(szCommand);
    }
    else
    {
        if(szDir != NULL)
            free(szDir);
        if(szCommand != NULL)
            free(szCommand);
    }

    return(NULL);
}

/*
 * Get config values
 */
void GetBMSConfig()
{
    HKEY hKey;
    DWORD dwResult, dwBuffLen = sizeof(dwResult);

    if(RegOpenKeyEx(HKEY_LOCAL_MACHINE,
        PREFS_REG_KEY,
        0,
        KEY_QUERY_VALUE,
        &hKey) == ERROR_SUCCESS)
    {
        dwResult = 0;
        RegQueryValueEx(hKey, "TCPNoDelay", NULL, NULL, (LPBYTE)&dwResult, &dwBuffLen);
        bmsConfig.tcpNoDelay = (dwResult != 0);

        dwResult = MAX_CONNECTIONS;
        RegQueryValueEx(hKey, "MaxConnections", NULL, NULL, (LPBYTE)&dwResult, &dwBuffLen);
        bmsConfig.maxConnections = dwResult;

        dwResult = LISTEN_BACKLOG;
        RegQueryValueEx(hKey, "ListenBacklog", NULL, NULL, (LPBYTE)&dwResult, &dwBuffLen);
        bmsConfig.listenBacklog = dwResult;

        dwResult = 0;
        RegQueryValueEx(hKey, "LogLevel", NULL, NULL, (LPBYTE)&dwResult, &dwBuffLen);
        bmsConfig.logLevel = dwResult;

        memset(bmsConfig.logFile, 0, sizeof(bmsConfig.logFile));
        dwBuffLen = sizeof(bmsConfig.logFile)-1;
        if(RegQueryValueEx(hKey, "LogFile", NULL, NULL, (LPBYTE)bmsConfig.logFile, &dwBuffLen) != ERROR_SUCCESS)
            strcpy_s(bmsConfig.logFile, sizeof(bmsConfig.logFile), "");

        RegCloseKey(hKey);
    }
    else
    {
        bmsConfig.tcpNoDelay = false;
        bmsConfig.maxConnections = MAX_CONNECTIONS;
        bmsConfig.listenBacklog = LISTEN_BACKLOG;
        bmsConfig.logLevel = 0;
        bmsConfig.logFile[0] = '\0';

        WriteLog(PRIO_WARNING,
            "Core",
            "Could not open BMSService config registry key: %s",
            PREFS_REG_KEY);
    }

    WriteLog(PRIO_DEBUG,
        "Core",
        "Configuration: TCPNoDelay=%d, MaxConnections=%d, ListenBacklog=%d, LogLevel=%d, LogFile=%s",
        bmsConfig.tcpNoDelay ? 1 : 0,
        bmsConfig.maxConnections,
        bmsConfig.listenBacklog,
        bmsConfig.logLevel,
        bmsConfig.logFile);
}

/*
 * Get service port
 */
int GetPort(bool ssl = false)
{
    HKEY hKey;
    DWORD dwResult = ssl ? (DWORD)iDefaultSSLPorts[iMode] : (DWORD)iDefaultPorts[iMode],
        dwBuffLen = sizeof(dwResult);
    const char *regKey = ssl ? PREFS_REG_KEY "\\SSLPorts" : PREFS_REG_KEY "\\Ports";

    if(RegOpenKeyEx(HKEY_LOCAL_MACHINE,
        regKey,
        0,
        KEY_QUERY_VALUE,
        &hKey) == ERROR_SUCCESS)
    {
        if(ssl)
        {
            char keyName[32];
            sprintf_s(keyName, sizeof(keyName), "Enable%sSSL", szServiceShortNames[iMode]);

            dwResult = 0;
            dwBuffLen = sizeof(dwResult);
            if(RegQueryValueEx(hKey, keyName, NULL, NULL, (LPBYTE)&dwResult, &dwBuffLen) == ERROR_SUCCESS
                && dwResult == 1)
            {
                dwResult = iDefaultSSLPorts[iMode];
                dwBuffLen = sizeof(dwResult);

                RegQueryValueEx(hKey, szServiceShortNames[iMode], NULL, NULL, (LPBYTE)&dwResult, &dwBuffLen);
            }
            else
            {
                dwResult = 0;
            }
        }
        else
        {
            dwResult = iDefaultSSLPorts[iMode];
            dwBuffLen = sizeof(dwResult);

            RegQueryValueEx(hKey, szServiceShortNames[iMode], NULL, NULL, (LPBYTE)&dwResult, &dwBuffLen);
        }
        RegCloseKey(hKey);
    }
    else
    {
        if(!ssl)
        {
            WriteLog(PRIO_WARNING,
                szServiceShortNames[iMode],
                "Could not read port from registry key %s, falling back to default port %d",
                regKey,
                iDefaultSSLPorts[iMode]);
        }
        else
        {
            dwResult = 0;
        }
    }

    return((int)dwResult);
}

/*
 * Get service interface
 */
unsigned long GetInterface()
{
    HKEY hKey;
    if(RegOpenKeyEx(HKEY_LOCAL_MACHINE,
        PREFS_REG_KEY "\\Interfaces",
        0,
        KEY_QUERY_VALUE,
        &hKey) != ERROR_SUCCESS)
        return(NULL);

    char szInterface[32];
    memset(szInterface, 0, sizeof(szInterface));
    DWORD dwBuffLen = sizeof(szInterface)-1;

    if(RegQueryValueEx(hKey, szServiceShortNames[iMode], NULL, NULL, (LPBYTE)szInterface, &dwBuffLen) != ERROR_SUCCESS)
    {
        WriteLog(PRIO_WARNING,
            szServiceShortNames[iMode],
            "Could not read interface from registry key %s, falling back to default interface 0.0.0.0",
            PREFS_REG_KEY "\\Interfaces");
        strcpy_s(szInterface, sizeof(szInterface), "0.0.0.0");
    }

    RegCloseKey(hKey);

    if(strcmp(szInterface, "0.0.0.0") == 0
        || inet_addr(szInterface) == INADDR_NONE)
        return(INADDR_ANY);
    else
        return(inet_addr(szInterface));
}

/*
 * Get service config
 */
void GetServiceConfig()
{
    HKEY hKey;
    DWORD dwResult, dwBuffLen;

    memset(&bmsServiceConfig, 0, sizeof(bmsServiceConfig));

    char *szDir = GetDir(), *szCommand = GetCommand();

    bmsServiceConfig.port       = GetPort();
    bmsServiceConfig.sslPort    = GetPort(true);
    bmsServiceConfig.listenAddr = GetInterface();

    if(szDir != NULL)
    {
        strcpy_s(bmsServiceConfig.dir, sizeof(bmsServiceConfig.dir), szDir);
        free(szDir);
    }

    if(szCommand != NULL)
    {
        strcpy_s(bmsServiceConfig.command, sizeof(bmsServiceConfig.command), szCommand);
        sprintf_s(bmsServiceConfig.sslCommand, sizeof(bmsServiceConfig.sslCommand),
            "%s --ssl", szCommand);
        free(szCommand);
    }
}

/*
 * Client thread
 */
unsigned int __stdcall serverThread(void *threadData)
{
    thread_data_t *tData = (thread_data_t *)threadData;
    STARTUPINFOA sInfo;
    PROCESS_INFORMATION pInfo;
    LPVOID env;
    DWORD dwVal;
    char szPeerAddress[32];

    strcpy_s(szPeerAddress, sizeof(szPeerAddress), inet_ntoa(tData->sAddr->sin_addr));

    WriteLog(PRIO_DEBUG,
        szServiceShortNames[iMode],
        "[%d] New connection from %s (connection count: %d, ssl: %d)",
        tData->connectionID,
        szPeerAddress,
        iConnections,
        tData->isSSL ? 1 : 0);

    // disable nagle algorithm
    if(bmsConfig.tcpNoDelay)
    {
        dwVal = 1;
        setsockopt(tData->sSocket, IPPROTO_TCP, TCP_NODELAY, (const char *)&dwVal, sizeof(dwVal));

        WriteLog(PRIO_DEBUG,
            szServiceShortNames[iMode],
            "[%d] Nagle algorithm disabled",
            tData->connectionID);
    }

    // fill startup info
    memset(&sInfo, 0, sizeof(sInfo));
    sInfo.cb = sizeof(sInfo);
    sInfo.lpDesktop = "";
    sInfo.dwFlags = STARTF_USESTDHANDLES;

    // create handles
    if(!DuplicateHandle(GetCurrentProcess(),
        (HANDLE)tData->sSocket,
        GetCurrentProcess(),
        &sInfo.hStdInput,
        0,
        true,
        DUPLICATE_SAME_ACCESS))
    {
        WriteLog(PRIO_ERROR,
            szServiceShortNames[iMode],
            "[%d] DuplicateHandle failed (stdin), dropping connection",
            tData->connectionID);

        closesocket(tData->sSocket);
        free(tData);
        --iConnections;
        return(1);
    }
    if(!DuplicateHandle(GetCurrentProcess(),
        (HANDLE)tData->sSocket,
        GetCurrentProcess(),
        &sInfo.hStdOutput,
        0,
        true,
        DUPLICATE_SAME_ACCESS))
    {

        WriteLog(PRIO_ERROR,
            szServiceShortNames[iMode],
            "[%d] DuplicateHandle failed (stdout), dropping connection",
            tData->connectionID);

        closesocket(tData->sSocket);
        free(tData);
        --iConnections;
        return(1);
    }
    if(!DuplicateHandle(GetCurrentProcess(),
        (HANDLE)tData->sSocket,
        GetCurrentProcess(),
        &sInfo.hStdError,
        0,
        true,
        DUPLICATE_SAME_ACCESS))
    {
        WriteLog(PRIO_ERROR,
            szServiceShortNames[iMode],
            "[%d] DuplicateHandle failed (stderr), dropping connection",
            tData->connectionID);

        closesocket(tData->sSocket);
        free(tData);
        --iConnections;
        return(1);
    }

    // create environment
    char szEnvironment[255];
    sprintf_s(szEnvironment, sizeof(szEnvironment), "OS=%s%cPEER_IP=%s%cSystemRoot=%s%c%c",
        getenv("OS"),
        0,
        inet_ntoa(tData->sAddr->sin_addr),
        0,
        getenv("SystemRoot"),
        0,
        0);
    env = (LPVOID)szEnvironment;

    char *szCommand = tData->isSSL ? bmsServiceConfig.sslCommand : bmsServiceConfig.command;

    // create process
    WriteLog(PRIO_DEBUG,
        szServiceShortNames[iMode],
        "[%d] Creating process \"%s\" with working directory \"%s\"",
        tData->connectionID,
        szCommand,
        bmsServiceConfig.dir);

    if(!CreateProcess(NULL,
        szCommand,
        NULL,
        NULL,
        true,
        CREATE_NEW_PROCESS_GROUP | CREATE_NEW_CONSOLE,
        env,
        bmsServiceConfig.dir,
        &sInfo,
        &pInfo))
    {
        WriteLog(PRIO_ERROR,
            szServiceShortNames[iMode],
            "[%d] Failed to create process \"%s\" with working directory \"%s\", dropping connection",
            tData->connectionID,
            szCommand,
            bmsServiceConfig.dir);

        CloseHandle(sInfo.hStdInput);
        CloseHandle(sInfo.hStdOutput);
        CloseHandle(sInfo.hStdError);
        closesocket(tData->sSocket);
        free(tData);
        --iConnections;
        return(1);
    }

    // wait
    WriteLog(PRIO_DEBUG,
        szServiceShortNames[iMode],
        "[%d] Created process %d, waiting",
        tData->connectionID,
        (int)pInfo.dwProcessId);
    while(!bStopSVC)
    {
        if(WaitForSingleObject(pInfo.hProcess, 500) != WAIT_TIMEOUT)
        {
            WriteLog(PRIO_DEBUG,
                szServiceShortNames[iMode],
                "[%d] WaitForSingleObject finished (result != WAIT_TIMEOUT)",
                tData->connectionID);
            break;
        }
    }

    // stop?
    if(bStopSVC)
    {
        WriteLog(PRIO_DEBUG,
            szServiceShortNames[iMode],
            "[%d] Sending CTRL_C_EVENT to process",
            tData->connectionID);

        bool doTerminate = false;

        EnterCriticalSection(&consoleLock);
        {
            FreeConsole();
            AttachConsole(pInfo.dwProcessId);
            SetConsoleCtrlHandler(NULL, true);
            doTerminate = !GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
        }
        LeaveCriticalSection(&consoleLock);

        WriteLog(PRIO_DEBUG,
            szServiceShortNames[iMode],
            "[%d] CTRL_C_EVENT sent, doTerminate=%d",
            tData->connectionID,
            doTerminate ? 1 : 0);

        if(doTerminate || WaitForSingleObject(pInfo.hProcess, 10000) == WAIT_TIMEOUT)
        {
            WriteLog(PRIO_DEBUG,
                szServiceShortNames[iMode],
                "[%d] Process did not finish in time, terminating process",
                tData->connectionID);

            TerminateProcess(pInfo.hProcess, 3);
        }
    }

    WriteLog(PRIO_DEBUG,
        szServiceShortNames[iMode],
        "[%d] Closing connection",
        tData->connectionID);

    // clean up
    CloseHandle(sInfo.hStdInput);
    CloseHandle(sInfo.hStdOutput);
    CloseHandle(sInfo.hStdError);
    CloseHandle(pInfo.hThread);
    CloseHandle(pInfo.hProcess);

    closesocket(tData->sSocket);
    free(tData);

    --iConnections;
    return(0);
}

/*
 * Handle client
 */
void serverHandle(SOCKET sSocket, struct sockaddr_in *sAddr, bool ssl = false)
{
    unsigned int iThread;
    HANDLE hThread;
    thread_data_t *tData;

    if((tData = (thread_data_t *)malloc(sizeof(thread_data_t))) == NULL)
        return;

    tData->sSocket = sSocket;
    tData->sAddr = sAddr;
    tData->connectionID = ++iConnectionIDCounter;
    tData->isSSL = ssl;

    ++iConnections;

    if(!(hThread = (HANDLE)_beginthreadex(NULL, 0, serverThread, tData, 0, &iThread)))
    {
        WriteLog(PRIO_ERROR,
            szServiceShortNames[iMode],
            "[%d] Failed to create thread for new connection (connection count: %d)",
            tData->connectionID,
            iConnections);

        --iConnections;

        free(tData);
        return;
    }
    CloseHandle(hThread);

    return;
}

/*
 * Server main loop (msgqueue)
 */
int serverMainMSGQueue(int argc, char **argv)
{
    bStopSVC = false;

    WriteLog(PRIO_NOTE,
        szServiceShortNames[iMode],
        "Starting up service");

    while(!bStopSVC)
    {
        STARTUPINFOA sInfo;
        PROCESS_INFORMATION pInfo;

        // fill startup info
        memset(&sInfo, 0, sizeof(sInfo));
        sInfo.cb = sizeof(sInfo);
        sInfo.lpDesktop = "";
        sInfo.dwFlags = STARTF_USESTDHANDLES;

        // create process
        char *szCommand = GetCommand(),
            *szDir = GetDir();

        WriteLog(PRIO_DEBUG,
            szServiceShortNames[iMode],
            "Creating MSGQueue process \"%s\" with working directory \"%s\"",
            szCommand,
            szDir);

        if(szCommand == NULL || szDir == NULL || !CreateProcess(NULL,
            szCommand,
            NULL,
            NULL,
            false,
            CREATE_NEW_PROCESS_GROUP | CREATE_NEW_CONSOLE,
            NULL,
            szDir,
            &sInfo,
            &pInfo))
        {
            WriteLog(PRIO_ERROR,
                szServiceShortNames[iMode],
                "Failed to create MSGQueue process \"%s\" with working directory \"%s\"",
                szCommand,
                szDir);

            if(szCommand != NULL)
                free(szCommand);
            if(szDir != NULL)
                free(szDir);
            return(1);
        }

        // main loop
        WriteLog(PRIO_DEBUG,
            szServiceShortNames[iMode],
            "Created MSGQueue process %d, waiting",
            (int)pInfo.dwProcessId);
        while(!bStopSVC)
        {
            if(WaitForSingleObject(pInfo.hProcess, 500) != WAIT_TIMEOUT)
            {
                WriteLog(PRIO_DEBUG,
                    szServiceShortNames[iMode],
                    "WaitForSingleObject finished (result != WAIT_TIMEOUT)");
                break;
            }
        }

        // stop?
        if(bStopSVC)
        {
            WriteLog(PRIO_DEBUG,
                szServiceShortNames[iMode],
                "Sending CTRL_C_EVENT to MSGQueue process");

            FreeConsole();
            AttachConsole(pInfo.dwProcessId);
            SetConsoleCtrlHandler(NULL, true);
            if(!GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0) || WaitForSingleObject(pInfo.hProcess, 30000) == WAIT_TIMEOUT)
            {
                WriteLog(PRIO_DEBUG,
                    szServiceShortNames[iMode],
                    "Process did not finish in time, terminating process");

                TerminateProcess(pInfo.hProcess, 3);
            }
        }

        // clean up
        CloseHandle(pInfo.hThread);
        CloseHandle(pInfo.hProcess);
        free(szCommand);
        free(szDir);

        if(!bStopSVC)
        {
            WriteLog(PRIO_NOTE,
                szServiceShortNames[iMode],
                "Restarting MSGQueue process after termination (after 1 s delay)");
            Sleep(1000);
        }
    }

    WriteLog(PRIO_NOTE,
        szServiceShortNames[iMode],
        "Shutting down service");

    return(0);
}

/*
 * Server main loop (inet)
 */
int serverMain(int argc, char **argv)
{
    bStopSVC = false;

    struct linger lLing;
    SOCKET sListen, sListenSSL = NULL;
    SOCKADDR_IN sListenAddr;

    WriteLog(PRIO_NOTE,
        szServiceShortNames[iMode],
        "Starting up server");

    GetServiceConfig();

    WSADATA wsaVersion;
    if(WSAStartup(MAKEWORD(2, 0), &wsaVersion))
    {
        WriteLog(PRIO_ERROR,
            szServiceShortNames[iMode],
            "WSAStartup failed");
        return(1);
    }

    // init mutexes
    InitializeCriticalSection(&consoleLock);

    // create listener
    if((sListen = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED)) == INVALID_SOCKET)
    {
        WriteLog(PRIO_ERROR,
            szServiceShortNames[iMode],
            "WSASocket failed");
        return(1);
    }
    if(bmsServiceConfig.sslPort)
    {
        if((sListenSSL = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED)) == INVALID_SOCKET)
        {
            WriteLog(PRIO_ERROR,
                szServiceShortNames[iMode],
                "WSASocket failed (SSL)");
            return(1);
        }
    }

    // linger
    lLing.l_linger = 60;
    lLing.l_onoff = 1;
    if(setsockopt(sListen, SOL_SOCKET, SO_LINGER, (char *)&lLing, sizeof(lLing)))
    {
        WriteLog(PRIO_ERROR,
            szServiceShortNames[iMode],
            "setsockopt failed (SO_LINGER)");
        return(1);
    }
    if(bmsServiceConfig.sslPort)
    {
        lLing.l_linger = 60;
        lLing.l_onoff = 1;
        if(setsockopt(sListenSSL, SOL_SOCKET, SO_LINGER, (char *)&lLing, sizeof(lLing)))
        {
            WriteLog(PRIO_ERROR,
                szServiceShortNames[iMode],
                "setsockopt failed (SO_LINGER, SSL)");
            return(1);
        }
    }

    // bind + listen
    memset(&sListenAddr, 0, sizeof(SOCKADDR_IN));
    sListenAddr.sin_addr.s_addr = bmsServiceConfig.listenAddr;
    sListenAddr.sin_port = htons(bmsServiceConfig.port);
    sListenAddr.sin_family = AF_INET;
    if(bind(sListen, (const struct sockaddr *)&sListenAddr, sizeof(SOCKADDR_IN)))
    {
        WriteLog(PRIO_ERROR,
            szServiceShortNames[iMode],
            "Failed to bind socket to port %d",
            bmsServiceConfig.port);
        return(1);
    }
    if(listen(sListen, bmsConfig.listenBacklog))
    {
        WriteLog(PRIO_ERROR,
            szServiceShortNames[iMode],
            "Failed to listen to port %d",
            bmsServiceConfig.port);
        return(1);
    }
    WriteLog(PRIO_DEBUG,
        szServiceShortNames[iMode],
        "Bound to port %d and listening",
        bmsServiceConfig.port);

    if(bmsServiceConfig.sslPort)
    {
        memset(&sListenAddr, 0, sizeof(SOCKADDR_IN));
        sListenAddr.sin_addr.s_addr = bmsServiceConfig.listenAddr;
        sListenAddr.sin_port = htons(bmsServiceConfig.sslPort);
        sListenAddr.sin_family = AF_INET;
        if(bind(sListenSSL, (const struct sockaddr *)&sListenAddr, sizeof(SOCKADDR_IN)))
        {
            WriteLog(PRIO_ERROR,
                szServiceShortNames[iMode],
                "Failed to bind socket to SSL port %d",
                bmsServiceConfig.sslPort);
            return(1);
        }
        if(listen(sListenSSL, bmsConfig.listenBacklog))
        {
            WriteLog(PRIO_ERROR,
                szServiceShortNames[iMode],
                "Failed to listen to SSL port %d",
                bmsServiceConfig.sslPort);
            return(1);
        }
        WriteLog(PRIO_DEBUG,
            szServiceShortNames[iMode],
            "Bound to SSL port %d and listening",
            bmsServiceConfig.sslPort);
    }

    // main loop
    struct timeval tVal;
    int iSelRes, iAdrLen;
    struct sockaddr_in sAddr;
    FD_SET fdSet;
    SOCKET sSock;
    while(!bStopSVC)
    {
        FD_ZERO(&fdSet);
        FD_SET(sListen, &fdSet);

        if(sListenSSL != NULL)
            FD_SET(sListenSSL, &fdSet);

        tVal.tv_sec = 1;
        tVal.tv_usec = 0;

        if(!(iSelRes = select(0, &fdSet, NULL, NULL, &tVal)) || iSelRes < 0)
            continue;

        if(bStopSVC)
            break;

        if(FD_ISSET(sListen, &fdSet))
        {
            iAdrLen = sizeof(sAddr);

            if((sSock = accept(sListen, (struct sockaddr *)&sAddr, &iAdrLen)) != INVALID_SOCKET)
            {
                if(iConnections >= bmsConfig.maxConnections)
                {
                    WriteLog(PRIO_WARNING,
                        szServiceShortNames[iMode],
                        "Connection limit of %d connections exceeded, dropping connection (connection count: %d)",
                        bmsConfig.maxConnections,
                        iConnections);
                    closesocket(sSock);
                }
                else
                    serverHandle(sSock, &sAddr);
            }
        }

        if(FD_ISSET(sListenSSL, &fdSet))
        {
            iAdrLen = sizeof(sAddr);

            if((sSock = accept(sListenSSL, (struct sockaddr *)&sAddr, &iAdrLen)) != INVALID_SOCKET)
            {
                if(iConnections >= bmsConfig.maxConnections)
                {
                    WriteLog(PRIO_WARNING,
                        szServiceShortNames[iMode],
                        "Connection limit of %d connections exceeded, dropping SSL connection (connection count: %d)",
                        bmsConfig.maxConnections,
                        iConnections);
                    closesocket(sSock);
                }
                else
                    serverHandle(sSock, &sAddr, true);
            }
        }
    }

    WriteLog(PRIO_NOTE,
        szServiceShortNames[iMode],
        "Shutting down server");

    closesocket(sListen);
    if(sListenSSL != NULL)
        closesocket(sListenSSL);

    while(iConnections > 0)
        Sleep(100);

    WriteLog(PRIO_NOTE,
        szServiceShortNames[iMode],
        "Shutdown completed");

    // destroy mutexes
    DeleteCriticalSection(&consoleLock);

    WSACleanup();
    return(0);
}

/**************************************************************************
 * Service code                                                           *
 **************************************************************************/

/*
 * Report status to SCM
 */
static int scmReport(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint)
{
    static DWORD dwCheckPoint = 1;

    if(dwCurrentState == SERVICE_START_PENDING)
        svcStatus.dwControlsAccepted = 0;
    else
        svcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;

    svcStatus.dwWin32ExitCode = dwWin32ExitCode;
    svcStatus.dwWaitHint = dwWaitHint;
    svcStatus.dwCurrentState = dwCurrentState;

    if(dwCurrentState == SERVICE_STOPPED
        || dwCurrentState == SERVICE_RUNNING)
        svcStatus.dwCheckPoint = 0;
    else
        svcStatus.dwCheckPoint = dwCheckPoint++;

    return(SetServiceStatus(hsvcStatus, &svcStatus));
}

/*
 * Stop service
 */
void serviceStop()
{
    bStopSVC = true;
}

/*
 * Service control
 */
static void WINAPI serviceControl(DWORD dwControl)
{
    if(dwControl == SERVICE_CONTROL_STOP)
    {
        scmReport(SERVICE_STOP_PENDING, NO_ERROR, 4000);
        serviceStop();
    }
    else
    {
        scmReport(svcStatus.dwCurrentState, NO_ERROR, 0);
    }
}

/*
 * Service entry point
 */
static void WINAPI serviceMain(DWORD argc, LPTSTR *argv)
{
    if((hsvcStatus = RegisterServiceCtrlHandler(szServiceNames[iMode], serviceControl)) != 0)
    {
        svcStatus.dwServiceSpecificExitCode = 0;
        svcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
        if(scmReport(SERVICE_RUNNING, NO_ERROR, 4000))
        {
            if(iMode == MODE_MSGQUEUE)
                serverMainMSGQueue(argc, argv);
            else
                serverMain(argc, argv);
        }
        scmReport(SERVICE_STOPPED, NO_ERROR, 0);
    }
}

/*
 * Uninstall service
 */
int serviceUninstall(bool bStopOnly = false)
{
    HWND hDialog;
    SC_HANDLE scSCM, scSVC;
    int iResult = 1;

    if((scSCM = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS)) != NULL)
    {
        if((scSVC = OpenService(scSCM, szServiceNames[iMode], SERVICE_ALL_ACCESS)) != NULL)
        {
            if(ControlService(scSVC, SERVICE_CONTROL_STOP, &svcStatus))
            {
                hDialog = CreateDialog(GetModuleHandle(NULL),
                    MAKEINTRESOURCE(IDD_PROGRESS),
                    NULL,
                    NULL);
                SetWindowText(hDialog, szServiceNames[iMode]);
                SetDlgItemText(hDialog, IDC_TEXT, "Dienst stoppen...");
                ShowWindow(hDialog, SW_SHOW);
                UpdateWindow(hDialog);

                while(QueryServiceStatus(scSVC, &svcStatus))
                {
                    if(svcStatus.dwCurrentState == SERVICE_STOP_PENDING)
                    {
                        UpdateWindow(hDialog);
                        Sleep(200);
                    }
                    else
                        break;
                }

                DestroyWindow(hDialog);

                if(svcStatus.dwCurrentState != SERVICE_STOPPED)
                {
                    MessageBox(GetFocus(),
                        "Error: Failed to stop service",
                        szServiceNames[iMode],
                        MB_OK|MB_ICONERROR);
                }
            }

            if(!bStopOnly)
            {
                if(DeleteService(scSVC))
                {
                    iResult = 0;

                    WriteLog(PRIO_NOTE,
                        szServiceShortNames[iMode],
                        "Service uninstalled");
                }
                else
                {
                    MessageBox(GetFocus(),
                        "Error: Cannot uninstall service",
                        szServiceNames[iMode],
                        MB_OK|MB_ICONERROR);
                }
            }
            else
            {
                iResult = 0;

                WriteLog(PRIO_NOTE,
                    szServiceShortNames[iMode],
                    "Service stopped");
            }

            CloseServiceHandle(scSVC);
        }
        else
        {
            MessageBox(GetFocus(),
                "Error: Cannot open service",
                szServiceNames[iMode],
                MB_OK|MB_ICONERROR);
        }
        CloseServiceHandle(scSCM);
    }
    else
    {
        MessageBox(GetFocus(),
            "Error: Cannot open service manager",
            szServiceNames[iMode],
            MB_OK|MB_ICONERROR);
    }

    if(iResult != 0)
    {
        WriteLog(PRIO_WARNING,
            szServiceShortNames[iMode],
            "Failed to %s service",
            bStopOnly ? "stop" : "uninstall");
    }

    return(iResult);
}

/*
 * Install service
 */
int serviceInstall()
{
    SC_HANDLE scSCM, scSVC;
    char szPath[MAX_PATH+1] = { 0 }, szBinaryPathName[MAX_PATH+64+1] = { 0 };
    int iResult = 1;

    if(GetModuleFileName(GetModuleHandle(NULL), szPath, MAX_PATH) == 0)
    {
        MessageBox(GetFocus(),
            "Error: Cannot find module path",
            szServiceNames[iMode],
            MB_OK|MB_ICONERROR);
        return(1);
    }
    else
    {
        sprintf_s(szBinaryPathName, sizeof(szBinaryPathName),
            "\"%s\" %s",
            szPath,
            szServiceArgs[iMode]);
    }

    if((scSCM = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS)) != NULL)
    {
        if(scSVC = CreateService(scSCM,
            szServiceNames[iMode],
            szServiceNames[iMode],
            SERVICE_ALL_ACCESS,
            SERVICE_WIN32_OWN_PROCESS,
            iMode == MODE_HTTP ? SERVICE_DEMAND_START : SERVICE_AUTO_START,
            SERVICE_ERROR_NORMAL,
            szBinaryPathName,
            NULL,
            NULL,
            "",
            NULL,
            NULL))
        {
            WriteLog(PRIO_NOTE,
                szServiceShortNames[iMode],
                "Service installed");

            iResult = 0;
            CloseServiceHandle(scSVC);
        }
        else
        {
            DWORD dwError = GetLastError();

            if(dwError != ERROR_SERVICE_EXISTS)
            {
                MessageBox(GetFocus(),
                    "Error: Cannot create service",
                    szServiceNames[iMode],
                    MB_OK|MB_ICONERROR);
            }
        }
        CloseServiceHandle(scSCM);
    }
    else
    {
        MessageBox(GetFocus(),
            "Error: Cannot open service manager",
            szServiceNames[iMode],
            MB_OK|MB_ICONERROR);
    }

    if(iResult != 0)
    {
        WriteLog(PRIO_WARNING,
            szServiceShortNames[iMode],
            "Failed to install service");
    }

    return(iResult);
}

/*
 * Main entry point
 */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    InitializeCriticalSection(&logLock);

    GetBMSConfig();

    // scan arguments for mode
    if(strstr(lpCmdLine, "--pop3") != NULL)
        iMode = MODE_POP3;
    else if(strstr(lpCmdLine, "--imap") != NULL)
        iMode = MODE_IMAP;
    else if(strstr(lpCmdLine, "--smtp") != NULL)
        iMode = MODE_SMTP;
    else if(strstr(lpCmdLine, "--http") != NULL)
        iMode = MODE_HTTP;
    else if(strstr(lpCmdLine, "--msgqueue") != NULL)
        iMode = MODE_MSGQUEUE;

    // install/uninstall
    if(iMode != MODE_UNKNOWN)
    {
        if(strstr(lpCmdLine, "--install") != NULL)
            return(serviceInstall());
        else if(strstr(lpCmdLine, "--uninstall") != NULL)
            return(serviceUninstall());
        else if(strstr(lpCmdLine, "--stop") != NULL)
            return(serviceUninstall(true));
    }

    // exit if no mode is given
    if(iMode == MODE_UNKNOWN)
        return(1);

    // run service dispatcher
    SERVICE_TABLE_ENTRY serviceTable[] = {
        { szServiceNames[iMode], (LPSERVICE_MAIN_FUNCTION)serviceMain },
        { NULL, NULL }
    };
    StartServiceCtrlDispatcher(serviceTable);
    return(0);
}
