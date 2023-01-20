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

#ifndef _CORE_UTILS_H_
#define _CORE_UTILS_H_

#include <core/core.h>
#include <plugin/plugin.h>
#include <fcntl.h>
#include <math.h>
#ifdef WIN32
#include <sys/utime.h>
#else
#include <utime.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif
#include <pthread.h>
#include <set>
#include <map>

#ifdef __BIG_ENDIAN__
#define ENDIAN(x)       utils->ConvertEndianess(x)
#else
#define ENDIAN(x)       x
#endif

#define ENCODED_CHUNK_SIZE          4
#define DECODED_CHUNK_SIZE          3
#define IS_BASE64_CHAR(cm)          (((cm >= 'A') && (cm <= 'Z')) || ((cm >= 'a') && (cm <= 'z')) || ((cm >= '0') && (cm <= '9')) || (cm == '+') || (cm == '/'))
#define IS_HEX_CHAR(cm)             (((cm >= 'A') && (cm <= 'F')) || ((cm >= 'a') && (cm <= 'f')) || ((cm >= '0') && (cm <= '9')))
#define IS_DIGIT(cm)                ((cm >= '0') && (cm <= '9'))
#define IS_UPPER(cm)                ((cm >= 'A') && (cm <= 'F'))
#define MAX(x, y)                   (x > y ? x : y)
#define MIN(x, y)                   (x < y ? x : y)

#define DEFAULT_QUEUE_DIR           "/opt/b1gmailserver/queue"
#define GET_QUEUE_DIR()             (cfg->Get("queue_dir") == NULL ? DEFAULT_QUEUE_DIR : cfg->Get("queue_dir"))
#define QUEUE_STATE_FILE            "state"

#define BMS_EVENT_STOREMAIL         1
#define BMS_EVENT_DELETEMAIL        2
#define BMS_EVENT_MOVEMAIL          3
#define BMS_EVENT_CHANGEMAILFLAGS   4

#define BMS_CORE_FEATURE_TLS        1
#define BMS_CORE_FEATURE_ALTERMIME  2

#define FAILBAN_POP3LOGIN           1
#define FAILBAN_IMAPLOGIN           2
#define FAILBAN_SMTPLOGIN           4
#define FAILBAN_SMTPRCPT            8
#define FAILBAN_FTPLOGIN            1

#define BMAP_SEND_RECP_LIMIT            1
#define BMAP_SEND_FREQ_LIMIT            2
#define BMAP_SEND_RECP_BLOCKED          3
#define BMAP_SEND_RECP_LOCAL_INVALID    4
#define BMAP_SEND_RECP_DOMAIN_INVALID   5
#define BMAP_SEND_WITHOUT_RECEIVE       6
#define BMAP_RECV_FREQ_LIMIT            21
#define BMAP_RECV_TRAFFIC_LIMIT         22

#define PEER_IP()                   ((!strPeer.empty() && strPeer != "(unknown)") ? IPAddress(strPeer) : 0)

int ts_rand();

namespace Core
{
    class LockGuard
    {
    public:
        LockGuard(pthread_mutex_t *mutex)
        {
            this->mutex_ = mutex;
            pthread_mutex_lock(this->mutex_);
        }

        ~LockGuard()
        {
            pthread_mutex_unlock(this->mutex_);
        }

    private:
        pthread_mutex_t *mutex_;

        LockGuard(const LockGuard &);
        LockGuard &operator=(const LockGuard &);
    };

    class IPAddress
    {
    public:
        IPAddress(in6_addr ip);
        IPAddress(in_addr ip);
        IPAddress(in_addr_t ip);
        IPAddress(const string &ip);

    public:
        string dbString() const;
        bool isLocalhost() const;
        void toCharBuff(char *buffer, int bufferLength) const;
        string toReversedString();
        bool matches(const string &subnet) const;
        bool matches(const string &net, const string &mask) const;
        bool isMappedIPv4() const;
        IPAddress mappedIPv4() const;

    public:
        bool isIPv6;
        in6_addr v6Addr;
        in_addr v4Addr;
    };

    class MySQL_DB;
    class BlobStorageProvider;

    class Utils : public b1gMailServer::Utils
    {
    public:
        string GetAPNSTopic();

        void InitializeIMAPUIDs();

        bool MaySendMail(int userID, int recipientCount, int sendLimitCount, int sendLimitTime, int *actualRecipientCount = NULL);

        unsigned long AddSendStat(int userID, int recipientCount);

        BlobStorageProvider *CreateBlobStorageProvider(int id, int userID = -1);

        bool DeleteBlob(int blobStorageID, int type, int id, int userID);

        bool DomainMatches(const string &host1, const string &host2);

        void GetLocalDomains(set<string> &domains);

        // is string a valid number?
        bool IsNumeric(const string &in);

        // get user pref
        string GetUserPref(int userID, const string &name, const string &defaultValue = "");

        // get group option
        string GetGroupOption(int userID, const string &name, const string &defaultValue = "");

        // escape pcre special chars
        string PCREEscape(const string &str);

        // check if regular expression matches string
        bool Match(string expr, const string &str, bool caseless = false);

        // check if recipient is blocked
        bool IsRecipientBlocked(const string &addresss);

