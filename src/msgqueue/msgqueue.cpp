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
#include <msgqueue/apnsdispatcher.h>
#include <msgqueue/deliveryrules.h>
#include <sstream>
#include <map>
#include <stdexcept>

#ifndef WIN32
#ifndef __APPLE__
#include <sys/inotify.h>
#define USE_INOTIFY
#endif
#endif

MSGQueue *cMSGQueue_Instance = NULL;
bool bQueueUpdated = false;
static const int LOCALDOMAINS_UPDATE_INTERVAL = 60;

/*
 * Signal handler
 */
void MSGQueue_SignalHandler(int iSignal)
{
#ifndef WIN32
    if(iSignal == SIGUSR1)
        return;
#endif

    if(cMSGQueue_Instance != NULL)
        cMSGQueue_Instance->bQuit = true;
}
#ifdef WIN32
bool MSGQueue_CtrlHandler(DWORD fdwCtrlType)
{
    if(fdwCtrlType == CTRL_C_EVENT || fdwCtrlType == CTRL_BREAK_EVENT)
    {
        if(cMSGQueue_Instance != NULL)
        {
            cMSGQueue_Instance->bQuit = true;
            return(true);
        }
    }

    return(false);
}
#endif

/*
 * Constructor
 */
MSGQueue::MSGQueue()
{
    if(cMSGQueue_Instance == NULL)
        cMSGQueue_Instance = this;
    this->control = NULL;
    this->resultsWaiting = false;
    this->moreQueueEntries = false;
    this->deliveryRules = NULL;
    this->apnsDispatcher = NULL;
    pthread_mutex_init(&this->resultsLock, NULL);
    pthread_cond_init(&this->resultsAvailable, NULL);
    pthread_mutex_init(&this->greylistQueueLock, NULL);
    pthread_mutex_init(&this->localDomainsLock, NULL);
}

/*
 * Destructor
 */
MSGQueue::~MSGQueue()
{
    if(cMSGQueue_Instance == this)
        cMSGQueue_Instance = NULL;
    pthread_mutex_destroy(&this->resultsLock);
    pthread_cond_destroy(&this->resultsAvailable);
    pthread_mutex_destroy(&this->greylistQueueLock);
    pthread_mutex_destroy(&this->localDomainsLock);
}

/*
 * Control thread entry
 */
void *MSGQueue::ControlThreadEntry(void *arg)
{
    MSGQueue *mq = (MSGQueue *)arg;
    mq->ControlThread();

    pthread_exit(0);
    return(NULL);
}

/*
 * Control thread
 */
void MSGQueue::ControlThread()
{
    this->control = NULL;

    try
    {
        this->control = new MSGQueue_Control(this->apnsDispatcher);
        this->control->Run();
    }
    catch(Core::Exception &ex)
    {
        ex.Output();
    }

    if(this->control != NULL)
    {
        delete this->control;
        this->control = NULL;
    }

    this->bQuit = true;
}

/**
 * APNS Dispatcher thread entry
 */
void *MSGQueue::APNSDispatcherEntry(void *arg)
{
    MSGQueue *mq = (MSGQueue *)arg;
    mq->APNSDispatcherThread();

    pthread_exit(0);
    return(NULL);
}

/**
 * APNS Dispatcher thread
 */
void MSGQueue::APNSDispatcherThread()
{
    try
    {
        this->apnsDispatcher->run();
    }
    catch(const std::runtime_error &ex)
    {
    }

    if(this->apnsDispatcher != NULL)
    {
        delete this->apnsDispatcher;
        this->apnsDispatcher = NULL;
    }
}

/*
 * Add host to greylist add queue
 */
void MSGQueue::AddToGreylist(in_addr_t addr)
{
    pthread_mutex_lock(&this->greylistQueueLock);
    this->greylistQueue.push(addr);
    pthread_mutex_unlock(&this->greylistQueueLock);
}

/*
 * Main loop
 */
