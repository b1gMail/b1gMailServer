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

#ifndef _BMSPLUGIN_H
#define _BMSPLUGIN_H

#include <stdio.h>
#include <string>
#include <vector>

/**
 * Plugin interface version
 */
#define BMS_PLUGIN_INTERFACE_VERSION    12

/**
 * Log priority constants
 */
#define BMS_PRIO_NOTE                   1
#define BMS_PRIO_WARNING                2
#define BMS_PRIO_ERROR                  4
#define BMS_PRIO_DEBUG                  8

/**
 * Log component constants
 */
#define BMS_CMP_CORE                    1
#define BMS_CMP_POP3                    2
#define BMS_CMP_IMAP                    4
#define BMS_CMP_HTTP                    8
#define BMS_CMP_SMTP                    16
#define BMS_CMP_MSGQUEUE                32
#define BMS_CMP_PLUGIN                  64

/**
 * Message type constants
 */
#define BMS_MESSAGE_INBOUND             0
#define BMS_MESSAGE_OUTBOUND            1

/**
 * Peer origin constants
 */
#define BMS_SMTP_PEER_ORIGIN_UNKNOWN    0
#define BMS_SMTP_PEER_ORIGIN_DEFAULT    1
#define BMS_SMTP_PEER_ORIGIN_TRUSTED    2
#define BMS_SMTP_PEER_ORIGIN_DIALUP     3
#define BMS_SMTP_PEER_ORIGIN_REJECT     4

/**
 * something special for redmond
 */
#ifdef WIN32
#include <windows.h>
#define EXPORT_FUNCTION                 __declspec(dllexport)
#define LIB_ENTRY_POINT()               BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD fwdReason, LPVOID lpvReserved) \
                                        { \
                                            return(TRUE); \
                                        }
#else
#define EXPORT_FUNCTION
#define LIB_ENTRY_POINT()
#endif

/**
 * Export a plugin class
 *
 * @var name Class name
 */
#define EXPORT_BMS_PLUGIN(name)         extern "C" EXPORT_FUNCTION b1gMailServer::Plugin *CreatePluginInstance() \
                                        { \
                                            return(new name()); \
                                        } \
                                        extern "C" EXPORT_FUNCTION void DestroyPluginInstance(b1gMailServer::Plugin *ptr) \
                                        { \
                                            delete ptr; \
                                        } \
                                        extern "C" EXPORT_FUNCTION int GetPluginInterfaceVersion() \
                                        { \
                                            return(BMS_PLUGIN_INTERFACE_VERSION); \
                                        } \
                                        LIB_ENTRY_POINT()

/**
 * Define ENABLE_BMS_IO in order to pipe I/O through bMS (allows correct communication in TLS mode)
 */
#ifdef ENABLE_BMS_IO
#define fwrite                          this->BMSUtils->IO_fwrite
#define printf                          this->BMSUtils->IO_printf
#define fgets                           this->BMSUtils->IO_fgets
#define fread                           this->BMSUtils->IO_fread
#endif

/**
 * b1gMailServer namespace
 */
namespace b1gMailServer
{
    /*
     * MSGQueue class
     */
    class MSGQueue
    {
    public:
        virtual ~MSGQueue() { }

    public:
        /**
         * Generate queue file name
         *
         * @var int iID Queue ID
         * @return const char * Absolute path
         */
        virtual const char *QueueFileName(int iID) = 0;
    };

    /*
     * MySQL result class
     */
    class MySQL_Result
    {
    public:
        virtual ~MySQL_Result() { }

    public:
        /**
         * Fetch a row from the result set
         *
         * @return char ** Row (success) or NULL (error / end of result set), automatically freed when fetching new row or deleting the result object
         */
        virtual char **FetchRow() = 0;

        /**
         * Get number of rows in the result set
         *
         * @return unsigned long Number
         */
        virtual unsigned long NumRows() = 0;
    };

