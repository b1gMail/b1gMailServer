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
#include <core/blobstorage.h>

/*
 * APPEND command
 */
void IMAP::Append(char *szLine)
{
    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] APPEND",
        this->strPeer.c_str()));

    IMAPArgList cArgs = IMAPHelper::ParseLine(szLine);

    if(cArgs.size() >= 4 && cArgs.size() <= 6)
    {
        string strMailboxName = IMAPHelper::StrDecode(cArgs.at(2).c_str());
        string strMessage, strDate, strFlags;
        time_t iInternalDate = 0,
            iMailDate = 0;

        // determine given values
        if(cArgs.size() == 4)
        {
            strMessage = cArgs.at(3);
        }
        else if(cArgs.size() == 5)
        {
            strMessage = cArgs.at(4);
            if(IMAPHelper::IsList(cArgs.at(3)))
                strFlags = cArgs.at(3);
            else
                strDate = cArgs.at(3);
        }
        else if(cArgs.size() == 6)
        {
            strFlags = cArgs.at(3);
            strDate = cArgs.at(4);
            strMessage = cArgs.at(5);
        }

        if(strDate.size() > 0)
            iInternalDate = IMAPHelper::ParseDate(strDate.c_str());

        if(iInternalDate == 0)
            iInternalDate = time(NULL);

        if(strMailboxName.size() > 0 && strMailboxName[strMailboxName.size()-1] == '/')
        {
            strMailboxName.erase(strMailboxName.size()-1);
        }

        // search mailbox
        IMAPFolder fFolder;
        for(int i=0; i<(int)this->cFolders.size(); i++)
        {
            db->Log(CMP_IMAP, PRIO_DEBUG, utils->PrintF("Comparing folder \"%s\" to \"%s\"",
                strMailboxName.c_str(),
                this->cFolders.at(i).strFullName.c_str()));
            if(strcasecmp(this->cFolders.at(i).strFullName.c_str(), strMailboxName.c_str()) == 0)
            {
                fFolder = this->cFolders.at(i);
                break;
            }
        }

        if(!fFolder)
        {
            db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] APPEND: Folder does not exist (%s)",
                this->strPeer.c_str(),
                strMailboxName.c_str()));

            printf("%s NO [TRYCREATE] APPEND failed: Folder does not exist\r\n",
                this->szTag);
        }
        else if(fFolder.bIntelligent)
        {
            db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] APPEND: Cannot append to intelligent folder (%s)",
                this->strPeer.c_str(),
                strMailboxName.c_str()));

            printf("%s NO APPEND failed: Cannot append to intelligent folder\r\n",
                this->szTag);
        }
        else if(strMessage.size() == 0)
        {
            db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] APPEND: Cannot append with empty message",
                this->strPeer.c_str()));

            printf("%s NO APPEND failed: Cannot append with empty message\r\n",
                this->szTag);
        }
        else
        {
            if(IMAPHelper::UserSize(db, this->iUserID) + strMessage.size() > this->iSizeLimit)
            {
                db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] APPEND: No space left in mailbox",
                    this->strPeer.c_str()));

                printf("%s NO [ALERT] APPEND failed: No space left in mailbox\r\n",
                    this->szTag);
            }
            else
            {
                bool haveBlobStorage = strcmp(cfg->Get("enable_blobstorage"), "1") == 0;

                // parse it
                Mail cMail;
                cMail.bDecode = true;

                vector<string> mailLines;
                utils->Explode(strMessage, mailLines, '\n');
                for(vector<string>::iterator lineIt = mailLines.begin(); lineIt != mailLines.end(); ++lineIt)
                {
                    if(lineIt->empty()
                        || *lineIt == "\r")
                    {
                        break;
                    }
                    cMail.Parse(lineIt->c_str());
                }
                cMail.ParseInfo();

                // parse date
                const char *szDate = cMail.GetHeader("date");
                if(szDate != NULL)
                    iMailDate = IMAPHelper::ParseRFC822Date(szDate);
                if(iMailDate == 0)
                    iMailDate = iInternalDate;

                // write to database
                int iMailID = 0, blobStorage = BMBLOBSTORAGE_SEPARATEFILES;
                if (haveBlobStorage)
                {
                    blobStorage = atoi(cfg->Get("blobstorage_provider"));
                    db->Query("INSERT INTO bm60_mails(userid,"
                        "betreff,"
                        "von,"
                        "an,"
                        "cc,"
                        "folder,"
                        "datum,"
                        "trashstamp,"
                        "priority,"
                        "fetched,"
                        "msg_id,"
                        "flags,"
                        "size,"
                        "blobstorage) "
                        "VALUES ('%d','%q','%q','%q','%q','%d','%u',0,'%q','%u','%s','%d','%d','%d')",
                        this->iUserID,
                        HVAL("subject", "(no subject)"),
                        HVAL("from", ""),
                        HVAL("to", this->strUser.c_str()),
                        HVAL("cc", ""),
                        fFolder.iID,
                        static_cast<unsigned long>(iMailDate),
                        cMail.strPriority.empty() ? "normal" : cMail.strPriority.c_str(),
                        static_cast<unsigned long>(iInternalDate),
                        HVAL("message-id", "0"),
                        0,
                        (int)strMessage.size(),
                        blobStorage);
                    iMailID = (int)db->InsertId();
                }
                else
                {
                    db->Query("INSERT INTO bm60_mails(userid,"
                        "betreff,"
                        "von,"
                        "an,"
                        "cc,"
                        "body,"
                        "folder,"
                        "datum,"
                        "trashstamp,"
                        "priority,"
                        "fetched,"
                        "msg_id,"
                        "flags,"
                        "size) "
                        "VALUES ('%d','%q','%q','%q','%q','%q','%d','%u',0,'%q','%u','%s','%d','%d')",
                        this->iUserID,
                        HVAL("subject", "(no subject)"),
                        HVAL("from", ""),
                        HVAL("to", this->strUser.c_str()),
                        HVAL("cc", ""),
                        strcmp(cfg->Get("storein"), "db") == 0 && strMessage.size() <= 1024 * 1024 ? strMessage.c_str() : "file",
                        fFolder.iID,
                        static_cast<unsigned long>(iMailDate),
                        cMail.strPriority.empty() ? "normal" : cMail.strPriority.c_str(),
                        static_cast<unsigned long>(iInternalDate),
                        HVAL("message-id", "0"),
                        0,
                        (int)strMessage.size());
                    iMailID = (int)db->InsertId();
                }

                IMAPHelper::IncGeneration(db, iUserID, 1, 0);

                // save message data
                BlobStorageProvider *storage = utils->CreateBlobStorageProvider(blobStorage, iUserID);
                if (storage != NULL)
                {
                    storage->storeBlob(BMBLOB_TYPE_MAIL, iMailID, strMessage);
                    delete storage;
                }

                // update space usage
                db->Query("UPDATE bm60_users SET mailspace_used=mailspace_used+'%d' WHERE id='%d'",
                    (int)strMessage.size(),
                    this->iUserID);

                // flag message
                IMAPHelper::FlagMessage(db,
                    strFlags.empty() ? FLAG_UNREAD : IMAPHelper::FlagMaskFromList(strFlags),
                    iMailID,
                    this->iUserID);

                // post event
                utils->PostEvent(this->iUserID, BMS_EVENT_STOREMAIL, iMailID);

                // re-read messages, if selected
                if(this->iState == IMAP_STATE_SELECTED)
                {
                    this->vMessages = IMAPHelper::FetchMessages(db, this->iSelected, this->iUserID, this->iLimit, this->bReadonly);
                }

                printf("%s OK APPEND completed\r\n",
                    this->szTag);
            }
        }
    }
    else
    {
        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Invalid parameters for APPEND",
            this->strPeer.c_str()));

        printf("* BAD Syntax: APPEND [mailboxname] ([flags]) ([date]) [message]\r\n");
    }
}

