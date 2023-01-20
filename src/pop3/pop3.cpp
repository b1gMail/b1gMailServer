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

#include <pop3/pop3.h>
#include <core/blobstorage.h>

#define REDIRECT_BUFFER_SIZE    1024
#define POP3_LINEMAX            512
#define VALID_MSG(x)            ((x > 0) && (x <= (int)this->vMessages.size())) && (this->vMessages.at(x-1).bDele == false)

POP3 *cPOP3_Instance = NULL;

/*
 * Signal handler
 */
void POP3_SignalHandler(int iSignal)
{
    if(cPOP3_Instance == NULL)
        return;

    if(iSignal == SIGALRM)
        cPOP3_Instance->bTimeout = true;
    else
        cPOP3_Instance->bQuit = true;
#ifdef WIN32
    InterruptFGets();
#else
    fclose(stdin);
#endif
}
#ifdef WIN32
bool POP3_CtrlHandler(DWORD fdwCtrlType)
{
    if(fdwCtrlType == CTRL_C_EVENT || fdwCtrlType == CTRL_BREAK_EVENT)
    {
        if(cPOP3_Instance != NULL)
        {
            cPOP3_Instance->bQuit = true;
            InterruptFGets();
            return(true);
        }
    }

    return(false);
}
#endif

/*
 * Constructor
 */
POP3::POP3()
{
    this->iCommands = 0;
    this->iFetched = 0;
    this->iPartFetched = 0;
    this->iBadTries = 0;
    this->iDele = 0;
    this->iDeleSize = 0;
    this->iUserID = 0;
    this->strUser = "";
    this->bTimeout = false;
    this->bQuit = false;
    this->bTLSMode = false;
    this->bBanned = false;

    const char *szPeer = utils->GetPeerAddress();
    if(szPeer == NULL)
        this->strPeer = "(unknown)";
    else
        this->strPeer = szPeer;
    this->iState = POP3_STATE_AUTHORIZATION;

    cPOP3_Instance = this;

    db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] Connected",
        this->strPeer.c_str()));
}

/*
 * Destructor
 */
POP3::~POP3()
{
    cPOP3_Instance = NULL;

    // plugins
    if(this->iUserID > 0)
        PLUGIN_FUNCTION(OnLogoutUser);

    db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] Disconnected (deleted: %d / fetched: %d / partialy fetched: %d)",
        this->strPeer.c_str(),
        this->iDele,
        this->iFetched,
        this->iPartFetched));

#ifdef _UNIX_EXP
    if(bTLSMode)
        SSL_free(ssl);
#endif
}

/*
 * Main loop
 */
void POP3::Run()
{
    char szBuffer[POP3_LINEMAX+1];
    int iTimeout = atoi(cfg->Get("pop3_timeout"));

    // set signal handlers
    signal(SIGINT, POP3_SignalHandler);
    signal(SIGTERM, POP3_SignalHandler);
#ifndef WIN32
    signal(SIGALRM, POP3_SignalHandler);
    signal(SIGHUP, POP3_SignalHandler);
    signal(SIGUSR1, POP3_SignalHandler);
#else
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)POP3_CtrlHandler, true);
    SetAlarmSignalCallback(POP3_SignalHandler);
#endif

    // check failban status
    if(utils->Failban_IsBanned(PEER_IP(), FAILBAN_POP3LOGIN))
    {
        this->bBanned = true;
        printf("-ERR IP temporarily banned because of too many failed login attempts\r\n");
        db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] Closing connection (IP banned)",
                                                   this->strPeer.c_str()));
        return;
    }

    // greeting!
    if(iTimeout > 0)
        alarm((unsigned int)iTimeout);
    printf("+OK %s\r\n",
        cfg->Get("pop3greeting"));
    while(!this->bBanned && fgets(szBuffer, POP3_LINEMAX, stdin) != NULL)
    {
        // disable timeout
        alarm(0);

        // process
        if(!this->ProcessLine(szBuffer))
            break;

        // reset timeout
        alarm((unsigned int)iTimeout);
    }

    // disable alarm
    alarm(0);

    // timeout?
    if(this->bTimeout)
    {
        db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] Closing connection due to timeout",
            this->strPeer.c_str()));
        printf("-ERR Timeout (%ds)\r\n",
            iTimeout);
    }

    // quit flag?
    else if(this->bQuit)
    {
        db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] Closing connection due to set quit flag",
                                                   this->strPeer.c_str()));
        printf("-ERR Closing connection\r\n");
    }

    // banned flag?
    else if(this->bBanned)
    {
        db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] Closing connection (IP still banned)",
                                                   this->strPeer.c_str()));
    }
}

