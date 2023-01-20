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

/*
 * Login user
 */
void IMAP::DoLogin()
{
    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Logged in as %s",
        this->strPeer.c_str(),
        this->strUser.c_str()));
    this->iState = IMAP_STATE_AUTHENTICATED;
    this->cFolders = IMAPHelper::FetchFolders(db, this->iUserID);
}

/*
 * LOGIN command
 */
void IMAP::Login(char *szLine)
{
    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] LOGIN",
        this->strPeer.c_str()));

    string strUser;
    string strPass;
    IMAPArgList vArgs = IMAPHelper::ParseLine(szLine);

    if(vArgs.size() == 4)
    {
        strUser = vArgs.at(2);
        strPass = vArgs.at(3);

        // check user+pw
        bool bWebLoginIssue = false;
        int iLastLogin = 0;
        int iAlias = utils->GetAlias(strUser.c_str());
        MySQL_Result *res = NULL;
        if(strcmp(cfg->Get("salted_passwords"), "1") == 0)
        {
            if(strcmp(cfg->Get("user_space_add"), "1") == 0)
            {
                res = db->Query("SELECT bm60_users.id,bm60_users.email,(bm60_gruppen.storage+bm60_users.mailspace_add) AS storage,bm60_users.mailbox_generation,bm60_users.mailbox_structure_generation,bm60_users.`lastlogin` FROM bm60_users,bm60_gruppen WHERE (bm60_users.passwort=MD5(CONCAT(MD5('%q'),bm60_users.passwort_salt))) AND (bm60_gruppen.id=bm60_users.gruppe) AND bm60_gruppen.imap='yes' AND (bm60_users.locked='no' AND bm60_users.gesperrt='no') AND (bm60_users.email='%q' OR bm60_users.id='%d')",
                    strPass.c_str(),
                    strUser.c_str(),
                    iAlias);
            }
            else
            {
                res = db->Query("SELECT bm60_users.id,bm60_users.email,bm60_gruppen.storage,bm60_users.mailbox_generation,bm60_users.mailbox_structure_generation,bm60_users.`lastlogin` FROM bm60_users,bm60_gruppen WHERE (bm60_users.passwort=MD5(CONCAT(MD5('%q'),bm60_users.passwort_salt))) AND (bm60_gruppen.id=bm60_users.gruppe) AND bm60_gruppen.imap='yes' AND (bm60_users.locked='no' AND bm60_users.gesperrt='no') AND (bm60_users.email='%q' OR bm60_users.id='%d')",
                    strPass.c_str(),
                    strUser.c_str(),
                    iAlias);
            }
        }
        else
        {
            res = db->Query("SELECT bm60_users.id,bm60_users.email,bm60_gruppen.storage,bm60_users.mailbox_generation,bm60_users.mailbox_structure_generation,bm60_users.`lastlogin` FROM bm60_users,bm60_gruppen WHERE (bm60_users.passwort=MD5('%q')) AND (bm60_gruppen.id=bm60_users.gruppe) AND bm60_gruppen.imap='yes' AND (bm60_users.locked='no' AND bm60_users.gesperrt='no') AND (bm60_users.email='%q' OR bm60_users.id='%d')",
                strPass.c_str(),
                strUser.c_str(),
                iAlias);
        }
        MYSQL_ROW row;
        bool bOk = false;
        while((row = res->FetchRow()))
        {
            iLastLogin = atoi(row[5]);

            int iWebLoginInterval = atoi(utils->GetGroupOption(atoi(row[0]), "weblogin_interval", "0").c_str());
            bool bRequireWebLogin = utils->GetGroupOption(atoi(row[0]), "require_weblogin", "1") == "1";

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
            else
            {
                bOk = true;
                this->iGeneration = atoi(row[3]);
                this->iStructureGeneration = atoi(row[4]);
                this->iSizeLimit = 0;
                sscanf(row[2], "%llu", &this->iSizeLimit);
                this->iUserID = atoi(row[0]);
                this->strUser = row[1];

                if(atoi(cfg->Get("user_choseimaplimit")) == 1)
                    this->iLimit = atoi(utils->GetUserPref(this->iUserID, "imapLimit", cfg->Get("imap_limit")).c_str());
                else
                    this->iLimit = atoi(cfg->Get("imap_limit"));
            }
        }
        delete res;

        // tell user what happened
        if(bOk)
        {
            this->DoLogin();

            db->Query("UPDATE bm60_users SET last_imap=UNIX_TIMESTAMP() WHERE id='%d'",
                      this->iUserID);
            this->iLastLoginUpdate = time(NULL);

            // plugins
            FOR_EACH_PLUGIN(Plugin)
                Plugin->OnLoginUser(this->iUserID, strUser.c_str(), strPass.c_str());
            END_FOR_EACH()

            printf("%s OK LOGIN completed\r\n",
                this->szTag);
        }
        else
        {
            this->iBadTries++;
            utils->MilliSleep(3*1000);

            if(bWebLoginIssue)
            {
                db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Invalid login attempt as %s (web login requirement violated)",
                    this->strPeer.c_str(),
                    strUser.c_str()));
                printf("%s NO LOGIN failed: Please login to your webmail account first\r\n",
                       this->szTag);
            }
            else
            {
                db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Invalid login attempt as %s",
                    this->strPeer.c_str(),
                    strUser.c_str()));
                printf("%s NO LOGIN failed: Invalid login\r\n",
                       this->szTag);
            }


            if(utils->Failban_LoginFailed(PEER_IP(), FAILBAN_IMAPLOGIN))
                this->bBanned = true;
        }
    }
    else
    {
        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Invalid parameters for LOGIN",
            this->strPeer.c_str()));

        printf("* BAD Syntax: LOGIN [user] [password]\r\n");
    }
}