/*
 * SELECT/EXAMINE command
 */
void IMAP::Select(char *szLine, bool bEXAMINE)
{
    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] %s",
        this->strPeer.c_str(),
        bEXAMINE ? "EXAMINE" : "SELECT"));

    IMAPArgList cArgs = IMAPHelper::ParseLine(szLine);

    if(cArgs.size() == 3)
    {
        string strMailboxName = IMAPHelper::StrDecode(cArgs.at(2).c_str());

        if(strMailboxName.size() > 0 && strMailboxName[strMailboxName.size()-1] == '/')
        {
            strMailboxName.erase(strMailboxName.size()-1);
        }

        // search mailbox
        IMAPFolder fFolder;
        for(int i=0; i<(int)this->cFolders.size(); i++)
        {
            if(strcasecmp(this->cFolders.at(i).strFullName.c_str(), strMailboxName.c_str()) == 0)
            {
                fFolder = this->cFolders.at(i);
                break;
            }
        }

        if(!fFolder)
        {
            db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] %s: Folder does not exist (%s)",
                this->strPeer.c_str(),
                bEXAMINE ? "EXAMINE" : "SELECT",
                strMailboxName.c_str()));

            printf("%s NO %s failed: Folder does not exist\r\n",
                this->szTag,
                bEXAMINE ? "EXAMINE" : "SELECT");
        }
        else
        {
            // close open mailbox
            if(this->iState == IMAP_STATE_SELECTED)
            {
                if(this->bAutoExpunge)
                    this->Expunge(true);
                this->iSelected = 0;
            }

            this->iState = IMAP_STATE_SELECTED;
            this->bReadonly = bEXAMINE;
            this->iSelected = fFolder.iID;

            int iUnseen;

            // read messages
            this->vMessages = IMAPHelper::FetchMessages(db, this->iSelected, this->iUserID, this->iLimit, this->bReadonly);
            IMAPHelper::GetGeneration(db, this->iUserID, &this->iGeneration, NULL);

            // print statistics
            printf("* %d EXISTS\r\n",
                this->vMessages.size());
            printf("* %d RECENT\r\n",
                IMAPHelper::CountNotFlaggedMails(this->vMessages, FLAG_SEEN));

            printf("* FLAGS (\\Answered \\Deleted \\Seen \\Draft \\Recent \\Flagged)\r\n");

            if((iUnseen = IMAPHelper::FirstUnseen(this->vMessages)) != -1)
                printf("* OK [UNSEEN %d] Message %d is first unseen\r\n",
                    iUnseen,
                    iUnseen);
            printf("* OK [UIDVALIDITY 1] UIDs valid\r\n");
            int iNextUID = IMAPHelper::GetLastUID(this->vMessages) + 1;
            printf("* OK [UIDNEXT %d] %d is the next UID\r\n",
                iNextUID, iNextUID);
            printf("* OK [PERMANENTFLAGS (\\Answered \\Deleted \\Seen \\Draft \\Recent \\Flagged)] Limited\r\n");

            db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] %s: Opened folder %d",
                this->strPeer.c_str(),
                bEXAMINE ? "EXAMINE" : "SELECT",
                this->iSelected));

            printf("%s OK %s %s completed\r\n",
                this->szTag,
                bEXAMINE || fFolder.bIntelligent ? "[READ-ONLY]" : "[READ-WRITE]",
                bEXAMINE ? "EXAMINE" : "SELECT");
        }
    }
    else
    {
        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Invalid parameters for %s",
            this->strPeer.c_str(),
            bEXAMINE ? "EXAMINE" : "SELECT"));

        printf("* BAD Syntax: %s [mailboxname]\r\n",
            bEXAMINE ? "EXAMINE" : "SELECT");
    }
}