/*
 * Redirect to alternative pop3 server
 */
void POP3::Redirect(int iPort, const string &strUser)
{
#ifdef WIN32
    SOCKET sSocket, sIn = (SOCKET)GetStdHandle(STD_INPUT_HANDLE);
#else
    int sSocket, sIn = fileno(stdin);
#endif
    int sMax = 0;

    sSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

#ifdef WIN32
    if(sSocket == INVALID_SOCKET)
        return;
#else
    if(sSocket == -1)
        return;
    fcntl(sSocket, F_SETFD, fcntl(sSocket, F_GETFD) | FD_CLOEXEC);
#endif

    // fill sockaddr struct
    struct sockaddr_in serverInfo;
    serverInfo.sin_family = AF_INET;
    serverInfo.sin_addr.s_addr = inet_addr("127.0.0.1");
    serverInfo.sin_port = htons(iPort);

    if(connect(sSocket, (struct sockaddr *)&serverInfo, sizeof(struct sockaddr)) != 0)
    {
        db->Log(CMP_POP3, PRIO_WARNING, utils->PrintF("[%s] Failed to connect to POP3 server at port %d",
            this->strPeer.c_str(),
            iPort));
        return;
    }

    char szBuffer[REDIRECT_BUFFER_SIZE];
    int iBytes;

    // read greeting
    if(recv(sSocket, szBuffer, REDIRECT_BUFFER_SIZE, 0) == -1)
    {
        db->Log(CMP_POP3, PRIO_WARNING, utils->PrintF("[%s] Failed to read greeting from POP3 server at port %d",
            this->strPeer.c_str(),
            iPort));
        return;
    }

    // send username
    char *szCommand = utils->PrintF("USER %s\r\n", strUser.c_str());
    if(send(sSocket, szCommand, strlen(szCommand), 0) == -1)
    {
        db->Log(CMP_POP3, PRIO_WARNING, utils->PrintF("[%s] Failed to send username to POP3 server at port %d",
            this->strPeer.c_str(),
            iPort));
        return;
    }

    // read answer
    if((iBytes = recv(sSocket, szBuffer, REDIRECT_BUFFER_SIZE, 0)) == -1)
    {
        db->Log(CMP_POP3, PRIO_WARNING, utils->PrintF("[%s] Failed to read answer from POP3 server at port %d",
            this->strPeer.c_str(),
            iPort));
        return;
    }
    else
    {
        fwrite(&szBuffer, iBytes, 1, stdout);
        fflush(stdout);
    }

    fd_set fdSet;
    while(true)
    {
        FD_ZERO(&fdSet);
        FD_SET(sSocket, &fdSet);
        FD_SET(sIn, &fdSet);

#ifndef WIN32
        if(sSocket > sIn)
            sMax = sSocket;
        else
            sMax = sIn;
#endif

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        if(select(sMax+1, &fdSet, NULL, NULL, &timeout) ==  -1)
            break;

        if(FD_ISSET(sSocket, &fdSet))
        {
            if((iBytes = (int)recv(sSocket, szBuffer, REDIRECT_BUFFER_SIZE, 0)) < 1
                || (int)fwrite(szBuffer, iBytes, 1, stdout) == EOF)
                break;
            else
                fflush(stdout);
        }

        if(FD_ISSET(sIn, &fdSet))
        {
            if((iBytes = read(sIn, szBuffer, REDIRECT_BUFFER_SIZE)) < 1
                || send(sSocket, szBuffer, iBytes, 0) < 1)
                    break;
        }
    }

#ifdef WIN32
    closesocket(sSocket);
#else
    close(sSocket);
#endif
}

