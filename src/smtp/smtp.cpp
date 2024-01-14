/*
 * b1gMailServer
 * Copyright (c) 2002-2024
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

#include <smtp/smtp.h>
#include <smtp/spf.h>

#include <algorithm>

#define MAILBOX_MIN_QUOTA       (1024*2)        // according to several sources, the average mail size these days
                                                // is around 10-100 kB, but we are conservative here

SMTP *cSMTP_Instance = NULL;

/*
 * Signal handler
 */
void SMTP_SignalHandler(int iSignal)
{
    if(cSMTP_Instance == NULL)
        return;

    if(iSignal == SIGALRM)
        cSMTP_Instance->bTimeout = true;
    else
        cSMTP_Instance->bQuit = true;
#ifdef WIN32
    if(iSignal == SIGALRM)
        InterruptFGets();
#else
    fclose(stdin);
#endif
}
#ifdef WIN32
bool SMTP_CtrlHandler(DWORD fdwCtrlType)
{
    if(fdwCtrlType == CTRL_C_EVENT || fdwCtrlType == CTRL_BREAK_EVENT)
    {
        if(cSMTP_Instance != NULL)
        {
            cSMTP_Instance->bQuit = true;
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
SMTP::SMTP()
{
    this->iPeerOrigin = SMTP_PEER_ORIGIN_UNKNOWN;
    this->iInboundMessages = 0;
    this->iOutboundMessages = 0;
    this->iErrorCounter = 0;
    this->iCommands = 0;
    this->iRecipientLimit = atoi(cfg->Get("smtp_recipient_limit"));
    this->iMessageSizeLimit = atoi(cfg->Get("smtp_size_limit"));
    this->iMessageCountLimit = -1;
    this->iSMTPLimitCount = 0;
    this->iSMTPLimitTime = 0;
    this->bTodayKeyValid = false;
    this->bGreylisted = false;
    this->bTLSMode = false;
    this->bBanned = false;
    this->iSenderCheckMode = SMTP_SENDERCHECK_NO;

    this->strHeloHost = "";
    this->strReturnPath = "";
    this->strAuthUser = "";
    this->bEhlo = false;
    this->bAuthenticated = false;
    this->strAuthMethod = "";
    this->iUserID = 0;
    this->iLastFetch = 0;
    this->ib1gMailUserID = 0;
    this->bSubmission = false;

    this->bQuit = false;
    this->bTimeout = false;

    this->bHasReverseDNS = false;

    const char *szPeer = utils->GetPeerAddress();
    if(szPeer == NULL)
        this->strPeer = "(unknown)";
    else
        this->strPeer = szPeer;

    const char *realPeerAddress = utils->GetPeerAddress(true);
    if(realPeerAddress == NULL)
        this->strRealPeer = "(unknown)";
    else
        this->strRealPeer = realPeerAddress;

    if(strcmp(cfg->Get("smtp_reversedns"), "1") == 0 && realPeerAddress != NULL)
        this->strPeerHost = utils->GetHostByAddr(this->strRealPeer.c_str(), &this->bHasReverseDNS);
    else
        this->strPeerHost = this->strRealPeer;

    this->iState = SMTP_STATE_PREHELO;

    this->ClassifyPeer();

    cSMTP_Instance = this;

    db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] Connected",
        this->strPeer.c_str()));
}

/*
 * Destructor
 */
SMTP::~SMTP()
{
    // plugins
    if(this->iUserID > 0)
        PLUGIN_FUNCTION(OnLogoutUser);

    db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] Disconnected (inbound: %d / outbound: %d)",
        this->strPeer.c_str(),
        this->iInboundMessages,
        this->iOutboundMessages));

#ifdef _UNIX_EXP
    if(bTLSMode)
        SSL_free(ssl);
#endif

    cSMTP_Instance = NULL;
}

/*
 * Main loop
 */
