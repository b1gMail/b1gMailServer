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

#include <imap/imap.h>

IMAP *cIMAP_Instance = NULL;
char _szIMAPFolderName_SENT[128]    = "Sent",
     _szIMAPFolderName_SPAM[128]    = "Spam",
     _szIMAPFolderName_DRAFTS[128]  = "Drafts",
     _szIMAPFolderName_TRASH[128]   = "Trash";
time_t g_lastIMAPCommand = 0;
int g_imapTimeout = 0;
bool g_imapMySQLClose = false;

/*
 * Signal handler
 */
void IMAP_SignalHandler(int iSignal)
{
    if(cIMAP_Instance == NULL)
        return;

    if(iSignal == SIGALRM)
    {
        if(g_lastIMAPCommand + g_imapTimeout < time(NULL))
        {
            cIMAP_Instance->bTimeout = true;
        }
        else
        {
            if(g_imapMySQLClose)
                db->TempClose();

            if(g_imapTimeout > 0)
                alarm(g_imapTimeout);
            return;
        }
    }
    else
        cIMAP_Instance->bQuit = true;

#ifdef WIN32
    if(iSignal == SIGALRM)
        InterruptFGets();
#else
    fclose(stdin);
#endif
}
#ifdef WIN32
bool IMAP_CtrlHandler(DWORD fdwCtrlType)
{
    if(fdwCtrlType == CTRL_C_EVENT || fdwCtrlType == CTRL_BREAK_EVENT)
    {
        if(cIMAP_Instance != NULL)
        {
            cIMAP_Instance->bQuit = true;
            InterruptFGets();
            return(true);
        }
    }

    return(false);
}
#endif

/*
 * CAPABILITY command
 */
void IMAP::Capability()
{
    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] CAPABILITY",
        this->strPeer.c_str()));

    printf("* CAPABILITY IMAP4rev1 %sAUTH=PLAIN AUTH=LOGIN QUOTA IDLE%s\r\n",
           bTLSMode || !utils->TLSAvailable() ? "" : "STARTTLS ",
           bAPNS ? " XAPPLEPUSHSERVICE" : ""
         );
    printf("%s OK CAPABILITY completed\r\n",
        this->szTag);
}

/*
 * NOOP command
 */
void IMAP::Noop()
{
    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] NOOP",
        this->strPeer.c_str()));

    printf("%s OK NOOP completed\r\n",
        this->szTag);
}

/*
 * Constructor
 */
IMAP::IMAP()
{
    const char *szFolderName;

    if((szFolderName = cfg->Get("imap_folder_sent")) != NULL
       && strlen(szFolderName) < sizeof(_szIMAPFolderName_SENT))
        strcpy(_szIMAPFolderName_SENT, szFolderName);
    if((szFolderName = cfg->Get("imap_folder_spam")) != NULL
       && strlen(szFolderName) < sizeof(_szIMAPFolderName_SPAM))
        strcpy(_szIMAPFolderName_SPAM, szFolderName);
    if((szFolderName = cfg->Get("imap_folder_drafts")) != NULL
       && strlen(szFolderName) < sizeof(_szIMAPFolderName_DRAFTS))
        strcpy(_szIMAPFolderName_DRAFTS, szFolderName);
    if((szFolderName = cfg->Get("imap_folder_trash")) != NULL
       && strlen(szFolderName) < sizeof(_szIMAPFolderName_TRASH))
        strcpy(_szIMAPFolderName_TRASH, szFolderName);

    this->bAPNS = atoi(cfg->Get("apns_enable")) == 1;
    this->bAutoExpunge = atoi(cfg->Get("imap_autoexpunge")) == 1;
    this->iState = IMAP_STATE_NON_AUTHENTICATED;
    strcpy(this->szTag, "");
    this->iCommands = 0;
    this->strUser.clear();
    this->iUserID = -1;
    this->iBadTries = 0;
    this->iSelected = 0;
    this->vMessages.clear();
    this->bTLSMode = false;
    this->bTimeout = false;
    this->bQuit = false;
    this->bBanned = false;
    this->iLimit = 0;
    this->iLastLoginUpdate = 0;

    const char *szPeer = utils->GetPeerAddress();
    if(szPeer == NULL)
        this->strPeer = "(unknown)";
    else
        this->strPeer = szPeer;

    if(cIMAP_Instance == NULL)
        cIMAP_Instance = this;

    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Connected",
        this->strPeer.c_str()));
}