/*
 * CAPA command
 */
void POP3::Capa()
{
    db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] CAPA",
        this->strPeer.c_str()));

    printf("+OK Capability list follows\r\n");
    printf("TOP\r\n");
    printf("USER\r\n");
    printf("UIDL\r\n");
    if(!bTLSMode && utils->TLSAvailable())
        printf("STLS\r\n");
    printf(".\r\n");
}

/*
 * QUIT command
 */
void POP3::Quit()
{
    db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] QUIT",
        this->strPeer.c_str()));

    if(this->iState == POP3_STATE_TRANSACTION)
    {
        size_t freedSpace = 0;
        for(int i=0; i<(int)this->vMessages.size(); i++)
            if(this->vMessages.at(i).bDele)
            {
                if(this->vMessages.at(i).bFile)
                    utils->DeleteBlob(this->vMessages.at(i).iBlobStorage, BMBLOB_TYPE_MAIL, this->vMessages.at(i).iID, this->iUserID);

                db->Query("DELETE FROM bm60_mails WHERE id='%d' AND userid='%d'",
                    this->vMessages.at(i).iID,
                    this->iUserID);
                unsigned int affectedRows = db->AffectedRows();

                if(affectedRows == 1)
                {
                    freedSpace += this->vMessages.at(i).iSize;

                    db->Query("DELETE FROM bm60_attachments WHERE `mailid`=%d AND `userid`=%d",
                              this->vMessages.at(i).iID,
                              this->iUserID);
                }

                db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] Deleted message %d",
                    this->strPeer.c_str(),
                    this->vMessages.at(i).iID));
                utils->PostEvent(this->iUserID, BMS_EVENT_DELETEMAIL, this->vMessages.at(i).iID);
            }

        if(freedSpace > 0)
        {
            db->Query("UPDATE bm60_users SET mailspace_used=mailspace_used-LEAST(mailspace_used,%d) WHERE id='%d'",
                (int)freedSpace,
                this->iUserID);
            this->IncGeneration(1, 0);
        }
    }

    printf("+OK Bye (deleted: %d / fetched: %d / partialy fetched: %d)\r\n",
        this->iDele,
        this->iFetched,
        this->iPartFetched);
}

/*
 * PASS command
 */