void MSGQueue::Run()
{
    struct stat st;
    string strQueueStateFile = utils->GetQueueStateFilePath();
    int iQueueInterval = atoi(cfg->Get("queue_interval"));

    this->bQuit = false;

    // close fds
    //fclose(stdin);
    //fclose(stdout);
    //fclose(stderr);

    // set signal handlers
#ifndef WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    signal(SIGINT, MSGQueue_SignalHandler);
    signal(SIGTERM, MSGQueue_SignalHandler);
#ifdef WIN32
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)MSGQueue_CtrlHandler, true);
#else
    signal(SIGHUP, MSGQueue_SignalHandler);
    signal(SIGUSR1, MSGQueue_SignalHandler);
#endif

    // get local domains
    this->RefreshLocalDomains();

    // read and compile delivery rules
    this->deliveryRules = DeliveryRules::ReadFromDB(db);

    // re-animate jobs
    db->Query("UPDATE bm60_bms_queue SET active=0,attempts=attempts+1,last_attempt=%d,last_status=%d,last_status_info='%q',`last_status_code`='%q',`last_diagnostic_code`='%q' WHERE active=1",
              (int)time(NULL),
              QUEUE_STATUS_UNKNOWN_ERROR,
              "Deferred after unexpected termination.",
              "4.0.0",
              "");

    // create APNS dispatcher thread
    pthread_t apnsDispatcherThread = pthread_self();
    if(atoi(cfg->Get("apns_enable")) == 1)
    {
        MySQL_DB *apnsLogDB = NULL;
        this->apnsDispatcher = NULL;

        try
        {
            apnsLogDB = new Core::MySQL_DB(cfg->Get("mysql_host"),
                    cfg->Get("mysql_user"),
                    cfg->Get("mysql_pass"),
                    cfg->Get("mysql_db"),
                    cfg->Get("mysql_sock"),
                    true);
            apnsLogDB->Query("SET NAMES latin1");
            this->apnsDispatcher = new APNSDispatcher(apnsLogDB,
                                        cfg->Get("apns_host"),
                                        atoi(cfg->Get("apns_port")),
                                        cfg->Get("apns_certificate"),
                                        cfg->Get("apns_privatekey"));
            if(pthread_create(&apnsDispatcherThread, NULL, MSGQueue::APNSDispatcherEntry, this) != 0)
                throw Core::Exception("Failed to create APNS dispatcher thread.");
        }
        catch(const std::runtime_error &ex)
        {
            if(apnsLogDB != NULL)
                delete apnsLogDB, apnsLogDB = NULL;
            if(this->apnsDispatcher != NULL)
                delete this->apnsDispatcher, this->apnsDispatcher = NULL;

            db->Log(CMP_MSGQUEUE, PRIO_WARNING, utils->PrintF("Failed to initialize APNS dispatcher (1): %s",
                ex.what()));
        }
        catch(Core::Exception &ex)
        {
            if(apnsLogDB != NULL)
                delete apnsLogDB, apnsLogDB = NULL;
            if(this->apnsDispatcher != NULL)
                delete this->apnsDispatcher, this->apnsDispatcher = NULL;

            db->Log(CMP_MSGQUEUE, PRIO_WARNING, utils->PrintF("Failed to initialize APNS dispatcher (2): %s",
                ex.strError.c_str()));
        }
    }

    // create control thread
    pthread_t controlThread;
    if(pthread_create(&controlThread, NULL, MSGQueue::ControlThreadEntry, this) != 0)
        throw Core::Exception("Failed to create control thread.");

    // create threads and pools
    this->smtpPool = new SMTPSessionPool();
    this->inboundPool = new InboundProcessPool();
    this->threadPool = new MSGQueue_ThreadPool(atoi(cfg->Get("queue_threads")),
        atoi(cfg->Get("queue_maxthreads")),
        MSGQueue::WorkProcessorEntry);
    this->threadPool->start();

#ifdef USE_INOTIFY
    // use inotify under linux
    char notifyBuffer[1024*(sizeof(struct inotify_event)+16)];
    int notifyFD = inotify_init();
    int notifyWD = inotify_add_watch(notifyFD, strQueueStateFile.c_str(), IN_ATTRIB);
    fd_set fdSet;
