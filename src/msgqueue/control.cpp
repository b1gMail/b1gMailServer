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

#include <msgqueue/control.h>
#include <msgqueue/apnsdispatcher.h>
#include <msgqueue/msgqueue.h>

MSGQueue_Control *cMSGQueue_Control_Instance = NULL;
MSGQueue_Control_Connection *cMSGQueue_Control_Connection_Instance = NULL;

MSGQueue_Control_Connection::MSGQueue_Control_Connection(Socket *sock, const char *secret, MySQL_DB *db, APNSDispatcher *apnsDispatcher)
{
    this->sock              = sock;
    this->secret            = secret;
    this->db                = db;
    this->authenticated     = false;
    this->quit              = false;
    this->apnsDispatcher    = apnsDispatcher;

    cMSGQueue_Control_Connection_Instance = this;
}

MSGQueue_Control_Connection::~MSGQueue_Control_Connection()
{
    if(cMSGQueue_Control_Connection_Instance == this)
        cMSGQueue_Control_Connection_Instance = NULL;

    delete this->sock;
}

void MSGQueue_Control_Connection::Process()
{
    while(!this->quit)
    {
        char lineBuffer[512];

        try
        {
            if(this->sock->ReadLine(lineBuffer, sizeof(lineBuffer)) == NULL)
                break;
        }
        catch(...)
        {
            break;
        }

        if(!this->ProcessRequestLine(lineBuffer))
            break;
    }
}