void POP3::Pass(char *szLine)
{
    db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] PASS",
        this->strPeer.c_str()));

    if(strlen(szLine) > 7)
    {
        string strPass = utils->RTrim(szLine + 5, "\r\n");

        if(this->strUser.empty())
        {
            db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] PASS without USER",
                this->strPeer.c_str()));
            printf("-ERR Please send username first\r\n");
        }
        else if(this->iUserID < 0)
        {
            db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] PASS without valid USER",
                this->strPeer.c_str()));

            this->iBadTries++;
            utils->MilliSleep(3*1000);
            printf("-ERR Login failed\r\n");

            if(utils->Failban_LoginFailed(PEER_IP(), FAILBAN_POP3LOGIN))
                this->bBanned = true;
        }
        else
        {
            int iLastPOP3 = 0, iMinPOP3 = atoi(cfg->Get("minpop3")), iLastLogin = 0;
            bool bMinPOP3Issue = false, bWebLoginIssue = false;

            // check password
            b1gMailServer::MySQL_Result *res = NULL;
            if(strcmp(cfg->Get("salted_passwords"), "1") == 0)
            {
                res = db->Query("SELECT bm60_users.`gruppe`,bm60_users.`last_pop3`,bm60_users.`lastlogin` FROM bm60_users,bm60_gruppen WHERE bm60_users.passwort=MD5(CONCAT(MD5('%q'),bm60_users.passwort_salt)) AND (bm60_users.locked='no' AND bm60_users.gesperrt='no') AND bm60_gruppen.id=bm60_users.gruppe AND bm60_gruppen.pop3='yes' AND bm60_users.id='%d'",
                    strPass.c_str(),
                    this->iUserID);
            }
            else
            {
                res = db->Query("SELECT bm60_users.`gruppe`,bm60_users.`last_pop3`,bm60_users.`lastlogin` FROM bm60_users,bm60_gruppen WHERE bm60_users.passwort=MD5('%q') AND (bm60_users.locked='no' AND bm60_users.gesperrt='no') AND bm60_gruppen.id=bm60_users.gruppe AND bm60_gruppen.pop3='yes' AND bm60_users.id='%d'",
                    strPass.c_str(),
                    this->iUserID);
            }
            MYSQL_ROW row;
            bool bOk = (res->NumRows() == 1);
            if(bOk)
            {
                row = res->FetchRow();
                iLastPOP3 = atoi(row[1]);
                iLastLogin = atoi(row[2]);
            }
            delete res;

            // check minpop3
            if(bOk)
            {
                iMinPOP3 = atoi(utils->GetGroupOption(this->iUserID, "minpop3", "0").c_str());

                if(iMinPOP3 > 0 && iLastPOP3 > (int)time(NULL)-iMinPOP3)
                {
                    bMinPOP3Issue = true;
                    bOk = false;
                }
            }

            // check last login
            if(bOk)
            {
                int iWebLoginInterval = atoi(utils->GetGroupOption(this->iUserID, "weblogin_interval", "0").c_str());
                bool bRequireWebLogin = utils->GetGroupOption(this->iUserID, "require_weblogin", "1") == "1";

                if(bRequireWebLogin && iLastLogin == 0)
                {
                    bWebLoginIssue = true;
                    bOk = false;
                }
                else if(bRequireWebLogin && iWebLoginInterval > 0 && iLastLogin < time(NULL)-iWebLoginInterval*86400)
                {
                    bWebLoginIssue = true;
                    bOk = false;
                }
            }

            if(bOk)
            {
                db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] Logged in as %s",
                    this->strPeer.c_str(),
                    this->strUser.c_str()));
                db->Query("UPDATE bm60_users SET last_pop3=UNIX_TIMESTAMP() WHERE id='%d'",
                    this->iUserID);

                this->iState = POP3_STATE_TRANSACTION;

                // folder query condition
                string folderQuery, folderQueryPref;
                if(atoi(cfg->Get("user_chosepop3folders")) == 1)
                    folderQueryPref = utils->GetUserPref(this->iUserID, "pop3Folders", cfg->Get("pop3_folders"));
                else
                    folderQueryPref = cfg->Get("pop3_folders");

                bool allUserFolders = false;
                vector<string> folderIDs;
                utils->ExplodeOutsideOfQuotation(folderQueryPref, folderIDs, ',');
                for(vector<string>::iterator it = folderIDs.begin(); it != folderIDs.end(); ++it)
                {
                    if(*it == "-128")
                    {
                        allUserFolders = true;
                    }
                    else if(utils->IsNumeric(*it))
                    {
                        if(folderQuery.length() > 0)
                            folderQuery += string(",");
                        folderQuery += *it;
                    }
                }

                if(allUserFolders)
                {
                    if(folderQuery.length() > 0)
                    {
                        folderQuery = string("folder>0 OR folder IN(") + folderQuery + string(")");
                    }
                    else
                    {
                        folderQuery = string("folder>0");
                    }
                }
                else if(folderQuery.length() > 0)
                {
                    folderQuery = string("folder IN(") + folderQuery + string(")");
                }
                else
                {
                    folderQuery = "0";
                }

                // fetch message information
                this->iDropSize = 0;

                bool haveBlobStorage = strcmp(cfg->Get("enable_blobstorage"), "1") == 0;
                if (haveBlobStorage)
                {
                    res = db->Query("SELECT id,blobstorage,size FROM bm60_mails WHERE userid='%d' AND (%s) ORDER BY id ASC",
                        this->iUserID,
                        folderQuery.c_str());
                }
                else
                {
                    res = db->Query("SELECT id,LENGTH(body),size FROM bm60_mails WHERE userid='%d' AND (%s) ORDER BY id ASC",
                        this->iUserID,
                        folderQuery.c_str());
                }

                this->vMessages.reserve(res->NumRows());

                while((row = res->FetchRow()))
                {
                    // create message struct
                    POP3Msg msg;
                    msg.bDele = false;
                    msg.bFile = haveBlobStorage || (atoi(row[1]) == 4);
                    msg.iID = atoi(row[0]);
                    msg.iNr = (int)this->vMessages.size()+1;
                    msg.iSize = atoi(row[2]);
                    msg.iBlobStorage = haveBlobStorage ? atoi(row[1]) : 0;
                    msg.strUID = utils->MD5(string(row[0]) + this->strUser);

                    this->iDropSize += msg.iSize;

                    // push message to vector
                    this->vMessages.push_back(msg);
                }
                delete res;

                // plugins
                FOR_EACH_PLUGIN(Plugin)
                    Plugin->OnLoginUser(this->iUserID, this->strUser.c_str(), strPass.c_str());
                END_FOR_EACH()

                printf("+OK Logged in, you have %d messages (%ld octets)\r\n",
                    (int)this->vMessages.size(),
                    (long int)this->iDropSize);
            }
            else
            {
                db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] %s %s",
                    this->strPeer.c_str(),
                    bMinPOP3Issue
                        ? "Minimum POP3 session interval violated by"
                        : (bWebLoginIssue
                            ? "Web login requirement violated by "
                            : "Invalid password for"),
                    this->strUser.c_str()));

                this->iBadTries++;
                utils->MilliSleep(3*1000);

                if(bMinPOP3Issue)
                    printf("-ERR Minimum login interval is %d seconds\r\n", iMinPOP3);
                else if(bWebLoginIssue)
                    printf("-ERR Please login to your webmail account first\r\n");
                else
                    printf("-ERR Login failed\r\n");

                if(utils->Failban_LoginFailed(PEER_IP(), FAILBAN_POP3LOGIN))
                    this->bBanned = true;
            }
        }
    }
    else
    {
        db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] Invalid parameters for PASS",
            this->strPeer.c_str()));
        printf("-ERR Syntax: PASS [password]\r\n");
    }
}

