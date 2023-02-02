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

#include <msgqueue/msgqueue.h>
#include <sstream>
#include <iomanip>
#include <stdint.h>

/**
 * Release enqueued messages
 */
void MSGQueue::ReleaseMessages(vector<int> &IDs)
{
    stringstream ssIDs;

    if(IDs.size() < 1)
        return;

    for(unsigned int i=0; i<IDs.size(); i++)
    {
        if(i > 0)
            ssIDs << ",";
        ssIDs << IDs.at(i);
    }

    db->Query("UPDATE bm60_bms_queue SET `active`=0 WHERE `id` IN(%s)",
        ssIDs.str().c_str());

    // generate queue state file name
    std::size_t buffLength = strlen(GET_QUEUE_DIR()) + sizeof(QUEUE_STATE_FILE) + 1;
    char *szQueueStateFile = new char[buffLength];
    snprintf(szQueueStateFile, buffLength, "%s%c%s", GET_QUEUE_DIR(), PATH_SEP, QUEUE_STATE_FILE);
    utils->Touch(szQueueStateFile);
    delete[] szQueueStateFile;
}

/**
 * Delete unreleased enqueued messages
 */
void MSGQueue::DeleteMessages(vector<int> &IDs)
{
    stringstream ssIDs;

    if(IDs.size() < 1)
        return;

    for(unsigned int i=0; i<IDs.size(); i++)
    {
        if(i > 0)
            ssIDs << ",";
        ssIDs << IDs.at(i);

        unlink(this->QueueFileName(IDs.at(i)));
    }

    db->Query("DELETE FROM bm60_bms_queue WHERE `id` IN(%s)",
        ssIDs.str().c_str());
}

/*
 * Generate unique random queue ID
 */
int MSGQueue::GenerateRandomQueueID()
{
    int32_t Result = 0;
    char RandNumbers[sizeof(Result)];
    bool bAlreadyExists = false;

    do
    {
        RandNumbers[0] = ts_rand() % 256;
        RandNumbers[1] = ts_rand() % 256;
        RandNumbers[2] = ts_rand() % 256;
        RandNumbers[3] = ts_rand() % 256;

        memcpy(&Result, RandNumbers, sizeof(Result));

        if(Result == 0)
            continue;
        if(Result < 0)
            Result *= -1;

        MySQL_Result *res = db->Query("SELECT COUNT(*) FROM bm60_bms_queue WHERE `id`=%d",
                                     Result);
        MYSQL_ROW row = res->FetchRow();
        bAlreadyExists = row != NULL && row[0] != NULL && strcmp(row[0], "0") != 0;
        delete res;
    }
    while(bAlreadyExists);

    return(Result);
}

class StreamStateSaver
{
public:
    StreamStateSaver(iostream &stream_)
        : stream(stream_), flags(stream_.flags())
    {
    }

    ~StreamStateSaver()
    {
        stream.flags(flags);
    }

private:
    iostream &stream;
    ios::fmtflags flags;
};

/*
 * Enqueue a message
 */