void SMTP::Run()
{
    char szBuffer[SMTP_LINEMAX+1];

    // check failban status
    if(utils->Failban_IsBanned(PEER_IP(), FAILBAN_SMTPLOGIN|FAILBAN_SMTPRCPT))
    {
        this->bBanned = true;
        printf("554 5.7.1 IP temporarily banned because of too many failed login attempts\r\n");
        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] Closing connection (IP banned)",
                                                   this->strPeer.c_str()));
        return;
    }

    // reject?
    if(this->iPeerOrigin == SMTP_PEER_ORIGIN_REJECT)
    {
        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] Connection rejected due to peer classification",
            this->strPeer.c_str()));
        printf("554 5.7.1 Connection rejected\r\n");
        return;
    }

    // greylisting?
    bool bGreylistingEnabled = strcmp(cfg->Get("grey_enabled"), "1") == 0;
    if(bGreylistingEnabled
        && this->strPeer != "(unknown)"
        && this->iPeerOrigin != SMTP_PEER_ORIGIN_TRUSTED
        && this->iPeerOrigin != SMTP_PEER_ORIGIN_NOGREY
        && this->iPeerOrigin != SMTP_PEER_ORIGIN_NOGREYANDBAN)
    {
        // lookup
        bool bBreak = true;
        IPAddress peerIP(this->strPeer);
        MYSQL_ROW row;
        MySQL_Result *res;
        if(!peerIP.isIPv6)
        {
            res = db->Query("SELECT `ip`,`time`,`confirmed` FROM bm60_bms_greylist WHERE `ip`='%s' AND `ip6`=''",
                    peerIP.dbString().c_str());
        }
        else
        {
            res = db->Query("SELECT `ip6`,`time`,`confirmed` FROM bm60_bms_greylist WHERE `ip`='0' AND `ip6`='%s'",
                    peerIP.dbString().c_str());
        }
        if(res->NumRows() >= 1)
        {
            // entry
            if((row = res->FetchRow()))
            {
                // unconfirmed?
                if(atoi(row[2]) == 0)
                {
                    // entry still active?
                    if(atoi(row[1]) + atoi(cfg->Get("grey_wait_time")) >= (int)time(NULL))
                    {
                        // interval elapsed?
                        if(atoi(row[1]) + atoi(cfg->Get("grey_interval")) <= (int)time(NULL))
                        {
                            if(!peerIP.isIPv6)
                            {
                                db->Query("UPDATE bm60_bms_greylist SET `confirmed`=1 WHERE `ip`='%s' AND `ip6`=''",
                                    peerIP.dbString().c_str());
                            }
                            else
                            {
                                db->Query("UPDATE bm60_bms_greylist SET `confirmed`=1 WHERE `ip`='0' AND `ip6`='%s'",
                                    peerIP.dbString().c_str());
                            }
                            bBreak = false;
                        }

                        // interval not yet elapsed
                        else
                        {
                            db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("[%s] Peer tried to re-connect before greylisting wait time elapsed",
                                this->strPeer.c_str()));
                        }
                    }

                    // entry expired, update
                    else
                    {
                        if(!peerIP.isIPv6)
                        {
                            db->Query("UPDATE bm60_bms_greylist SET `time`=%d,`confirmed`=0 WHERE `ip`='%s' AND `ip6`=''",
                                (int)time(NULL),
                                peerIP.dbString().c_str());
                        }
                        else
                        {
                            db->Query("UPDATE bm60_bms_greylist SET `time`=%d,`confirmed`=0 WHERE `ip`='0' AND `ip6`='%s'",
                                (int)time(NULL),
                                peerIP.dbString().c_str());
                        }
                    }
                }

                // entry confirmed
                else
                {
                    // still active?
                    if(atoi(row[1]) + atoi(cfg->Get("grey_good_time")) >= (int)time(NULL))
                    {
                        bBreak = false;
                    }

                    // good_time elapsed, create new entry
                    else
                    {
                        if(!peerIP.isIPv6)
                        {
                            db->Query("UPDATE bm60_bms_greylist SET `time`=%d,`confirmed`=0 WHERE `ip`='%s' AND `ip6`=''",
                                (int)time(NULL),
                                peerIP.dbString().c_str());
                        }
                        else
                        {
                            db->Query("UPDATE bm60_bms_greylist SET `time`=%d,`confirmed`=0 WHERE `ip`='' AND `ip6`='%s'",
                                (int)time(NULL),
                                peerIP.dbString().c_str());
                        }
                    }
                }
            }
        }
        else
        {
            // no entry
            if(!peerIP.isIPv6)
            {
                db->Query("INSERT INTO bm60_bms_greylist(`ip`,`time`,`confirmed`) VALUES('%s','%d',0)",
                    peerIP.dbString().c_str(),
                    (int)time(NULL));
            }
            else
            {
                db->Query("INSERT INTO bm60_bms_greylist(`ip6`,`time`,`confirmed`) VALUES('%s','%d',0)",
                    peerIP.dbString().c_str(),
                    (int)time(NULL));
            }
        }
        delete res;

        // break?
        if(bBreak)
            this->bGreylisted = true;
    }

    // delay?
    int iGreetingDelay = atoi(cfg->Get("smtp_greeting_delay"));
    if(iGreetingDelay > 0 && this->iPeerOrigin != SMTP_PEER_ORIGIN_TRUSTED && !this->bSubmission)
    {
        // enter non-blocking mode
#ifdef WIN32
        SOCKET sSocket = (SOCKET)GetStdHandle(STD_INPUT_HANDLE);
        unsigned long ul = 1;
        ioctlsocket(sSocket, FIONBIO, &ul);
#else
        int sSocket = fileno(stdin);
        fcntl(sSocket, F_SETFL, fcntl(sSocket, F_GETFL) | O_NONBLOCK);
#endif

        // add stdin to fd set
        fd_set fdSet;
        FD_ZERO(&fdSet);
        FD_SET(sSocket, &fdSet);

        // set timeout
        struct timeval timeout;
        timeout.tv_sec = iGreetingDelay;
        timeout.tv_usec = 0;

        // select
        select(sSocket+1, &fdSet, NULL, NULL, &timeout);

        // flow violation?
        if(FD_ISSET(sSocket, &fdSet))
        {
            db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] Delay synchronization error, connection rejected",
                this->strPeer.c_str()));
            printf("554 5.5.1 Synchronization error\r\n");
            return;
        }

        // re-enter blocking mode
#ifdef WIN32
        ul = 0;
        ioctlsocket(sSocket, FIONBIO, &ul);