        // md5
        string MD5(const string &input);

        // parse IMAP AUTHENTICATE PLAIN token
        void ParseIMAPAuthPlain(const string &input, string &user, string &password);

        // explode outside of quotation
        void ExplodeOutsideOfQuotation(const string &Str, vector<string> &Result, char Sep = ' ', bool preserveQuotes = false);

        // explode
        void Explode(const string &Str, vector<string> &Result, char Sep = ' ');

        // check if szAddress is a correct sender address for iUserID
        bool IsValidSenderAddressForUser(int iUserID, const char *szAddress);

        // failban check function
        bool Failban_IsBanned(const IPAddress &ip, char iType);

        // failban bad login notifier
        bool Failban_LoginFailed(const IPAddress &ip, char iType);

        // trim a string
        string Trim(const string &s, const std::string &drop = " \r\n\t");

        // rtrim a string
        string RTrim(const string &s, const std::string &drop = " \r\n\t");

        // base64 encode
        char *Base64Encode(const char *szString, bool bModified = false, int iLen = -1);

        // base64 decode
        char *Base64Decode(const char *szString, bool bModified = false, int *iLen = NULL);

        // convert string to boolean
        bool StrToBool(const char *szStr);

        // convert string to lowercase
        char *StrToLower(char *szStr);
        string StrToLower(const string &in);

        // convert string to uppercace
        char *StrToUpper(char *szStr);

        // check if file exists
        bool FileExists(const char *szFile);

        // get file size
        size_t FileSize(const char *szFile);

        // initialize utils
        void Init();

        // uninitialize utils
        void UnInit();

        // generate a random key
        void MakeRandomKey(char *szKey, int iLength);

        // convert endianess of an integer
        long ConvertEndianess(long in);

        // log function
        void Log(const char *szMessage, ...);

        // safe mmalloc
        void *SafeMalloc(size_t iSize);

        // safe strdup
        char *SafeStrDup(const char *in);

        // nicer printf function
        char *PrintF(const char *szFormat, ...);

        // nicer printf function
        char *VPrintF(const char *szFormat, va_list arglist);

        // get peer address
        const char *GetPeerAddress(bool bReally = false);
        unsigned short GetPeerPort();

        // get mail path
        char *MailPath(int iID, const char *szExt = "msg", bool bCreate = false);

        // check if alias exists
        int GetAlias(const char *szEMail);

        // lookup user
        int LookupUser(const char *szAddress, bool findDeleted = true);

        // sleep for milliseconds
        void MilliSleep(unsigned int milliSeconds);

        // touch a file
        bool Touch(const char *szFileName);

        // get hostname by ip address
        const char *GetHostByAddr(const char *szIPAddress, bool *success = NULL);

        // post event
        void PostEvent(int iUserID, int iEventType, int iParam1 = 0, int iParam2 = 0);

        // get alter mime path
        char *GetAlterMimePath();

        // get plugin path
        string GetPluginPath();

        // tls mode available?
        bool TLSAvailable();

        // begin TLS
        bool BeginTLS(SSL_CTX *ssl_ctx, char *szErrorOut, int iTimeout = 0);

        // get SHA256 hash over cert in DER format
        string CertHash();

        // set TLS DH params
        void SetTLSDHParams(SSL_CTX *ssl_ctx, const char *dhPath);

        // get core features bitmask
        int GetCoreFeatures();

        // get timezone difference
        int GetTimeZone();

        // bidi popen
#ifdef WIN32
        HANDLE POpen(const char *command, int *infp, int *outfp);
#else
        pid_t POpen(const char *command, int *infp, int *outfp);
#endif

        // get mysql_db instance
        b1gMailServer::MySQL_DB *GetMySQLConnection();

        // get msgqueue instance
        b1gMailServer::MSGQueue *CreateMSGQueueInstance();

        // get mail instance
        b1gMailServer::Mail *CreateMailInstance();

        // get config instance
        b1gMailServer::Config *GetConfig();

        // get message stream
        FILE *GetMessageFP(int ID, int UserID = 0);

        // get queue state file path
        const char *GetQueueStateFilePath();

        // get queue dir
        const char *GetQueueDir();

        // set socket receive timeout
        bool SetSocketRecvTimeout(int sock, int seconds);

        // convert timestamp to string
        string TimeToString(time_t theTime);

        // add abuse point
        int AddAbusePoint(int userID, int type, const char *comment, ...);

        // get abuse type config
        string GetAbuseTypeConfig(int type, const string &key);

        // I/O functions
        int IO_printf(const char *str, ...);
        size_t IO_fwrite(const void *buf, size_t s1, size_t s2, FILE *fp);
        char *IO_fgets(char *buf, int s, FILE *fp);
        size_t IO_fread(void *buf, size_t s1, size_t s2, FILE *fp);

    public:
        static pthread_mutex_t *OpenSSL_locks;
        static unsigned long OpenSSL_id_function();
        static void OpenSSL_locking_function(int mode, int type, const char *file, int line);
        MySQL_DB *db;

    private:
        map<int, bool> apEnabledCache;
        map<int, int> apTypeCache;
        map<int, map<string, string> > apConfigCache;
    };
}

extern Core::Utils *utils;

#endif