/*
 * GETQUOTA command
 */
void IMAP::GetQuota(char *szLine)
{
    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] GETQUOTA",
        this->strPeer.c_str()));

    IMAPArgList cArgs = IMAPHelper::ParseLine(szLine);

    if(cArgs.size() == 3)
    {
        if(strcasecmp(cArgs.at(2).c_str(), this->strUser.c_str()) == 0)
        {
            unsigned long long iSpaceUsed =  IMAPHelper::UserSize(db, this->iUserID) / 1024,
                iSpaceLimit = this->iSizeLimit / 1024;

            printf("* QUOTA %s (STORAGE %llu %llu)\r\n",
                strUser.c_str(),
                iSpaceUsed,
                iSpaceLimit);
            printf("%s OK GETQUOTA completed\r\n",
                this->szTag);
        }
        else
        {
            printf("%s NO Permission denied\r\n",
                this->szTag);
        }
    }
    else
    {
        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Invalid parameters for GETQUOTAROOT",
            this->strPeer.c_str()));

        printf("* BAD Syntax: GETQUOTAROOT [mailboxname]\r\n");
    }
}

/*
 * GETQUOTAROOT command
 */
void IMAP::GetQuotaRoot(char *szLine)
{
    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] GETQUOTAROOT",
        this->strPeer.c_str()));

    IMAPArgList cArgs = IMAPHelper::ParseLine(szLine);

    if(cArgs.size() == 3)
    {
        unsigned long long iSpaceUsed =  IMAPHelper::UserSize(db, this->iUserID) / 1024,
            iSpaceLimit = this->iSizeLimit / 1024;

        printf("* QUOTAROOT %s %s\r\n",
            cArgs.at(2).c_str(),
            strUser.c_str());
        printf("* QUOTA %s (STORAGE %llu %llu)\r\n",
            strUser.c_str(),
            iSpaceUsed,
            iSpaceLimit);
        printf("%s OK GETQUOTAROOT completed\r\n",
            this->szTag);
    }
    else
    {
        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Invalid parameters for GETQUOTAROOT",
            this->strPeer.c_str()));

        printf("* BAD Syntax: GETQUOTAROOT [mailboxname]\r\n");
    }
}

/*
 * SETQUOTA command
 */
void IMAP::SetQuota()
{
    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] SETQUOTA",
        this->strPeer.c_str()));
    printf("%s NO Permission denied\r\n",
        this->szTag);
}

/*
 * XAPPLEPUSHSERVICE command
 */