#else
        fcntl(sSocket, F_SETFL, fcntl(sSocket, F_GETFL) & ~(O_NONBLOCK));
#endif
    }

    // set signal handlers
    signal(SIGTERM, SMTP_SignalHandler);
    signal(SIGINT, SMTP_SignalHandler);
#ifndef WIN32
    signal(SIGALRM, SMTP_SignalHandler);
    signal(SIGHUP, SMTP_SignalHandler);
    signal(SIGUSR1, SMTP_SignalHandler);
#else
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)SMTP_CtrlHandler, true);
    SetAlarmSignalCallback(SMTP_SignalHandler);
#endif

    // greeting!
    int iErrorDelay = atoi(cfg->Get("smtp_error_delay")),
        iErrorSoftLimit = atoi(cfg->Get("smtp_error_softlimit")),
        iTimeout = atoi(cfg->Get("smtp_timeout"));
    if(iTimeout > 0)
        alarm((unsigned int)iTimeout);
    printf("220 %s %s%s\r\n",
        cfg->Get("b1gmta_host"),
        cfg->Get("smtpgreeting"),
        this->iPeerOrigin == SMTP_PEER_ORIGIN_TRUSTED ? " [bMS-" BMS_VERSION "]" : "");
    while(!this->bBanned && fgets(szBuffer, SMTP_LINEMAX, stdin) != NULL)
    {
        // disable timeout during delay
        if(this->iState != SMTP_STATE_DATA)
            alarm(0);

        // soft limit delay
        if(this->iState != SMTP_STATE_DATA
            && iErrorDelay > 0
            && iErrorSoftLimit > 0
            && this->iErrorCounter > iErrorSoftLimit
            && this->iPeerOrigin != SMTP_PEER_ORIGIN_TRUSTED)
            utils->MilliSleep(iErrorDelay*1000);

        // process command
        if(!this->ProcessLine(szBuffer) || this->iState == SMTP_STATE_QUIT)
            break;

        // reset timeout
        if(iTimeout > 0)
            alarm((unsigned int)iTimeout);
    }

    // disable alarm
    alarm(0);

    // timeout?
    if(this->bTimeout)
    {
        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] Closing connection due to timeout",
            this->strPeer.c_str()));
        printf("421 %s Closing connection due to timeout (%ds)\r\n",
            cfg->Get("b1gmta_host"),
            iTimeout);
    }

    // quit flag?
    else if(this->bQuit)
    {
        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] Closing connection due to set quit flag",
            this->strPeer.c_str()));
        printf("421 %s Closing connection\r\n",
            cfg->Get("b1gmta_host"));
    }

    // banned flag?
    else if(this->bBanned)
    {
        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] Closing connection (IP still banned)",
                                                   this->strPeer.c_str()));
    }

    // save message counter
    if(this->bAuthenticated && (this->iInboundMessages + this->iOutboundMessages) > 0)
    {
        if(bTodayKeyValid)
        {
            db->Query("UPDATE bm60_users SET `today_sent`=`today_sent`+%d,`sent_mails`=`sent_mails`+%d,`mta_sentmails`=`mta_sentmails`+%d WHERE `id`='%d'",
                this->iInboundMessages + this->iOutboundMessages,
                this->iInboundMessages + this->iOutboundMessages,
                this->iInboundMessages + this->iOutboundMessages,
                this->iUserID);
        }
        else
        {
            char szTodayKey[7];
            time_t iNow = time(NULL);

#ifdef WIN32
            struct tm *cNow = localtime(&iNow);
#else
            struct tm _cNow, *cNow = localtime_r(&iNow, &_cNow);
#endif
            strftime(szTodayKey, sizeof(szTodayKey), "%d%m%y", cNow);

            db->Query("UPDATE bm60_users SET `today_sent`=%d,`today_key`='%q',`sent_mails`=`sent_mails`+%d,`mta_sentmails`=`mta_sentmails`+%d WHERE `id`='%d'",
                this->iInboundMessages + this->iOutboundMessages,
                szTodayKey,
                this->iInboundMessages + this->iOutboundMessages,
                this->iInboundMessages + this->iOutboundMessages,
                this->iUserID);
        }
    }
}

/*
 * raise error
 */
void SMTP::Error(int iErrNo, const char *szMsg)
{
    int iErrorHardLimit = atoi(cfg->Get("smtp_error_hardlimit")),
        iErrorDelay = atoi(cfg->Get("smtp_error_delay"));

    db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] Error: %d %s",
        this->strPeer.c_str(),
        iErrNo,
        szMsg));

    this->iErrorCounter++;

    if(iErrorDelay > 0 && this->iPeerOrigin != SMTP_PEER_ORIGIN_TRUSTED)
        utils->MilliSleep(iErrorDelay * 1000);

    if(iErrorHardLimit > 0
        && this->iErrorCounter > iErrorHardLimit
        && this->iPeerOrigin != SMTP_PEER_ORIGIN_TRUSTED)
    {
        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] Error hard limit exceeded, closing connection",
            this->strPeer.c_str()));
        printf("554 5.7.0 Too many erroneous commands, closing connection\r\n");
        this->iState = SMTP_STATE_QUIT;
    }
    else
    {
        printf("%03d %s\r\n",
            iErrNo,
            szMsg);
    }
}