/*
 * AUTHENTICATE command
 */
void IMAP::Authenticate(char *szLine)
{
    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] AUTHENTICATE",
        this->strPeer.c_str()));

    IMAPArgList vArgs = IMAPHelper::ParseLine(szLine);
    string strMethod;

    if(vArgs.size() == 3 || vArgs.size() == 4)
    {
        strMethod = vArgs.at(2);

        if(strcasecmp(strMethod.c_str(), "plain") != 0
            && strcasecmp(strMethod.c_str(), "login") != 0)
        {
            printf("%s NO AUTHENTICATE failed: Unknown authentication mechanism\r\n",
                this->szTag);
            return;
        }

        // read user and password
        string strUser, strPassword, strAuthInput;
        char szBuffer[IMAP_MAXLINE+1], *szDecoded;

        if(strcasecmp(strMethod.c_str(), "plain") == 0)
        {
            if(vArgs.size() == 4)
            {
                strAuthInput = vArgs.at(3);
            }
            else
            {
                printf("+\r\n");
                if(fgets(szBuffer, IMAP_MAXLINE, stdin) == NULL)
                {
                    return;
                }
                else
                {
                    strAuthInput = szBuffer;
                }
            }

            db->Log(CMP_IMAP, PRIO_DEBUG, utils->PrintF("[%s] PLAIN auth token: %s",
                                                     this->strPeer.c_str(),
                                                     strAuthInput.c_str()));

            utils->ParseIMAPAuthPlain(strAuthInput, strUser, strPassword);

            db->Log(CMP_IMAP, PRIO_DEBUG, utils->PrintF("[%s] PLAIN auth: user=%s pass=%s",
                                                     this->strPeer.c_str(),
                                                     strUser.c_str(),
                                                     strPassword.c_str()));
        }
        else if(strcasecmp(strMethod.c_str(), "login") == 0)
        {
            printf("+ VXNlcm5hbWU=\r\n");
            if(fgets(szBuffer, IMAP_MAXLINE, stdin) == NULL)
            {
                return;
            }
            else
            {
                szDecoded = utils->Base64Decode(szBuffer);
                if(szDecoded != NULL)
                {
                    strUser = szDecoded;
                    free(szDecoded);
                }
            }

            printf("+ UGFzc3dvcmQ=\r\n");
            if(fgets(szBuffer, IMAP_MAXLINE, stdin) == NULL)
            {
                return;
            }
            else
            {
                szDecoded = utils->Base64Decode(szBuffer);
                if(szDecoded != NULL)
                {
                    strPassword = szDecoded;
                    free(szDecoded);
                }
            }
        }

        // check them
        bool bWebLoginIssue = false;
        int iLastLogin = 0;
        int iAlias = utils->GetAlias(strUser.c_str());
        MySQL_Result *res = NULL;
        if(strcmp(cfg->Get("salted_passwords"), "1") == 0)
        {
            if(strcmp(cfg->Get("user_space_add"), "1") == 0)
            {
                res = db->Query("SELECT bm60_users.id,bm60_users.email,(bm60_gruppen.storage+bm60_users.mailspace_add) AS storage,bm60_users.mailbox_generation,bm60_users.mailbox_structure_generation,bm60_users.`lastlogin` FROM bm60_users,bm60_gruppen WHERE (bm60_users.passwort=MD5(CONCAT(MD5('%q'),bm60_users.passwort_salt))) AND (bm60_users.locked='no' AND bm60_users.gesperrt='no') AND (bm60_gruppen.id=bm60_users.gruppe) AND bm60_gruppen.imap='yes' AND (bm60_users.email='%q' OR bm60_users.id='%d')",
                    strPassword.c_str(),
                    strUser.c_str(),
                    iAlias);
            }
            else
            {
                res = db->Query("SELECT bm60_users.id,bm60_users.email,bm60_gruppen.storage,bm60_users.mailbox_generation,bm60_users.mailbox_structure_generation,bm60_users.`lastlogin` FROM bm60_users,bm60_gruppen WHERE (bm60_users.passwort=MD5(CONCAT(MD5('%q'),bm60_users.passwort_salt))) AND (bm60_users.locked='no' AND bm60_users.gesperrt='no') AND (bm60_gruppen.id=bm60_users.gruppe) AND bm60_gruppen.imap='yes' AND (bm60_users.email='%q' OR bm60_users.id='%d')",
                    strPassword.c_str(),
                    strUser.c_str(),
                    iAlias);
            }
        }
        else
        {
            res = db->Query("SELECT bm60_users.id,bm60_users.email,bm60_gruppen.storage,bm60_users.mailbox_generation,bm60_users.mailbox_structure_generation,bm60_users.`lastlogin` FROM bm60_users,bm60_gruppen WHERE (bm60_users.passwort=MD5('%q')) AND (bm60_users.locked='no' AND bm60_users.gesperrt='no') AND (bm60_gruppen.id=bm60_users.gruppe) AND bm60_gruppen.imap='yes' AND (bm60_users.email='%q' OR bm60_users.id='%d')",
                strPassword.c_str(),
                strUser.c_str(),
                iAlias);
        }

        MYSQL_ROW row;
        bool bOk = false;
        while((row = res->FetchRow()))
        {
            iLastLogin = atoi(row[5]);

            int iWebLoginInterval = atoi(utils->GetGroupOption(atoi(row[0]), "weblogin_interval", "0").c_str());
            bool bRequireWebLogin = utils->GetGroupOption(atoi(row[0]), "require_weblogin", "1") == "1";

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
            else
            {
                bOk = true;
                this->iGeneration = atoi(row[3]);
                this->iStructureGeneration = atoi(row[4]);
                this->iSizeLimit = 0;
                sscanf(row[2], "%llu", &this->iSizeLimit);
                this->iUserID = atoi(row[0]);
                this->strUser = row[1];

                if(atoi(cfg->Get("user_choseimaplimit")) == 1)
                    this->iLimit = atoi(utils->GetUserPref(this->iUserID, "imapLimit", cfg->Get("imap_limit")).c_str());
                else
                    this->iLimit = atoi(cfg->Get("imap_limit"));
            }
        }
        delete res;

        // tell user what happened
        if(bOk)
        {
            this->DoLogin();

            db->Query("UPDATE bm60_users SET last_imap=UNIX_TIMESTAMP() WHERE id='%d'",
                      this->iUserID);
            this->iLastLoginUpdate = time(NULL);

            // plugins
            FOR_EACH_PLUGIN(Plugin)
                Plugin->OnLoginUser(this->iUserID, strUser.c_str(), strPassword.c_str());
            END_FOR_EACH()

            printf("%s OK AUTHENTICATE completed\r\n",
                this->szTag);
        }
        else
        {
            this->iBadTries++;
            utils->MilliSleep(3*1000);

            if(bWebLoginIssue)
            {
                db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Invalid login attempt as %s (web login requirement violation)",
                    this->strPeer.c_str(),
                    strUser.c_str()));
                printf("%s NO AUTHENTICATE failed: Please login to your webmail account first\r\n",
                       this->szTag);
            }
            else
            {
                db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Invalid login attempt as %s",
                    this->strPeer.c_str(),
                    strUser.c_str()));
                printf("%s NO AUTHENTICATE failed: Invalid login\r\n",
                       this->szTag);
            }

            if(utils->Failban_LoginFailed(PEER_IP(), FAILBAN_IMAPLOGIN))
                this->bBanned = true;
        }
    }
    else
    {
        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Invalid parameters for AUTHENTICATE",
            this->strPeer.c_str()));

        printf("* BAD Syntax: AUTHENTICATE [method]\r\n");
    }
}