#endif

    // main loop
    bQueueUpdated = true;
    time_t lastMTime = 0;
    time_t lastLocalDomainsUpdate = time(NULL);
    bool bCloseMySQL = atoi(cfg->Get("queue_mysqlclose")) == 1;
    stat(strQueueStateFile.c_str(), &st);

    while(!this->bQuit)
    {
        if(!bQueueUpdated)
        {
#ifdef USE_INOTIFY
            struct timeval timeout;

            time_t waitTime = 10;

            if(this->moreQueueEntries)
                waitTime = 1;

            time_t waitUntil = time(NULL) + waitTime;
            while(!this->bQuit && !this->resultsWaiting && time(NULL) < waitUntil)
            {
                FD_ZERO(&fdSet);
                FD_SET(notifyFD, &fdSet);

                timeout.tv_sec = 0;
                timeout.tv_usec = 100000;

                if(select(notifyFD+1, &fdSet, NULL, NULL, &timeout) == -1)
                    break;

                if(FD_ISSET(notifyFD, &fdSet))
                    break;
            }

            if(!this->bQuit && FD_ISSET(notifyFD, &fdSet))
            {
                // discard socket data
                while(true)
                {
                    // read event buffer
                    size_t length = read(notifyFD, notifyBuffer, sizeof(notifyBuffer));

                    unsigned int i = 0;
                    while(i < length)
                    {
                        if(i + sizeof(struct inotify_event) > length)
                            break;

                        struct inotify_event *event = (struct inotify_event *)(notifyBuffer + i);

                        if((event->mask & IN_ATTRIB) != 0)
                        {
                            lastMTime = 0; // enforce queue processing
                            break;
                        }

                        i += sizeof(struct inotify_event) + event->len;
                    }

                    // more events to read?
                    timeout.tv_sec = 0;
                    timeout.tv_usec = 0;

                    FD_ZERO(&fdSet);
                    FD_SET(notifyFD, &fdSet);

                    if(select(notifyFD+1, &fdSet, NULL, NULL, &timeout) == -1)
                        break;

                    if(!FD_ISSET(notifyFD, &fdSet))
                        break;
                }
            }
#else
            // sleep 0.5 seconds
            utils->MilliSleep(500);
#endif
        }

        if(this->bQuit)
            break;

        // shrink thread pool
        this->threadPool->shrink();

        // more queue entries?
        if(this->moreQueueEntries)
        {
            this->threadPool->lockQueue();
            unsigned int queueEntries = this->threadPool->queue.size();
            this->threadPool->unlockQueue();

            if(queueEntries <= QUEUE_RUN_MAX-(unsigned int)atoi(cfg->Get("queue_maxthreads"))*2)
            {
                bQueueUpdated = true;
                this->moreQueueEntries = false;
            }
        }

        // updates?
        if(!bQueueUpdated)
            stat(strQueueStateFile.c_str(), &st);

        // process queue?
        if(bQueueUpdated
            || (st.st_mtime > lastMTime)
            || (lastMTime < (time(NULL) - iQueueInterval)))
        {
            // update last mtime
#ifdef USE_INOTIFY
            inotify_rm_watch(notifyFD, notifyWD);
#endif
            utils->Touch(strQueueStateFile.c_str());
#ifdef USE_INOTIFY
            notifyWD = inotify_add_watch(notifyFD, strQueueStateFile.c_str(), IN_ATTRIB);
#endif
            stat(strQueueStateFile.c_str(), &st);
            lastMTime = st.st_mtime;

            // process queue
            bQueueUpdated = false;
            this->ProcessQueue();
        }

        // process results
        this->ProcessResults();

        // clean up smtp session pool
        this->smtpPool->cleanUp();

        // refresh local domains?
        if(lastLocalDomainsUpdate < (time(NULL) - LOCALDOMAINS_UPDATE_INTERVAL))
        {
            db->Log(CMP_MSGQUEUE, PRIO_DEBUG, utils->PrintF("Refreshing cached list of local domains"));
            this->RefreshLocalDomains();
            lastLocalDomainsUpdate = time(NULL);
        }

        // temp-close mysql
        if(!bQueueUpdated && bCloseMySQL)
            db->TempClose();
    }

#ifdef USE_INOTIFY
    inotify_rm_watch(notifyFD, notifyWD);
    close(notifyFD);
