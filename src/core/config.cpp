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

#include <core/config.h>

using namespace Core;

#define CHECKVAL(x)     if(this->Get(x) == NULL) { throw Core::Exception("Required configuration value not defined", x); }

Config::Config()
{
    pthread_mutex_init(&this->mutex, NULL);

    Core::LockGuard lg(&this->mutex);

    // build config file path
#ifndef WIN32
    string strCfgPath = "/opt/b1gmailserver/b1gmailserver.cfg";
#else

    char szBuff[MAX_PATH];
    GetModuleFileName(GetModuleHandle(NULL), szCfgPath, MAX_PATH);

    char *szSlash = strrchr(szCfgPath, '\\');
    if(szSlash == NULL || szSlash == szSlash + strlen(szSlash))
        throw Core::Exception("Config", "Cannot find myself");

    *++szSlash = '\0';

    string strCfgPath = string(szBuff) + string("..\\b1gmailserver.cfg");
#endif

    // check if config file exists
    if(!utils->FileExists(strCfgPath.c_str()))
        throw Core::Exception("Config", "Config file not found");

    // open and read file
    FILE *fp;
    if((fp = fopen(strCfgPath.c_str(), "r")) == NULL)
        throw Core::Exception("Config", "Cannot open config file");
    char szBuffer[512],
        *szEQ;
    while(fgets(szBuffer, 512, fp) != NULL)
    {
        if(szBuffer[0] != '#' && (szEQ = strchr(szBuffer, '=')) != NULL)
        {
            string key = utils->Trim(string(szBuffer, szEQ-szBuffer));
            string val = utils->Trim(string(szEQ+1));
            this->items[key] = val;
        }
    }
    fclose(fp);
}

Config::~Config()
{
    pthread_mutex_destroy(&this->mutex);
}