/*
 * USER command
 */
bool POP3::User(char *szLine)
{
    db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] USER",
        this->strPeer.c_str()));

    char szUser[POP3_LINEMAX+1];
    if(sscanf(szLine, "%*5s %s", szUser) == 1)
    {
        string strUser(szUser);

        bool bOk = false;
        int iUserID = -1, iAltPOP3Port, iAlias = utils->GetAlias(strUser.c_str());

        MySQL_Result *res = db->Query("SELECT bm60_users.id,bm60_users.email FROM bm60_users WHERE ((bm60_users.email='%q') OR (bm60_users.id='%d')) LIMIT 1",
            strUser.c_str(),
            iAlias);
        MYSQL_ROW row;
        while((row = res->FetchRow()))
        {
            bOk = true;
            iUserID = atoi(row[0]);

            // set new username
            strUser = row[1];
        }
        delete res;

        this->strUser = strUser;

        if(bOk)
        {
            db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] Username %s (local user #%d)",
                this->strPeer.c_str(),
                strUser.c_str(),
                iUserID));
            this->iUserID = iUserID;

            printf("+OK Password required\r\n");
        }
        else if(cfg->Get("altpop3") != NULL && (iAltPOP3Port = atoi(cfg->Get("altpop3"))) > 0)
        {
            db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] Username %s (unknown user - redirecting to port %d)",
                this->strPeer.c_str(),
                strUser.c_str(),
                iAltPOP3Port));
            this->iUserID = -1;
            this->Redirect(iAltPOP3Port, strUser);
            return(false);
        }
        else
        {
            db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] Username %s (unknown user)",
                this->strPeer.c_str(),
                strUser.c_str()));
            this->iUserID = -1;

            printf("+OK Password required\r\n");
        }
    }
    else
    {
        db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] Invalid parameters for USER",
            this->strPeer.c_str()));
        printf("-ERR Syntax: USER [username]\r\n");
    }

    return(true);
}