#endif

    // stop threads
    this->threadPool->stop();

    // process not-yet-saved results
    this->ProcessResults();

    // re-activate queue items which have not been processed
    this->ClearQueue();

    // exit APNS dispatcher thread
    if(this->apnsDispatcher != NULL)
        this->apnsDispatcher->quit();
    if(apnsDispatcherThread != pthread_self())
        pthread_join(apnsDispatcherThread, NULL);

    // exit control thread
    if(this->control != NULL)
        this->control->bQuit = true;
    pthread_join(controlThread, NULL);

    delete this->threadPool;
    delete this->inboundPool;
    delete this->smtpPool;

    delete this->deliveryRules;

    return;
}

/*
 * Check if domain is local
 */
bool MSGQueue::IsLocalDomain(const std::string &domain)
{
    pthread_mutex_lock(&this->localDomainsLock);
    bool result = (this->localDomains.find(domain) != this->localDomains.end());
    pthread_mutex_unlock(&this->localDomainsLock);
    return result;
}

/*
 * Refresh list of local domains
 */
void MSGQueue::RefreshLocalDomains()
{
    std::set<std::string> newLocalDomains;
    utils->GetLocalDomains(newLocalDomains);

    pthread_mutex_lock(&this->localDomainsLock);
    this->localDomains.swap(newLocalDomains);
    pthread_mutex_unlock(&this->localDomainsLock);
}

/*
 * Clear thread pool queue (mark removed items as not active in DB)
 */
void MSGQueue::ClearQueue()
{
    if(this->threadPool->queue.empty())
        return;

    stringstream ssIDs;
    bool firstItem = true;

    for(list<void *>::iterator it = this->threadPool->queue.begin(); it != this->threadPool->queue.end(); ++it)
    {
        if(firstItem)
            firstItem = false;
        else
            ssIDs << ",";

        MSGQueueItem *item = (MSGQueueItem *)*it;
        ssIDs << item->id;
    }

    this->threadPool->queue.clear();

    db->Query("UPDATE bm60_bms_queue SET `active`=0 WHERE `id` IN (%s)",
        ssIDs.str().c_str());
}

/*
 * Process delivery results queue
 */
void MSGQueue::ProcessResults()
{
    // process results
    queue<MSGQueueResult *> resultsQueue;
    pthread_mutex_lock(&this->resultsLock);
    while(!this->results.empty())
    {
        resultsQueue.push(this->results.front());
        this->results.pop();
    }
    this->resultsWaiting = false;
    pthread_mutex_unlock(&this->resultsLock);

    while(!resultsQueue.empty())
    {
        MSGQueueResult *result = resultsQueue.front();
        this->ProcessResult(result);

        delete result->item;
        delete result;

        resultsQueue.pop();
    }

    // process greylist add queue
    queue<in_addr_t> greylistItems;
    pthread_mutex_lock(&this->greylistQueueLock);
    while(!this->greylistQueue.empty())
    {
        greylistItems.push(this->greylistQueue.front());
        this->greylistQueue.pop();
    }
    pthread_mutex_unlock(&this->greylistQueueLock);

    while(!greylistItems.empty())
    {
        int iID = 0;

        MYSQL_ROW row;
        MySQL_Result *res = db->Query("SELECT `id` FROM bm60_bms_greylist WHERE `ip`='%d' AND `ip6`=''");
        while((row = res->FetchRow()))
            iID = atoi(row[0]);
        delete res;

        if(iID > 0)
        {
            db->Query("UPDATE bm60_bms_greylist SET `time`=%d,`confirmed`=%d WHERE `id`=%d",
                (int)time(NULL),
                1,
                iID);
        }
        else
        {
            db->Query("INSERT INTO bm60_bms_greylist(`ip`,`time`,`confirmed`) VALUES(%d,%d,%d)",
                      greylistItems.front(),
                      (int)time(NULL),
                      1);
        }

        greylistItems.pop();
    }
}

/*
 * Update mail delivery status entry
 */
void MSGQueue::SetMailDeliveryStatus(int id, eMDStatus status, const char *deliveredTo)
{
    if (id > 0 && strcmp(cfg->Get("enable_mdstatus"), "1") == 0)
    {
        if (deliveredTo == NULL)
        {
            db->Query("UPDATE bm60_maildeliverystatus SET `status`=%d,`updated`=UNIX_TIMESTAMP() WHERE `deliverystatusid`=%d",
                static_cast<int>(status),
                id);
        }
        else
        {
            db->Query("UPDATE bm60_maildeliverystatus SET `status`=%d,`delivered_to`='%q',`updated`=UNIX_TIMESTAMP() WHERE `deliverystatusid`=%d",
                static_cast<int>(status),
                deliveredTo,
                id);
        }
    }
}