/*
 * HELO/EHLO command
 */
void SMTP::Helo(char *szLine, bool bEhlo)
{
    char szHostname[128];

    // check state
    /*if(this->iState != SMTP_STATE_PREHELO)
    {
        this->Error(503, "Duplicate HELO/EHLO");
        return;
    }*/

    // scan host name
    if(sscanf(szLine, "%*4s %128s", szHostname) == 1)
    {
        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] %s %s",
            this->strPeer.c_str(),
            bEhlo ? "EHLO" : "HELO",
            szHostname));

        this->strHeloHost = szHostname;
        this->iState = bEhlo ? SMTP_STATE_EHLO : SMTP_STATE_HELO;
        this->bEhlo = bEhlo;

        printf("250%c%s Pleased to meet you, %s [%s]\r\n",
            bEhlo ? '-' : ' ',
            cfg->Get("b1gmta_host"),
            this->strPeerHost.c_str(),
            this->strRealPeer.c_str());

        if(bEhlo)
        {
            if(this->iMessageSizeLimit != 0)
                printf("250-SIZE %d\r\n",
                    this->iMessageSizeLimit);
            if(atoi(cfg->Get("smtp_auth_enabled")) == 1)
            {
                printf("250-AUTH PLAIN LOGIN\r\n");
                printf("250-AUTH=PLAIN LOGIN\r\n");
            }
            if(utils->TLSAvailable()
                && !bTLSMode)
            {
                printf("250-STARTTLS\r\n");
            }
            printf("250 8BITMIME\r\n");
        }
    }
    else
    {
        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] %s (missing hostname)",
            this->strPeer.c_str(),
            bEhlo ? "EHLO" : "HELO"));

        if(bEhlo)
            this->Error(501, "5.5.4 Usage: EHLO [hostname]");
        else
            this->Error(501, "5.5.4 Usage: HELO [hostname]");
    }
}

/*
 * NOOP command
 */
void SMTP::Noop()
{
    db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] NOOP",
        this->strPeer.c_str()));

    printf("250 Doing nothing\r\n");
}

/*
 * QUIT command
 */
void SMTP::Quit()
{
    db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] QUIT",
        this->strPeer.c_str()));

    printf("221 %s Closing connection\r\n",
        cfg->Get("b1gmta_host"));
    this->iState = SMTP_STATE_QUIT;
}

/*
 * MAIL command
 */