bool MSGQueue_Control_Connection::ProcessRequestLine(const char *lineBuffer)
{
    bool result = true;

    char command[32];

    if(sscanf(lineBuffer, "%32s", command) != 1)
    {
        this->sock->Write("-ERR Syntax error\r\n");
        return(true);
    }

    if(strcasecmp(command, "ping") == 0)
    {
        this->sock->Write("+OK\r\n");
    }

    else if(strcasecmp(command, "quit") == 0)
    {
        this->sock->Write("+OK\r\n");
        result = false;
    }

    else if(strcasecmp(command, "get_threadcount") == 0)
    {
        this->sock->PrintF("+OK %d\r\n",
            cMSGQueue_Instance->threadPool->createdThreads);
    }

    else if(strcasecmp(command, "apns_notify") == 0
         && this->authenticated)
    {
        if(this->apnsDispatcher != NULL)
        {
            int subscriptionID = 0;

            if(sscanf(lineBuffer, "%*s %d", &subscriptionID))
            {
                char **row;
                MySQL_Result *res = this->db->Query("SELECT `account_id`,`device_token` FROM bm60_bms_apns_subscription WHERE `subscriptionid`=%d",
                    subscriptionID);
                while((row = res->FetchRow()))
                {
                    APNSMessage msg;
                    msg.deviceToken = row[1];
                    msg.payload = "{\"aps\":{\"account-id\":\"" + string(row[0]) + "\"}}";
                    this->apnsDispatcher->enqueue(msg);
                }
                delete res;

                this->sock->Write("+OK\r\n");
            }
            else
            {
                this->sock->Write("-ERR Usage: APNS_NOTIFY [subscription-id]\r\n");
            }
        }
        else
        {
            this->sock->Write("-ERR APNS not enabled\r\n");
        }
    }

    else if(strcasecmp(command, "delete_queueitem") == 0
         && this->authenticated)
    {
        int id = 0;

        if(sscanf(lineBuffer, "%*s %d", &id) == 1)
        {
            MySQL_Result *res;
            char **row;
            bool active = true;
            MSGQueue *mq = new MSGQueue();

            // lock tables
            this->db->Query("LOCK TABLES bm60_bms_queue WRITE");

            // remove message files
            res = this->db->Query("SELECT `active` FROM bm60_bms_queue WHERE `id`=%d",
                         id);
            while((row = res->FetchRow()))
            {
                active = strcmp(row[0], "1") == 0;
            }
            delete res;

            if(active)
            {
                this->sock->Write("-ERR Queue item not found or currently in use\r\n");
            }
            else
            {
                this->db->Query("DELETE FROM bm60_bms_queue WHERE `id`=%d", id);
                unlink(mq->QueueFileName(id));

                this->sock->Write("+OK\r\n");
            }

            // unlock tables
            this->db->Query("UNLOCK TABLES");

            delete mq;
        }
        else
        {
            this->sock->Write("-ERR Usage: DELETE_QUEUEITEM [id]\r\n");
        }
    }

    else if(strcasecmp(command, "restart_msgqueue") == 0
         && this->authenticated)
    {
        this->sock->Write("+OK\r\n");
        cMSGQueue_Control_Instance->bQuit = true;
        result = false;
    }

    else if(strcasecmp(command, "get_queueitem") == 0
         && this->authenticated)
    {
        int id;

        if(sscanf(lineBuffer, "%*s %d", &id) == 1)
        {
            MSGQueue *mq = new MSGQueue();
            string fileName = mq->QueueFileName(id);

            if(utils->FileExists(fileName.c_str()))
            {
                FILE *messageFP = fopen(fileName.c_str(), "rb");
                if(messageFP != NULL)
                {
                    this->sock->Write("+OK\r\n");

                    char szBuffer[4096];
                    while(!feof(messageFP))
                    {
                        memset(szBuffer, 0, 4096);
                        fgets(szBuffer, 4095, messageFP);

                        if(szBuffer[0] == '.')
                        {
                            const char *szDot = ".";
                            this->sock->Write(szDot, 1);
                        }

                        this->sock->Write(szBuffer, strlen(szBuffer));
                    }
                    fclose(messageFP);

                    this->sock->Write("\r\n.\r\n");
                }
                else
                {
                    this->sock->Write("-ERR Internal error\r\n");
                }
            }
            else
            {
                this->sock->Write("-ERR Item not found\r\n");
            }

            delete mq;
        }
        else
        {
            this->sock->Write("-ERR Usage: GET_QUEUEITEM [id]\r\n");
        }
    }

    else if(strcasecmp(command, "flush_msgqueue") == 0
         && this->authenticated)
    {
        const char *QueueStateFile = utils->GetQueueStateFilePath();

        // flush
        this->db->Query("UPDATE bm60_bms_queue SET `last_attempt`=0 WHERE `deleted`=0");

        // touch state file
        utils->Touch(QueueStateFile);

        // ok
        this->sock->Write("+OK\r\n");
    }

    else if(strcasecmp(command, "clear_msgqueue") == 0
         && this->authenticated)
    {
        MSGQueue *mq = new MSGQueue();
        MySQL_Result *res;
        char **row;

        // lock tables
        this->db->Query("LOCK TABLES bm60_bms_queue WRITE");

        // remove message files
        res = this->db->Query("SELECT `id` FROM bm60_bms_queue WHERE `active`=0");
        while((row = res->FetchRow()))
        {
            unlink(mq->QueueFileName(atoi(row[0])));
        }
        delete res;

        // delete messages from DB
        this->db->Query("DELETE FROM bm60_bms_queue WHERE `active`=0");

        // unlock tables
        this->db->Query("UNLOCK TABLES");

        // clean up
        delete mq;

        // ok
        this->sock->Write("+OK\r\n");
    }

    else if(strcasecmp(command, "get_tlsarecord") == 0
         && this->authenticated)
    {
        string hash = utils->CertHash();
        if(hash.empty())
        {
            this->sock->Write("-ERR\r\n");
        }
        else
        {
            this->sock->PrintF("+OK %d %d %d %s\r\n", 3, 1, 1, utils->CertHash().c_str());
        }
    }

    else if(strcasecmp(command, "auth") == 0)
    {
        char suppliedSecret[33];

        if(sscanf(lineBuffer, "%*s %32s", suppliedSecret) == 1)
        {
            if(strcmp(suppliedSecret, this->secret) == 0)
            {
                this->authenticated = true;
                this->sock->Write("+OK\r\n");
            }
            else
            {
                this->authenticated = false;
                this->sock->Write("-ERR\r\n");
            }
        }
        else
        {
            this->sock->Write("-ERR Usage: AUTH [secret]\r\n");
        }
    }

    else
    {
        this->sock->Write("-ERR Unknown command\r\n");
    }

    return(result);
}