/*
 * Process single item result
 */
void MSGQueue::ProcessResult(MSGQueueResult *cResult)
{
    bool bBounced = false;
    MSGQueueItem *cQueueItem = (MSGQueueItem *)cResult->item;

    // log
    db->Log(CMP_MSGQUEUE, PRIO_DEBUG, utils->PrintF("Processed queue item %d, status = %d",
        cQueueItem->id,
        cResult->status));
    cQueueItem->lastStatus = cResult->status;

    // success
    if(cResult->status == QUEUE_STATUS_SUCCESS)
    {
        db->Query("DELETE FROM bm60_bms_queue WHERE `id`=%d",
            cQueueItem->id);
        unlink(MSGQueue::QueueFileNameStr(cQueueItem->id).c_str());

        SetMailDeliveryStatus(cQueueItem->deliveryStatusID, MDSTATUS_DELIVERED_BY_MTA, cResult->deliveredTo.c_str());

        // log
        db->Log(CMP_MSGQUEUE, PRIO_NOTE, utils->PrintF("Queue message %x (%d; type: %d) processed successfuly: %s",
            cQueueItem->id,
            cQueueItem->id,
            cQueueItem->type,
            cResult->statusInfo.c_str()));
    }

    // fatal error
    else if(cResult->status == QUEUE_STATUS_FATAL_ERROR)
    {
        // bounce
        if(cQueueItem->from.length() > 3)
            bBounced = MSGQueue::BounceMessage(cQueueItem, cResult);

        // delete
        db->Query("DELETE FROM bm60_bms_queue WHERE `id`=%d",
            cQueueItem->id);
        unlink(MSGQueue::QueueFileNameStr(cQueueItem->id).c_str());

        SetMailDeliveryStatus(cQueueItem->deliveryStatusID, MDSTATUS_DELIVERY_FAILED);

        // log
        db->Log(CMP_MSGQUEUE, PRIO_NOTE, utils->PrintF("Fatal error while processing queue message %x (%d; type: %d; status: %d): %s; %s",
            cQueueItem->id,
            cQueueItem->id,
            cQueueItem->type,
            cQueueItem->lastStatus,
            cResult->statusInfo.c_str(),
            bBounced ? "bounced" : "deleted"));
    }

    // temporary or unknown error
    else
    {
        cQueueItem->attempts++;
        cQueueItem->lastAttempt = (int)time(NULL);

        // lifetime exceeded => delete, bounce
        if(cQueueItem->date + atoi(cfg->Get("queue_lifetime")) < (int)time(NULL))
        {
            // bounce
            if(cQueueItem->from.length() > 3)
                bBounced = MSGQueue::BounceMessage(cQueueItem, cResult);

            db->Query("DELETE FROM bm60_bms_queue WHERE `id`=%d",
                cQueueItem->id);
            unlink(MSGQueue::QueueFileNameStr(cQueueItem->id).c_str());

            SetMailDeliveryStatus(cQueueItem->deliveryStatusID, MDSTATUS_DELIVERY_FAILED);

            db->Log(CMP_MSGQUEUE, PRIO_NOTE, utils->PrintF("Queue message %x (%d; type: %d; status: %d; %s) not processed after >= %d day(s) and %d attempt(s); %s",
                cQueueItem->id,
                cQueueItem->id,
                cQueueItem->type,
                cQueueItem->lastStatus,
                cResult->statusInfo.c_str(),
                atoi(cfg->Get("queue_lifetime")) / 86400,
                cQueueItem->attempts,
                bBounced ? "bounced" : "deleted"));
        }

        // still in lifetime
        else
        {
            db->Query("UPDATE bm60_bms_queue SET `active`=0,`attempts`='%d',`last_attempt`='%d',`last_status`='%d',`last_status_info`='%q',`last_status_code`='%q',`last_diagnostic_code`='%q' WHERE `id`=%d",
                cQueueItem->attempts,
                cQueueItem->lastAttempt,
                cQueueItem->lastStatus,
                cResult->statusInfo.c_str(),
                cResult->statusCode.c_str(),
                cResult->diagnosticCode.c_str(),
                cQueueItem->id);

            SetMailDeliveryStatus(cQueueItem->deliveryStatusID, MDSTATUS_DELIVERY_DEFERRED);

            db->Log(CMP_MSGQUEUE, PRIO_NOTE, utils->PrintF("(Temporarily) failed to deliver queue message %x (%d; type: %d; status: %d; %s); trying again later",
                cQueueItem->id,
                cQueueItem->id,
                cQueueItem->type,
                cQueueItem->lastStatus,
                cResult->statusInfo.c_str()));
        }
    }

    // abuse protect
    if(cResult->apPointType > 0
        && (cQueueItem->smtpUser > 0 || cQueueItem->b1gMailUser > 0))
    {
        int userID = (cQueueItem->b1gMailUser > 0) ? cQueueItem->b1gMailUser : cQueueItem->smtpUser;
        utils->AddAbusePoint(userID, cResult->apPointType, cResult->apPointComment.c_str());
    }
}