void IMAP::XApplePushService(char *szLine)
{
    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] XAPPLEPUSHSERVICE",
        this->strPeer.c_str()));

    string apnsTopic = utils->GetAPNSTopic();
    if(apnsTopic.empty())
    {
        printf("* BAD Internal configuration error\r\n");
        return;
    }

    IMAPArgList cArgs = IMAPHelper::ParseLine(szLine);

    IMAPArgList cMailboxes;
    string apsVersion, apsAccountId, apsDeviceId, apsDeviceToken, apsSubtopic;

    if(cArgs.size() > 3)
    {
        for(IMAPArgList::iterator it = cArgs.begin()+2; it != cArgs.end() && it != cArgs.end()-1; it += 2)
        {
            const char *szKey = it->c_str(), *szValue = (it+1)->c_str();
            if(strcasecmp(szKey, "aps-version") == 0)
                apsVersion = szValue;
            else if(strcasecmp(szKey, "aps-account-id") == 0)
                apsAccountId = szValue;
            else if(strcasecmp(szKey, "aps-device-token") == 0)
                apsDeviceToken = szValue;
            else if(strcasecmp(szKey, "aps-subtopic") == 0)
                apsSubtopic = szValue;
            else if(strcasecmp(szKey, "mailboxes") == 0 && cMailboxes.empty())
                cMailboxes = IMAPHelper::ParseList(szValue);
        }
    }

    if(apsVersion != "1" && apsVersion != "2")
    {
        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Unsupported aps-version: %s",
            this->strPeer.c_str(), apsVersion.c_str()));
        printf("* BAD Unsupported or missing aps-version\r\n");
    }
    else if(apsAccountId.empty())
    {
        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Missing aps-account-id",
            this->strPeer.c_str()));
        printf("* BAD Missing aps-account-id\r\n");
    }
    else if(apsDeviceToken.empty())
    {
        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Missing aps-device-token",
            this->strPeer.c_str()));
        printf("* BAD Missing aps-device-token\r\n");
    }
    else if(apsSubtopic != "com.apple.mobilemail")
    {
        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Unsupported aps-subtopic: %s",
            this->strPeer.c_str(), apsSubtopic.c_str()));
        printf("* BAD Unsupported or missing aps-subtopic\r\n");
    }
    else
    {
        vector<int> folderIDs;
        if(!cMailboxes.empty())
        {
            for(IMAPArgList::iterator it = cMailboxes.begin(); it != cMailboxes.end(); ++it)
            {
                string strMailboxName = IMAPHelper::StrDecode(it->c_str());

                if(strMailboxName.size() > 0 && strMailboxName[strMailboxName.size()-1] == '/')
                {
                    strMailboxName.erase(strMailboxName.size()-1);
                }

                // search mailbox
                for(int i=0; i<(int)this->cFolders.size(); i++)
                {
                    if(strcasecmp(this->cFolders.at(i).strFullName.c_str(), strMailboxName.c_str()) == 0)
                    {
                        folderIDs.push_back(this->cFolders.at(i).iID);
                        break;
                    }
                }
            }
        }

        if(folderIDs.empty())
            folderIDs.push_back(0); // INBOX

        int iSubscriptionID = 0;

        db->Query("BEGIN");

        MySQL_Result *res = db->Query("SELECT `subscriptionid` FROM bm60_bms_apns_subscription WHERE `userid`=%d AND `account_id`='%q' AND `device_token`='%q' AND `subtopic`='%q'",
            this->iUserID,
            apsAccountId.c_str(),
            apsDeviceToken.c_str(),
            apsSubtopic.c_str());
        if (res->NumRows() > 0)
        {
            MYSQL_ROW row;
            while((row = res->FetchRow()))
                iSubscriptionID = atoi(row[0]);
        }
        delete res;

        if(iSubscriptionID == 0)
        {
            db->Query("INSERT INTO bm60_bms_apns_subscription(`userid`,`account_id`,`device_token`,`subtopic`) VALUES(%d,'%q','%q','%q')",
                this->iUserID,
                apsAccountId.c_str(),
                apsDeviceToken.c_str(),
                apsSubtopic.c_str());
            iSubscriptionID = db->InsertId();
        }

        db->Query("DELETE FROM bm60_bms_apns_subscription_folder WHERE `subscriptionid`=%d",
            iSubscriptionID);
        for(vector<int>::iterator it = folderIDs.begin(); it != folderIDs.end(); ++it)
        {
            db->Query("REPLACE INTO bm60_bms_apns_subscription_folder(`folderid`,`subscriptionid`) VALUES(%d,%d)",
                *it,
                iSubscriptionID);
        }

        db->Query("COMMIT");

        db->Log(CMP_IMAP, PRIO_DEBUG, utils->PrintF("Created subscription #%d with %d folders, returning aps-topic=<%s>",
            iSubscriptionID,
            folderIDs.size(),
            apnsTopic.c_str())),

        printf("* XAPPLEPUSHSERVICE aps-version \"%s\" aps-topic \"%s\"\r\n",
            apsVersion.c_str(),
            apnsTopic.c_str());
        printf("%s OK XAPPLEPUSHSERVICE completed.\r\n",
            this->szTag);
    }
}

/*
 * IDLE command
 */
void IMAP::Idle()
{
    int iTimeout = atoi(cfg->Get("imap_timeout"));
    bool bCloseMySQL = atoi(cfg->Get("imap_idle_mysqlclose")) == 1;

    if(iTimeout > 0)
        alarm((unsigned int)iTimeout);

    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] IDLE",
        this->strPeer.c_str()));

    printf("+ idling\r\n");

    // read mailbox generation
    int iPollInterval = atoi(cfg->Get("imap_idle_poll")),
        iCurrentGeneration = this->iGeneration,
        iCurrentStructureGeneration = this->iStructureGeneration;
    fd_set fdSet;

#ifdef WIN32
    SOCKET sSocket = (SOCKET)GetStdHandle(STD_INPUT_HANDLE);
#else
    int sSocket = fileno(stdin);