/*
 * LIST command
 */
void POP3::List(char *szLine)
{
    int iID;
    if(sscanf(szLine, "%*s %d", &iID) == 1)
    {
        // one msg
        db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] LIST (%d)",
            this->strPeer.c_str(),
            iID));

        if(VALID_MSG(iID))
        {
            printf("+OK %d %u\r\n",
                iID,
                (unsigned int)this->vMessages.at(iID-1).iSize);
        }
        else
        {
            printf("-ERR Invalid message\r\n");
        }
    }
    else
    {
        // all msgs
        db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] LIST",
            this->strPeer.c_str()));

        printf("+OK %d messages (%ld octets)\r\n",
            (int)this->vMessages.size() - this->iDele,
            (long int)(this->iDropSize - this->iDeleSize));
        for(int i=0; i<(int)this->vMessages.size(); i++)
            if(!this->vMessages.at(i).bDele)
                printf("%d %u\r\n",
                    this->vMessages.at(i).iNr,
                    (unsigned int)this->vMessages.at(i).iSize);
        printf(".\r\n");
    }
}

/*
 * UIDL command
 */
void POP3::Uidl(char *szLine)
{
    int iID;
    if(sscanf(szLine, "%*s %d", &iID) == 1)
    {
        // one msg
        db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] UIDL (%d)",
            this->strPeer.c_str(),
            iID));

        if(VALID_MSG(iID))
        {
            printf("+OK %d %32s\r\n",
                iID,
                this->vMessages.at(iID-1).strUID.c_str());
        }
        else
        {
            printf("-ERR Invalid message\r\n");
        }
    }
    else
    {
        // all msgs
        db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] LIST",
            this->strPeer.c_str()));

        printf("+OK %d messages\r\n",
            (int)this->vMessages.size() - this->iDele);
        for(int i=0; i<(int)this->vMessages.size(); i++)
            if(!this->vMessages.at(i).bDele)
                printf("%d %32s\r\n",
                    this->vMessages.at(i).iNr,
                    this->vMessages.at(i).strUID.c_str());
        printf(".\r\n");
    }
}

/*
 * DELE command
 */
void POP3::Dele(char *szLine)
{
    db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] DELE",
        this->strPeer.c_str()));

    int iID;
    if(sscanf(szLine, "%*s %d", &iID) == 1)
    {
        if(VALID_MSG(iID))
        {
            db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] Message %d marked for deletion",
                this->strPeer.c_str(),
                this->vMessages.at(iID-1).iID));

            this->vMessages.at(iID-1).bDele = true;
            this->iDele++;
            this->iDeleSize += this->vMessages.at(iID-1).iSize;

            printf("+OK Message marked for deletion\r\n");
        }
        else
        {
            printf("-ERR Invalid message\r\n");
        }
    }
    else
    {
        db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] Invalid parameters for DELE",
            this->strPeer.c_str()));

        printf("-ERR Syntax: DELE [msgnr]\r\n");
    }
}

/*
 * TOP command
 */
