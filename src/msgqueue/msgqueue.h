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

#ifndef _MSGQUEUE_MSGQUEUE_H
#define _MSGQUEUE_MSGQUEUE_H

#include <core/core.h>
#include <core/threadpool.h>
#include <plugin/plugin.h>
#include <msgqueue/control.h>
#include <msgqueue/msgqueue_threadpool.h>
#include <msgqueue/inboundprocesspool.h>
#include <msgqueue/smtpsessionpool.h>

#include <pthread.h>

#ifndef WIN32
#include <sys/time.h>
#endif

#define QUEUE_RUN_MAX               1000 //2000
#define QUEUE_BUFFER_SIZE           4096
#define MAIL_LINEMAX                (1000+1)
#define MAILER_DAEMON_USER          "postmaster"
#define BMUSER_SYSTEM               -1
#define BMUSER_ADMIN                -2
#define SMTP_SESSION_HOLD_TIME      5

enum eMDStatus
{
    MDSTATUS_INVALID                = 0,
    MDSTATUS_SUBMITTED_TO_MTA,
    MDSTATUS_RECEIVED_BY_MTA,
    MDSTATUS_DELIVERED_BY_MTA,
    MDSTATUS_DELIVERY_DEFERRED,
    MDSTATUS_DELIVERY_FAILED
};

enum eMessageType
{
    MESSAGE_INBOUND                 = 0,
    MESSAGE_OUTBOUND
};

enum eQueueStatus
{
    QUEUE_STATUS_SUCCESS            = 0,
    QUEUE_STATUS_UNKNOWN_ERROR,
    QUEUE_STATUS_TEMPORARY_ERROR,
    QUEUE_STATUS_FATAL_ERROR,
    QUEUE_STATUS_INVALID_ITEM
};

enum eOutboundTarget
{
    OUTBOUND_TARGET_SENDMAIL        = 0,
    OUTBOUND_TARGET_SMTPRELAY,
    OUTBOUND_TARGET_DELIVER_SELF
};

enum eMessageTarget
{
    MESSAGE_TARGET_INBOUND          = 0,
    MESSAGE_TARGET_SENDMAIL         = 1,
    MESSAGE_TARGET_SMTPRELAY        = 2,
    MESSAGE_TARGET_DELIVER_SELF     = 3
};

class DeliveryException
{
public:
    DeliveryException(string host, string statusInfo, int errorCode)
    {
        this->host = host;
        this->statusInfo = statusInfo;
        this->errorCode = errorCode;
    }
    DeliveryException(string host, string statusInfo, int errorCode, string statusCode)
    {
        this->host = host;
        this->statusInfo = statusInfo;
        this->errorCode = errorCode;
        this->statusCode = statusCode;
    }
    DeliveryException(string host, string statusInfo, int errorCode, string statusCode, string diagnosticCode)
    {
        this->host = host;
        this->statusInfo = statusInfo;
        this->errorCode = errorCode;
        this->statusCode = statusCode;
        this->diagnosticCode = diagnosticCode;
    }
    DeliveryException()
    {
        this->host = "";
        this->statusInfo = "";
        this->errorCode = QUEUE_STATUS_UNKNOWN_ERROR;
    }

public:
    string host;
    string statusInfo;
    string statusCode;
    string diagnosticCode;
    int errorCode;
};

class MSGQueueItem
{
public:
    int id;
    eMessageType type;
    int date;
    int size;
    string domain;
    string from;
    string to;
    int attempts;
    int lastAttempt;
    int lastStatus;
    string lastStatusCode;
    string lastDiagnosticCode;
    int smtpUser;
    int b1gMailUser;
    int deliveryStatusID;
    int flags;
};

class MSGQueueResult
{
public:
    MSGQueueItem *item;
    int status;
    string statusInfo;
    string statusCode;
    string deliveredTo;
    string diagnosticCode;
    int apPointType;
    string apPointComment;
};

class RelayServerInfo
{
public:
    string host;
    int port;
    bool requiresAuth;
    string user;
    string pass;
};

class DeliveryRule;
class DeliveryRules;
class APNSDispatcher;

enum MSGQueueFlags
{
    MSGQUEUEFLAG_IS_SPAM        = (1 << 0),
    MSGQUEUEFLAG_IS_INFECTED    = (1 << 1)
};

class MSGQueue : public b1gMailServer::MSGQueue
{
public:
    MSGQueue();
    ~MSGQueue();

public:
    int EnqueueMessage(vector<pair<string, string> > headers,
                        const string &body,
                        int iType,
                        const char *strFrom, const char *strTo,
                        int iSMTPUser = 0,
                        bool bWriteHeaders = false, bool bWriteMessageID = false,
                        bool bWriteDate = false,
                        const char *szHeloHost = NULL,
                        const char *szPeer = NULL,
                        const char *szPeerHost = NULL,
                        int ib1gMailUser = 0,
                        bool bInstantRelease = true,
                        bool bTLS = false,
                        int iDeliveryStatusID = 0,
                        int iFlags = 0);
    void ReleaseMessages(vector<int> &IDs);
    void DeleteMessages(vector<int> &IDs);
    bool BounceMessage(MSGQueueItem *cQueueItem, MSGQueueResult *result);
    const char *QueueFileName(int iID);
    string QueueFileNameStr(int iID);
    int GenerateRandomQueueID();

public:
    void Run();
    static void WorkProcessorEntry(void *arg);
    static void *ControlThreadEntry(void *arg);
    static void *APNSDispatcherEntry(void *arg);
    void APNSDispatcherThread();
    void ControlThread();
    void WorkProcessor(MSGQueueItem *item);
    void ProcessInbound(MSGQueueItem *item, MSGQueueResult *result);
    void ProcessOutbound(MSGQueueItem *item, MSGQueueResult *result);
    void ProcessRule(MSGQueueItem *item, MSGQueueResult *result, const DeliveryRule *rule);
    void DeliverOutboundToSendmail(MSGQueueItem *item, MSGQueueResult *result, const char *sendmailPath);
    void DeliverOutboundToSMTPRelay(MSGQueueItem *item, MSGQueueResult *result, const RelayServerInfo &relayServer);
    void DeliverOutbound(MSGQueueItem *item, MSGQueueResult *result);
    void AddToGreylist(in_addr_t addr);

private:
    void ClearQueue();
    void ProcessQueue();
    void ProcessResults();
    void ProcessResult(MSGQueueResult *cResult);
    void RowToMSGQueueItem(MYSQL_ROW row, MSGQueueItem *cQueueItem);
    void SetMailDeliveryStatus(int id, eMDStatus status, const char *deliveredTo = NULL);
    bool IsLocalDomain(const std::string &domain);
    void RefreshLocalDomains();

public:
    bool bQuit;
    MSGQueue_ThreadPool *threadPool;

private:
    InboundProcessPool *inboundPool;
    SMTPSessionPool *smtpPool;
    queue<MSGQueueResult *> results;
    pthread_mutex_t resultsLock;
    pthread_cond_t resultsAvailable;
    bool resultsWaiting;
    pthread_mutex_t greylistQueueLock;
    queue<in_addr_t> greylistQueue;
    MSGQueue_Control *control;
    bool moreQueueEntries;
    set<string> localDomains;
    pthread_mutex_t localDomainsLock;
    DeliveryRules *deliveryRules;
    APNSDispatcher *apnsDispatcher;

    MSGQueue(const MSGQueue &);
    MSGQueue &operator=(const MSGQueue &);
};

extern MSGQueue *cMSGQueue_Instance;
extern bool bQueueUpdated;

#endif
