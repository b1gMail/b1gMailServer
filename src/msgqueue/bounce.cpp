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

bool MSGQueue::BounceMessage(MSGQueueItem *cQueueItem, MSGQueueResult *result)
{
    string strBounceReturnPath = "";

    // check return path
    if(cQueueItem->from.length() < 5)
    {
        db->Log(CMP_MSGQUEUE, PRIO_NOTE, utils->PrintF("Not bouncing message %d - return path <%s> invalid/empty",
            cQueueItem->id,
            (char *)cQueueItem->from.c_str()));
        return(false);
    }

    // compare return path and forward path
    if(strcasecmp(cQueueItem->from.c_str(), cQueueItem->to.c_str()) == 0)
    {
        db->Log(CMP_MSGQUEUE, PRIO_NOTE, utils->PrintF("Not bouncing message %d - return path <%s> equals to <%s> forward path",
            cQueueItem->id,
            (char *)cQueueItem->from.c_str(),
            (char *)cQueueItem->to.c_str()));
        return(false);
    }

    // local user id?
    int iReturnPathUserID = utils->LookupUser((char *)cQueueItem->from.c_str(), false);

    // generate boundary string
    char szBoundaryKey[33], szMessageID[33];
    string strBoundary = "--Boundary-=_";
    utils->MakeRandomKey(szBoundaryKey, 32);
    utils->MakeRandomKey(szMessageID, 32);
    strBoundary.append(szBoundaryKey);

    // generate queue item date
    char szQueueItemDate[128] = { 0 };
    time_t iTime = cQueueItem->date;
#ifdef WIN32
    struct tm *cTime = gmtime(&iTime);
#else
    struct tm _cTime, *cTime = gmtime_r(&iTime, &_cTime);
#endif
    strftime(szQueueItemDate, 128, "%a, %d %b %Y %H:%M:%S", cTime);

    // header part
    vector<pair<string, string> > headers;
    headers.push_back(pair<string, string>("Return-Path", "<>"));
    headers.push_back(pair<string, string>("To", "<" + cQueueItem->from + ">"));
    headers.push_back(pair<string, string>("From", "\"Mail Delivery System\" <" MAILER_DAEMON_USER "@" + string(cfg->Get("b1gmta_host")) + ">"));
    headers.push_back(pair<string, string>("Subject", "Returned mail: see error report for details"));
    headers.push_back(pair<string, string>("Message-ID", string("<") + szMessageID + string("@") + cfg->Get("b1gmta_host") + string(">")));
    headers.push_back(pair<string, string>("MIME-Version", "1.0"));
    headers.push_back(pair<string, string>("Content-type", "multipart/report; report-type=delivery-status; boundary=\"" + strBoundary + "\""));

    stringstream ss;
    ss << "This is a multi-part message in MIME format.\r\n";

    // report part
    ss << "\r\n--" << strBoundary << "\r\n";
    ss << "Content-Type: text/plain; charset=\"US-ASCII\"\r\n";
    ss << "Content-Transfer-Encoding: 8bit\r\n";
    ss << "\r\nThe original message was received at " << szQueueItemDate << ".\r\n\r\n";
    ss << "   ----- Error description -----\r\n";
    ss << result->statusInfo << "\r\n\r\n";
    ss << "   ----- The message was not delivered to the following addresses -----\r\n";
    ss << "<" << cQueueItem->to << ">\r\n";
    ss << "\r\n";

    // delivery-status
    ss << "\r\n--" << strBoundary << "\r\n";
    ss << "Content-Type: message/delivery-status\r\n";
    ss << "Content-Transfer-Encoding: 8bit\r\n";
    ss << "\r\n";
    ss << "Reporting-MTA: dns; " << cfg->Get("b1gmta_host") << "\r\n";
    ss << "Arrival-Date: " << utils->TimeToString((time_t)cQueueItem->date) << "\r\n";
    ss << "\r\n";
    ss << "Final-Recipient: rfc822; " << cQueueItem->to << "\r\n";
    ss << "Action: failed\r\n";
    if(!result->statusCode.empty())
    {
        ss << "Status: " << result->statusCode << "\r\n";
    }
    if(!result->diagnosticCode.empty())
    {
        ss << "Diagnostic-Code: " << result->diagnosticCode << "\r\n";
    }
    ss << "Last-Attempt-Date: " << utils->TimeToString((time_t)cQueueItem->lastAttempt) << "\r\n";
    ss << "\r\n";

    // message part
    ss << "\r\n--" << strBoundary << "\r\n";
    ss << "Content-Type: message/rfc822\r\n";
    ss << "Content-Transfer-Encoding: 8bit\r\n";
    ss << "Content-Disposition: attachment; filename=\"returned-message.eml\"\r\n";
    ss << "\r\n";

    // get queue file name
    string strMessageFileName = MSGQueue::QueueFileName(cQueueItem->id);

    // try to open message
    char szBuffer[QUEUE_BUFFER_SIZE];
    FILE *fpMessage = fopen(strMessageFileName.c_str(), "rb");
    if(fpMessage != NULL)
    {
        while(!feof(fpMessage))
        {
            size_t iReadBytes = fread(szBuffer, 1, QUEUE_BUFFER_SIZE, fpMessage);
            if(iReadBytes <= 0)
                break;
            ss << string(szBuffer, iReadBytes);
        }
        fclose(fpMessage);
    }
    else
        ss << "The original message was lost.\r\n";

    // finish
    ss << "\r\n--" << strBoundary << "--\r\n";

    // enqueue
    int iQueueID = MSGQueue::EnqueueMessage(headers, ss.str(),
        iReturnPathUserID == 0 ? MESSAGE_OUTBOUND : MESSAGE_INBOUND,
        strBounceReturnPath.c_str(),
        cQueueItem->from.c_str(),
        0, false, false, false, NULL, NULL, NULL,
        BMUSER_SYSTEM);

    // log
    if(iQueueID > 0)
    {
        db->Log(CMP_MSGQUEUE, PRIO_NOTE, utils->PrintF("Bounced message %d, bounce queue id: %d",
            cQueueItem->id,
            iQueueID));
        return(true);
    }
    else
    {
        db->Log(CMP_MSGQUEUE, PRIO_WARNING, utils->PrintF("Failed to bounce message %d - enqueueing failed",
            cQueueItem->id));
        return(false);
    }
}