void Config::ReadDBConfig()
{
    Core::LockGuard lg(&this->mutex);

    // bm60_prefs
    MYSQL_ROW row;
    MySQL_Result *res = db->Query("SELECT datafolder,storein,domains,b1gmta_host,structstorage,selffolder,blocked FROM bm60_prefs LIMIT 1");
    MYSQL_FIELD *fields = res->FetchFields();
    unsigned long iNumFields = res->NumFields();
    while((row = res->FetchRow()))
    {
        for(unsigned int i = 0; i<iNumFields; i++)
        {
            this->items[fields[i].name] = row[i];
        }
    }
    delete res;

    // bm60_bms_prefs
    res = db->Query("SELECT core_version,core_features,pop3greeting,imapgreeting,loglevel,altpop3,minpop3,minimap,smtpgreeting,smtp_greeting_delay,grey_interval,grey_wait_time,grey_good_time,grey_enabled,smtp_auth_enabled,pop3_timeout,imap_timeout,smtp_timeout,queue_interval,queue_lifetime,queue_retry,smtp_error_delay,smtp_error_softlimit,smtp_error_hardlimit,php_path,outbound_target,outbound_sendmail_path,outbound_smtp_relay_host,outbound_smtp_relay_port,smtp_recipient_limit,smtp_size_limit,imap_idle_poll,queue_timeout,smtp_reversedns,outbound_add_signature,outbound_signature_sep,imap_folder_sent,imap_folder_spam,imap_folder_drafts,imap_folder_trash,outbound_smtp_relay_auth,outbound_smtp_relay_user,outbound_smtp_relay_pass,ftpgreeting,ftp_timeout,inbound_reuse_process,failban_time,failban_bantime,failban_types,failban_attempts,random_queue_id,received_header_no_expose,imap_idle_mysqlclose,queue_mysqlclose,imap_mysqlclose,imap_intelligentfolders,smtp_check_helo,inbound_headers,outbound_headers,spf_enable,spf_disable_greylisting,spf_reject_mails,spf_inject_header,queue_threads,queue_maxthreads,pop3_folders,user_chosepop3folders,outbound_smtp_usetls,imap_autoexpunge,imap_limit,user_choseimaplimit,smtp_hop_limit,smtp_auth_no_received,ssl_cipher_list,ssl_ciphersuites,ssl_min_version,ssl_max_version,imap_uids_initialized,apns_enable,apns_host,apns_port,apns_certificate,apns_privatekey,control_addr,smtp_reject_noreversedns,outbound_smtp_usedane,outbound_smtp_usednssec FROM bm60_bms_prefs LIMIT 1");
    fields = res->FetchFields();
    iNumFields = res->NumFields();
    while((row = res->FetchRow()))
    {
        for(unsigned int i = 0; i<iNumFields; i++)
        {
            this->items[fields[i].name] = row[i];
        }
    }
    delete res;

    // check bm60_users fields
    bool bSaltedPasswords = false, bUserAliasDomains = false, bUserSpaceAdd = false;
    res = db->Query("SHOW FIELDS FROM bm60_users");
    while((row = res->FetchRow()))
    {
        if(strcmp(row[0], "passwort_salt") == 0)
        {
            bSaltedPasswords = true;
        }
        else if(strcmp(row[0], "saliase") == 0)
        {
            bUserAliasDomains = true;
        }
        else if(strcmp(row[0], "mailspace_add") == 0)
        {
            bUserSpaceAdd = true;
        }
    }
    delete res;

    this->items["salted_passwords"] = bSaltedPasswords ? "1" : "0";
    this->items["user_alias_domains"] = bUserAliasDomains ? "1" : "0";
    this->items["user_space_add"] = bUserSpaceAdd ? "1" : "0";

    // check tables
    bool bHaveAPTable = false, bHaveMDStatusTable = false, bHaveBlobStorage = false, bHaveSendStats = false;
    res = db->Query("SHOW TABLES");
    while((row = res->FetchRow()))
    {
        if (strcmp(row[0], "bm60_abuse_points_config") == 0)
        {
            bHaveAPTable = true;
        }
        else if (strcmp(row[0], "bm60_maildeliverystatus") == 0)
        {
            bHaveMDStatusTable = true;
        }
        else if (strcmp(row[0], "bm60_blobstate") == 0)
        {
            bHaveBlobStorage = true;
        }
        else if (strcmp(row[0], "bm60_sendstats") == 0)
        {
            bHaveSendStats = true;
        }

        if (bHaveAPTable && bHaveMDStatusTable && bHaveBlobStorage && bHaveSendStats)
            break;
    }
    delete res;

    if(bHaveAPTable)
    {
        bHaveAPTable = false;

        res = db->Query("SHOW FIELDS FROM `bm60_abuse_points_config`");
        while((row = res->FetchRow()))
        {
            if(strcmp(row[0], "prefs") == 0)
            {
                bHaveAPTable = true;
                break;
            }
        }
        delete res;
    }

    if(bHaveBlobStorage)
    {
        res = db->Query("SELECT blobstorage_webdisk_compress,blobstorage_compress,blobstorage_provider_webdisk,blobstorage_provider FROM bm60_prefs LIMIT 1");
        fields = res->FetchFields();
        iNumFields = res->NumFields();
        while ((row = res->FetchRow()))
        {
            for (unsigned int i = 0; i<iNumFields; i++)
            {
                this->items[fields[i].name] = row[i];
            }
        }
        delete res;
    }

    bool bHaveAliasLoginField = false;
    res = db->Query("SHOW FIELDS FROM `bm60_aliase`");
    while((row = res->FetchRow()))
    {
        if(strcmp(row[0], "login") == 0)
        {
            bHaveAliasLoginField = true;
            break;
        }
    }
    delete res;

    this->items["enable_ap"] = bHaveAPTable ? "1" : "0";
    this->items["enable_mdstatus"] = bHaveMDStatusTable ? "1" : "0";
    this->items["enable_blobstorage"] = bHaveBlobStorage ? "1" : "0";
    this->items["enable_sendstats"] = bHaveSendStats ? "1" : "0";
    this->items["enable_aliaslogin"] = bHaveAliasLoginField ? "1" : "0";
}

void Config::Dump()
{
    Core::LockGuard lg(&this->mutex);

    for(map<string, string>::const_iterator it = this->items.begin();
        it != this->items.end();
        ++it)
    {
        printf("`%s` = `%s`\n",
               it->first.c_str(),
               it->second.c_str());
    }
}

void Config::CheckRequiredValues()
{
    CHECKVAL("mysql_host");
    CHECKVAL("mysql_user");
    CHECKVAL("mysql_pass");
    CHECKVAL("mysql_db");
}

void Config::CheckDBRequiredValues()
{
    CHECKVAL("datafolder");
    CHECKVAL("selffolder");
    CHECKVAL("storein");
    CHECKVAL("pop3greeting");
    CHECKVAL("imapgreeting");
    CHECKVAL("loglevel");
}

const char *Config::Get(const char *szKey)
{
    Core::LockGuard lg(&this->mutex);

    map<string, string>::iterator it = this->items.find(szKey);
    if(it != this->items.end())
    {
        return it->second.c_str();
    }

    if(strcmp(szKey, "mysql_sock") != 0
        && strcmp(szKey, "queue_dir") != 0
        && strcmp(szKey, "disable_iplog") != 0
        && strcmp(szKey, "logfile") != 0
        && strcmp(szKey, "user") != 0
        && strcmp(szKey, "group") != 0
        && strcmp(szKey, "client_addr") != 0
        && strcmp(szKey, "queue_workers") != 0
        && strcmp(szKey, "log_smtp_sessions") != 0)
        throw Core::Exception("Required configuration value not defined", (char *)szKey);
    return(NULL);
}