#endif

    // poll loop
    bool bContinue = true;
    while(!feof(stdin)
        && !this->bTimeout
        && !this->bQuit
        && bContinue)
    {
        bool bHaveInput = false;
        char szInput[IMAP_MAXLINE+1];
        memset(szInput, 0, IMAP_MAXLINE+1);

        if(bCloseMySQL)
            db->TempClose();

        // add stdin to fd set
        FD_ZERO(&fdSet);
        FD_SET(sSocket, &fdSet);

        // set timeout
        struct timeval timeout;
        timeout.tv_sec = iPollInterval;
        timeout.tv_usec = 0;

        bool sslPending = bIO_SSL && SSL_pending(ssl) > 0;

        // select
        if(!sslPending && select(sSocket+1, &fdSet, NULL, NULL, &timeout) < 0)
        {
            this->bQuit = true;
            bContinue = false;

            db->Log(CMP_IMAP, PRIO_DEBUG, utils->PrintF("[%s] select() failed in IDLE",
                                                this->strPeer.c_str()));
            return;
        }

        // input => stop idling
        if(sslPending || FD_ISSET(sSocket, &fdSet))
        {
            if(fgets(szInput, IMAP_MAXLINE, stdin) == NULL)
                break;
            bHaveInput = true;
        }

        // stop?
        if(!bContinue)
            break;

        // input?
        if(bHaveInput)
        {
            db->Log(CMP_IMAP, PRIO_DEBUG, utils->PrintF("[%s] Data during idle: %s",
                                                        this->strPeer.c_str(),
                                                        szInput));
            break;
        }

        // no input => poll, continue
        else
        {
            db->Log(CMP_IMAP, PRIO_DEBUG, utils->PrintF("[%s] Polling mailbox generation",
                this->strPeer.c_str()));

            // new mailbox generation?
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
        }
    }

    if(!feof(stdin)
        && !this->bTimeout
        && !this->bQuit)
    {
        db->Log(CMP_IMAP, PRIO_DEBUG, utils->PrintF("[%s] IDLE done",
                                                    this->strPeer.c_str()));
        printf("%s OK IDLE terminated\r\n",
            this->szTag);
    }
    else
    {
        db->Log(CMP_IMAP, PRIO_DEBUG, utils->PrintF("[%s] IDLE aborted because of timeout / connection loss",
                                                    this->strPeer.c_str()));
    }
}

/*
 * RENAME command
 */
void IMAP::Rename(char *szLine)
{
    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] RENAME",
        this->strPeer.c_str()));

    IMAPArgList cArgs = IMAPHelper::ParseLine(szLine);

    if(cArgs.size() == 4)
    {
        string strMailboxName = IMAPHelper::StrDecode(cArgs.at(2).c_str());
        string strNewMailboxName = IMAPHelper::StrDecode(cArgs.at(3).c_str());

        if(strMailboxName.size() > 0 && strMailboxName[strMailboxName.size()-1] == '/')
        {
            strMailboxName.erase(strMailboxName.size()-1);
        }
        if(strNewMailboxName.size() > 0 && strNewMailboxName[strNewMailboxName.size()-1] == '/')
        {
            strNewMailboxName.erase(strNewMailboxName.size()-1);
        }

        // search mailbox
        IMAPFolder fFolder;
        for(int i=0; i<(int)this->cFolders.size(); i++)
        {
            if(strcasecmp(this->cFolders.at(i).strFullName.c_str(), strMailboxName.c_str()) == 0)
            {
                fFolder = this->cFolders.at(i);
                break;
            }
        }

        if(!fFolder || fFolder.iID <= 0)
        {
            db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] RENAME: Invalid source folder name",
                this->strPeer.c_str()));

            printf("%s NO RENAME failed: Source folder does not exist\r\n",
                this->szTag);
        }
        else
        {
            // exists?
            IMAPFolder fDestFolder;
            for(int i=0; i<(int)this->cFolders.size(); i++)
            {
                if(strcasecmp(this->cFolders.at(i).strFullName.c_str(), strNewMailboxName.c_str()) == 0)
                {
                    fDestFolder = this->cFolders.at(i);
                    break;
                }
            }

            if(fDestFolder)
            {
                db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] RENAME: Destination folder exists",
                    this->strPeer.c_str()));

                printf("%s NO RENAME failed: Destination folder exists\r\n",
                    this->szTag);
            }
            else
            {
                std::size_t lastSlashPos = strNewMailboxName.find_last_of("/");

                IMAPFolder fRefFolder;
                bool bContinue = true;

                if(lastSlashPos != string::npos)
                {
                    string strNewMailboxRef = strNewMailboxName.substr(0, lastSlashPos);
                    strNewMailboxName = strNewMailboxName.substr(lastSlashPos + 1);

                    // search ref
                    for(int i=0; i<(int)this->cFolders.size(); i++)
                    {
                        if(strcasecmp(this->cFolders.at(i).strFullName.c_str(), strNewMailboxRef.c_str()) == 0)
                        {
                            fRefFolder = this->cFolders.at(i);
                            break;
                        }
                    }

                    // ref folder exists?
                    if(!fRefFolder || (fFolder.iID != 0 && (fRefFolder.iID == fFolder.iID))
                        || fRefFolder.iID < 0)
                    {
                        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] RENAME: Invalid parent of destination folder",
                            this->strPeer.c_str()));

                        printf("%s NO RENAME failed: Invalid parent of destination folder\r\n",
                            this->szTag);
                        bContinue = false;
                    }
                }

                if(bContinue)
                {
                    if(fFolder.iID == 0)
                    {
                        // create dest mailbox, ...
                        db->Query("INSERT INTO bm60_folders(titel,userid,parent,subscribed) VALUES('%q','%d','%d',1)",
                            strNewMailboxName.c_str(),
                            this->iUserID,
                            !fRefFolder ? -1 : fRefFolder.iID);
                        int iFolderID = (int)db->InsertId();

                        // ...post events...
                        b1gMailServer::MySQL_Result *res;
                        char **row;
                        res = db->Query("SELECT `id` FROM bm60_mails WHERE folder='0' AND userid='%d'",
                                                    this->iUserID);
                        while((row = res->FetchRow()))
                            utils->PostEvent(iUserID, BMS_EVENT_MOVEMAIL, atoi(row[0]), iFolderID);
                        delete res;

                        // ...and move all mails from INBOX to it, ...
                        db->Query("UPDATE bm60_mails SET folder='%d' WHERE folder='0' AND userid='%d'",
                            iFolderID,
                            this->iUserID);

                        // ... finally inc generation
                        IMAPHelper::IncGeneration(db, iUserID, 1, 1);
                    }
                    else
                    {
                        // change title and parent of folder
                        db->Query("UPDATE bm60_folders SET titel='%q', parent='%d' WHERE id='%d'",
                            strNewMailboxName.c_str(),
                            !fRefFolder ? -1 : fRefFolder.iID,
                            fFolder.iID);
                        IMAPHelper::IncGeneration(db, iUserID, 1, 1);
                    }

                    // re-read folders
                    this->cFolders = IMAPHelper::FetchFolders(db, this->iUserID);

                    // ok
                    printf("%s OK RENAME completed\r\n",
                        this->szTag);
                }
            }
        }
    }
    else
    {
        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Invalid parameters for RENAME",
            this->strPeer.c_str()));

        printf("* BAD Syntax: RENAME [mailboxname] [newmailboxname]\r\n");
    }
}