void SMTP::Mail(char *szLine)
{
    // check syntax
    char *szReturnPath = szLine + sizeof("MAIL");
    if(strlen(szReturnPath) < sizeof("FROM:")
        || strncasecmp(szReturnPath, "FROM:", 5) != 0)
    {
        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] MAIL (without valid return path)",
            this->strPeer.c_str()));

        this->Error(501, "5.5.4 Usage: MAIL FROM:<return-path>");
        return;
    }
    string origReturnPath = string(szReturnPath);

    // extract return path
    string returnPath;
    if(strstr(szReturnPath, "<>") != NULL)
    {
        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] MAIL FROM:<>",
            this->strPeer.c_str()));

        // set return path
        returnPath = "";
    }
    else
    {
        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] MAIL FROM:%s",
            this->strPeer.c_str(),
            szReturnPath + 5));

        szReturnPath = Mail::ExtractMailAddress(szReturnPath + 5);
        if(szReturnPath == NULL)
        {
            if(this->iPeerOrigin != SMTP_PEER_ORIGIN_TRUSTED)
            {
                this->Error(501, "5.1.7 Invalid mail address in MAIL FROM:");
                return;
            }
            else
            {
                string tmpReturnPath = utils->Trim(string(origReturnPath.c_str() + 5));

                if(tmpReturnPath.empty())
                {
                    this->Error(501, "5.1.7 Invalid mail address in MAIL FROM:");
                    return;
                }
                else
                {
                    if(!tmpReturnPath.empty() && tmpReturnPath.at(0) == '<')
                        tmpReturnPath.erase(tmpReturnPath.begin());
                    if(!tmpReturnPath.empty() && *(tmpReturnPath.end()-1) == '>')
                        tmpReturnPath.erase(tmpReturnPath.end()-1);

                    if(tmpReturnPath.find('@') == string::npos)
                    {
                        if(tmpReturnPath.empty())
                            tmpReturnPath = string("postmaster@") + string(cfg->Get("b1gmta_host"));
                        else
                            tmpReturnPath = tmpReturnPath + string("@") + string(cfg->Get("b1gmta_host"));
                    }

                    returnPath = tmpReturnPath;
                }
            }
        }
        else
        {
            returnPath = string(szReturnPath);
        }

        free(szReturnPath);
    }

    // check state
    if(this->iState == SMTP_STATE_PREHELO)
    {
        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] MAIL (without HELO)",
            this->strPeer.c_str()));

        this->Error(503, "5.5.1 Please say HELO/EHLO first");
        return;
    }
    else if(this->iState == SMTP_STATE_MAIL)
    {
        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] MAIL (return path already specified)",
            this->strPeer.c_str()));

        this->Error(503, "5.5.1 Return path already specified");
        return;
    }

    // submission
    if(this->bSubmission && !this->bAuthenticated)
    {
        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] MAIL (transaction rejected (unauthenticated in submission mode))",
            this->strPeer.c_str()));

        this->Error(530, "5.7.0 Authentication required");
        return;
    }

    // dialup?
    if(this->iPeerOrigin == SMTP_PEER_ORIGIN_DIALUP && !this->bAuthenticated)
    {
        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] MAIL (transaction rejected due to dial-up peer)",
            this->strPeer.c_str()));

        if(atoi(cfg->Get("smtp_auth_enabled")) == 1)
            this->Error(530, "5.7.1 Authentication required");
        else
            this->Error(554, "5.7.1 Transaction failed");

        return;
    }

    // spf?
    this->spfResult = SPF_RESULT_NONE;
    this->spfExplanation.clear();
    if(!this->bAuthenticated && this->iPeerOrigin != SMTP_PEER_ORIGIN_TRUSTED
        && strcmp(cfg->Get("spf_enable"), "1") == 0)
    {
        string returnPathDomain;

        size_t atPos = returnPath.find('@');
        if(atPos != string::npos)
            returnPathDomain = returnPath.substr(atPos+1);
        else
            returnPathDomain = returnPath;
        if(returnPathDomain.length() > 0 && returnPathDomain.at(returnPathDomain.length()-1) != '.')
            returnPathDomain.append(1, '.');

        SPF spf(this->strHeloHost, this->strRealPeer.c_str(), this->strPeerHost);
        this->spfResult = spf.CheckHost(this->strRealPeer.c_str(), returnPathDomain, returnPath, this->spfExplanation);

        switch(this->spfResult)
        {
        case SPF_RESULT_PASS:
            db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("[%s] SPF check result: Pass",
                this->strPeer.c_str()));
            break;

        case SPF_RESULT_NEUTRAL:
            db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("[%s] SPF check result: Neutral",
                this->strPeer.c_str()));
            break;

        case SPF_RESULT_FAIL:
            if(strcmp(cfg->Get("spf_reject_mails"), "1") == 0)
            {
                db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] MAIL transaction rejected due to SPF result: Fail",
                    this->strPeer.c_str()));

                if(this->spfExplanation.length() > 0)
                {
                    printf("550-5.7.1 SPF MAIL FROM check failed:\r\n");
                    printf("550-5.7.1 The domain %s explains:\r\n", returnPathDomain.c_str());
                    string errorMessage = string("5.7.1 ") + this->spfExplanation;
                    this->Error(550, errorMessage.c_str());
                }
                else
                {
                    this->Error(550, "5.7.1 SPF MAIL FROM check failed");
                }

                return;
            }
            break;

        case SPF_RESULT_SOFTFAIL:
            db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] SPF check result: SoftFail",
                this->strPeer.c_str()));
            break;

        case SPF_RESULT_TEMPERROR:
            db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] SPF check returned TempError",
                this->strPeer.c_str()));
            break;

        case SPF_RESULT_PERMERROR:
            db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] SPF check returned PermError",
                this->strPeer.c_str()));
            break;

        default:
            db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("[%s] SPF check returned unknown value: %d",
                this->strPeer.c_str(),
                (int)this->spfResult));
            break;
        };
    }

    // greylisted?
    if(this->bGreylisted && !this->bAuthenticated
        && !(this->spfResult == SPF_RESULT_PASS && strcmp(cfg->Get("spf_disable_greylisting"), "1") == 0))
    {
        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] MAIL (transaction rejected due to greylisted peer)",
            this->strPeer.c_str()));

        this->Error(451, "4.7.0 Greylisted, please try again later");
        return;
    }

    // limit?
    if(this->bAuthenticated
        && this->iMessageCountLimit != -1
        && this->iMessageCountLimit - this->iInboundMessages - this->iOutboundMessages <= 0)
    {
        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] MAIL (transaction rejected, mail count limit exceeded)",
            this->strPeer.c_str()));
        this->Error(451, "4.7.0 User message limit exceeded for today");
        return;
    }

    // no reverse DNS?
    if(!this->bAuthenticated
        && this->iPeerOrigin != SMTP_PEER_ORIGIN_TRUSTED
        && strcmp(cfg->Get("smtp_reversedns"), "1") == 0
        && strcmp(cfg->Get("smtp_reject_noreversedns"), "1") == 0)
    {
        if(!this->bHasReverseDNS)
        {
            db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] MAIL (transaction rejected due to missing reverse DNS)",
                this->strPeer.c_str()));

            this->Error(550, "5.7.1 Your reverse DNS is misconfigured");
            return;
        }
    }

    // forged helo?
    if(!this->bAuthenticated
        && this->iPeerOrigin != SMTP_PEER_ORIGIN_TRUSTED
        && strcmp(cfg->Get("smtp_reversedns"), "1") == 0)
    {
        int heloCheckMode = atoi(cfg->Get("smtp_check_helo"));
        bool hostnameError = false;

        if(heloCheckMode == 1)
        {
            // hostnames must match exactly
            if(strcasecmp(this->strPeerHost.c_str(), this->strHeloHost.c_str()) != 0)
                hostnameError = true;
        }
        else if(heloCheckMode == 2)
        {
            // just the domain needs to match
            if(!utils->DomainMatches(this->strPeerHost, this->strHeloHost))
                hostnameError = true;
        }

        if(hostnameError)
        {
            db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] MAIL (transaction rejected due to forged HELO hostname, %s != %s)",
                this->strPeer.c_str(),
                this->strPeerHost.c_str(),
                this->strHeloHost.c_str()));

            this->Error(550, "5.7.1 Forged HELO hostname detected");
            return;
        }
    }

    // X-B1GMAIL-USERID?
    if(this->iPeerOrigin == SMTP_PEER_ORIGIN_TRUSTED)
    {
        const char *szXb1gMailUserID = strstr(origReturnPath.c_str(), "X-B1GMAIL-USERID=");
        if(szXb1gMailUserID != NULL)
        {
            szXb1gMailUserID += sizeof("X-B1GMAIL-USERID=")-1;

            int iXb1gMailUserID = 0;
            if(sscanf(szXb1gMailUserID, "%d", &iXb1gMailUserID) == 1)
            {
                db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("[%s] Associated b1gMail user: %d",
                    this->strPeer.c_str(),
                    iXb1gMailUserID));
                this->ib1gMailUserID = iXb1gMailUserID;
            }
        }
    }

    // SIZE?
    const char *szSize = strstr(origReturnPath.c_str(), "SIZE=");
    if(szSize != NULL)
    {
        szSize += sizeof("SIZE=")-1;

        int iSize = 0;
        if(sscanf(szSize, "%d", &iSize) == 1)
        {
            if(iSize > this->iMessageSizeLimit)
            {
                db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] Message size given in MAIL command (%d bytes) is bigger than message size limit (%d bytes)",
                    this->strPeer.c_str(),
                    iSize,
                    this->iMessageSizeLimit));

                this->Error(552, "5.3.4 Message size exceeds fixed maximium message size");
                return;
            }
        }
    }

    // empty return path is OK
    if(returnPath.length() == 0)
    {
        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] MAIL FROM:<>",
            this->strPeer.c_str()));

        // set return path
        this->strReturnPath = "";
    }

    // return path given
    else
    {
        // check return path?
        if(this->bAuthenticated && (this->iSenderCheckMode == SMTP_SENDERCHECK_MAILFROM || this->iSenderCheckMode == SMTP_SENDERCHECK_FULL))
        {
            if(!utils->IsValidSenderAddressForUser(this->iUserID, returnPath.c_str()))
            {
                db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] MAIL FROM address rejected (not one of account's email addresses)",
                    this->strPeer.c_str()));

                this->Error(501, "5.1.0 Please use one of your account's email addresses in MAIL FROM:");
                return;
            }
        }

        // set return path
        this->strReturnPath = returnPath;
    }

    // success
    this->iState = SMTP_STATE_MAIL;
    printf("250 2.1.0 Return path OK\r\n");
}