int MSGQueue::EnqueueMessage(vector<pair<string, string> > headers,
                        const string &body,
                        int iType,
                        const char *strFrom, const char *strTo,
                        int iSMTPUser,
                        bool bWriteHeaders, bool bWriteMessageID, bool bWriteDate,
                        const char *szHeloHost,
                        const char *szPeer,
                        const char *szPeerHost,
                        int ib1gMailUser,
                        bool bInstantRelease,
                        bool bTLS,
                        int iDeliveryStatusID)
{
    int iID, iSize = 1024 + (int)body.size(); // just a wild guess
    string strFileName;
    char szBuffer[QUEUE_BUFFER_SIZE], *szQueueStateFile = new char[strlen(GET_QUEUE_DIR()) + sizeof(QUEUE_STATE_FILE) + 1],
            *szAlterMimePath = utils->GetAlterMimePath(), szSignatureFileName[255];
#ifndef WIN32
    MySQL_Result *res;
    MYSQL_ROW row;
#endif
    const char *strToDomain = NULL;

    // to domain
    strToDomain = strchr(strTo, '@');
    if(strToDomain != NULL)
        strToDomain++;
    else
        strToDomain = "";

    // generate queue state file name
    snprintf(szQueueStateFile, 255, "%s%c%s", GET_QUEUE_DIR(), PATH_SEP, QUEUE_STATE_FILE);

    // default signature file name
    strcpy(szSignatureFileName, "/tmp/bms-signature.sig.0");

    // insert a row marked as active so we do not process it before enqueueing is finished
    if(strcmp(cfg->Get("random_queue_id"), "1") == 0)
    {
        // lock table to avoid duplicate random IDs
        db->Query("LOCK TABLES bm60_bms_queue WRITE");
        db->Query("INSERT INTO bm60_bms_queue(`id`,`active`,`type`,`date`,`size`,`from`,`to`,`to_domain`,`attempts`,`last_attempt`,`last_status`,`smtp_user`,`b1gmail_user`,`deliverystatusid`) "
                    "VALUES(%d,1,'%d','%d','%d','%q','%q','%q',0,0,0,'%d','%d','%d')",
                    this->GenerateRandomQueueID(),
                    iType,
                    (int)time(NULL),
                    iSize,
                    strFrom,
                    strTo,
                    strToDomain,
                    iSMTPUser,
                    ib1gMailUser,
                    iDeliveryStatusID);
        iID = (int)db->InsertId();
        db->Query("UNLOCK TABLES");
    }
    else
    {
        db->Query("INSERT INTO bm60_bms_queue(`active`,`type`,`date`,`size`,`from`,`to`,`to_domain`,`attempts`,`last_attempt`,`last_status`,`smtp_user`,`b1gmail_user`,`deliverystatusid`) "
                    "VALUES(1,'%d','%d','%d','%q','%q','%q',0,0,0,'%d','%d','%d')",
                    iType,
                    (int)time(NULL),
                    iSize,
                    strFrom,
                    strTo,
                    strToDomain,
                    iSMTPUser,
                    ib1gMailUser,
                    iDeliveryStatusID);
        iID = (int)db->InsertId();
    }

    // write headers?
    if(bWriteHeaders)
    {
        // create time string
        char szTime[128] = { 0 }, szTimeZoneAbbrev[16] = { 0 };
        time_t iTime = time(NULL);
#ifdef WIN32
        struct tm *cTime = localtime(&iTime);
#else
        struct tm _cTime, *cTime = localtime_r(&iTime, &_cTime);
#endif
        strftime(szTime, 128, "%a, %d %b %Y %H:%M:%S", cTime);
        strftime(szTimeZoneAbbrev, 16, "%Z", cTime);
        int iTimeZone = utils->GetTimeZone();

        // date?
        if(bWriteDate)
        {
            stringstream ss;
            ss      << szTime << (iTimeZone >= 0 ? " +" : " -");

            {
                StreamStateSaver state(ss);
                ss  << setw(4) << setfill('0') << iTimeZone;
            }

            ss      << " (" << szTimeZoneAbbrev << ")";

            headers.insert(headers.begin(),
                pair<string, string>("Date", ss.str()));
        }

        // message id?
        if(bWriteMessageID)
        {
            char szMessageID[33];
            utils->MakeRandomKey(szMessageID, 32);

            stringstream ss;
            ss << "<" << szMessageID << "@" << cfg->Get("b1gmta_host") << ">";

            headers.insert(headers.begin(),
                pair<string, string>("Message-ID", ss.str()));
        }

        // abuse tracking header
        if(iSMTPUser > 0)
        {
            stringstream ss;
            ss << hex << uppercase << setw(8) << setfill('0') << iSMTPUser;

            headers.insert(headers.begin(),
                pair<string, string>("X-ESMTP-Authenticated-User", ss.str()));
        }

        // received-header
        if(iSMTPUser <= 0 || strcmp(cfg->Get("smtp_auth_no_received"), "0") == 0)
        {
            stringstream ss;
            ss      << "from " << szHeloHost << " (" << szPeerHost << " [" << szPeer << "])\r\n"
                    << "\tby " << cfg->Get("b1gmta_host");
            if(strcmp(cfg->Get("received_header_no_expose"), "1") != 0)
                ss  << " (b1gMailServer)";
            ss      << " with " << (bTLS ? "ESMTPS" : "ESMTP") << " id ";

            {
                StreamStateSaver state(ss);
                ss  << hex << uppercase << setw(8) << setfill('0') << iID;
            }

            ss      << "\r\n"
                    << "\tfor <" << strTo << ">; " << szTime << (iTimeZone >= 0 ? " +" : " -");

            {
                StreamStateSaver state(ss);
                ss  << setw(4) << setfill('0') << iTimeZone;
            }

            ss      << " (" << szTimeZoneAbbrev << ")";

            headers.insert(headers.begin(),
                pair<string, string>("Received", ss.str()));
        }

        // return-path header
        if(iType == MESSAGE_INBOUND)
        {
            headers.insert(headers.begin(),
                pair<string, string>("Return-Path", "<"+string(strFrom)+">"));
        }
    }

    // assemble message
    FILE *fpMessage = tmpfile();
    if(fpMessage == NULL)
    {
        db->Log(CMP_MSGQUEUE, PRIO_ERROR, utils->PrintF("Failed to create temporary FP"));
        free(szAlterMimePath);
        delete[] szQueueStateFile;
        return(0);
    }
    for(vector<pair<string, string> >::const_iterator it = headers.begin();
        it != headers.end();
        ++it)
    {
        fprintf(fpMessage, "%s: %s\r\n", it->first.c_str(), it->second.c_str());
    }
    if(bWriteHeaders)
    {
        // own headers
        const char *szOwnHeaders = NULL;
        if(iType == MESSAGE_INBOUND)
            szOwnHeaders = cfg->Get("inbound_headers");
        else if(iType == MESSAGE_OUTBOUND)
            szOwnHeaders = cfg->Get("outbound_headers");
        if(szOwnHeaders != NULL && strlen(szOwnHeaders) > 0)
        {
            fwrite(szOwnHeaders, strlen(szOwnHeaders), 1, fpMessage);
        }
    }
    fprintf(fpMessage, headers.empty() ? "\r\n\r\n" : "\r\n");
    fwrite(body.c_str(), 1, body.size(), fpMessage);
    iSize = (int)ftell(fpMessage);
    fseek(fpMessage, 0, SEEK_SET);

#ifndef WIN32
    // add signature?
    if(iType == MESSAGE_OUTBOUND
       && iSMTPUser > 0
       && cfg->Get("outbound_add_signature") != NULL
       && strcmp(cfg->Get("outbound_add_signature"), "1") == 0)
    {
        if(utils->FileExists(szAlterMimePath))
        {
            bool bSignatureFound = false;

            // copy signature to temp file
            snprintf(szSignatureFileName, 255, "/tmp/bms-signature.sig.%d", iID);
            FILE *fpSignature = fopen(szSignatureFileName, "w");
            if(fpSignature != NULL)
            {
                // plugins
                FOR_EACH_PLUGIN(Plugin)
                {
                    if(Plugin->GetUserMailSignature(iSMTPUser, cfg->Get("outbound_signature_sep"), fpSignature))
                    {
                        bSignatureFound = true;
                        break;
                    }
                }
                END_FOR_EACH()

                // not found => use group signature
                if(!bSignatureFound)
                {
                    res = db->Query("SELECT bm60_gruppen.signatur FROM bm60_gruppen,bm60_users WHERE bm60_gruppen.id=bm60_users.gruppe AND bm60_users.id=%d",
                                    iSMTPUser);
                    if(res->NumRows() == 1)
                    {
                        row = res->FetchRow();

                        string strSignature = row[0];

                        if(strSignature.length() > 0)
                        {
                            size_t rPos;

                            while((rPos = strSignature.find_first_of('\r')) != string::npos)
                                strSignature.erase(rPos, 1);

                            fprintf(fpSignature, "\n%s\n%s\n", cfg->Get("outbound_signature_sep"), strSignature.c_str());

                            bSignatureFound = true;
                        }
                    }
                    delete res;
                }

                fclose(fpSignature);
            }

            // success?
            if(utils->FileExists(szSignatureFileName))
            {
                if(bSignatureFound)
                {
                    // prepare command
                    std::size_t buffLength = strlen(szAlterMimePath)+strlen(szSignatureFileName)+255;
                    char *szCommand = new char[buffLength];
                    snprintf(szCommand, buffLength, "%s --input=- --disclaimer=%s --htmltoo",
                            szAlterMimePath,
                            szSignatureFileName);

                    int fdIn, fdOut;
                    pid_t iPID;
                    if((iPID = utils->POpen(szCommand, &fdIn, &fdOut)) > 0)
                    {
                        FILE *fpNewMessage = NULL;

                        if(fcntl(fdOut, F_SETFL, fcntl(fdOut, F_GETFL) | O_NONBLOCK) == 0)
                        {
                            ssize_t iReadBytes, iWrittenBytes;
                            fpNewMessage = tmpfile();

                            fd_set fdSet;
                            while(true)
                            {
                                struct timeval timeout;

                                if(!feof(fpMessage))
                                {
                                    // read from msg file
                                    iReadBytes = fread(szBuffer, 1, QUEUE_BUFFER_SIZE, fpMessage);
                                    if(iReadBytes <= 0)
                                    {
                                        if(fdIn != -1)
                                        {
                                            close(fdIn);
                                            fdIn = -1;
                                        }
                                        if(iReadBytes < 0)
                                            break;
                                    }

                                    // write to altermime
                                    if(iReadBytes > 0)
                                    {
                                        iWrittenBytes = write(fdIn, szBuffer, iReadBytes);
                                        if(iWrittenBytes <= 0)
                                        {
                                            if(fdIn != -1)
                                            {
                                                close(fdIn);
                                                fdIn = -1;
                                            }
                                            break;
                                        }
                                    }

                                    // eof?
                                    if(feof(fpMessage))
                                    {
                                        if(fdIn != -1)
                                        {
                                            close(fdIn);
                                            fdIn = -1;
                                        }
                                    }

                                    // just poll
                                    timeout.tv_sec = 0;
                                    timeout.tv_usec = 0;
                                }
                                else
                                {
                                    // wait
                                    timeout.tv_sec = 0;
                                    timeout.tv_usec = 500000;
                                }

                                FD_ZERO(&fdSet);
                                FD_SET(fdOut, &fdSet);
                                if(select(fdOut+1, &fdSet, NULL, NULL, &timeout) == -1)
                                    break;

                                if(FD_ISSET(fdOut, &fdSet))
                                {
                                    iReadBytes = read(fdOut, szBuffer, QUEUE_BUFFER_SIZE);
                                    if(iReadBytes <= 0)
                                        break;

                                    iWrittenBytes = fwrite(szBuffer, 1, iReadBytes, fpNewMessage);
                                    if(iWrittenBytes <= 0)
                                        break;
                                }
                            }

                            close(fdOut);
                            fdOut = -1;
                        }

                        if(fdIn != -1)
                        {
                            close(fdIn);
                            fdIn = -1;
                        }
                        if(fdOut != -1)
                        {
                            close(fdOut);
                            fdOut = -1;
                        }

                        int iAlterMIMEResult;
                        waitpid(iPID, &iAlterMIMEResult, 0);

                        if(WIFEXITED(iAlterMIMEResult) && WEXITSTATUS(iAlterMIMEResult) == 0)
                        {
                            if (fpNewMessage != NULL)
                            {
                                fclose(fpMessage);
                                fpMessage = fpNewMessage;
                                fseek(fpMessage, 0, SEEK_SET);
                            }
                            else
                            {
                                db->Log(CMP_MSGQUEUE, PRIO_WARNING, utils->PrintF("Cannot add signature to outbound mail - fpNewMessage is NULL"));
                            }
                        }
                        else
                        {
                            db->Log(CMP_MSGQUEUE, PRIO_WARNING, utils->PrintF("Cannot add signature to outbound mail - altermime returned error (%s: %d)",
                                                                            szAlterMimePath,
                                                                            WEXITSTATUS(iAlterMIMEResult)));
                        }
                    }
                    else
                    {
                        db->Log(CMP_MSGQUEUE, PRIO_WARNING, utils->PrintF("Cannot add signature to outbound mail - failed to invoke altermime (%s)",
                                                                        szAlterMimePath));
                    }

                    delete[] szCommand;
                }

                unlink(szSignatureFileName);
            }
            else
            {
                db->Log(CMP_MSGQUEUE, PRIO_WARNING, utils->PrintF("Cannot add signature to outbound mail - could not fetch / save signature for user %d",
                                                                  iSMTPUser));
            }
        }
        else
        {
            db->Log(CMP_MSGQUEUE, PRIO_WARNING, utils->PrintF("Cannot add signature to outbound mail - altermime binary at %s not found",
                                                              szAlterMimePath));
        }
    }
#endif

    // create queue file
    strFileName = MSGQueue::QueueFileName(iID);
    FILE *fpQueueFile = fopen(strFileName.c_str(), "wb");
    if(fpQueueFile == NULL)
    {
        db->Log(CMP_MSGQUEUE, PRIO_ERROR, utils->PrintF("Failed to create queue file %s for queue item %d",
            strFileName.c_str(),
            iID));

        free(szAlterMimePath);
        delete[] szQueueStateFile;

        fclose(fpMessage);

        db->Query("DELETE FROM bm60_bms_queue WHERE `id`=%d",
            iID);

        return(0);
    }

    // copy message
    while(!feof(fpMessage))
    {
        size_t iReadBytes = fread(szBuffer, 1, QUEUE_BUFFER_SIZE, fpMessage);
        if(iReadBytes <= 0)
            break;
        size_t iWrittenBytes = fwrite(szBuffer, 1, iReadBytes, fpQueueFile);
        if(iWrittenBytes != iReadBytes)
            break;
    }
    iSize = (int)ftell(fpQueueFile);

    // close queue file
    fclose(fpQueueFile);

    // close modified queue file
    fclose(fpMessage);

    // update delivery status
    SetMailDeliveryStatus(iDeliveryStatusID, MDSTATUS_RECEIVED_BY_MTA);

    // set active to 0 if caller wants instant release
    db->Query("UPDATE bm60_bms_queue SET `active`=%d,`size`='%d' WHERE `id`='%d'",
        bInstantRelease ? 0 : 1,
        iSize,
        iID);

    // touch queue state file
    if(bInstantRelease) utils->Touch(szQueueStateFile);
    bQueueUpdated = true;
    delete[] szQueueStateFile;
    free(szAlterMimePath);

    return(iID);
}