    /*
     * MySQL DB class
     */
    class MySQL_DB
    {
    public:
        virtual ~MySQL_DB() { }

    public:
        /**
         * Perform a MySQL query
         *
         * @var char *strQuery Query, may contain one of the following format strings: %s (string), %d (int), %f (float), %li (long int), %lu (unsigned long), %q (escaped string)
         * @return MySQL_Result * Result object (success, available result set) or NULL (error or no available result set)
         */
        virtual MySQL_Result *Query(const char *strQuery, ...) = 0;

        /**
         * Get the latest insert ID
         *
         * @return unsigned long ID
         */
        virtual unsigned long InsertId() = 0;

        /**
         * Write a log entry
         *
         * @var int iComponent Component (BMS_CMP_ constant)
         * @var int iSeverity Severity (BMS_PRIO_ constant)
         * @var const char *szEntry Entry (freed by Log() - always use Utils::PrintF!)
         */
        virtual void Log(int iComponent, int iSeverity, char *szEntry) = 0;
    };

    /*
     * Config class
     */
    class Config
    {
    public:
        virtual ~Config() { }

    public:
        /**
         * Get a b1gmailserver.cfg or bm60_bms_prefs config entry
         *
         * @var char *szKey Key
         * @return char * Value (DO NOT free)
         */
        virtual const char *Get(const char *szKey) = 0;
    };

    /*
     * Mail class
     */
    class Mail
    {
    public:
        virtual ~Mail() { }

    public:
        /**
         * Get (decoded) header field value
         *
         * @var char *key Key
         * @return const char * Value (DO NOT free) or NULL if not found
         */
        virtual const char *GetHeader(const char *key) const = 0;

        /**
         * Get (raw) header field value
         *
         * @var char *key Key
         * @return const char * Value (DO NOT free) or NULL if not found
         */
        virtual const char *GetRawHeader(const char *key) const = 0;

        /**
         * Parse header line
         *
         * @var const char *line Line
         */
        virtual void Parse(const char *line) = 0;

        /**
         * Extract mail addresses from string
         *
         * @var char *in Input string
         * @var std::vector<char *> *to Output vector (free all elements after use!)
         */
        virtual void ExtractMailAddresses(const char *in, std::vector<char *> *to) = 0;
    };

    /*
     * Utilities class
     */
    class Utils
    {
    public:
        virtual ~Utils() { }

    public:
        /**
         * Base64-encode a string
         *
         * @var char *Input Input string
         * @var bool bModified Use modified Base64 for IMAP use?
         * @var int Len Length of input string (when using -1, length is automatically determined using strlen())
         * @return char * Encoded string (free after use!)
         */
        virtual char *Base64Encode(const char *Input, bool bModified = false, int Len = -1) = 0;

        /**
         * Base64-decode a string
         *
         * @var char *Input Input string
         * @var bool bModified Use modified Base64 for IMAP use?
         * @var int *Len Output of length of decoded string
         * @return char * Decoded string (free after use!)
         */
        virtual char *Base64Decode(const char *Input, bool bModified = false, int *Len = NULL) = 0;

        /**
         * Convert 1/0 string to bool
         *
         * @var char *Input
         * @return bool Boolean equivalent
         */
        virtual bool StrToBool(const char *Input) = 0;

        /**
         * Convert string to lower case
         *
         * @var char *Input Input string
         * @return char * Pointer to Input
         */
        virtual char *StrToLower(char *Input) = 0;

        /**
         * Convert string to upper case
         *
         * @var char *Input Input string
         * @return char * Pointer to Input
         */
        virtual char *StrToUpper(char *Input) = 0;

        /**
         * Check if file exists
         *
         * @var char *FileName File name
         * @return bool Result
         */
        virtual bool FileExists(const char *FileName) = 0;

        /**
         * Determine file size
         *
         * @var char *FileName File name
         * @return size_t File size
         */
        virtual size_t FileSize(const char *FileName) = 0;