/*
 * RCPT command
 */
void SMTP::Rcpt(char *szLine)
{
    // check state
    if(this->iState != SMTP_STATE_MAIL)
    {
        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] RCPT (without MAIL command)",
            this->strPeer.c_str()));

        this->Error(503, "5.5.1 Please send MAIL command first");
        return;
    }

    // check syntax
    char *szForwardPath = szLine + sizeof("RCPT");
    if(strlen(szForwardPath) < sizeof("TO:")
        || strncasecmp(szForwardPath, "TO:", 3) != 0)
    {
        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] RCPT (without valid forward path)",
            this->strPeer.c_str()));

        this->Error(501, "5.5.4 Usage: RCPT TO:<forward-path>");
        return;
    }

    // log
    db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] RCPT TO:%s",
        this->strPeer.c_str(),
        szForwardPath + 3));
    string origForwardPath(szForwardPath + 3);

    // get mail address
    szForwardPath = Mail::ExtractMailAddress(szForwardPath + 3);
    if(szForwardPath == NULL)
    {
        this->Error(501, "5.1.3 Invalid mail address in RCPT TO:");
        return;
    }

    // X-B1GMAIL-DSID?
    int iXb1gMailDSID = 0;
    if(this->iPeerOrigin == SMTP_PEER_ORIGIN_TRUSTED)
    {
        const char *szXb1gMailDSID = strstr(origForwardPath.c_str(), "X-B1GMAIL-DSID=");
        if(szXb1gMailDSID != NULL)
        {
            szXb1gMailDSID += sizeof("X-B1GMAIL-DSID=") - 1;

            if (sscanf(szXb1gMailDSID, "%d", &iXb1gMailDSID) == 1)
            {
                db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("[%s] Associated b1gMail delivery status ID: %d",
                    this->strPeer.c_str(),
                    iXb1gMailDSID));
            }
        }
    }

    // check szForwardPath
    bool bOK = false;
    SMTPRecipient cRecipient;
    cRecipient.strAddress = szForwardPath;
    cRecipient.iLocalRecipient = utils->LookupUser(szForwardPath, false, false);
    cRecipient.iDeliveryStatusID = iXb1gMailDSID;

    // outbound
    if(cRecipient.iLocalRecipient == 0)
    {
        if(this->iPeerOrigin == SMTP_PEER_ORIGIN_TRUSTED
            || this->bAuthenticated)
        {
            if(this->iPeerOrigin != SMTP_PEER_ORIGIN_TRUSTED
                && utils->IsRecipientBlocked(cRecipient.strAddress))
            {
                utils->AddAbusePoint(this->iUserID, BMAP_SEND_RECP_BLOCKED, "SMTP, to %s",
                    cRecipient.strAddress.c_str());

                this->Error(550, "Relaying to this recipient is blocked");

                if(this->iPeerOrigin != SMTP_PEER_ORIGIN_NOGREYANDBAN
                    && utils->Failban_LoginFailed(PEER_IP(), FAILBAN_SMTPRCPT))
                    this->bBanned = true;
            }
            else
            {
                bOK = true;
            }
        }
        else
        {
            this->Error(550, "No such local user, unauthenticated relaying denied");

            if(this->iPeerOrigin != SMTP_PEER_ORIGIN_NOGREYANDBAN
                && utils->Failban_LoginFailed(PEER_IP(), FAILBAN_SMTPRCPT))
                this->bBanned = true;
        }
    }

    // inbound
    else
    {
        bool bOverQuota = false;

        // for now, check quota only if acting as MX server
        // otherwise, a bounce mail is okay - we do not want to reject mails from trusted origins
        //  which might be the web interface which expected the relay server to accept any email
        //  and report errors by bounce mails and not during the SMTP dialogue
        if(this->iPeerOrigin != SMTP_PEER_ORIGIN_TRUSTED
            && !this->bAuthenticated
            && cRecipient.iLocalRecipient > 0)
        {
            MYSQL_ROW row;
            MySQL_Result *res = NULL;

            if(strcmp(cfg->Get("user_space_add"), "1") == 0)
            {
                res = db->Query("SELECT bm60_users.`mailspace_used`,(bm60_gruppen.`storage`+bm60_users.`mailspace_add`) AS `storage` FROM bm60_users INNER JOIN bm60_gruppen ON bm60_gruppen.`id`=bm60_users.`gruppe` WHERE bm60_users.`id`=%d",
                    cRecipient.iLocalRecipient);
            }
            else
            {
                res = db->Query("SELECT bm60_users.`mailspace_used`,bm60_gruppen.`storage` FROM bm60_users INNER JOIN bm60_gruppen ON bm60_gruppen.`id`=bm60_users.`gruppe` WHERE bm60_users.`id`=%d",
                    cRecipient.iLocalRecipient);
            }
            while((row = res->FetchRow()))
            {
                unsigned long long userSize = 0, userQuota = 0;

                sscanf(row[0], "%llu", &userSize);
                sscanf(row[1], "%llu", &userQuota);

                if(userQuota > 0 && userSize > 0)
                {
                    if(userSize+MAILBOX_MIN_QUOTA > userQuota)
                    {
                        bOverQuota = true;

                        db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("[%s] User %d is over quota",
                                                                    this->strPeer.c_str(),
                                                                    cRecipient.iLocalRecipient));
                    }
                }
            }
            delete res;
        }

        if(bOverQuota)
        {
            this->Error(552, "5.5.2 User quota limit exceeded (mailbox is full)");
        }
        else
        {
            bOK = true;
        }
    }

    // limit?
    if(bOK
        && this->iRecipientLimit != 0
        && (int)this->vRecipients.size()+1 > this->iRecipientLimit)
    {
        utils->AddAbusePoint(this->iUserID, BMAP_SEND_RECP_LIMIT, "SMTP, %d recipients",
            (int)this->vRecipients.size()+1);
        this->Error(450, "4.5.3 Recipient limit reached");
    }
    else if(bOK)
    {
        bool bLimitError = false;
        int iRecipientCount = 0;

        // check group smtp limit
        if(this->bAuthenticated
            && this->iSMTPLimitCount > 0
            && this->iSMTPLimitTime > 0)
        {
            bool bHaveSendStats = strcmp(cfg->Get("enable_sendstats"), "1") == 0;

            if(bHaveSendStats)
            {
                bLimitError = !utils->MaySendMail(this->iUserID,
                    (int)this->vRecipients.size()+1,
                    this->iSMTPLimitCount,
                    this->iSMTPLimitTime,
                    &iRecipientCount);
            }
            else
            {
                MYSQL_ROW row;
                MySQL_Result *res = db->Query("SELECT SUM(`recipients`) FROM bm60_bms_smtpstats WHERE `userid`=%d AND `time`>=%d",
                                             this->iUserID,
                                             (int)time(NULL)-this->iSMTPLimitTime*60);
                if(res->NumRows() == 1)
                {
                    row = res->FetchRow();

                    if(row[0] != NULL && ((iRecipientCount = atoi(row[0])) + (int)this->vRecipients.size() + 1) > this->iSMTPLimitCount)
                        bLimitError = true;
                }
                delete res;
            }
        }

        if(bLimitError)
        {
            utils->AddAbusePoint(this->iUserID, BMAP_SEND_FREQ_LIMIT, "SMTP, %d recipients in the last %d minutes",
                iRecipientCount,
                this->iSMTPLimitTime);
            this->Error(450, "4.5.3 Mail limit reached. Please try again later.");
            db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("Recipient rejected (group SMTP limit for user %d reached)",
                                                        this->iUserID));
        }
        else
        {
            // check send-without-receive limit
            if(this->bAuthenticated && this->iUserID > 0 && strcmp(cfg->Get("enable_ap"), "1") == 0)
            {
                int iIntervalM = atoi(utils->GetAbuseTypeConfig(BMAP_SEND_WITHOUT_RECEIVE, "interval").c_str());
                if(iIntervalM > 0)
                {
                    if(this->iLastFetch < time(NULL)-iIntervalM*60)
                    {
                        utils->AddAbusePoint(this->iUserID, BMAP_SEND_WITHOUT_RECEIVE, "SMTP, last IMAP/POP3 login at %s",
                            this->iLastFetch == 0 ? "(never)" : utils->TimeToString((time_t)this->iLastFetch).c_str());
                    }
                }
            }

            // check if entry exists (avoid duplicates)
            bool bExists = false;
            for(unsigned int i=0; i<this->vRecipients.size(); i++)
            {
                if(strcasecmp(this->vRecipients.at(i).strAddress.c_str(), cRecipient.strAddress.c_str()) == 0
                    || (this->vRecipients.at(i).iLocalRecipient != 0
                        && cRecipient.iLocalRecipient == this->vRecipients.at(i).iLocalRecipient))
                {
                    bExists = true;
                    break;
                }
            }

            if(!bExists)
                this->vRecipients.push_back(cRecipient);
            printf("250 2.1.5 Recipient OK\r\n");
        }
    }

    // clean up
    free(szForwardPath);
}

