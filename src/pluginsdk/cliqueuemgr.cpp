#define ENABLE_BMS_IO       // Pipe I/O through bMS
#include "bmsplugin.h"

#include <stdio.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#ifndef WIN32
#include <unistd.h>
#endif

// 
// Plugin class definition
//
class CLIQueueManager : public b1gMailServer::Plugin
{
public:     // Constructor / destructor
    CLIQueueManager();
    virtual ~CLIQueueManager() { }
    
public:     // Event handlers
    virtual void OnDisplayModeHelp();
    virtual bool OnCallWithoutValidMode(int argc, char **argv, int &ResultCode);
    
private:    // Internally used functions
    void _ShowMSGQueue();
    void _FlushMSGQueue();
    void _ClearMSGQueue();
};

//
// Export plugin class to b1gMailServer
//
EXPORT_BMS_PLUGIN(CLIQueueManager);

//
// Plugin class implementation
//

/**
 * Constructor
 */
CLIQueueManager::CLIQueueManager()
{
    this->Name          = "CLIQueueManager";
    this->Title         = "Command Line Queue Manager";
    this->Version       = "1.0.0";
    this->Author        = "B1G Software";
    this->AuthorWebsite = "http://www.b1g.de";
    this->UpdateURL     = "http://service.b1gmail.com/plugin_updates/";
}

/**
 * Display help
 */
void CLIQueueManager::OnDisplayModeHelp()
{
    printf("   --show-msgqueue   Show message queue\n");
    printf("   --flush-msgqueue  Flush message queue\n");
    printf("   --clear-msgqueue  Erase ALL inactive messages in message queue\n");
}

/**
 * Handle our command line switches
 */
bool CLIQueueManager::OnCallWithoutValidMode(int argc, char **argv, int &ResultCode)
{
    bool ShowMSGQueue = false,
    FlushMSGQueue = false,
    ClearMSGQueue = false;
    
    for(int i=0; i<argc; i++)
        if(strcmp(argv[i], "--show-msgqueue") == 0)
        {
            ShowMSGQueue = true;
            break;
        }
        else if(strcmp(argv[i], "--flush-msgqueue") == 0)
        {
            FlushMSGQueue = true;
            break;
        }
        else if(strcmp(argv[i], "--clear-msgqueue") == 0)
        {
            ClearMSGQueue = true;
            break;
        }
    
    if(!ShowMSGQueue && !FlushMSGQueue && !ClearMSGQueue)
        return(false);
    
    if(ShowMSGQueue)
    {
        this->_ShowMSGQueue();
    }
    else if(FlushMSGQueue)
    {
        printf("Flushing queue... ");
        fflush(stdout);
        
        this->_FlushMSGQueue();
        
        printf("Done\n");
    }
    else if(ClearMSGQueue)
    {
        printf("Clearing queue... ");
        fflush(stdout);
        
        this->_ClearMSGQueue();
        
        printf("Done\n");
    }
    
    ResultCode = 0;
    return(true);
}

/**
 * --show-msgqueue
 */
void CLIQueueManager::_ShowMSGQueue()
{
    char **row;
    b1gMailServer::MySQL_DB *db = this->BMSUtils->GetMySQLConnection();
    b1gMailServer::MySQL_Result *res;
    unsigned int iOverallSize = 0, iMessageCount = 0;
    
    printf("-Msg ID- -Flags- --Size-- -------Arrival Time------- -Attempts- -Sender/Recipient-------\n");
    
    res = db->Query("SELECT `id`,`active`,`type`,`date`,`size`,`from`,`to`,`attempts`,`last_attempt`,`last_status`,`smtp_user`,`last_status_info` FROM bm60_bms_queue WHERE `deleted`=0 ORDER BY `id` ASC");
    while((row = res->FetchRow()))
    {
        size_t nPos;
        
        // counter
        iOverallSize += atoi(row[4]);
        iMessageCount++;
        
        // flags
        std::string strFlags = "";
        if(atoi(row[2]) == BMS_MESSAGE_INBOUND)
            strFlags += "<";
        else if(atoi(row[2]) == BMS_MESSAGE_OUTBOUND)
            strFlags += ">";
        else
            strFlags += "?";
        if(atoi(row[10]) > 0)
            strFlags += "L";
        else
            strFlags += "l";
        if(atoi(row[1]) == 1)
            strFlags += "A";
        else
            strFlags += "a";
        
        // format date
        time_t iDate = (time_t)atoi(row[3]);
        struct tm *cTimeInfo = localtime(&iDate);
        std::string strTime = asctime(cTimeInfo);
        while((nPos = strTime.find_last_of("\r\n")) != std::string::npos)
            strTime.erase(nPos, 1);
        
        // meta + sender
        printf("%08X % 7s % 8d % 26s % 10d  %s\n",
               atoi(row[0]),
               strFlags.c_str(),
               atoi(row[4]),
               strTime.c_str(),
               atoi(row[7]),
               row[5]);
        
        // last status info
        if(strlen(row[11]) > 0)
        {
            std::string strStatusInfo = row[11];
            
            while((nPos = strStatusInfo.find("\r\n")) != std::string::npos)
                strStatusInfo.replace(nPos, 2, "; ");
            
            while((nPos = strStatusInfo.find_last_of("\r\n")) != std::string::npos)
                strStatusInfo.erase(nPos, 1);
            
            printf("(%s)\n",
                   strStatusInfo.c_str());
        }
        
        // recipient
        printf("% 63s  %s\n\n",
               "",
               row[6]);
    }
    delete res;
    
    printf("-- %d KiB in %d entries.\n",
           (int)ceil((double)iOverallSize / 1024.0),
           iMessageCount);      
}

/**
 * --flush-msgqueue
 */
void CLIQueueManager::_FlushMSGQueue()
{
    b1gMailServer::MySQL_DB *db = this->BMSUtils->GetMySQLConnection();
    const char *QueueStateFile = this->BMSUtils->GetQueueStateFilePath();
    
    // flush
    db->Query("UPDATE bm60_bms_queue SET `last_attempt`=0 WHERE `deleted`=0");
    
    // touch state file
    this->BMSUtils->Touch(QueueStateFile);
}

/**
 * --clear-msgqueue
 */
void CLIQueueManager::_ClearMSGQueue()
{
    b1gMailServer::MSGQueue *mq = this->BMSUtils->CreateMSGQueueInstance();
    b1gMailServer::MySQL_DB *db = this->BMSUtils->GetMySQLConnection();
    b1gMailServer::MySQL_Result *res;
    char **row;
    
    // lock tables
    db->Query("LOCK TABLES bm60_bms_queue WRITE");
    
    // remove message files
    res = db->Query("SELECT `id` FROM bm60_bms_queue WHERE `active`=0");
    while((row = res->FetchRow()))
    {
        unlink(mq->QueueFileName(atoi(row[0])));
    }
    delete res;
    
    // delete messages from DB
    db->Query("DELETE FROM bm60_bms_queue WHERE `active`=0");
    
    // unlock tables
    db->Query("UNLOCK TABLES");
    
    // clean up
    delete mq;
}
