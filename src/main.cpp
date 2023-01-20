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

#include <core/core.h>
#include <smtp/smtp.h>
#include <msgqueue/msgqueue.h>
#include <pop3/pop3.h>
#include <imap/imap.h>
#include <http/http.h>

#ifndef WIN32
#include <grp.h>
#include <pwd.h>
#include <syslog.h>
#endif

#define VERSION_STRING      BMS_VERSION " (build " BMS_BUILD ", " BMS_BUILD_ARCH ", " BMS_BUILD_DATE ")"
#define BEGINTLS_TIMEOUT    30

Core::MySQL_DB *db = NULL;
Core::Config *cfg = NULL;
FILE *fpBMSLog = NULL;

/*
 * Add a session to session log
 */
void AddSession(int iComponent, int iUserID)
{
    db->Query("INSERT INTO bm60_bms_stats(`date`,`component`,`connections`,`in`,`out`) VALUES(CURDATE(), %d, 1, %u, %u)"
                " ON DUPLICATE KEY UPDATE `connections`=`connections`+1,`in`=`in`+%u,`out`=`out`+%u",
            iComponent,
            (unsigned long)iTrafficIn,
            (unsigned long)iTrafficOut,
            (unsigned long)iTrafficIn,
            (unsigned long)iTrafficOut);
}

/*
 * Check if peer is local or unknown (the latter indicates access from console)
 */
bool PeerLocalOrUnknown()
{
#ifdef WIN32
    SOCKET sSock = (SOCKET)GetStdHandle(STD_INPUT_HANDLE);
#else
    int sSock = fileno(stdin);
#endif

    struct sockaddr_storage sName;
    int iNameLen = sizeof(sName);

    if(getpeername(sSock, (struct sockaddr *)&sName, (socklen_t *)&iNameLen) == 0)
    {
        if(sName.ss_family == AF_INET)
        {
            struct sockaddr_in *s = (struct sockaddr_in *)&sName;

            IPAddress theIP(s->sin_addr);

            if(theIP.isLocalhost())
                return(true);

            return(false);
        }
        else if(sName.ss_family == AF_INET6)
        {
            struct sockaddr_in6 *s = (struct sockaddr_in6 *)&sName;

            IPAddress theIP(s->sin6_addr);

            if(theIP.isLocalhost())
                return(true);

            return(false);
        }
    }

    return(true);
}

/*
 * Entry point
 */