MSGQueue_Control::MSGQueue_Control(APNSDispatcher *apnsDispatcher)
{
    if(cMSGQueue_Control_Instance == NULL)
        cMSGQueue_Control_Instance = this;

    this->db = new Core::MySQL_DB(cfg->Get("mysql_host"),
            cfg->Get("mysql_user"),
            cfg->Get("mysql_pass"),
            cfg->Get("mysql_db"),
            cfg->Get("mysql_sock"),
            true);
    this->db->Query("SET NAMES latin1");

    this->bQuit             = false;
    this->backlog           = 5;
    this->port              = MSGQUEUE_CONTROL_PORT_MIN;

    this->addr              = inet_addr("127.0.0.1");
    const char *szInterface = cfg->Get("control_addr");
    if(szInterface != NULL && strcmp(szInterface, "") != 0)
    {
        in_addr_t iInterfaceAddr = inet_addr(szInterface);
        if(iInterfaceAddr != INADDR_NONE)
            this->addr = iInterfaceAddr;
    }

    this->apnsDispatcher    = apnsDispatcher;
}

MSGQueue_Control::~MSGQueue_Control()
{
    delete this->db;

    if(cMSGQueue_Control_Instance == this)
        cMSGQueue_Control_Instance = NULL;
}

bool MSGQueue_Control::CheckPortStatus(const int port)
{
    if(port <= 0)
        return(false);

    bool result = false;

    int sock = socket(AF_INET, SOCK_STREAM, 0);

#ifdef WIN32
    BOOL optVal = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&optVal, sizeof(optVal));
#else
    int optVal = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(optVal));
#endif

    struct sockaddr_in inAddr;
    memset(&inAddr, 0, sizeof(inAddr));
    inAddr.sin_port = htons(port);
    inAddr.sin_addr.s_addr = this->addr;
    inAddr.sin_family = AF_INET;

    result = bind(sock, (struct sockaddr *)&inAddr, sizeof(inAddr)) >= 0;

#ifndef WIN32
    close(sock);
#else
    closesocket(sock);
#endif

    return(result);
}

void MSGQueue_Control::DeterminePort()
{
    while(!this->CheckPortStatus(this->port))
    {
        this->port++;

        if(this->port > MSGQUEUE_CONTROL_PORT_MAX)
            throw Core::Exception("MSGQueue-Control", "No useable control port found");
    }
}

void MSGQueue_Control::ProcessConnection(int sock, sockaddr_in* clientAddr)
{
    MSGQueue_Control_Connection ctx(new Socket(sock, MSGQUEUE_CONTROL_TIMEOUT),
        this->secret,
        this->db,
        this->apnsDispatcher);
    ctx.Process();
}

void MSGQueue_Control::Run()
{
    this->DeterminePort();

    this->db->Log(CMP_MSGQUEUE, PRIO_DEBUG, utils->PrintF("Opening control channel on port %d", this->port));

    // prepare socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0)
        throw Core::Exception("MSGQueue-Control", "Failed to create control port socket");
#ifndef WIN32
    fcntl(sock, F_SETFD, fcntl(sock, F_GETFD) | FD_CLOEXEC);
#endif

    // prepare server addr
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family       = AF_INET;
    serverAddr.sin_addr.s_addr  = this->addr;
    serverAddr.sin_port         = htons(this->port);

    // set socket options
#ifdef WIN32
    BOOL optVal = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&optVal, sizeof(optVal));
#else
    int optVal = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(optVal));
#endif

    // bind
    if(bind(sock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
        throw Core::Exception("MSGQueue-Control", "Failed to bind control port socket");

    // listen
    if(listen(sock, this->backlog) != 0)
        throw Core::Exception("MSGQueue-Control", "Failed to put control port socket in listen mode");

    // update control port info in DB
    utils->MakeRandomKey(this->secret, 32);
    this->db->Query("UPDATE bm60_bms_prefs SET `control_port`=%d,`control_secret`='%q'",
        this->port,
        this->secret);

    bool bCloseMySQL = atoi(cfg->Get("queue_mysqlclose")) == 1;

    fd_set fdSet;

    while(!this->bQuit)
    {
        struct sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);

        if(bCloseMySQL)
            this->db->TempClose();

        FD_ZERO(&fdSet);
        FD_SET(sock, &fdSet);

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        if(select(sock+1, &fdSet, NULL, NULL, &timeout) ==  -1)
            break;

        int clientSock = 0;

        if(FD_ISSET(sock, &fdSet))
        {
            clientSock = accept(sock, (struct sockaddr *)&clientAddr, &clientAddrLen);
        }
        else
            continue;

        if(clientSock < 0)
            break;

        this->ProcessConnection(clientSock, &clientAddr);

#ifndef WIN32
        close(clientSock);
#else
        closesocket(clientSock);
#endif
    }

#ifndef WIN32
    close(sock);
#else
    closesocket(sock);
#endif
}