/*
 * STATUS command
 */
void IMAP::Status(char *szLine)
{
    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] STATUS",
        this->strPeer.c_str()));

    IMAPArgList cArgs = IMAPHelper::ParseLine(szLine);

    if(cArgs.size() == 4)
    {
        string strMailboxName = IMAPHelper::StrDecode(cArgs.at(2).c_str());
        string strList = cArgs.at(3);

        if(strMailboxName.size() > 0 && strMailboxName[strMailboxName.size()-1] == '/')
        {
            strMailboxName.erase(strMailboxName.size()-1);
        }

        // search mailbox
        IMAPFolder fFolder;
        for(int i=0; i<(int)this->cFolders.size(); i++)
        {
            if(strcasecmp(this->cFolders.at(i).strFullName.c_str(), strMailboxName.c_str()) == 0)
            {
                fFolder = this->cFolders.at(i);
                break;
            }
        }

        if(!fFolder)
        {
            db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] STATUS: Invalid folder name",
                this->strPeer.c_str()));

            printf("%s NO STATUS failed: Folder does not exist\r\n",
                this->szTag);
        }
        else
        {
            if(!IMAPHelper::IsList(strList))
            {
                db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Invalid parameters for STATUS",
                    this->strPeer.c_str()));

                printf("* BAD Syntax: STATUS [mailboxname] [itemlist]\r\n");
            }
            else
            {
                IMAPArgList cList = IMAPHelper::ParseList(strList);
                IMAPMsgList vMsgs = IMAPHelper::FetchMessages(db, fFolder.iID, this->iUserID, this->iLimit, this->bReadonly);

                printf("* STATUS \"%s\" (",
                    IMAPHelper::Escape(strMailboxName.c_str()).c_str());

                for(std::size_t i=0; i < cList.size(); i++)
                {
                    int iResult = -1;
                    const char *szKey = cList.at(i).c_str();

                    if(strcasecmp(szKey, "messages") == 0)
                    {
                        // count messages
                        iResult = static_cast<int>(vMsgs.size());
                        printf("MESSAGES %d",
                            iResult);
                    }
                    else if(strcasecmp(szKey, "recent") == 0)
                    {
                        // count recent messages
                        iResult = IMAPHelper::CountNotFlaggedMails(vMsgs, FLAG_SEEN);
                        printf("RECENT %d",
                            iResult);
                    }
                    else if(strcasecmp(szKey, "uidnext") == 0)
                    {
                        iResult = IMAPHelper::GetLastUID(vMsgs) + 1;
                        printf("UIDNEXT %d",
                            iResult);
                    }
                    else if(strcasecmp(szKey, "uidvalidity") == 0)
                    {
                        printf("UIDVALIDITY 1");
                    }
                    else if(strcasecmp(szKey, "unseen") == 0)
                    {
                        iResult = IMAPHelper::CountFlaggedMails(vMsgs, FLAG_UNREAD);
                        printf("UNSEEN %d",
                            iResult);
                    }

                    if(i != cList.size()-1)
                        printf(" ");
                }

                printf(")\r\n");
                printf("%s OK STATUS completed\r\n",
                    this->szTag);
            }
        }
    }
    else
    {
        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Invalid parameters for STATUS",
            this->strPeer.c_str()));

        printf("* BAD Syntax: STATUS [mailboxname] [itemlist]\r\n");
    }
}