int main(int argc, char *argv[])
{
    bool bPOP3 = false,
            bIMAP = false,
            bHelp = false,
            bVersion = false,
            bHTTP = false,
            bSMTP = false,
            bSubmission = false,
            bMSGQueue = false,
            bListPlugins = false,
            bSSL = false,
            bDumpConfig = false,
            bDebug = false;
    int iResult = 0;

    // parse command line arguments
    for(int i=1; i<argc; i++)
        if(strcmp(argv[i], "--pop3") == 0)
            bPOP3 = true;
        else if(strcmp(argv[i], "--imap") == 0)
            bIMAP = true;
        else if(strcmp(argv[i], "--http") == 0)
            bHTTP = true;
        else if(strcmp(argv[i], "--smtp") == 0)
            bSMTP = true;
        else if(strcmp(argv[i], "--submission") == 0)
            bSubmission = true;
        else if(strcmp(argv[i], "--msgqueue") == 0)
            bMSGQueue = true;
        else if(strcmp(argv[i], "--list-plugins") == 0)
            bListPlugins = true;
        else if(strcmp(argv[i], "--help") == 0
            || strcmp(argv[i], "-h") == 0)
            bHelp = true;
        else if(strcmp(argv[i], "--version") == 0
            || strcmp(argv[i], "-v") == 0)
            bVersion = true;
        else if(strcmp(argv[i], "--ssl") == 0)
            bSSL = true;
        else if(strcmp(argv[i], "--dump-config") == 0)
            bDumpConfig = true;
        else if(strcmp(argv[i], "--debug") == 0)
            bDebug = true;

    if(bVersion)
    {
        printf("b1gMailServer " VERSION_STRING "\n");
        return(0);
    }

    try
    {
#ifdef WIN32
        WSAData wsaData;
        if(WSAStartup(MAKEWORD(2, 0), &wsaData) != 0)
            throw Core::Exception("Cannot initialize winsock library");

        setmode(fileno(stdout), O_BINARY);
        setmode(fileno(stdin), O_BINARY);

#   if 0
        // disable output buffering
        // TODO: check, test; maybe also for unix versions?
        setvbuf(stdout, NULL, 0, _IONBF);
#   endif
#endif

        // initialize utils
        utils = new Core::Utils();
        utils->Init();

        // read config
        cfg = new Core::Config();

        // check if required values are available
        cfg->CheckRequiredValues();

        // file log?
        if(cfg->Get("logfile") != NULL
            && strlen(cfg->Get("logfile")) > 2)
        {
            fpBMSLog = fopen(cfg->Get("logfile"), "a");
        }

#ifndef WIN32
        chdir("/opt/b1gmailserver");

        const char *szGroupName = cfg->Get("group");
        if(szGroupName != NULL)
        {
            struct group *grpEnt = getgrnam((const char *)szGroupName);
            if(grpEnt != NULL)
            {
                if(getgid() != grpEnt->gr_gid && setgid(grpEnt->gr_gid) < 0)
                    throw Core::Exception("Failed to switch group", (char *)szGroupName);
            }
            else
                throw Core::Exception("Group not found", (char *)szGroupName);
        }

        const char *szUserName = cfg->Get("user");
        if(szUserName != NULL)
        {
            struct passwd *pwEnt = getpwnam((const char *)szUserName);
            if(pwEnt != NULL)
            {
                if(getuid() != pwEnt->pw_uid && setuid(pwEnt->pw_uid) < 0)
                    throw Core::Exception("Failed to switch user", (char *)szUserName);
            }
            else
                throw Core::Exception("User not found", (char *)szUserName);
        }
#endif

        // connect to db
        db = new Core::MySQL_DB(cfg->Get("mysql_host"),
                cfg->Get("mysql_user"),
                cfg->Get("mysql_pass"),
                cfg->Get("mysql_db"),
                cfg->Get("mysql_sock"));
        db->Query("SET NAMES latin1");
        utils->db = db;

        // get db config
        cfg->ReadDBConfig();

        // check if required values are available
        cfg->CheckDBRequiredValues();

		// update version/features in db?
		bool updateDbStatus = true;
		do
		{
			if (strcmp(cfg->Get("core_version"), VERSION_STRING) != 0)
				break;

			if (atoi(cfg->Get("core_features")) != utils->GetCoreFeatures())
				break;

			updateDbStatus = false;
		} while (false);

		if (updateDbStatus)
		{
			db->Query("UPDATE bm60_bms_prefs SET core_version='%q',core_features='%d'",
				VERSION_STRING,
				utils->GetCoreFeatures());
		}

        // load plugins
        PluginMgr = new PluginManager();
        PluginMgr->Init();
        PLUGIN_FUNCTION(Init);

        // ensure IMAP UIDs are initialized
        utils->InitializeIMAPUIDs();

        // unknown mode?
        if(bHelp || (!bPOP3
                        && !bHTTP
                        && !bSMTP
                        && !bMSGQueue
                        && !bListPlugins
                        && !bIMAP
                        && !bDumpConfig))
        {
            bool bShowHelp = true;

            // plugins
            if(!bHelp)
            {
                iResult = 1;

                FOR_EACH_PLUGIN(Plugin)
                {
                    if(Plugin->OnCallWithoutValidMode(argc, argv, iResult))
                        bShowHelp = false;
                }
                END_FOR_EACH()
            }

            // usage help
            if(bShowHelp)
            {
                printf("b1gMailServer " VERSION_STRING "\n");
                printf("(c) 2002-2023 B1G Software\n");
                printf("\n");
                printf("Usage: %s [mode]\n",
                    argv[0]);
                printf("Valid modes are:\n");

                printf("   --pop3            POP3 mode\n");
                printf("   --smtp            SMTP mode\n");
                printf("   --imap            IMAP mode\n");
                printf("   --http            HTTP mode\n");

                // plugins
                PLUGIN_FUNCTION(OnDisplayModeHelp);

                printf("   --list-plugins    Show list of active plugins\n");
                printf("   --version (-v)    Display b1gMailServer version\n");
                printf("   --help (-h)       Display this help screen\n");
                printf("\n");

                iResult = 0;
            }
        }

        // pop3 mode?
        else if(bPOP3)
        {
            POP3 cPOP3Server;

            char szError[255];
            if(!bSSL || utils->BeginTLS(cPOP3Server.ssl_ctx, szError, BEGINTLS_TIMEOUT))
            {
                if(bSSL)
                {
                    cPOP3Server.bTLSMode = true;
                    bIO_SSL = true;
                }

                cPOP3Server.Run();
                if((iTrafficIn > 0 || iTrafficOut > 0))
                    AddSession(CMP_POP3, cPOP3Server.iUserID);
            }
        }

        // imap mode?
        else if(bIMAP)
        {
            IMAP cIMAPServer;

            char szError[255];
            if(!bSSL || utils->BeginTLS(cIMAPServer.ssl_ctx, szError, BEGINTLS_TIMEOUT))
            {
                if(bSSL)
                {
                    cIMAPServer.bTLSMode = true;
                    bIO_SSL = true;
                }

                cIMAPServer.Run();
                if((iTrafficIn > 0 || iTrafficOut > 0))
                    AddSession(CMP_IMAP, cIMAPServer.iUserID);
            }
        }

        // smtp mode?
        else if(bSMTP)
        {
            SMTP cSMTPServer;
            cSMTPServer.bSubmission = bSubmission;

            char szError[255];
            if(!bSSL || utils->BeginTLS(cSMTPServer.ssl_ctx, szError, BEGINTLS_TIMEOUT))
            {
                if(bSSL)
                {
                    cSMTPServer.bTLSMode = true;
                    bIO_SSL = true;
                }

                cSMTPServer.Run();
                if((iTrafficIn > 0 || iTrafficOut > 0))
                    AddSession(CMP_SMTP, cSMTPServer.iUserID);
            }
        }

        // message queue?
        else if(bMSGQueue)
        {
            MSGQueue cMSGQueue;
            cMSGQueue.Run();
        }

        // list plugins
        else if(bListPlugins)
        {
            PluginMgr->ShowPluginList();
        }

        // http mode?
        else if(bHTTP)
        {
            HTTP cHTTPServer;
            cHTTPServer.Run();
        }

        // dump config
        else if(bDumpConfig)
        {
            cfg->Dump();
        }

        // uninit plugins
        PLUGIN_FUNCTION(UnInit);

        // clean up
        delete PluginMgr;
        delete cfg;
        delete db;
        if(fpBMSLog != NULL)
            fclose(fpBMSLog);
        utils->UnInit();
        delete utils;

#ifdef WIN32
        WSACleanup();
#endif
    }
    catch(Core::Exception &e)
    {
        bool bDetails = bDebug || PeerLocalOrUnknown();

        if(bSMTP)
            e.SMTPOutput(bDetails);
        else if(bPOP3)
            e.POP3Output(bDetails);
        else if(bIMAP)
            e.IMAPOutput(bDetails);
        else
            e.Output();

#ifndef WIN32
        openlog("b1gMailServer", LOG_PID, LOG_MAIL);
        syslog(LOG_ERR, "Exception while running b1gMailServer: %s%s%s",
                e.strPart.c_str(),
                e.strPart.length() == 0 ? "" : ": ",
                e.strError.c_str());
        closelog();
#endif

        exit(1);
    }

    return(iResult);
}