void POP3::Top(char *szLine)
{
    db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] TOP",
        this->strPeer.c_str()));

    int iID, iLines;
    if(sscanf(szLine, "%*s %d %d", &iID, &iLines) == 2)
    {
        if(VALID_MSG(iID))
        {
            printf("+OK Header and first %d lines follow\r\n",
                iLines);

            int iSentLines = 0;
            bool bPassedHeader = false;

            // fetch from file
            FILE *fp = utils->GetMessageFP(this->vMessages.at(iID-1).iID, this->iUserID);
            if(fp != NULL)
            {
                char szBuffer[4096];
                while(!feof(fp))
                {
                    memset(szBuffer, 0, 4096);
                    fgets(szBuffer, 4095, fp);

                    if(!bPassedHeader || iSentLines < iLines)
                    {
                        if(szBuffer[0] == '.')
                        {
                            const char *szDot = ".";
                            fwrite(szDot, 1, 1, stdout);
                        }

                        fwrite(szBuffer, strlen(szBuffer), 1, stdout);
                        if(bPassedHeader)
                            iSentLines++;
                    }
                    else
                    {
                        break;
                    }

                    if(strcmp(szBuffer, "\n") == 0
                        || strcmp(szBuffer, "\r\n") == 0
                        || strcmp(szBuffer, "") == 0
                        || strcmp(szBuffer, "\r") == 0)
                    {
                        bPassedHeader = true;
                    }
                }
                fclose(fp);
            }
            else
            {
                printf("Internal error\r\n");
                db->Log(CMP_POP3, PRIO_WARNING, utils->PrintF("[%s] Cannot open message %d",
                    this->strPeer.c_str(),
                    this->vMessages.at(iID-1).iID));
            }

            printf("\r\n.\r\n");

            db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] Partialy retrieved message %d (headers + %d lines)",
                this->strPeer.c_str(),
                this->vMessages.at(iID-1).iID,
                iLines));
            this->iPartFetched++;
        }
        else
        {
            printf("-ERR Invalid message\r\n");
        }
    }
    else
    {
        db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] Invalid parameters for TOP",
            this->strPeer.c_str()));

        printf("-ERR Syntax: TOP [msgnr] [lines]\r\n");
    }
}

/*
 * RETR command
 */
void POP3::Retr(char *szLine)
{
    db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] RETR",
        this->strPeer.c_str()));

    int iID;
    if(sscanf(szLine, "%*s %d", &iID) == 1)
    {
        if(VALID_MSG(iID))
        {
            printf("+OK %u octets\r\n",
                (unsigned int)this->vMessages.at(iID-1).iSize);

            FILE *fp = utils->GetMessageFP(this->vMessages.at(iID-1).iID, this->iUserID);
            if(fp != NULL)
            {
                char szBuffer[4096];
                while(!feof(fp))
                {
                    memset(szBuffer, 0, 4096);
                    fgets(szBuffer, 4095, fp);

                    if(szBuffer[0] == '.')
                    {
                        const char *szDot = ".";
                        fwrite(szDot, 1, 1, stdout);
                    }

                    fwrite(szBuffer, strlen(szBuffer), 1, stdout);
                }
                fclose(fp);
            }
            else
            {
                printf("Internal error\r\n");
                db->Log(CMP_POP3, PRIO_WARNING, utils->PrintF("[%s] Cannot open message %d",
                    this->strPeer.c_str(),
                    this->vMessages.at(iID-1).iID));
            }

            printf("\r\n.\r\n");

            db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] Retrieved message %d",
                this->strPeer.c_str(),
                this->vMessages.at(iID-1).iID));
            this->iFetched++;
        }
        else
        {
            printf("-ERR Invalid message\r\n");
        }
    }
    else
    {
        db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] Invalid parameters for RETR",
            this->strPeer.c_str()));

        printf("-ERR Syntax: RETR [msgnr]\r\n");
    }
}

/*
 * RSET command
 */
void POP3::Rset()
{
    db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] RSET",
        this->strPeer.c_str()));

    for(int i=0; i<(int)this->vMessages.size(); i++)
        this->vMessages.at(i).bDele = false;

    this->iDele = 0;
    this->iDeleSize = 0;

    printf("+OK Done\r\n");
}

/*
 * STAT command
 */
void POP3::Stat()
{
    db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] STAT",
        this->strPeer.c_str()));

    printf("+OK %d %ld\r\n",
        (int)this->vMessages.size() - this->iDele,
        (long int)(this->iDropSize - this->iDeleSize));
}

/*
 * NOOP command
 */
void POP3::Noop()
{
    db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] NOOP",
        this->strPeer.c_str()));

    printf("+OK Yup\r\n");
}

/*
 * Process a line
 */