/*
 * DELETE command
 */
void IMAP::Delete(char *szLine)
{
    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] DELETE",
        this->strPeer.c_str()));

    IMAPArgList cArgs = IMAPHelper::ParseLine(szLine);

    if(cArgs.size() == 3)
    {
        string strMailboxName = IMAPHelper::StrDecode(cArgs.at(2).c_str());

        if(strMailboxName.size() > 0 && strMailboxName[strMailboxName.size()-1] == '/')
        {
            strMailboxName.erase(strMailboxName.size()-1);
        }

        // search mailbox
        IMAPFolder fFolder;
        for(int i=0; i<(int)this->cFolders.size(); i++)
        {
            if(strcasecmp(this->cFolders.at(i).strFullName.c_str(), strMailboxName.c_str()) == 0)
            {
                fFolder = this->cFolders.at(i);
                break;
            }
        }

        // exists?
        if(fFolder
            && fFolder.iID > 0
            && strcasecmp(fFolder.strFullName.c_str(), "INBOX") != 0
            && strcasecmp(fFolder.strFullName.c_str(), SENT) != 0
            && strcasecmp(fFolder.strFullName.c_str(), SPAM) != 0
            && strcasecmp(fFolder.strFullName.c_str(), DRAFTS) != 0
            && strcasecmp(fFolder.strFullName.c_str(), TRASH) != 0)
        {
            db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] DELETE: Folder %d deleted",
                this->strPeer.c_str(),
                fFolder.iID));

            // delete folder
            IMAPHelper::DeleteFolder(db,
                fFolder.iID,
                this->iUserID);

            // re-read folders
            this->cFolders = IMAPHelper::FetchFolders(db, this->iUserID);

            printf("%s OK DELETE completed\r\n",
                this->szTag);
        }
        else
        {
            db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] DELETE: Invalid folder name",
                this->strPeer.c_str()));

            printf("%s NO DELETE failed: Folder does not exist\r\n",
                this->szTag);
        }
    }
    else
    {
        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Invalid parameters for DELETE",
            this->strPeer.c_str()));

        printf("* BAD Syntax: DELETE [mailboxname]\r\n");
    }
}

/*
 * SUBSCRIBE/UNSUBSCRIBE command
 */
void IMAP::Subscribe(char *szLine, bool bUNSUBSCRIBE)
{
    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] %s",
        this->strPeer.c_str(),
        bUNSUBSCRIBE ? "UNSUBSCRIBE" : "SUBSCRIBE"));

    IMAPArgList cArgs = IMAPHelper::ParseLine(szLine);

    if(cArgs.size() == 3)
    {
        string strMailboxName = IMAPHelper::StrDecode(cArgs.at(2).c_str());

        if(strMailboxName.size() > 0 && strMailboxName[strMailboxName.size()-1] == '/')
        {
            strMailboxName.erase(strMailboxName.size()-1);
        }

        // search mailbox
        IMAPFolder fFolder;
        for(int i=0; i<(int)this->cFolders.size(); i++)
        {
            if(strcasecmp(this->cFolders.at(i).strFullName.c_str(), strMailboxName.c_str()) == 0)
            {
                fFolder = this->cFolders.at(i);
                break;
            }
        }

        // exists?
        if(fFolder)
        {
            db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] %s: Folder %s",
                this->strPeer.c_str(),
                bUNSUBSCRIBE ? "UNSUBSCRIBE" : "SUBSCRIBE",
                bUNSUBSCRIBE ? "unsubscribed" : "subscribed"));

            db->Query("UPDATE bm60_folders SET subscribed='%d' WHERE id='%d'",
                bUNSUBSCRIBE ? 0 : 1,
                fFolder.iID);
            IMAPHelper::IncGeneration(db, iUserID, 1, 1);
            fFolder.bSubscribed = !bUNSUBSCRIBE;

            printf("%s OK %s completed\r\n",
                this->szTag,
                bUNSUBSCRIBE ? "UNSUBSCRIBE" : "SUBSCRIBE");
        }
        else
        {
            db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] %s: Folder does not exist",
                this->strPeer.c_str(),
                bUNSUBSCRIBE ? "UNSUBSCRIBE" : "SUBSCRIBE"));

            printf("%s NO %s failed: Folder does not exist\r\n",
                this->szTag,
                bUNSUBSCRIBE ? "UNSUBSCRIBE" : "SUBSCRIBE");
        }
    }
    else
    {
        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Invalid parameters for %s",
            this->strPeer.c_str(),
            bUNSUBSCRIBE ? "UNSUBSCRIBE" : "SUBSCRIBE"));

        printf("* BAD Syntax: %s [mailboxname]\r\n",
            bUNSUBSCRIBE ? "UNSUBSCRIBE" : "SUBSCRIBE");
    }
}

/*
 * CREATE command
 */
