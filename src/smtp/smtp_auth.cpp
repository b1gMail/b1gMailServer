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

#include <smtp/smtp.h>

/*
 * AUTH command
 */
void SMTP::Auth(char *szLine)
{
    db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("[%s] AUTH",
        this->strPeer.c_str()));

    char szAuthMethod[17];

    // check for EHLO
    if(!this->bEhlo || atoi(cfg->Get("smtp_auth_enabled")) != 1)
    {
        this->Error(500, "5.5.1 Unrecognized command");
        return;
    }

    // check state
    if(this->iState == SMTP_STATE_MAIL)
    {
        this->Error(503, "5.5.1 Command not allowed during mail transaction");
        return;
    }

    // already authenticated?
    if(this->bAuthenticated)
    {
        this->Error(503, "5.5.1 Already authenticated");
        return;
    }

    // scan auth method
    if(sscanf(szLine, "%*4s %16s", szAuthMethod) == 1)
    {
        string strUser = "", strPassword = "";

        // PLAIN auth
        if(strcasecmp(szAuthMethod, "plain") == 0)
        {
            char szAuthString[256];

            if(sscanf(szLine, "%*4s %*16s %255s", szAuthString) != 1)
            {
                printf("334 \r\n");
                if(fgets(szAuthString, 255, stdin) == NULL)
                {
                    this->Error(501, "Invalid PLAIN authentication string");
                    return;
                }
            }

            int iDecodedAuthStringLen = 0;
            char *szDecodedAuthString = utils->Base64Decode(szAuthString, false, &iDecodedAuthStringLen);

            if(szDecodedAuthString == NULL)
            {
                this->Error(501, "Invalid PLAIN authentication string");
                return;
            }
            else
            {
                // user
                if(iDecodedAuthStringLen > (int)strlen(szDecodedAuthString) + 4)
                {
                    char *szUser = szDecodedAuthString + strlen(szDecodedAuthString) + 1;

                    if(strlen(szUser) > 0)
                    {
                        strUser = szUser;

                        if(iDecodedAuthStringLen > (int)strlen(szDecodedAuthString) + 1 + (int)strlen(szUser) + 2)
                        {
                            char *szPass = szUser + strlen(szUser) + 1;

                            if(strlen(szPass) > 0)
                                strPassword = szPass;
                        }
                    }
                }

                free(szDecodedAuthString);
            }
        }

        // LOGIN auth
        else if(strcasecmp(szAuthMethod, "login") == 0)
        {
            char szAuthBuffer[256] = { 0 }, *szAuthBufferDecoded;

            bool bFirstRound = false;

            if(sscanf(szLine, "%*4s %*16s %255s", szAuthBuffer) == 1)
            {
                bFirstRound = true;
            }
            else
            {
                printf("334 VXNlcm5hbWU6\r\n");
                if(fgets(szAuthBuffer, 255, stdin) != NULL)
                    bFirstRound = true;
            }

            if(bFirstRound)
            {
                szAuthBufferDecoded = utils->Base64Decode(szAuthBuffer);
                if(szAuthBufferDecoded != NULL)
                {
                    strUser = szAuthBufferDecoded;
                    free(szAuthBufferDecoded);

                    db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("[%s] LOGIN auth user input: %s = %s",
                        this->strPeer.c_str(),
                        szAuthBuffer,
                        strUser.c_str()));

                    printf("334 UGFzc3dvcmQ6\r\n");
                    if(fgets(szAuthBuffer, 255, stdin) != NULL)
                    {
                        szAuthBufferDecoded = utils->Base64Decode(szAuthBuffer);
                        if(szAuthBufferDecoded != NULL)
                        {
                            strPassword = szAuthBufferDecoded;
                            free(szAuthBufferDecoded);

                            db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("[%s] LOGIN auth password input: %s = %s",
                                this->strPeer.c_str(),
                                szAuthBuffer,
                                strPassword.c_str()));
                        }
                    }
                }
            }
        }

        // unknown auth method
        else
        {
            this->Error(504, "Unrecognized authentication method");
            return;
        }

        // perform authentication
        int iLastLogin = 0;
        bool bHaveSendStats = strcmp(cfg->Get("enable_sendstats"), "1") == 0;
        int iUserID = utils->LookupUser(strUser.c_str(), true), iGroupID = 0;
        if(iUserID > 0)
        {
            MYSQL_ROW row;
            MySQL_Result *res = NULL;
            if(strcmp(cfg->Get("salted_passwords"), "1") == 0)
            {
                if(bHaveSendStats)
                {
                    res = db->Query("SELECT bm60_gruppen.`anlagen`,bm60_gruppen.`send_limit_count`,bm60_gruppen.`send_limit_time`,bm60_users.`today_sent`,bm60_users.`today_key`,bm60_gruppen.`max_recps`,bm60_gruppen.`id`,bm60_users.`lastlogin`,bm60_users.`last_pop3`,bm60_users.`last_imap` FROM bm60_users,bm60_gruppen WHERE bm60_users.`id`='%d' AND bm60_users.`passwort`=MD5(CONCAT(MD5('%q'),bm60_users.passwort_salt)) AND (bm60_users.locked='no' AND bm60_users.gesperrt='no') AND bm60_gruppen.`id`=bm60_users.`gruppe` AND bm60_gruppen.`smtp`='yes'",
                        iUserID,
                        strPassword.c_str());
                }
                else
                {
                    res = db->Query("SELECT bm60_gruppen.`anlagen`,bm60_gruppen.`send_limit`,86400,bm60_users.`today_sent`,bm60_users.`today_key`,bm60_gruppen.`max_recps`,bm60_gruppen.`id`,bm60_users.`lastlogin`,bm60_users.`last_pop3`,bm60_users.`last_imap` FROM bm60_users,bm60_gruppen WHERE bm60_users.`id`='%d' AND bm60_users.`passwort`=MD5(CONCAT(MD5('%q'),bm60_users.passwort_salt)) AND (bm60_users.locked='no' AND bm60_users.gesperrt='no') AND bm60_gruppen.`id`=bm60_users.`gruppe` AND bm60_gruppen.`smtp`='yes'",
                        iUserID,
                        strPassword.c_str());
                }
            }
            else
            {
                if(bHaveSendStats)
                {
                    res = db->Query("SELECT bm60_gruppen.`anlagen`,bm60_gruppen.`send_limit_count`,bm60_gruppen.`send_limit_time`,bm60_users.`today_sent`,bm60_users.`today_key`,bm60_gruppen.`max_recps`,bm60_gruppen.`id`,bm60_users.`lastlogin`,bm60_users.`last_pop3`,bm60_users.`last_imap` FROM bm60_users,bm60_gruppen WHERE bm60_users.`id`='%d' AND bm60_users.`passwort`=MD5('%q') AND (bm60_users.locked='no' AND bm60_users.gesperrt='no') AND bm60_gruppen.`id`=bm60_users.`gruppe` AND bm60_gruppen.`smtp`='yes'",
                        iUserID,
                        strPassword.c_str());
                }
                else
                {
                    res = db->Query("SELECT bm60_gruppen.`anlagen`,bm60_gruppen.`send_limit`,86400,bm60_users.`today_sent`,bm60_users.`today_key`,bm60_gruppen.`max_recps`,bm60_gruppen.`id`,bm60_users.`lastlogin`,bm60_users.`last_pop3`,bm60_users.`last_imap` FROM bm60_users,bm60_gruppen WHERE bm60_users.`id`='%d' AND bm60_users.`passwort`=MD5('%q') AND (bm60_users.locked='no' AND bm60_users.gesperrt='no') AND bm60_gruppen.`id`=bm60_users.`gruppe` AND bm60_gruppen.`smtp`='yes'",
                        iUserID,
                        strPassword.c_str());
                }
            }
            if(res->NumRows() > 0)
            {
                row = res->FetchRow();

                if(!bHaveSendStats)
                {
                    if(atoi(row[1]) > 0)
                    {
                        this->iMessageCountLimit = (int)( 86400 / atoi(row[1]) );

                        char szTodayKey[7];
                        time_t iNow = time(NULL);
#ifdef WIN32
                        struct tm *cNow = localtime(&iNow);
#else
                        struct tm _cNow, *cNow = localtime_r(&iNow, &_cNow);
#endif
                        strftime(szTodayKey, sizeof(szTodayKey), "%d%m%y", cNow);

                        if(strcmp(row[4], szTodayKey) == 0)
                        {
                            this->bTodayKeyValid = true;
                            this->iMessageCountLimit -= atoi(row[3]);
                        }
                    }
                    else
                        this->iMessageCountLimit = -1;
                }
                else
                {
                    this->iMessageCountLimit = -1;
                    this->iSMTPLimitCount = atoi(row[1]);
                    this->iSMTPLimitTime = atoi(row[2]);
                }

                this->iRecipientLimit = atoi(row[5]);
                this->iMessageSizeLimit = atoi(row[0]);
                iGroupID = atoi(row[6]);
                iLastLogin = atoi(row[7]);

                int iLastPOP3 = atoi(row[8]), iLastIMAP = atoi(row[9]);
                this->iLastFetch = MAX(iLastPOP3, iLastIMAP);

                this->bAuthenticated = true;
                this->iUserID = iUserID;
                this->strAuthUser = strUser;
                this->strAuthMethod = szAuthMethod;
            }
            delete res;
        }

        bool bWebLoginIssue = false;
        if(this->bAuthenticated)
        {
            int iWebLoginInterval = atoi(utils->GetGroupOption(this->iUserID, "weblogin_interval", "0").c_str());
            bool bRequireWebLogin = utils->GetGroupOption(this->iUserID, "require_weblogin", "1") == "1";

            if(bRequireWebLogin && iLastLogin == 0)
            {
                bWebLoginIssue = true;
                this->bAuthenticated = false;
                this->strAuthMethod = "";
            }
            else if(bRequireWebLogin && iWebLoginInterval > 0 && iLastLogin < time(NULL)-iWebLoginInterval*86400)
            {
                bWebLoginIssue = true;
                this->bAuthenticated = false;
                this->strAuthMethod = "";
            }
        }

        // response
        if(this->bAuthenticated)
        {
            // get group options
            this->iSenderCheckMode  = SMTP_SENDERCHECK_MAILFROM;
            if(!bHaveSendStats)
            {
                this->iSMTPLimitCount   = 30;
                this->iSMTPLimitTime    = 60;
            }
            MYSQL_ROW row;
            MySQL_Result *res = db->Query("SELECT `key`,`value` FROM bm60_groupoptions WHERE `module`='B1GMailServerAdmin' AND `key` IN ('smtplimit_count','smtplimit_time','smtp_sendercheck') AND `gruppe`=%d",
                                          iGroupID);
            while((row = res->FetchRow()))
            {
                if(strcmp(row[0], "smtplimit_count") == 0 && !bHaveSendStats)
                {
                    sscanf(row[1], "%d", &this->iSMTPLimitCount);
                }
                else if(strcmp(row[0], "smtplimit_time") == 0 && !bHaveSendStats)
                {
                    sscanf(row[1], "%d", &this->iSMTPLimitTime);
                }
                else if(strcmp(row[0], "smtp_sendercheck") == 0)
                {
                    if(strcmp(row[1], "mailfrom") == 0)
                        this->iSenderCheckMode  = SMTP_SENDERCHECK_MAILFROM;
                    else if(strcmp(row[1], "full") == 0)
                        this->iSenderCheckMode  = SMTP_SENDERCHECK_FULL;
                    else
                        this->iSenderCheckMode  = SMTP_SENDERCHECK_NO;
                }
            }
            delete res;

            // update last smtp login time (may fail with older b1gMail versions)
            try
            {
                db->Query("UPDATE bm60_users SET `last_smtp`=%d WHERE `id`=%d",
                         (int)time(NULL),
                         iUserID);
            }
            catch(Core::Exception ex) { }

            // plugins
            FOR_EACH_PLUGIN(Plugin)
                Plugin->OnLoginUser(this->iUserID, strUser.c_str(), strPassword.c_str());
            END_FOR_EACH()

            db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("Logged in as %s (%d)",
                (char *)strUser.c_str(),
                iUserID));
            printf("235 Authentication successful\r\n");
        }
        else
        {
            this->iUserID = 0;

            if(bWebLoginIssue)
            {
                db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("Login attempt as %s failed (web login requirement violation)",
                    (char *)strUser.c_str()));
                this->Error(535, "Please login to your webmail account first");
            }
            else
            {
                db->Log(CMP_SMTP, PRIO_NOTE, utils->PrintF("Login attempt as %s failed",
                    (char *)strUser.c_str()));
                this->Error(535, "Authentication failed");
            }

            if(this->iPeerOrigin != SMTP_PEER_ORIGIN_NOGREYANDBAN
                && utils->Failban_LoginFailed(PEER_IP(), FAILBAN_SMTPLOGIN))
                this->bBanned = true;
        }
    }
    else
    {
        this->Error(501, "5.5.4 Usage: AUTH [method]");
    }
}