        /**
         * Generate a random string
         *
         * @var char *szKey Output buffer
         * @var int iLength Length of string to generate
         */
        virtual void MakeRandomKey(char *szKey, int iLength) = 0;

        /**
         * Format a string
         *
         * @var char *Format Format (may use %s/d/l/f/x without modifiers etc.)
         * @var char *Result Result string (free after use!)
         */
        virtual char *PrintF(const char *Format, ...) = 0;

        /**
         * Get address of connected peer
         *
         * @var bool IgnoreDisableIPLog Ignore disabled IP log?
         * @return char * Peer address (success; DO NOT free) or NULL (error / disabled IP log)
         */
        virtual const char *GetPeerAddress(bool IgnoreDisableIPLog = false) = 0;

        /**
         * Get data file name
         *
         * @var int ID
         * @var const char *Ext Extension
         * @var bool Create Create directories if not existent?
         * @return char * Absoulte path (free after use)
         */
        virtual char *MailPath(int ID, const char *Ext = "msg", bool Create = false) = 0;

        /**
         * Get user ID for alias
         *
         * @var char *EMail Email address
         * @return int User ID (success) or 0 (error)
         */
        virtual int GetAlias(const char *EMail) = 0;

        /**
         * Get user ID for email address
         *
         * @var char *EMail Email address
         * @var bool findDeleted Find users which are marked as deleted?
         * @return int User ID (success) or 0 (error)
         */
        virtual int LookupUser(const char *EMail, bool findDeleted = true) = 0;

        /**
         * Sleep for milliseconds
         *
         * @var unsigned int Milliseconds
         */
        virtual void MilliSleep(unsigned int MilliSeconds) = 0;

        /**
         * Touch a file (update last modified time)
         *
         * @var char *FileName File name
         * @bool Success
         */
        virtual bool Touch(const char *FileName) = 0;

        /**
         * Get host name by IP addres
         *
         * @var char *IPAddress IP address
         * @var bool *success If non-NULL, output for result status
         * @return const char * Host name (success) or "" (error)
         */
        virtual const char *GetHostByAddr(const char *IPAddress, bool *success = NULL) = 0;

        /**
         * Post an event to the bm60_bms_events table
         *
         * @var int UserId User ID
         * @var int EventType Event type
         * @var int Param1 First parameter
         * @var int Param2 Second parameter
         */
        virtual void PostEvent(int UserID, int EventType, int Param1 = 0, int Param2 = 0) = 0;

        /**
         * Get local time zone <-> GMT difference
         *
         * @return int Difference
         */
        virtual int GetTimeZone() = 0;

        /**
         * Get a MySQL_DB object
         *
         * @return MySQL_DB * Object (DO NOT delete)
         */
        virtual MySQL_DB *GetMySQLConnection() = 0;

        /**
         * Get a Config object
         *
         * @return Config * Object (DO NOT delete)
         */
        virtual Config *GetConfig() = 0;

        /**
         * Create a MSGQueue instance
         *
         * @return MSGQueue * Object (delete afer use!)
         */
        virtual MSGQueue *CreateMSGQueueInstance() = 0;

        /**
         * Create a Mail instance
         *
         * @return Mail * Object (delete afer use!)
         */
        virtual Mail *CreateMailInstance() = 0;

        /**
         * Get path of queue state file
         *
         * @return std::string Absolute path
         */
        virtual const char *GetQueueStateFilePath() = 0;

        /**
         * Get path of queue dir
         *
         * @return std::string Absolute path
         */
        virtual const char *GetQueueDir() = 0;

        /**
         * Internally used I/O functions
         */
        virtual int IO_printf(const char *str, ...) = 0;
        virtual size_t IO_fwrite(const void *buf, size_t s1, size_t s2, FILE *fp) = 0;
        virtual char *IO_fgets(char *buf, int s, FILE *fp) = 0;
        virtual size_t IO_fread(void *buf, size_t s1, size_t s2, FILE *fp) = 0;
    };