/*
 * RSET command
 */
void SMTP::Rset(bool bSilent)
{
    if(!bSilent)
        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] RSET",
            this->strPeer.c_str()));

    // reset data
    this->strReturnPath = "";
    this->vRecipients.clear();
    this->ib1gMailUserID = 0;

    // reset state
    this->iState = this->bEhlo ? SMTP_STATE_EHLO : SMTP_STATE_HELO;

    // success
    if(!bSilent)
        printf("250 OK\r\n");
}

/*
 * Process a line
 */
bool SMTP::ProcessLine(char *szLine)
{
    if(this->iState == SMTP_STATE_DATA)
    {
        db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("[%s] Data: %s",
                                                    this->strPeer.c_str(),
                                                    szLine));
        bool thisLineEndsWithCrlf = strlen(szLine) >= 2
            && szLine[strlen(szLine)-2] == '\r'
            && szLine[strlen(szLine)-1] == '\n';
        this->ProcessDataLine(szLine);
        bPrevLineEndsWithCrLf = thisLineEndsWithCrlf;
        return(true);
    }

    db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("[%s] Data: %s",
        this->strPeer.c_str(),
        szLine));

    char szCommand[12];
    int iCount;

    if((iCount = sscanf(szLine, "%10s", szCommand)) == 1)
    {
        this->iCommands++;

        if(strcasecmp(szCommand, "helo") == 0)
        {
            this->Helo(szLine);
        }
        else if(strcasecmp(szCommand, "ehlo") == 0)
        {
            this->Helo(szLine, true);
        }
        else if(!this->bTLSMode
                && strcasecmp(szCommand, "starttls") == 0
                && utils->TLSAvailable())
        {
            this->StartTLS();
        }
        else if(strcasecmp(szCommand, "noop") == 0)
        {
            this->Noop();
        }
        else if(strcasecmp(szCommand, "quit") == 0)
        {
            this->Quit();
            return(false);
        }
        else if(this->bEhlo
            && strcasecmp(szCommand, "auth") == 0
            && atoi(cfg->Get("smtp_auth_enabled")) == 1)
        {
            this->Auth(szLine);
        }
        else if(strcasecmp(szCommand, "mail") == 0)
        {
            this->Mail(szLine);
        }
        else if(strcasecmp(szCommand, "rcpt") == 0)
        {
            this->Rcpt(szLine);
        }
        else if(strcasecmp(szCommand, "data") == 0)
        {
            this->Data();
        }
        else if(strcasecmp(szCommand, "rset") == 0)
        {
            this->Rset();
        }
        else
        {
            this->Error(500, "5.5.1 Unrecognized command");
        }
    }
    else
    {
        this->Error(500, "5.5.2 Syntax error");
    }

    // return
    return(true);
}

/*
 * Start TLS
 */
void SMTP::StartTLS()
{
    db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] STARTTLS",
                                               this->strPeer.c_str()));

    if(bTLSMode)
    {
        printf("454 Already in TLS mode\r\n");
        return;
    }

    printf("220 Begin TLS negotiation\r\n");

    // initialize TLS mode
    char szError[255];
    if(utils->BeginTLS(ssl_ctx, szError))
    {
        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] TLS negotiation succeeded",
                                                   this->strPeer.c_str()));

        bTLSMode = true;
        bIO_SSL = true;
    }
    else
    {
        bIO_SSL = false;

        db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] TLS negotiation failed (%s)",
                                                   this->strPeer.c_str(),
                                                   szError));
        printf("454 %s\r\n",
               szError);
    }
}