bool POP3::ProcessLine(char *szLine)
{
    db->Log(CMP_POP3, PRIO_DEBUG, utils->PrintF("[%s] Data: %s",
        this->strPeer.c_str(),
        szLine));

    char szCommand[6];
    int iCount;

    if((iCount = sscanf(szLine, "%5s", szCommand)) == 1)
    {
        this->iCommands++;

        if(strcasecmp(szCommand, "capa") == 0)
        {
            this->Capa();
        }
        else if(strcasecmp(szCommand, "stls") == 0
                && this->iState == POP3_STATE_AUTHORIZATION
                && utils->TLSAvailable()
                && !bTLSMode)
        {
            this->STLS();
        }
        else if(strcasecmp(szCommand, "quit") == 0)
        {
            this->Quit();
            return(false);
        }
        else if(strcasecmp(szCommand, "noop") == 0)
        {
            this->Noop();
        }
        else if(strcasecmp(szCommand, "user") == 0
            && this->iState == POP3_STATE_AUTHORIZATION)
        {
            if(!this->User(szLine))
                return(false);
        }
        else if(strcasecmp(szCommand, "pass") == 0
            && this->iState == POP3_STATE_AUTHORIZATION)
        {
            this->Pass(szLine);
        }
        else if(strcasecmp(szCommand, "rset") == 0
            && this->iState == POP3_STATE_TRANSACTION)
        {
            this->Rset();
        }
        else if(strcasecmp(szCommand, "stat") == 0
            && this->iState == POP3_STATE_TRANSACTION)
        {
            this->Stat();
        }
        else if(strcasecmp(szCommand, "list") == 0
            && this->iState == POP3_STATE_TRANSACTION)
        {
            this->List(szLine);
        }
        else if(strcasecmp(szCommand, "uidl") == 0
            && this->iState == POP3_STATE_TRANSACTION)
        {
            this->Uidl(szLine);
        }
        else if(strcasecmp(szCommand, "dele") == 0
            && this->iState == POP3_STATE_TRANSACTION)
        {
            this->Dele(szLine);
        }
        else if(strcasecmp(szCommand, "retr") == 0
            && this->iState == POP3_STATE_TRANSACTION)
        {
            this->Retr(szLine);
        }
        else if(strcasecmp(szCommand, "top") == 0
            && this->iState == POP3_STATE_TRANSACTION)
        {
            this->Top(szLine);
        }
        else
        {
            db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] Unknown command: %s",
                this->strPeer.c_str(),
                szCommand));

            this->iCommands--;
            printf("-ERR Unknown command: %s\r\n",
                        szCommand);
        }
    }
    else
    {
        printf("-ERR Syntax error\r\n");
    }

    // abort connection if more than 3 failed login attempts
    return(this->iBadTries < 3);
}

/*
 * Increment mailbox generation
 */
void POP3::IncGeneration(int iGeneration, int iStructureGeneration)
{
    if(this->iUserID < 1)
        return;

    db->Query("UPDATE bm60_users SET mailbox_generation=mailbox_generation+%d,mailbox_structure_generation=mailbox_structure_generation+%d WHERE id='%d'",
              iGeneration,
              iStructureGeneration,
              iUserID);
}

/*
 * Start TLS
 */
void POP3::STLS()
{
    db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] STLS",
                                               this->strPeer.c_str()));

    if(bTLSMode)
    {
        printf("-ERR Already in TLS mode\r\n");
        return;
    }

    printf("+OK Begin TLS negotiation\r\n");

    // initialize TLS mode
    char szError[255];
    if(utils->BeginTLS(ssl_ctx, szError))
    {
        db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] TLS negotiation succeeded",
                                                   this->strPeer.c_str()));

        bTLSMode = true;
        bIO_SSL = true;
    }
    else
    {
        bIO_SSL = false;

        db->Log(CMP_POP3, PRIO_NOTE, utils->PrintF("[%s] TLS negotiation failed (%s)",
                                                   this->strPeer.c_str(),
                                                   szError));
        printf("-ERR %s\r\n",
               szError);
    }
}