/*
 * Destructor
 */
IMAP::~IMAP()
{
    if(cIMAP_Instance == this)
        cIMAP_Instance = NULL;

    // plugins
    if(this->iUserID > 0)
        PLUGIN_FUNCTION(OnLogoutUser);

    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Disconnected",
        this->strPeer.c_str()));

#ifdef _UNIX_EXP
    if(bTLSMode)
        SSL_free(ssl);
#endif
}

/*
 * Main loop
 */
void IMAP::Run()
{
    char szBuffer[IMAP_MAXLINE+1];
    int iTimeout = atoi(cfg->Get("imap_timeout"));

    g_imapMySQLClose = atoi(cfg->Get("imap_mysqlclose")) == 1;
    g_imapTimeout = iTimeout;

    // set signal handlers
    signal(SIGINT, IMAP_SignalHandler);
    signal(SIGTERM, IMAP_SignalHandler);
#ifndef WIN32
    signal(SIGALRM, IMAP_SignalHandler);
    signal(SIGHUP, IMAP_SignalHandler);
    signal(SIGUSR1, IMAP_SignalHandler);
#else
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)IMAP_CtrlHandler, true);
    SetAlarmSignalCallback(IMAP_SignalHandler);
#endif

    // check failban status
    if(utils->Failban_IsBanned(PEER_IP(), FAILBAN_IMAPLOGIN))
    {
        this->bBanned = true;
        printf("* BYE IP temporarily banned because of too many failed login attempts\r\n");
        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Closing connection (IP banned)",
                                                   this->strPeer.c_str()));
        return;
    }

    // greeting!
    g_lastIMAPCommand = time(NULL);
    //if(iTimeout > 0)
    //  alarm((unsigned int)iTimeout);
    alarm(2);
    printf("* OK %s\r\n",
        cfg->Get("imapgreeting"));
    while(!this->bBanned && fgets(szBuffer, IMAP_MAXLINE, stdin) != NULL)
    {
        // disable timeout
        alarm(0);

        // process
        if(!this->ProcessLine(szBuffer))
            break;
        g_lastIMAPCommand = time(NULL);

        // reset timeout
        alarm(2);
        //if(iTimeout > 0)
        //  alarm((unsigned int)iTimeout);
    }

    // disable alarm
    alarm(0);

    // timeout?
    if(this->bTimeout)
    {
        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Closing connection due to timeout",
            this->strPeer.c_str()));
        printf("* BYE Timeout (%ds)\r\n",
            iTimeout);
    }

    // quit flag?
    else if(this->bQuit)
    {
        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Closing connection due to set quit flag",
            this->strPeer.c_str()));
        printf("* BYE Closing connection\r\n");
    }

    // banned flag?
    else if(this->bBanned)
    {
        printf("* BYE IP temporarily banned because of too many failed login attempts\r\n");
        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Closing connection (IP still banned)",
                                                   this->strPeer.c_str()));
    }
}

/*
 * LOGOUT command
 */
void IMAP::Logout()
{
    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] LOGOUT",
        this->strPeer.c_str()));

    if(this->iState == IMAP_STATE_SELECTED)
    {
        if(this->bAutoExpunge)
            this->Expunge(true);
        this->vMessages.clear();
        this->iSelected = -1;
    }

    this->iState = IMAP_STATE_LOGOUT;

    printf("* BYE Have a nice day\r\n");
    printf("%s OK LOGOUT completed\r\n",
        this->szTag);
}

/*
 * Process a line
 */