    /*
     * Plugin base class
     */
    class Plugin
    {
    public:
        /**
         * Constructor
         */
        Plugin() { }

        /**
         * Destructor
         */
        virtual ~Plugin() { }

    public:
        /**
         * Init plugin
         * (calles at bMS startup)
         */
        virtual void Init() { }

        /**
         * Uninitialize plugin
         * (called before bMS exit)
         */
        virtual void UnInit() { }

        /**
         * Classify a SMTP peer
         *
         * @var int IPAddress IP address
         * @return BMS_SMTP_PEER constant
         */
        virtual int ClassifySMTPPeer(int IPAddress) { return(BMS_SMTP_PEER_ORIGIN_UNKNOWN); }

        /**
         * Classify a SMTP peer (IPv6)
         *
         * @var unsigned char IPAddress[16]
         * @return BMS_SMTP_PEER constant
         */
        virtual int ClassifySMTPPeer(unsigned char IPAddress[16]) { return(BMS_SMTP_PEER_ORIGIN_UNKNOWN); }

        /**
         * Get Mail signature for user (text format)
         *
         * @param int UserID User ID
         * @param const char * Separator Signature separator string
         * @param FILE * fpSignature Destination stream
         * @return bool Signature written?
         */
        virtual bool GetUserMailSignature(int UserID, const char *Separator, FILE *fpSignature) { return(false); }

        /**
         * Called on user login (POP3/SMTP/IMAP)
         *
         * @var int UserID User ID
         * @var const char * EMail User email address
         * @var const char * Password User plain text password
         */
        virtual void OnLoginUser(int UserID, const char *EMail, const char *Password) { }

        /**
         * Called on user logout
         */
        virtual void OnLogoutUser() { }

        /**
         * Called when opening a mail body
         *
         * @var FILE *RawMessage File pointer to message body
         * @return FILE * NEW file pointer with modified message body or NULL
         */
        virtual FILE *OnOpenMailBody(FILE *RawMessage) { return(NULL); }

        /**
         * Called when saving a mail body
         *
         * @var FILE *RawMessage File pointer to message body
         * @return FILE * NEW file pointer with modified message body or NULL
         */
        virtual void OnSaveMailBody(FILE *RawMessage) { }

        /**
         * Called when displaying command line help
         */
        virtual void OnDisplayModeHelp() { }

        /**
         * Called when bMS is called without a valid mode parameter
         *
         * @var int argc Argument count
         * @var char **argv Arguments
         * @var int &ResultCode bMS exit code
         * @return bool TRUE if plugin is able to handle mode (-> bMS quits), FALSE otherwise (-> bMS tries other plugins)
         */
        virtual bool OnCallWithoutValidMode(int argc, char **argv, int &ResultCode) { return(false); }

        /**
         * Called when no valid user could be found
         *
         * @var const char * EMail
         * @return int User ID (> 0) for existing email addresses of real users, -1 for existing email addresses which do not belong to a specific user, 0 if not found
         */
        virtual int OnLookupUser(const char *EMail) { return(0); }

    public:
        virtual void SetFileName(const char *FileName) { this->FileName = FileName; }

    public:
        /**
         * Plugin name
         * Set by plugin
         */
        std::string Name;

        /**
         * Plugin title
         * Set by plugin
         */
        std::string Title;

        /**
         * Plugin version
         * Set by plugin
         */
        std::string Version;

        /**
         * Plugin author name
         * Set by plugin
         */
        std::string Author;

        /**
         * Plugin author website
         * Set by plugin
         */
        std::string AuthorWebsite;

        /**
         * Plugin update URL
         * Set by plugin
         */
        std::string UpdateURL;

        /**
         * Plugin .so-/.dylib-/.dll file name (without path)
         * Set by bMS
         */
        std::string FileName;

    public:
        /**
         * Utils instance
         */
        Utils *BMSUtils;
    };
};

#endif