void IMAP::Create(char *szLine)
{
    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] CREATE",
        this->strPeer.c_str()));

    IMAPArgList cArgs = IMAPHelper::ParseLine(szLine);

    if(cArgs.size() == 3)
    {
        string strMailboxName = IMAPHelper::StrDecode(cArgs.at(2).c_str());
        string strRef;

        if(strMailboxName.size() > 0 && strMailboxName[strMailboxName.size()-1] == '/')
        {
            strMailboxName.erase(strMailboxName.size()-1);
        }

        // exists?
        bool bExists = false;
        for(int i=0; i<(int)this->cFolders.size(); i++)
        {
            if(strcasecmp(strMailboxName.c_str(), this->cFolders.at(i).strFullName.c_str()) == 0)
            {
                bExists = true;
                break;
            }
        }

        if(bExists)
        {
            db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] CREATE: Folder exists",
                this->strPeer.c_str()));

            printf("%s NO CREATE failed: Folder exists\r\n",
                this->szTag);
        }
        else
        {
            std::size_t lastSlashPos = strMailboxName.find_last_of("/");
            bool bContinue = true;
            IMAPFolder fParent;

            if(lastSlashPos != string::npos)
            {
                strRef = strMailboxName.substr(0, lastSlashPos);
                strMailboxName = strMailboxName.substr(lastSlashPos + 1);

                // search parent
                for(int i=0; i<(int)this->cFolders.size(); i++)
                {
                    if(strcasecmp(this->cFolders.at(i).strFullName.c_str(), strRef.c_str()) == 0)
                    {
                        fParent = this->cFolders.at(i);
                        break;
                    }
                }

                if(!fParent)
                {
                    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] CREATE: Parent folder does not exist",
                        this->strPeer.c_str()));

                    printf("%s NO CREATE failed: Invalid folder name\r\n",
                        this->szTag);
                    bContinue = false;
                }
            }

            if(bContinue)
            {
                db->Query("INSERT INTO bm60_folders(titel,userid,parent,subscribed) VALUES('%q','%d','%d',0)",
                    strMailboxName.c_str(),
                    this->iUserID,
                    !fParent ? -1 : fParent.iID);
                db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] CREATE: Folder %d created",
                    this->strPeer.c_str(),
                    db->InsertId()));
                IMAPHelper::IncGeneration(db, iUserID, 0, 1);

                this->cFolders = IMAPHelper::FetchFolders(db, this->iUserID);

                printf("%s OK CREATE completed\r\n",
                    this->szTag);
            }
        }
    }
    else
    {
        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Invalid parameters for CREATE",
            this->strPeer.c_str()));

        printf("* BAD Syntax: CREATE [mailboxname]\r\n");
    }
}

/*
 * LIST/LSUB command
 */
void IMAP::List(char *szLine, bool bLSUB)
{
    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] %s",
        this->strPeer.c_str(),
        bLSUB ? "LSUB" : "LIST"));

    IMAPArgList cArgs = IMAPHelper::ParseLine(szLine);

    if(cArgs.size() == 4)
    {
        string strRefName = IMAPHelper::StrDecode(cArgs.at(2).c_str());
        string strMailboxName = IMAPHelper::StrDecode(cArgs.at(3).c_str());     // may contain wildcards

        // empty mailbox name?
        if(strMailboxName.empty())
        {
            printf("* %s (\\NoSelect) \"/\" \"\"\r\n",
                bLSUB ? "LSUB" : "LIST");
            printf("%s OK %s completed\r\n",
                this->szTag,
                bLSUB ? "LSUB" : "LIST");
        }
        else
        {
            // build mailbox search pattern
            string strPattern = IMAPHelper::TransformWildcards(strMailboxName.c_str());
            string strSearchPattern;

            if(strRefName.empty())
            {
                strSearchPattern = string("^") + strPattern + string("$");
            }
            else
            {
                strSearchPattern = string("^") + strRefName + strPattern + string("$");
            }

            // display matching folders
            for(std::size_t i=0; i < this->cFolders.size(); i++)
            {
                if((IMAPHelper::Match(strSearchPattern.c_str(), this->cFolders.at(i).strFullName.c_str())
                    || (strcasecmp(this->cFolders.at(i).strFullName.c_str(), "INBOX") == 0
                        && IMAPHelper::Match(strSearchPattern.c_str(), "INBOX")))
                    && (bLSUB ? this->cFolders.at(i).bSubscribed : true))
                {
                    string strUTF7 = IMAPHelper::StrEncode(this->cFolders.at(i).strFullName.c_str());
                    printf("* %s (%s) \"/\" \"%s\"\r\n",
                        bLSUB ? "LSUB" : "LIST",
                        this->cFolders.at(i).strAttributes.c_str(),
                        IMAPHelper::Escape(strUTF7.c_str()).c_str());
                }
            }

            // ok
            printf("%s OK %s completed\r\n",
                this->szTag,
                bLSUB ? "LSUB" : "LIST");
        }
    }
    else
    {
        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Invalid parameters for %s",
            this->strPeer.c_str(),
            bLSUB ? "LSUB" : "LIST"));

        printf("* BAD Syntax: %s [refname] [mailboxname]\r\n",
            bLSUB ? "LSUB" : "LIST");
    }
}