/*
 * Get queue items from DB.
 */
void MSGQueue::ProcessQueue()
{
    MYSQL_ROW row;

    db->Log(CMP_MSGQUEUE, PRIO_DEBUG, utils->PrintF("ProcessQueue() started"));

    // delete messages marked for deletion
    MySQL_Result *res = db->Query("SELECT `id` FROM bm60_bms_queue WHERE `deleted`=1 AND `active`=0");
    while((row = res->FetchRow()))
    {
        int iQueueID = atoi(row[0]);

        // delete
        db->Query("DELETE FROM bm60_bms_queue WHERE `id`=%d",
                 iQueueID);
        unlink(MSGQueue::QueueFileNameStr(iQueueID).c_str());

        // log
        db->Log(CMP_MSGQUEUE, PRIO_NOTE, utils->PrintF("Deleted queue message %x (was marked for deletion)",
            iQueueID));
    }
    delete res;

    // lock table (avoid double processing)
    db->Query("LOCK TABLES bm60_bms_queue WRITE");

    // determine count of messages to read
    unsigned int itemsToRead = QUEUE_RUN_MAX, queueSize = 0, i = 0;

    this->threadPool->lockQueue();
    queueSize = this->threadPool->queue.size();
    this->threadPool->unlockQueue();

    if(queueSize >= itemsToRead)
        itemsToRead = 0;
    else
        itemsToRead -= queueSize;

    // get messages
    queue<MSGQueueItem *> work;
    res = db->Query("SELECT `id`,`type`,`date`,`size`,`from`,`to`,`attempts`,`last_attempt`,`last_status`,`smtp_user`,LOWER(`to_domain`),`last_status_code`,`last_diagnostic_code`,`b1gmail_user`,`deliverystatusid` FROM bm60_bms_queue WHERE `deleted`=0 AND `last_attempt`<(%d-`attempts`*%d) AND `active`=0 ORDER BY `attempts`,`last_attempt`,`id` LIMIT %d",
                   (int)time(NULL), atoi(cfg->Get("queue_retry")),
                   itemsToRead+1);
    moreQueueEntries = res->NumRows() > itemsToRead;
    while((row = res->FetchRow()))
    {
        if(i == itemsToRead)
        {
            break;
        }
        else
        {
            MSGQueueItem *cQueueItem = new MSGQueueItem;
            this->RowToMSGQueueItem(row, cQueueItem);
            work.push(cQueueItem);
        }

        i++;
    }
    delete res;

    // set active
    if(!work.empty())
    {
        map<string, queue<MSGQueueItem *> >::iterator it;
        stringstream ssIDs;
        bool bFirstElem = true;

        this->threadPool->lockQueue();
        while(!work.empty())
        {
            MSGQueueItem *msg = work.front();

            if(bFirstElem)
                bFirstElem = false;
            else
                ssIDs << ",";
            ssIDs << msg->id;

            // add msg to queue
            this->threadPool->addToQueue(msg);

            work.pop();
        }
        this->threadPool->unlockQueue();

        db->Query("UPDATE bm60_bms_queue SET `active`=1 WHERE `id` IN(%s)",
            ssIDs.str().c_str());
    }

    // unlock table
    db->Query("UNLOCK TABLES");

    db->Log(CMP_MSGQUEUE, PRIO_DEBUG, utils->PrintF("ProcessQueue() finished"));
}