bool IMAP::ProcessLine(char *szLine)
{
    char szCommand[20], szCommand2[20];
    int iScanf = 0, iCurrentGeneration = 0, iCurrentStructureGeneration = 0;

    db->Log(CMP_IMAP, PRIO_DEBUG, utils->PrintF("[%s] Data: %s",
        this->strPeer.c_str(),
        szLine));

    if((iScanf = sscanf(szLine, "%s %19s %19s", this->szTag, szCommand, szCommand2)) >= 2)
    {
        this->iCommands++;

        if(this->iUserID > 0
            && this->iLastLoginUpdate <= time(NULL) - LASTLOGIN_UPDATE_INTERVAL)
        {
            db->Query("UPDATE bm60_users SET last_imap=UNIX_TIMESTAMP() WHERE id='%d'",
                      this->iUserID);
            this->iLastLoginUpdate = time(NULL);
        }

        if(this->iUserID > 0
            && (strcasecmp(szCommand, "fetch") != 0 && strcasecmp(szCommand, "store") != 0 && strcasecmp(szCommand, "search") != 0
                && strcasecmp(szCommand, "logout") != 0 && strcasecmp(szCommand, "idle") != 0))
        {
            IMAPHelper::GetGeneration(db, this->iUserID, &iCurrentGeneration, &iCurrentStructureGeneration);

            // refresh mails
            if(this->iState == IMAP_STATE_SELECTED
                && iCurrentGeneration != this->iGeneration)
            {
                db->Log(CMP_IMAP, PRIO_DEBUG, utils->PrintF("[%s] Refreshing mail list (mailbox generation changed)",
                    this->strPeer.c_str()));
                IMAPMsgList newMessages = IMAPHelper::FetchMessages(db, this->iSelected, this->iUserID, this->iLimit, this->bReadonly);
                this->iGeneration = iCurrentGeneration;

                IMAPHelper::ReportDelta(db, this->vMessages, newMessages);

                this->vMessages = newMessages;
            }

            // refresh folders
            if(!this->cFolders.empty() && iCurrentStructureGeneration != this->iStructureGeneration)
            {
                this->cFolders = IMAPHelper::FetchFolders(db, this->iUserID);
                this->iStructureGeneration = iCurrentStructureGeneration;
            }
        }

        // any state
        if(strcasecmp(szCommand, "capability") == 0)
        {
            this->Capability();
        }
        else if(strcasecmp(szCommand, "noop") == 0)
        {
            this->Noop();
        }
        else if(strcasecmp(szCommand, "logout") == 0)
        {
            this->Logout();
            return(false);
        }

        // non authenticated state
        else if(strcasecmp(szCommand, "authenticate") == 0
            && this->iState == IMAP_STATE_NON_AUTHENTICATED)
        {
            this->Authenticate(szLine);
        }
        else if(strcasecmp(szCommand, "login") == 0
                && this->iState == IMAP_STATE_NON_AUTHENTICATED)
        {
            this->Login(szLine);
        }
        else if(strcasecmp(szCommand, "starttls") == 0
                && this->iState == IMAP_STATE_NON_AUTHENTICATED
                && utils->TLSAvailable()
                && !bTLSMode)
        {
            this->StartTLS(szLine);
        }

        // authenticated state
        else if(strcasecmp(szCommand, "append") == 0
            && (this->iState == IMAP_STATE_AUTHENTICATED
                || this->iState == IMAP_STATE_SELECTED))
        {
            this->Append(szLine);
        }
        else if(strcasecmp(szCommand, "list") == 0
            && (this->iState == IMAP_STATE_AUTHENTICATED
                || this->iState == IMAP_STATE_SELECTED))
        {
            this->List(szLine);
        }
        else if(strcasecmp(szCommand, "lsub") == 0
            && (this->iState == IMAP_STATE_AUTHENTICATED
                || this->iState == IMAP_STATE_SELECTED))
        {
            this->List(szLine, true);
        }
        else if(strcasecmp(szCommand, "create") == 0
            && (this->iState == IMAP_STATE_AUTHENTICATED
                || this->iState == IMAP_STATE_SELECTED))
        {
            this->Create(szLine);
        }
        else if(strcasecmp(szCommand, "subscribe") == 0
            && (this->iState == IMAP_STATE_AUTHENTICATED
                || this->iState == IMAP_STATE_SELECTED))
        {
            this->Subscribe(szLine);
        }
        else if(strcasecmp(szCommand, "unsubscribe") == 0
            && (this->iState == IMAP_STATE_AUTHENTICATED
                || this->iState == IMAP_STATE_SELECTED))
        {
            this->Subscribe(szLine, true);
        }
        else if(strcasecmp(szCommand, "delete") == 0
            && (this->iState == IMAP_STATE_AUTHENTICATED
                || this->iState == IMAP_STATE_SELECTED))
        {
            this->Delete(szLine);
        }
        else if(strcasecmp(szCommand, "status") == 0
            && (this->iState == IMAP_STATE_AUTHENTICATED
                || this->iState == IMAP_STATE_SELECTED))
        {
            this->Status(szLine);
        }
        else if(strcasecmp(szCommand, "rename") == 0
            && (this->iState == IMAP_STATE_AUTHENTICATED
                || this->iState == IMAP_STATE_SELECTED))
        {
            this->Rename(szLine);
        }
        else if(strcasecmp(szCommand, "select") == 0
            && (this->iState == IMAP_STATE_AUTHENTICATED
                || this->iState == IMAP_STATE_SELECTED))
        {
            this->Select(szLine);
        }
        else if(strcasecmp(szCommand, "examine") == 0
            && (this->iState == IMAP_STATE_AUTHENTICATED
                || this->iState == IMAP_STATE_SELECTED))
        {
            this->Select(szLine, true);
        }
        else if(strcasecmp(szCommand, "getquota") == 0
            && (this->iState == IMAP_STATE_AUTHENTICATED
                || this->iState == IMAP_STATE_SELECTED))
        {
            this->GetQuota(szLine);
        }
        else if(strcasecmp(szCommand, "getquotaroot") == 0
            && (this->iState == IMAP_STATE_AUTHENTICATED
                || this->iState == IMAP_STATE_SELECTED))
        {
            this->GetQuotaRoot(szLine);
        }
        else if(strcasecmp(szCommand, "setquota") == 0
            && (this->iState == IMAP_STATE_AUTHENTICATED
                || this->iState == IMAP_STATE_SELECTED))
        {
            this->SetQuota();
        }
        else if(strcasecmp(szCommand, "idle") == 0
            && (this->iState == IMAP_STATE_AUTHENTICATED
                || this->iState == IMAP_STATE_SELECTED))
        {
            this->Idle();
        }
        else if(strcasecmp(szCommand, "xapplepushservice") == 0
            && this->bAPNS
            && (this->iState == IMAP_STATE_AUTHENTICATED
                || this->iState == IMAP_STATE_SELECTED))
        {
            this->XApplePushService(szLine);
        }

        // selected state
        else if(strcasecmp(szCommand, "check") == 0
            && this->iState == IMAP_STATE_SELECTED)
        {
            this->Check();
        }
        else if(strcasecmp(szCommand, "close") == 0
            && this->iState == IMAP_STATE_SELECTED)
        {
            this->Close();
        }
        else if(strcasecmp(szCommand, "expunge") == 0
            && this->iState == IMAP_STATE_SELECTED)
        {
            this->Expunge();
        }
        else if(strcasecmp(szCommand, "store") == 0
            && this->iState == IMAP_STATE_SELECTED)
        {
            this->Store(szLine);
        }
        else if(strcasecmp(szCommand, "copy") == 0
            && this->iState == IMAP_STATE_SELECTED)
        {
            this->Copy(szLine);
        }
        else if(strcasecmp(szCommand, "fetch") == 0
            && this->iState == IMAP_STATE_SELECTED)
        {
            this->Fetch(szLine);
        }
        else if(strcasecmp(szCommand, "search") == 0
            && this->iState == IMAP_STATE_SELECTED)
        {
            this->Search(szLine);
        }
        else if(strcasecmp(szCommand, "uid") == 0
            && this->iState == IMAP_STATE_SELECTED)
        {
            if(iScanf == 3)
            {
                if(strcasecmp(szCommand2, "fetch") == 0)
                {
                    this->Fetch(szLine + strlen(this->szTag) + 1, true);
                }
                else if(strcasecmp(szCommand2, "search") == 0)
                {
                    this->Search(szLine + strlen(this->szTag) + 1, true);
                }
                else if(strcasecmp(szCommand2, "store") == 0)
                {
                    this->Store(szLine + strlen(this->szTag) + 1, true);
                }
                else if(strcasecmp(szCommand2, "copy") == 0)
                {
                    this->Copy(szLine + strlen(this->szTag) + 1, true);
                }
                else
                {
                    printf("%s NO UID failed: Bad command\r\n",
                        this->szTag);
                }
            }
            else
            {
                printf("* BAD Syntax: UID [FETCH|SEARCH|COPY|STORE] [args]\r\n");
            }
        }

        // unknown command
        else
        {
            db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Unknown command: %s",
                this->strPeer.c_str(),
                szCommand));

            this->iCommands--;
            printf("%s NO Command unknown: %s\r\n",
                this->szTag,
                szCommand);
        }
    }
    else
    {
        printf("* BAD Syntax error\r\n");
    }

    return(true);
}