/*
 * Process a work item
 */
void MSGQueue::WorkProcessor(MSGQueueItem *item)
{
    // initialize result
    MSGQueueResult *result = new MSGQueueResult;
    result->item            = item;
    result->status          = QUEUE_STATUS_TEMPORARY_ERROR;
    result->statusInfo      = "Unknown error";
    result->diagnosticCode  = "";
    result->apPointType     = 0;
    result->apPointComment  = "";
    result->statusCode      = "";

    // check if a delivery rule matches this item
    const DeliveryRule *rule = this->deliveryRules->FindMatch(item);

    // process
    try
    {
        if(rule == NULL)
        {
            switch(item->type)
            {
            case MESSAGE_INBOUND:
                this->ProcessInbound(item, result);
                break;

            case MESSAGE_OUTBOUND:
                this->ProcessOutbound(item, result);
                break;

            default:
                throw DeliveryException("WorkProcessor",
                    "Unknown queue item type.",
                    QUEUE_STATUS_FATAL_ERROR,
                    "5.3.5");
                break;
            };
        }
        else
        {
            this->ProcessRule(item, result, rule);
        }
    }
    catch(DeliveryException &ex)
    {
        result->status  = ex.errorCode;

        if(ex.host.empty())
            result->statusInfo = ex.statusInfo;
        else
            result->statusInfo = ex.host + ": " + ex.statusInfo;

        result->statusCode = ex.statusCode;
        result->diagnosticCode = ex.diagnosticCode;
    }

    // store result
    pthread_mutex_lock(&this->resultsLock);
    this->results.push(result);
    this->resultsWaiting = true;
    pthread_cond_signal(&this->resultsAvailable);
    pthread_mutex_unlock(&this->resultsLock);
}

/*
 * Static work processor entry point
 */
void MSGQueue::WorkProcessorEntry(void *arg)
{
    cMSGQueue_Instance->WorkProcessor((MSGQueueItem *)arg);
}

/*
 * Fill MSGQueueItem with row data
 */
void MSGQueue::RowToMSGQueueItem(MYSQL_ROW row, MSGQueueItem *cQueueItem)
{
    cQueueItem->id                  = atoi(row[0]);
    cQueueItem->type                = (eMessageType)atoi(row[1]);
    cQueueItem->date                = atoi(row[2]);
    cQueueItem->size                = atoi(row[3]);
    cQueueItem->domain              = row[10];
    cQueueItem->from                = row[4];
    cQueueItem->to                  = row[5];
    cQueueItem->attempts            = atoi(row[6]);
    cQueueItem->lastAttempt         = atoi(row[7]);
    cQueueItem->lastStatus          = atoi(row[8]);
    cQueueItem->smtpUser            = atoi(row[9]);
    cQueueItem->lastStatusCode      = row[11];
    cQueueItem->lastDiagnosticCode = row[12];
    cQueueItem->b1gMailUser         = atoi(row[13]);
    cQueueItem->deliveryStatusID    = atoi(row[14]);
}

/*
 * Generate a queue file name
 */
string MSGQueue::QueueFileNameStr(int iID)
{
    char szHexID[16];
    string strFileName = "";

    strFileName = GET_QUEUE_DIR();
    strFileName.append(1, PATH_SEP);

    // convert ID to hex
    if(snprintf(szHexID, 16, "%08X", iID) < 8 || strlen(szHexID) != 8)
        throw Core::Exception("ID to hex conversion failed");

    // directory
    strFileName.append(1, szHexID[7]);
    strFileName.append(1, PATH_SEP);

    // filename
    strFileName.append(szHexID);

    // return
    return(strFileName);
}

/*
 * Generate queue filename, const char version, not thread safe
 */
const char *MSGQueue::QueueFileName(int iID)
{
    static string strFileName = "";
    strFileName = this->QueueFileNameStr(iID);
    return(strFileName.c_str());
}
