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

#include <core/utils.h>
#include <msgqueue/msgqueue.h>
#include <imap/mail.h>
#include <core/md5.h>
#include <core/process.h>
#include <core/blobstorage.h>
#include <algorithm>
#include <stdarg.h>

#ifndef WIN32
#include <resolv.h>
#endif

#ifdef WIN32
#include <openssl/applink.c>
#endif

#include <openssl/ssl.h>
#include <openssl/err.h>

#include <core/tls_dh.h>

#include <mysql.h>

using namespace Core;

#define PIPE_READ   0
#define PIPE_WRITE  1

Utils *utils;

namespace {

pthread_mutex_t randMutex;

char szSaltChars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

/*
 * Table used by base64 functions
 */
unsigned char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
unsigned char base64_table_m[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+,";

}

int ts_rand()
{
    pthread_mutex_lock(&randMutex);
    int result = rand();
    pthread_mutex_unlock(&randMutex);

    return(result);
}

IPAddress::IPAddress(in6_addr ip)
{
    this->isIPv6 = true;
    this->v6Addr = ip;
}

IPAddress::IPAddress(in_addr ip)
{
    this->isIPv6 = false;
    this->v4Addr = ip;
}

IPAddress::IPAddress(in_addr_t ip)
{
    this->isIPv6 = false;
    this->v4Addr.s_addr = ip;
}

IPAddress::IPAddress(const string &ip)
{
    if(ip.find(':') != string::npos)
    {
        this->isIPv6 = true;
        inet_pton(AF_INET6, ip.c_str(), &this->v6Addr);
    }
    else
    {
        this->isIPv6 = false;
        inet_pton(AF_INET, ip.c_str(), &this->v4Addr);
    }
}

string IPAddress::toReversedString()
{
    string result;

    if(this->isIPv6)
    {
        for(int i=15; i>=0; i--)
        {
            char hexBuff[3];
            snprintf(hexBuff, 3, "%02x", (int)this->v6Addr.s6_addr[i]);

            result.append(1, hexBuff[1]);
            result.append(1, '.');
            result.append(1, hexBuff[0]);
            result.append(1, '.');
        }
    }
    else
    {
        char addressBuff[32];
        this->toCharBuff(addressBuff, sizeof(addressBuff));

        vector<string> ipComponents;
        utils->ExplodeOutsideOfQuotation(string(addressBuff), ipComponents, '.');
        reverse(ipComponents.begin(), ipComponents.end());

        for(vector<string>::iterator it = ipComponents.begin(); it != ipComponents.end(); ++it)
        {
            result += *it;
            result.append(1, '.');
        }
    }

    return(result.substr(0, result.length()-1));
}

bool IPAddress::isMappedIPv4() const
{
    return(this->isIPv6 && this->matches("0:0:0:0:0:FFFF::", "96"));
}

IPAddress IPAddress::mappedIPv4() const
{
    in_addr_t ip;

    memcpy(&ip, this->v6Addr.s6_addr + 12, sizeof(ip));

    return(IPAddress(ip));
}

bool IPAddress::matches(const string &subnet) const
{
    string ipPart, maskPart;

    size_t slashPos = subnet.find('/');
    if(slashPos == string::npos)
    {
        ipPart = subnet;
        maskPart = this->isIPv6 ? "128" : "32";
    }
    else
    {
        ipPart = subnet.substr(0, slashPos);
        maskPart = subnet.substr(slashPos+1);
    }

    return(this->matches(ipPart, maskPart));
}

bool IPAddress::matches(const string &net, const string &mask) const
{
    if(this->isIPv6)
    {
        struct in6_addr netAddr, netMask;

        // only supporting bit lengths as mask right now
        if(!utils->IsNumeric(mask))
            return(false);

        // convert rule address to binary
        if(inet_pton(AF_INET6, net.c_str(), &netAddr) != 1)
            return(false);

        // get bit count
        int bits = atoi(mask.c_str());
        if(bits > 128 || bits < 0)
            return(false);

        // create netmask from bits
        for(int i=0; i<16; i++)
        {
            if(bits >= 8)
            {
                netMask.s6_addr[i] = 0xFF;
                bits -= 8;
            }
            else if(bits > 0)
            {
                netMask.s6_addr[i] = 0xFF >> (8 - bits);
                bits = 0;
            }
            else
                netMask.s6_addr[i] = 0;
        }

        // peerAddrMasked = peerAddr & ruleMask
        struct in6_addr v6AddrMasked, netAddrMasked;
        for(int i=0; i<16; i++)
        {
            netAddrMasked.s6_addr[i] = netAddr.s6_addr[i] & netMask.s6_addr[i];
            v6AddrMasked.s6_addr[i]  = this->v6Addr.s6_addr[i] & netMask.s6_addr[i];
        }

        // compare
        return(memcmp(&v6AddrMasked, &netAddrMasked, sizeof(netAddrMasked)) == 0);
    }
    else
    {
        in_addr_t netAddr = ntohl(inet_addr(net.c_str())),
            netMask = 0xFFFFFFFF,
            myAddr = ntohl(this->v4Addr.s_addr);

        if(utils->IsNumeric(mask))
        {
            int bits = atoi(mask.c_str());
            if(bits >= 0 && bits <= 32)
                netMask = 0xFFFFFFFF << (32 - bits);
        }
        else if(mask.find('.') != string::npos)
        {
            netMask = ntohl(inet_addr(mask.c_str()));
        }

        return((myAddr & netMask) == (netAddr & netMask));
    }
}

bool IPAddress::isLocalhost() const
{
    if(this->isIPv6)
    {
        in6_addr ipLocal;
        inet_pton(AF_INET6, "::1", &ipLocal);

        return(memcmp(&this->v6Addr, &ipLocal, sizeof(ipLocal)) == 0);
    }
    else
    {
        in_addr ipLocal;
        inet_pton(AF_INET, "127.0.0.1", &ipLocal);

        return(memcmp(&this->v4Addr, &ipLocal, sizeof(ipLocal)) == 0);
    }
}

string IPAddress::dbString() const
{
    string result;

    if(this->isIPv6)
    {
        for(int i=15; i>=0; i--)
        {
            char hexBuff[3];
            snprintf(hexBuff, 3, "%02x", (int)this->v6Addr.s6_addr[i]);

            result.append(1, hexBuff[0]);
            result.append(1, hexBuff[1]);
        }
    }
    else
    {
        char szBuffer[32];
        snprintf(szBuffer, 32, "%d", (int)this->v4Addr.s_addr);
        result = szBuffer;
    }

    return(result);
}

void IPAddress::toCharBuff(char *buffer, int bufferLength) const
{
    if(this->isIPv6)
    {
        inet_ntop(AF_INET6, (void *)&this->v6Addr, buffer, bufferLength);
    }
    else
    {
        inet_ntop(AF_INET, (void *)&this->v4Addr, buffer, bufferLength);
    }
}

static DH *g_dh512 = NULL, *g_dh1024 = NULL, *g_dh2048 = NULL, *g_dh4096 = NULL;

static DH *TLS_DHCallback(SSL *s, int is_export, int keylength)
{
    DH *result = NULL;
    int type = EVP_PKEY_NONE;

    EVP_PKEY *pkey = SSL_get_privatekey(s);
    if(pkey != NULL)
        type = EVP_PKEY_base_id(pkey);

    if(type == EVP_PKEY_RSA || type == EVP_PKEY_DSA)
    {
        keylength = EVP_PKEY_bits(pkey);
    }

    switch(keylength)
    {
    case 512:
        if(g_dh512 == NULL)
            g_dh512 = get_dh512();
        result = g_dh512;
        break;

    case 1024:
        if(g_dh1024 == NULL)
            g_dh1024 = get_dh1024();
        result = g_dh1024;
        break;

    case 2048:
        if(g_dh2048 == NULL)
            g_dh2048 = get_dh2048();
        result = g_dh2048;
        break;

    case 4096:
        if(g_dh4096 == NULL)
            g_dh4096 = get_dh4096();
        result = g_dh4096;
        break;

    default:
        break;
    };

    return(result);
}

string Utils::GetAPNSTopic()
{
    string result, errMsg = "Unknown error";

    string CertData = cfg->Get("apns_certificate");
    BIO *in = BIO_new_mem_buf((char *)CertData.c_str(), CertData.length());
    if(in != NULL)
    {
        X509 *cert = PEM_read_bio_X509(in, NULL, NULL, 0);
        if(cert != NULL)
        {
            X509_NAME *sn = X509_get_subject_name(cert);
            if(sn != NULL)
            {
                int loc = X509_NAME_get_index_by_NID(sn, OBJ_txt2nid("UID"), -1);
                if(loc != -1)
                {
                    X509_NAME_ENTRY *entry = X509_NAME_get_entry(sn, loc);
                    if(entry != NULL)
                    {
                        ASN1_STRING *entryText = X509_NAME_ENTRY_get_data(entry);
                        if(entryText != NULL)
                        {
                            const char *entryString = reinterpret_cast<const char *>(ASN1_STRING_get0_data(entryText));
                            if(entryString != NULL)
                                result = entryString;
                        }
                        else
                            errMsg = "Failed to get ASN1 string";
                    }
                    else
                        errMsg = "Failed to get CN entry";
                }
                else
                    errMsg = "CN not found";
            }
            else
                errMsg = "Failed to get subject name";

            X509_free(cert);
        }
        else
            errMsg = "Failed to parse PEM data";

        BIO_free(in);
    }
    else
        errMsg = "Failed to create BIO";

    if(result.empty())
    {
        db->Log(CMP_CORE, PRIO_WARNING, utils->PrintF("Failed to extract CN from APNS certificate: %s",
            errMsg.c_str()));
    }

    return result;
}

void Utils::InitializeIMAPUIDs()
{
    if(atoi(cfg->Get("imap_uids_initialized")) != 0)
        return;

    db->Log(CMP_CORE, PRIO_DEBUG, utils->PrintF("Acquiring lock for first time initialization of IMAP UID index"));

    db->Query("LOCK TABLES bm60_mails WRITE, bm60_bms_imapuid WRITE, bm60_bms_prefs WRITE, bm60_bms_logs WRITE");

    MySQL_Result *res;
    MYSQL_ROW row;
    bool bContinue = false;

    // now that we have the lock, double-check we've acquired the lock first to avoid race conditions
    res = db->Query("SELECT `imap_uids_initialized` FROM bm60_bms_prefs LIMIT 1");
    while ((row = res->FetchRow()))
    {
        if (atoi(row[0]) == 0)
            bContinue = true;
    }
    delete res;

    if (bContinue)
    {
        db->Log(CMP_CORE, PRIO_NOTE, utils->PrintF("Starting first time initialization of IMAP UID index"));

        db->Query("BEGIN");
        db->Query("TRUNCATE TABLE bm60_bms_imapuid");
        db->Query("REPLACE INTO bm60_bms_imapuid(`imapuid`,`mailid`) SELECT `id` AS `imapuid`, `id` AS `mailid` FROM bm60_mails");
        db->Query("UPDATE bm60_bms_prefs SET `imap_uids_initialized`=1");
        db->Query("COMMIT");

        db->Log(CMP_CORE, PRIO_NOTE, utils->PrintF("IMAP UID index has been initialized"));
    }
    else
    {
        db->Log(CMP_CORE, PRIO_DEBUG, utils->PrintF("IMAP UID index already initialized"));
    }

    db->Query("UNLOCK TABLES");
}

bool Utils::MaySendMail(int userID, int recipientCount, int sendLimitCount, int sendLimitTime, int *actualRecipientCount)
{
    if(actualRecipientCount != NULL)
        *actualRecipientCount = 0;

    if(recipientCount < 1)
        return false;

    if(sendLimitCount <= 0 || sendLimitTime <= 0)
        return true;

    if(recipientCount > sendLimitCount)
        return false;

    MySQL_Result *res = db->Query("SELECT SUM(`recipients`) FROM bm60_sendstats WHERE `userid`=%d AND `time`>=%d",
        userID,
        time(NULL) - 60 * sendLimitTime);
    MYSQL_ROW row = res->FetchRow();
    int count = (row[0] == NULL ? 0 : atoi(row[0]));
    delete res;

    if(actualRecipientCount != NULL)
        *actualRecipientCount = count;

    return(count + recipientCount <= sendLimitCount);
}

unsigned long Utils::AddSendStat(int userID, int recipientCount)
{
    db->Query("INSERT INTO bm60_sendstats(`userid`,`recipients`,`time`) VALUES(%d,%d,UNIX_TIMESTAMP())",
        userID,
        max(1, recipientCount));
    return db->InsertId();
}

string Utils::GetAbuseTypeConfig(int type, const string &key)
{
    MySQL_Result *res;
    MYSQL_ROW row;

    // check if we're interfacing with a b1gMail version supporting abuse points
    if(strcmp(cfg->Get("enable_ap"), "1") != 0)
        return("");

    if(this->apConfigCache.empty())
    {
        res = db->Query("SELECT `type`,`prefs` FROM `bm60_abuse_points_config` WHERE `prefs`!=''");
        while((row = res->FetchRow()))
        {
            int type = atoi(row[0]);
            string prefs = row[1];

            vector<string> prefsItems;
            ExplodeOutsideOfQuotation(string(row[1]), prefsItems, ';');

            for(vector<string>::iterator it = prefsItems.begin();
                it != prefsItems.end();
                ++it)
            {
                size_t eqPos = it->find('=');
                if(eqPos == string::npos)
                    continue;

                string pKey = it->substr(0, eqPos), pVal = it->substr(eqPos+1);
                if(pKey.empty() || pVal.empty())
                    continue;

                this->apConfigCache[type][pKey] = pVal;
            }
        }
        delete res;
    }

    return(this->apConfigCache[type][key]);
}

int Utils::AddAbusePoint(int userID, int type, const char *comment, ...)
{
    int result = 0;
    MySQL_Result *res;
    MYSQL_ROW row;
    bool apEnabled = false;

    // user id valid?
    if(userID <= 0)
        return(0);

    // check if we're interfacing with a b1gMail version supporting abuse points
    if(strcmp(cfg->Get("enable_ap"), "1") != 0)
        return(0);

    // check if abuseprotect is enabled for ther user's group
    map<int, bool>::iterator it = this->apEnabledCache.find(userID);
    if(it == this->apEnabledCache.end())
    {
        res = db->Query("SELECT `abuseprotect` FROM `bm60_gruppen` INNER JOIN `bm60_users` ON `bm60_gruppen`.`id`=`bm60_users`.`gruppe` WHERE `bm60_users`.`id`=%d",
            userID);
        while((row = res->FetchRow()))
        {
            if(strcmp(row[0], "yes") == 0)
                apEnabled = true;
        }
        delete res;

        this->apEnabledCache[userID] = apEnabled;
    }
    else
    {
        apEnabled = it->second;
    }
    if(!apEnabled)
        return(0);

    // determine point count
    if(this->apTypeCache.empty())
    {
        res = db->Query("SELECT `type`,`points` FROM `bm60_abuse_points_config`");
        while((row = res->FetchRow()))
        {
            this->apTypeCache[atoi(row[0])] = atoi(row[1]);
        }
        delete res;
    }
    int points = this->apTypeCache[type];

    // format comment
    va_list list;
    va_start(list, comment);
    char *formattedComment = NULL;
#ifndef WIN32
    vasprintf(&formattedComment, comment, list);
#else
    int formattedCommentLength = _vscprintf(comment, list);
    formattedComment = new char[formattedCommentLength+1];
    vsprintf_s(formattedComment, formattedCommentLength+1, comment, list);
#endif

    // add points
    db->Query("INSERT INTO `bm60_abuse_points`(`userid`,`date`,`type`,`points`,`comment`) VALUES(%d,UNIX_TIMESTAMP(),%d,%d,'%q')",
        userID,
        type,
        points,
        formattedComment);

    // free formatted comment
#ifndef WIN32
    free(formattedComment);
#else
    delete[] formattedComment;
#endif

    return(result);
}

bool Utils::DomainMatches(const string &host1, const string &host2)
{
    if(strcasecmp(host1.c_str(), host2.c_str()) == 0)
        return(true);

    string tmp;
    if(host1.length() > host2.length())
        tmp = host1;
    else
        tmp = host2;

    unsigned int dotCount = 0;
    size_t domainLength = 0;
    for(string::reverse_iterator c = tmp.rbegin();
        c != tmp.rend();
        ++c)
    {
        if(*c == '.')
        {
            if(++dotCount == 2)
            {
                break;
            }
        }

        ++domainLength;
    }

    if(host1.length() < domainLength || host2.length() < domainLength)
        return(false);

    string cmp1 = host1.substr(host1.length()-domainLength),
        cmp2 = host2.substr(host2.length()-domainLength);

    return(strcasecmp(cmp1.c_str(), cmp2.c_str()) == 0);
}

void Utils::GetLocalDomains(set<string> &domains)
{
    MySQL_Result *res;
    MYSQL_ROW row;

    domains.clear();

    // global service domains
    if(strcmp(cfg->Get("user_alias_domains"), "1") == 0)
    {
        res = db->Query("SELECT LOWER(`domain`) FROM bm60_domains");
        while((row = res->FetchRow()))
        {
            domains.insert(row[0]);
        }
        delete res;
    }
    else
    {
        vector<string> vDomains;
        utils->Explode(cfg->Get("domains"), vDomains, ':');
        for(vector<string>::iterator it = vDomains.begin(); it != vDomains.end(); ++it)
        {
            if(!it->empty())
            {
                domains.insert(*it);
            }
        }
    }

    // group alias domains
    res = db->Query("SELECT LOWER(`saliase`) FROM bm60_gruppen WHERE `saliase`!=\'\'");
    while((row = res->FetchRow()))
    {
        vector<string> vDomains;
        utils->Explode(row[0], vDomains, ':');
        for(vector<string>::iterator it = vDomains.begin(); it != vDomains.end(); ++it)
        {
            if(!it->empty())
            {
                domains.insert(*it);
            }
        }
    }
    delete res;

    // user alias domains
    if(strcmp(cfg->Get("user_alias_domains"), "1") == 0)
    {
        res = db->Query("SELECT LOWER(`saliase`) FROM bm60_users WHERE `saliase`!=\'\'");
        while((row = res->FetchRow()))
        {
            vector<string> vDomains;
            utils->Explode(row[0], vDomains, ':');
            for(vector<string>::iterator it = vDomains.begin(); it != vDomains.end(); ++it)
            {
                if(!it->empty())
                {
                    domains.insert(*it);
                }
            }
        }
        delete res;
    }
}

bool Utils::IsNumeric(const string &in)
{
    bool result = true;

    for(std::size_t i=0; i < in.length(); i++)
    {
        char c = in.at(i);

        if(c == '-')
        {
            if(i != 0)
            {
                result = false;
                break;
            }
        }
        else if(c < '0' || c > '9')
        {
            result = false;
            break;
        }
    }

    return(result);
}

string Utils::GetUserPref(int userID, const string &name, const string &defaultValue)
{
    MYSQL_ROW row;
    MySQL_Result *res;
    string result = defaultValue;

    res = db->Query("SELECT `value` FROM bm60_userprefs WHERE `userid`=%d AND `key`='%q'",
        userID,
        name.c_str());
    while((row = res->FetchRow()))
    {
        result = row[0];
    }
    delete res;

    return(result);
}

string Utils::GetGroupOption(int userID, const string &name, const string &defaultValue)
{
    MYSQL_ROW row;
    MySQL_Result *res;
    string result = defaultValue;

    res = db->Query("SELECT `value` FROM bm60_groupoptions "
                    "INNER JOIN bm60_users ON bm60_groupoptions.`gruppe`=bm60_users.`gruppe` "
                    "WHERE bm60_groupoptions.`module`='B1GMailServerAdmin' AND bm60_groupoptions.`key`='%q' "
                    "AND bm60_users.`id`=%d",
                    name.c_str(),
                    userID);
    while((row = res->FetchRow()))
    {
        result = row[0];
    }
    delete res;

    return(result);
}

string Utils::MD5(const string &input)
{
    md5_state_t md5State;
    md5_byte_t md5Digest[16];

    md5_init(&md5State);
    md5_append(&md5State, (const md5_byte_t *)input.c_str(), input.length());
    md5_finish(&md5State, md5Digest);

    string result = "";
    char buff[3];

    for(unsigned int i=0; i<16; i++)
    {
        snprintf(buff, 3, "%02x", md5Digest[i]);
        result.append(buff);
    }

    return(result);
}

void Utils::ParseIMAPAuthPlain(const string &input, string &user, string &password)
{
    user.clear();
    password.clear();

    int length = 0;
    char *decodedData = utils->Base64Decode(input.c_str(), false, &length);

    if(decodedData == NULL)
        return;

    int nulNum = 0;
    for(int i=0; i<length; i++)
    {
        if(decodedData[i] == 0)
        {
            nulNum++;
            continue;
        }

        if(nulNum == 1)
        {
            user.append(1, decodedData[i]);
        }
        else if(nulNum == 2)
        {
            password.append(1, decodedData[i]);
        }
    }

    free(decodedData);
}

/*
 * escape special PCRE regular expression characters
 */
string Utils::PCREEscape(const string &str)
{
    string result = "";

    for(unsigned int i=0; i<str.length(); i++)
    {
        char c = str[i];

        switch(c)
        {
        case '.':
        case '\\':
        case '+':
        case '*':
        case '?':
        case '[':
        case '^':
        case ']':
        case '$':
        case '(':
        case ')':
        case '{':
        case '}':
        case '=':
        case '!':
        case '>':
        case '<':
        case '|':
        case ':':
        case '/':
            result.append(1, '\\');
            result.append(1, c);
            break;

        default:
            result.append(1, c);
            break;
        };
    }

    return(result);
}

/*
 * check if regular expression matches string
 */
bool Utils::Match(string expr, const string &str, bool caseless)
{
    pcre *p;
    bool matches;
    const char *error_str;
    int error_nr, c, ovector[3];

    p = pcre_compile(
        expr.c_str(),
        caseless ? PCRE_CASELESS : 0,
        &error_str,
        &error_nr,
        NULL
    );
    if(p == NULL)
        return(false);
    c = pcre_exec(
        p,
        NULL,
        str.c_str(),
        (int)str.length(),
        0,
        0,
        ovector,
        3);
    matches = (c >= 0);
    pcre_free(p);

    return(matches);
}

/*
 * check if recipient is blocked
 */
bool Utils::IsRecipientBlocked(const string &addr_)
{
    bool result = false;
    string addr = this->Trim(addr_);

    // get blocked addresses from config
    string blockedAddresses = cfg->Get("blocked");
    if(blockedAddresses.length() == 0)
        return(false);

    vector<string> vBlockedAddresses;
    utils->Explode(blockedAddresses, vBlockedAddresses, ':');
    for(vector<string>::iterator it = vBlockedAddresses.begin(); it != vBlockedAddresses.end(); ++it)
    {
        string blockedAddress = this->Trim(this->PCREEscape(*it));

        // replace \* by .+
        size_t wcPos = blockedAddress.find("\\*");
        while(wcPos != string::npos)
        {
            blockedAddress.replace(wcPos, 2, ".+");
            wcPos = blockedAddress.find("\\*");
        }

        // prepare regex
        blockedAddress = string("^") + blockedAddress + string("$");

        // match
        if(this->Match(blockedAddress, addr, true))
        {
            result = true;
            break;
        }
    }

    return(result);
}

/*
 * explode outside of quotation
 */
void Utils::ExplodeOutsideOfQuotation(const string &Str, vector<string> &Result, char Sep, bool preserveQuotes)
{
    Result.clear();

    bool inEscape = false, inQuote = false;
    string tmp = "";

    for(unsigned int i=0; i<Str.length(); i++)
    {
        char c = Str.at(i);

        if(c == Sep && !inQuote)
        {
            tmp = this->Trim(tmp, "\t ");
            if(tmp.length() > 0)
            {
                Result.push_back(tmp);
                tmp.clear();
            }
        }
        else if(c == '"' && !inEscape)
        {
            inQuote = !inQuote;
            if(preserveQuotes)
                tmp.append(1, c);
        }
        else if(c == '\\' && !inEscape)
        {
            inEscape = true;
        }
        else
        {
            tmp.append(1, c);
            inEscape = false;
        }
    }

    tmp = this->Trim(tmp, "\t ");
    if(tmp.length() > 0)
    {
        Result.push_back(tmp);
        tmp.clear();
    }
}

void Utils::Explode(const string &Str, vector<string> &Result, char Sep)
{
    Result.clear();

    string tmp = "";

    for(unsigned int i=0; i<Str.length(); i++)
    {
        char c = Str.at(i);

        if(c == Sep)
        {
            Result.push_back(tmp);
            tmp.clear();
        }
        else
        {
            tmp.append(1, c);
        }
    }

    if(tmp.length() > 0)
    {
        Result.push_back(tmp);
        tmp.clear();
    }
}

/*
 * check if szAddress is a correct sender address for iUserID
 */
bool Utils::IsValidSenderAddressForUser(int iUserID, const char *szAddress)
{
    bool bResult = false;

    // user row
    MYSQL_ROW row;
    MySQL_Result *res = db->Query("SELECT `email` FROM bm60_users WHERE `id`=%d",
                                 iUserID);
    while((row = res->FetchRow()))
    {
        if(strcasecmp(row[0], szAddress) == 0)
        {
            bResult = true;
            break;
        }
    }
    delete res;

    // aliases
    if(!bResult)
    {
        res = db->Query("SELECT `email` FROM bm60_aliase WHERE `user`=%d AND (`type`&4)=0",
                     iUserID);
        while((row = res->FetchRow()))
        {
            if(strcasecmp(row[0], szAddress) == 0)
            {
                bResult = true;
                break;
            }
        }
        delete res;
    }

    return(bResult);
}

/*
 * failban check function
 */
bool Utils::Failban_IsBanned(const IPAddress &ip, char iType)
{
    if((atoi(cfg->Get("failban_types")) & iType) == 0
       || ip.isLocalhost())
        return(false);

    bool bResult = false;

    MySQL_Result *res;
    if(!ip.isIPv6)
    {
        res = db->Query("SELECT COUNT(*) FROM bm60_bms_failban WHERE `ip`='%s' AND `ip6`='' AND `banned_until`>=%d AND (`type`&%d)!=0",
                        ip.dbString().c_str(),
                        (int)time(NULL),
                        (int)iType);
    }
    else
    {
        res = db->Query("SELECT COUNT(*) FROM bm60_bms_failban WHERE `ip`=0 AND `ip6`='%s' AND `banned_until`>=%d AND (`type`&%d)!=0",
                        ip.dbString().c_str(),
                        (int)time(NULL),
                        (int)iType);
    }

    MYSQL_ROW row = res->FetchRow();
    bResult = atoi(row[0]) > 0;
    delete res;

    return(bResult);
}

/*
 * failban bad login notifier
 */
bool Utils::Failban_LoginFailed(const IPAddress &ip, char iType)
{
    if((atoi(cfg->Get("failban_types")) & iType) == 0
       || ip.isLocalhost())
        return(false);

    bool bResult = false;

    MySQL_Result *res;
    if(!ip.isIPv6)
    {
        res = db->Query("SELECT `id`,`last_update`,`entry_date`,`attempts`,`banned_until` FROM bm60_bms_failban WHERE `ip`='%s' AND `ip6`='' AND `type`=%d",
                                  ip.dbString().c_str(),
                                  (int)iType);
    }
    else
    {
        res = db->Query("SELECT `id`,`last_update`,`entry_date`,`attempts`,`banned_until` FROM bm60_bms_failban WHERE `ip`=0 AND `ip6`='%s' AND `type`=%d",
                                  ip.dbString().c_str(),
                                  (int)iType);
    }
    if(res->NumRows() > 0)
    {
        MYSQL_ROW row = res->FetchRow();

        int iID = atoi(row[0]), iLastUpdate = atoi(row[1]), iEntryDate = atoi(row[2]), iAttempts = atoi(row[3]),
                    iBannedUntil = atoi(row[4]);

        if(iBannedUntil < time(NULL))
        {
            if(iLastUpdate < (int)time(NULL)-atoi(cfg->Get("failban_time")))
                iBannedUntil = 0;
            else if(iAttempts+1 >= atoi(cfg->Get("failban_attempts")))
                iBannedUntil = (int)time(NULL) + atoi(cfg->Get("failban_bantime"));
            else
                iBannedUntil = 0;
        }

        db->Query("UPDATE bm60_bms_failban SET `last_update`=%d,`entry_date`=%d,`attempts`=%d,`banned_until`=%d WHERE `id`=%d",
                  (int)time(NULL),
                  iLastUpdate < (int)time(NULL)-atoi(cfg->Get("failban_time")) ? (int)time(NULL) : iEntryDate,
                  iLastUpdate < (int)time(NULL)-atoi(cfg->Get("failban_time")) ? 1 : iAttempts+1,
                  iBannedUntil,
                  iID);

        if(iAttempts+1 >= atoi(cfg->Get("failban_attempts"))
           && iLastUpdate >= (int)time(NULL)-atoi(cfg->Get("failban_time")))
        {
            bResult = true;
        }
    }
    else
    {
        if(!ip.isIPv6)
        {
            db->Query("INSERT INTO bm60_bms_failban(`ip`,`entry_date`,`last_update`,`attempts`,`type`) "
                      "VALUES('%s',%d,%d,%d,%d)",
                      ip.dbString().c_str(),
                      (int)time(NULL),
                      (int)time(NULL),
                      1,
                      (int)iType);
        }
        else
        {
            db->Query("INSERT INTO bm60_bms_failban(`ip6`,`entry_date`,`last_update`,`attempts`,`type`) "
                      "VALUES('%s',%d,%d,%d,%d)",
                      ip.dbString().c_str(),
                      (int)time(NULL),
                      (int)time(NULL),
                      1,
                      (int)iType);
        }
    }
    delete res;

    return(bResult);
}

/*
 * trim a string
 */
string Utils::Trim(const string &s, const std::string &drop)
{
    string r = s;
    r.erase(r.find_last_not_of(drop)+1);
    return(r.erase(0, r.find_first_not_of(drop)));
}

/*
 * rtrim a string
 */
string Utils::RTrim(const string &s, const std::string &drop)
{
    string r = s;
    r.erase(r.find_last_not_of(drop)+1);
    return(r);
}

/*
 * Used by Base64Decode
 */
unsigned char decodeB64char(unsigned char c, bool bModified)
{
    if(!IS_BASE64_CHAR(c))
        return(0);
    unsigned char *p = bModified ? base64_table_m : base64_table, i = 0;
    while(*(p + i) != c)
        i++;
    return(i);
}

/*
 * Encode string to base64
 */
char *Utils::Base64Encode(const char *szString, bool bModified, int iLength)
{
    int i = 0, j = 0, in_len = iLength;

    if(iLength == -1)
    {
        in_len = (int)strlen(szString);
    }

    string out;
    out.reserve(in_len);

    unsigned char chunk[DECODED_CHUNK_SIZE], echunk[ENCODED_CHUNK_SIZE];

    while (in_len--)
    {
        chunk[i++] = *(szString++);
        if(i == DECODED_CHUNK_SIZE)
        {
            echunk[0] = (chunk[0] & 0xfc) >> 2;
            echunk[1] = ((chunk[0] & 0x03) << 4) + ((chunk[1] & 0xf0) >> 4);
            echunk[2] = ((chunk[1] & 0x0f) << 2) + ((chunk[2] & 0xc0) >> 6);
            echunk[3] = chunk[2] & 0x3f;

            for(i = 0; (i < ENCODED_CHUNK_SIZE); i++)
            {
                out.append(1, bModified ? base64_table_m[echunk[i]] : base64_table[echunk[i]]);
            }
            i = 0;
        }
    }

    if (i)
    {
        for(j = i; j < 3; j++)
            chunk[j] = '\0';

        echunk[0] = (chunk[0] & 0xfc) >> 2;
        echunk[1] = ((chunk[0] & 0x03) << 4) + ((chunk[1] & 0xf0) >> 4);
        echunk[2] = ((chunk[1] & 0x0f) << 2) + ((chunk[2] & 0xc0) >> 6);
        echunk[3] = chunk[2] & 0x3f;

        for (j = 0; (j < i + 1); j++)
        {
            out.append(1, bModified ? base64_table_m[echunk[j]] : base64_table[echunk[j]]);
        }

        if(!bModified)
        {
            while(i++ < 3)
            {
                out.append(1, '=');
            }
        }
    }

    return(mstrdup(out.c_str()));
}

/*
 * Decode base64 encoded string
 */
char *Utils::Base64Decode(const char *szString, bool bModified, int *iLen)
{
    unsigned char chunk[ENCODED_CHUNK_SIZE], c;
    int a = 0;
    int inLength = (int)strlen(szString);

    string ret;
    ret.reserve(inLength);

    for(int i=0; i < inLength; i++)
    {
        c = szString[i];
        if(IS_BASE64_CHAR(c))
        {
            chunk[a++] = c;
            if(a == ENCODED_CHUNK_SIZE)
            {
                for(int b=0; b<ENCODED_CHUNK_SIZE; b++)
                {
                    chunk[b] = decodeB64char(chunk[b], bModified);
                }

                ret.append(1, (chunk[0] << 2) + ((chunk[1] & 0x30) >> 4));
                ret.append(1, ((chunk[1] & 0xf) << 4) + ((chunk[2] & 0x3c) >> 2));
                ret.append(1, ((chunk[2] & 0x3) << 6) + chunk[3]);
                a = 0;
            }
        }
    }

    if(a != 0)
    {
        for(int b=a; b<ENCODED_CHUNK_SIZE; b++)
            chunk[b] = 0;
        for(int b=0; b<ENCODED_CHUNK_SIZE; b++)
            chunk[b] = decodeB64char(chunk[b], bModified);
        ret.append(1, (chunk[0] << 2) + ((chunk[1] & 0x30) >> 4));
        ret.append(1, ((chunk[1] & 0xf) << 4) + ((chunk[2] & 0x3c) >> 2));
        ret.append(1, ((chunk[2] & 0x3) << 6) + chunk[3]);
    }

    if(iLen != NULL)
        *iLen = ret.size();

    char *result = (char *)mmalloc(ret.size()+1);
    memset(result, 0, ret.size()+1);
    memcpy(result, ret.c_str(), ret.size());
    return(result);
}

void Utils::MilliSleep(unsigned int milliSeconds)
{
#ifndef _UNIX
    Sleep(milliSeconds);
#else
    usleep(milliSeconds*1000);
#endif
}

int Utils::LookupUser(const char *szAddress, bool forLogin, bool findDeleted)
{
    bool bHaveLoginField = strcmp(cfg->Get("enable_aliaslogin"), "1") == 0;

    MYSQL_ROW row;
    int iResult = 0;

    // user?
    MySQL_Result *res = db->Query("SELECT `id`,`gesperrt` FROM bm60_users WHERE `email`='%q' LIMIT 1",
        szAddress);
    if(res->NumRows()  == 1)
    {
        row = res->FetchRow();

        if(findDeleted || strcmp(row[1], "delete") != 0)
            iResult = atoi(row[0]);
    }
    delete res;

    // alias?
    if(iResult == 0)
    {
        res = db->Query((forLogin && bHaveLoginField)
                ? "SELECT bm60_users.`id`,bm60_users.`gesperrt` FROM bm60_users,bm60_aliase WHERE (bm60_aliase.`type`&2)!=0 AND bm60_aliase.`email`='%q' AND bm60_aliase.`login`='yes' AND bm60_users.`id`=bm60_aliase.`user` LIMIT 1"
                : "SELECT bm60_users.`id`,bm60_users.`gesperrt` FROM bm60_users,bm60_aliase WHERE (bm60_aliase.`type`&2)!=0 AND bm60_aliase.`email`='%q' AND bm60_users.`id`=bm60_aliase.`user` LIMIT 1",
            szAddress);
        if(res->NumRows()  == 1)
        {
            row = res->FetchRow();

            if(findDeleted || strcmp(row[1], "delete") != 0)
                iResult = atoi(row[0]);
        }
        delete res;
    }

    // workgroup?
    if(iResult == 0)
    {
        res = db->Query("SELECT `id` FROM bm60_workgroups WHERE `email`='%q' LIMIT 1",
            szAddress);
        if(res->NumRows()  == 1)
        {
            row = res->FetchRow();
            iResult = atoi(row[0])*(-1);        // return negative ID as this is not a real user ID
        }
        delete res;
    }

    // plugin?
    if(iResult == 0)
    {
        FOR_EACH_PLUGIN(Plugin)
        {
            if((iResult = Plugin->OnLookupUser(szAddress)) != 0)
                break;
        }
        END_FOR_EACH()
    }

    // return
    return(iResult);
}

int Utils::GetAlias(const char *szEMail)
{
    bool bHaveLoginField = strcmp(cfg->Get("enable_aliaslogin"), "1") == 0;
    int iResult = -1;

    MySQL_Result *res = db->Query(bHaveLoginField ? "SELECT user FROM bm60_aliase WHERE email='%q' AND `login`='yes' LIMIT 1" : "SELECT user FROM bm60_aliase WHERE email='%q' LIMIT 1",
        szEMail);
    MYSQL_ROW row;
    while((row = res->FetchRow()))
        iResult = atoi(row[0]);
    delete res;

    return(iResult);
}

char *Utils::MailPath(int iID, const char *szExt, bool bCreate)
{
    // default save method
    char *szPath = utils->PrintF("%s%d.%s", cfg->Get("datafolder"), iID, szExt);
    bool bStructStorage = (cfg->Get("structstorage") != NULL && strcmp(cfg->Get("structstorage"), "yes") == 0);

    // structured save method?
    if(!utils->FileExists(szPath))
    {
        string strAltPath;

        char *szID = utils->PrintF("%d", iID);

        // build filename
        strAltPath = cfg->Get("datafolder");
        for(std::size_t i=0; i < strlen(szID); i++)
        {
            strAltPath.append(1, *(szID + i));

            if((i+1) % 2 == 0)
            {
                strAltPath.append(1, PATH_SEP);

                if(bCreate && bStructStorage && !utils->FileExists(strAltPath.c_str()) && (i + 1 < strlen(szID)))
                {
                    mkdir(strAltPath.c_str(), 0777);
                    chmod(strAltPath.c_str(), 0777);
                }
            }
        }
        if(!strAltPath.empty() && *(strAltPath.end()-1) == PATH_SEP)
        {
            strAltPath.erase(strAltPath.size()-1);
        }
        strAltPath += ".";
        strAltPath += szExt;

        // yep, this file exists - return it
        if(utils->FileExists(strAltPath.c_str()) || (bCreate && bStructStorage))
        {
            free(szPath);

            szPath = mstrdup(strAltPath.c_str());
        }

        free(szID);
    }

    // return determined path
    return(szPath);
}

const char *Utils::GetPeerAddress(bool bReally)
{
    bool bNoIPLog = false;
    if(cfg != NULL && cfg->Get("disable_iplog") != NULL
       && strcmp(cfg->Get("disable_iplog"), "1") == 0
       && !bReally)
    {
        bNoIPLog = true;
    }

#ifdef WIN32
    SOCKET sSock = (SOCKET)GetStdHandle(STD_INPUT_HANDLE);
#else
    int sSock = fileno(stdin);
#endif

    struct sockaddr_storage sName;
    int iNameLen = sizeof(sName);
    static char szResult[INET6_ADDRSTRLEN];

    if(getpeername(sSock, (struct sockaddr *)&sName, (socklen_t *)&iNameLen) == 0)
    {
        if(sName.ss_family == AF_INET)
        {
            struct sockaddr_in *s = (struct sockaddr_in *)&sName;

            IPAddress theIP(s->sin_addr);
            theIP.toCharBuff(szResult, sizeof(szResult));

            if(theIP.isLocalhost() || !bNoIPLog)
                return(szResult);
        }
        else if(sName.ss_family == AF_INET6)
        {
            struct sockaddr_in6 *s = (struct sockaddr_in6 *)&sName;

            IPAddress theIP(s->sin6_addr);
            theIP.toCharBuff(szResult, sizeof(szResult));

            if(theIP.isLocalhost() || !bNoIPLog)
                return(szResult);
        }
    }

    return(NULL);
}

unsigned short Utils::GetPeerPort()
{
#ifdef WIN32
    SOCKET sSock = (SOCKET)GetStdHandle(STD_INPUT_HANDLE);
#else
    int sSock = fileno(stdin);
#endif

    struct sockaddr_storage sName;
    int iNameLen = sizeof(sName);

    if(getpeername(sSock, (struct sockaddr *)&sName, (socklen_t *)&iNameLen) == 0)
    {
        if(sName.ss_family == AF_INET)
        {
            struct sockaddr_in *s = (struct sockaddr_in *)&sName;
            return s->sin_port;
        }
        else if(sName.ss_family == AF_INET6)
        {
            struct sockaddr_in6 *s = (struct sockaddr_in6 *)&sName;
            return s->sin6_port;
        }
    }

    return 0;
}

char *Utils::PrintF(const char *szFormat, ...)
{
    va_list arglist;
    va_start(arglist, szFormat);
    return(utils->VPrintF(szFormat, arglist));
}

char *Utils::VPrintF(const char *szFormat, va_list arglist)
{
    char szBuff[255];
    string strQuery;

    // prepare query
    if(szFormat == NULL)
        throw Core::Exception("szFormat == NULL in Utils::PrintF");
    std::size_t formatLength = strlen(szFormat);
    for(std::size_t i=0; i < formatLength; i++)
    {
        char c = szFormat[i],
            c2 = szFormat[i+1],
            *str;
        if(c == '%')
        {
            switch(c2)
            {
            case '%':
                strQuery += '%';
                break;
            case 's':
                str = va_arg(arglist, char *);
                if(str == NULL)
                    throw Core::Exception("%s argument == NULL in Utils::PrintF", (char *)szFormat);
                strQuery.append(str);
                break;
            case 'd':
                snprintf(szBuff, 255, "%d", va_arg(arglist, int));
                strQuery.append(szBuff);
                break;
            case 'u':
                snprintf(szBuff, 255, "%u", va_arg(arglist, int));
                strQuery.append(szBuff);
                break;
            case 'f':
                snprintf(szBuff, 255, "%f", va_arg(arglist, double));
                strQuery.append(szBuff);
                break;
            case 'l':
                snprintf(szBuff, 255, "%li", va_arg(arglist, long int));
                strQuery.append(szBuff);
                break;
            case 'a':
                snprintf(szBuff, 255, "%llu", va_arg(arglist, unsigned long long));
                strQuery.append(szBuff);
                break;
            case 'x':
                snprintf(szBuff, 255, "%08X", va_arg(arglist, int));
                strQuery.append(szBuff);
                break;
            };
            i++;
        }
        else
        {
            strQuery += c;
        }
    }
    va_end(arglist);

    // allocate result string
    return(mstrdup(strQuery.c_str()));
}

void *Utils::SafeMalloc(size_t iSize)
{
    void *vResult = malloc(iSize);
    if(vResult == NULL)
    {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }
    return(vResult);
}

char *Utils::SafeStrDup(const char *in)
{
    char *result = strdup(in);
    if(result == NULL)
    {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }
    return(result);
}

void Utils::Log(const char *szMessage, ...)
{
    time_t zeit = time(NULL);
    char szBuff[255], cTime[32];

#ifdef WIN32
    strcpy(cTime, ctime(&zeit));
#else
    ctime_r(&zeit, cTime);
#endif

    string strQuery;
    va_list arglist;

    // prepare query
    va_start(arglist, szMessage);
    for(int i=0; i<(int)strlen(szMessage); i++)
    {
        char c = szMessage[i],
            c2 = szMessage[i+1];
        if(c == '%')
        {
            switch(c2)
            {
            case '%':
                strQuery += '%';
                break;
            case 's':
                strQuery.append(va_arg(arglist, char *));
                break;
            case 'd':
                snprintf(szBuff, 255, "%d", va_arg(arglist, int));
                strQuery.append(szBuff);
                break;
            case 'f':
                snprintf(szBuff, 255, "%f", va_arg(arglist, double));
                strQuery.append(szBuff);
                break;
            case 'l':
                snprintf(szBuff, 255, "%li", va_arg(arglist, long int));
                strQuery.append(szBuff);
                break;
            };
            i++;
        }
        else
        {
            strQuery += c;
        }
    }
    va_end(arglist);

    // write to stderr
    fprintf(stderr, "[%.*s] %s\n",
        (int)strlen(cTime)-1,
        cTime,
        strQuery.c_str());
}

long Utils::ConvertEndianess(long in)
{
    long out;
    char *p_in = (char *)&in;
    char *p_out = (char *)&out;
    p_out[0] = p_in[3];
    p_out[1] = p_in[2];
    p_out[2] = p_in[1];
    p_out[3] = p_in[0];
    return out;
}

bool Utils::StrToBool(const char *szStr)
{
    return(*szStr == '1');
}

char *Utils::StrToUpper(char *szStr)
{
    char *s = szStr;
    do
    {
        if(*szStr != 0)
            *szStr = toupper(*szStr);
    } while(*szStr++);
    return(s);
}

char *Utils::StrToLower(char *szStr)
{
    char *s = szStr;
    do
    {
        if(*szStr != 0)
            *szStr = tolower(*szStr);
    } while(*szStr++);
    return(s);
}

string Utils::StrToLower(const string &in)
{
    string res;
    res.reserve(in.size());
    for(std::size_t i = 0; i < in.size(); ++i)
    {
        res.push_back(tolower(in.at(i)));
    }
    return res;
}

bool Utils::FileExists(const char *szFile)
{
    struct stat s;
    return(stat(szFile, &s) == 0);
}

size_t Utils::FileSize(const char *szFile)
{
    struct stat s;
    if(stat(szFile, &s) == 0)
        return(s.st_size);
    else
        return(0);
}

pthread_mutex_t *Utils::OpenSSL_locks;

unsigned long Utils::OpenSSL_id_function()
{
    return((unsigned long)pthread_self());
}

void Utils::OpenSSL_locking_function(int mode, int type, const char *file, int line)
{
    if(mode & CRYPTO_LOCK)
    {
        pthread_mutex_lock(&Utils::OpenSSL_locks[type]);
    }
    else
    {
        pthread_mutex_unlock(&Utils::OpenSSL_locks[type]);
    }
}

void Utils::Init()
{
    int lockNum = CRYPTO_num_locks();
    Utils::OpenSSL_locks = (pthread_mutex_t *)OPENSSL_malloc(lockNum * sizeof(pthread_mutex_t));
    for(int i=0; i<lockNum; ++i)
        pthread_mutex_init(&Utils::OpenSSL_locks[i], NULL);

    CRYPTO_set_id_callback(Utils::OpenSSL_id_function);
    CRYPTO_set_locking_callback(Utils::OpenSSL_locking_function);

    SSL_load_error_strings();
    SSL_library_init();
    OpenSSL_add_ssl_algorithms();
    OpenSSL_add_all_algorithms();

    if(mysql_library_init(0, NULL, NULL) != 0)
        throw Core::Exception("Failed to initialize MySQL library");

    Process::Init();

    pthread_mutex_init(&randMutex, NULL);

#ifdef WIN32
    srand(GetTickCount());
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    srand((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
#endif

    CompilePatterns();

#ifndef WIN32
    res_init();
#endif
}

void Utils::UnInit()
{
    pthread_mutex_destroy(&randMutex);

    FreePatterns();
    ERR_free_strings();
    EVP_cleanup();
    CRYPTO_cleanup_all_ex_data();

    CRYPTO_set_locking_callback(NULL);

    for(int i=0; i<CRYPTO_num_locks(); ++i)
        pthread_mutex_destroy(&Utils::OpenSSL_locks[i]);

    OPENSSL_free(Utils::OpenSSL_locks);

    Process::UnInit();
}

void Utils::MakeRandomKey(char *szKey, int iLength)
{
    pthread_mutex_lock(&randMutex);
    for(int i=0; i<iLength; i++)
    {
        *szKey++ = szSaltChars[ rand()%62 ];
    }
    pthread_mutex_unlock(&randMutex);

    *szKey++ = '\0';
}

bool Utils::Touch(const char *szFileName)
{
#ifdef WIN32
    bool bResult = false;
    HANDLE hFile = CreateFile(szFileName, FILE_WRITE_ATTRIBUTES, FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if(hFile != INVALID_HANDLE_VALUE)
    {
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        bResult = SetFileTime(hFile, NULL, &now, &now) != 0;
        CloseHandle(hFile);
    }
    return(bResult);
#else
    int fd = open(szFileName, O_WRONLY | O_CREAT | O_NONBLOCK, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if(fd < 0)
        return(false);
    close(fd);
    return(utime(szFileName, NULL) == 0);
#endif
}

const char *Utils::GetHostByAddr(const char *szIPAddress, bool *success)
{
    if (success != NULL) *success = false;

    // IPv6
    if(strchr(szIPAddress, ':') != NULL)
    {
        struct sockaddr_in6 sAddr;

        memset(&sAddr, 0, sizeof(sAddr));
        sAddr.sin6_family = AF_INET6;

        if(inet_pton(AF_INET6, szIPAddress, &sAddr.sin6_addr) == 1)
        {
            static char szHost[NI_MAXHOST];
            if(getnameinfo((struct sockaddr *)&sAddr, sizeof(sAddr), szHost, sizeof(szHost), NULL, 0, 0) == 0)
            {
                if (success != NULL) *success = true;
                return(szHost);
            }
        }
    }

    // IPv4
    else
    {
#ifdef WIN32
        struct in_addr iIPAddress = { 0 };
        iIPAddress.S_un.S_addr = inet_addr(szIPAddress);
        struct hostent *res = gethostbyaddr((const char *)&iIPAddress, sizeof(iIPAddress), AF_INET);
#else
        in_addr_t iIPAddress = inet_addr(szIPAddress);
        struct hostent *res = gethostbyaddr((const void *)&iIPAddress, sizeof(iIPAddress), AF_INET);
#endif

        if(res != NULL && res->h_name != NULL)
        {
            if (success != NULL) *success = true;
            return(res->h_name);
        }
    }

    return(szIPAddress);
}

void Utils::PostEvent(int iUserID, int iEventType, int iParam1, int iParam2)
{
    db->Query("INSERT INTO bm60_bms_eventqueue(`userid`,`type`,`param1`,`param2`) VALUES(%d,%d,%d,%d)",
              iUserID,
              iEventType,
              iParam1,
              iParam2);
}

char *Utils::GetAlterMimePath()
{
#ifndef WIN32
    string strPath = mstrdup("/opt/b1gmailserver/bin/altermime");
#else
    char szBuff[MAX_PATH];
    GetModuleFileName(GetModuleHandle(NULL), szBuff, MAX_PATH);

    char *szSlash = strrchr(szBuff, '\\');

    if(szSlash == NULL || szSlash == szSlash + strlen(szSlash))
        throw Core::Exception("Core", "Cannot find myself");

    *++szSlash = '\0';

    string strPath = szPath;
    strPath += "altermime.exe";
#endif

    return(mstrdup(strPath.c_str()));
}

bool Utils::TLSAvailable()
{
#ifndef WIN32
    if(utils->FileExists("/opt/b1gmailserver/tls/server.key")
       && utils->FileExists("/opt/b1gmailserver/tls/server.cert"))
    {
        return(true);
    }
#else
    char szPath[MAX_PATH+1];
    GetModuleFileName(GetModuleHandle(NULL), szPath, MAX_PATH);

    char *szSlash = strrchr(szPath, '\\');
    if(szSlash == NULL || szSlash == szSlash + strlen(szSlash))
        throw Core::Exception("Core", "Cannot find myself");
    *szSlash = '\0';

    szSlash = strrchr(szPath, '\\');
    if(szSlash == NULL || szSlash == szSlash + strlen(szSlash))
        throw Core::Exception("Core", "Cannot find myself");
    *++szSlash = '\0';

    string keyPath = string(szPath) + string("tls\\server.key"),
        certPath = string(szPath) + string("tls\\server.cert");

    return(utils->FileExists(keyPath.c_str()) && utils->FileExists(certPath.c_str()));
#endif

    return(false);
}

bool Utils::SetSocketRecvTimeout(int sock, int seconds)
{
#ifdef WIN32
    DWORD tv = seconds * 1000;
    return(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv)) == 0);
#else
    struct timeval tv;
    tv.tv_sec   = seconds;
    tv.tv_usec  = 0;

    return(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv)) == 0);
#endif
}

void Utils::SetTLSDHParams(SSL_CTX *ssl_ctx, const char *dhPath)
{
    SSL_CTX_set_tmp_dh_callback(ssl_ctx, TLS_DHCallback);

    EC_KEY *ecDH = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    SSL_CTX_set_tmp_ecdh(ssl_ctx, ecDH);
    EC_KEY_free(ecDH);
}

string Utils::CertHash()
{
#ifndef WIN32
    string certPath = "/opt/b1gmailserver/tls/server.cert";
#else
    char szPath[MAX_PATH+1];
    GetModuleFileName(GetModuleHandle(NULL), szPath, MAX_PATH);

    char *szSlash = strrchr(szPath, '\\');
    if(szSlash == NULL || szSlash == szSlash + strlen(szSlash))
        throw Core::Exception("Core", "Cannot find myself");
    *szSlash = '\0';

    szSlash = strrchr(szPath, '\\');
    if(szSlash == NULL || szSlash == szSlash + strlen(szSlash))
        throw Core::Exception("Core", "Cannot find myself");
    *++szSlash = '\0';

    string certPath = string(szPath) + string("tls\\server.cert");
#endif

    string result;

    FILE *fp = fopen(certPath.c_str(), "rb");
    if(fp == NULL)
        return result;

    X509 *x509 = PEM_read_X509(fp, NULL, NULL, NULL);
    if(x509 != NULL)
    {
        int length = i2d_X509(x509, NULL);
        if(length > 0)
        {
            unsigned char *buffer = new unsigned char[length];
            unsigned char *tmp = buffer;

            if(i2d_X509(x509, &tmp) == length)
            {
                unsigned char hash[SHA256_DIGEST_LENGTH];
                SHA256_CTX sha256;
                SHA256_Init(&sha256);
                SHA256_Update(&sha256, buffer, length);
                SHA256_Final(hash, &sha256);

                char hexBuff[3];
                for(unsigned int i=0; i<sizeof(hash); i++)
                {
                    snprintf(hexBuff, 3, "%02X", hash[i]);
                    result.append(hexBuff);
                }
            }

            delete[] buffer;
        }

        X509_free(x509);
    }

    fclose(fp);

    return result;
}

bool Utils::BeginTLS(SSL_CTX *ssl_ctx, char *szErrorOut, int iTimeout)
{
#ifndef WIN32
    int sIn = fileno(stdin), sOut = fileno(stdout);
#else
    SOCKET sIn = (SOCKET)GetStdHandle(STD_INPUT_HANDLE), sOut = (SOCKET)GetStdHandle(STD_OUTPUT_HANDLE);
#endif

    if(!utils->TLSAvailable())
    {
        snprintf(szErrorOut, 255, "Server TLS configuration invalid (cannot find key/cert)");
        return(false);
    }

    if(iTimeout > 0)
        SetSocketRecvTimeout(sIn, iTimeout);

#ifndef WIN32
    string keyPath = "/opt/b1gmailserver/tls/server.key",
        certPath = "/opt/b1gmailserver/tls/server.cert",
        chainCertPath = "/opt/b1gmailserver/tls/chain.cert",
        dhPath = "/opt/b1gmailserver/tls/dh.pem";
#else
    char szPath[MAX_PATH+1];
    GetModuleFileName(GetModuleHandle(NULL), szPath, MAX_PATH);

    char *szSlash = strrchr(szPath, '\\');
    if(szSlash == NULL || szSlash == szSlash + strlen(szSlash))
        throw Core::Exception("Core", "Cannot find myself");
    *szSlash = '\0';

    szSlash = strrchr(szPath, '\\');
    if(szSlash == NULL || szSlash == szSlash + strlen(szSlash))
        throw Core::Exception("Core", "Cannot find myself");
    *++szSlash = '\0';

    string keyPath = string(szPath) + string("tls\\server.key"),
        certPath = string(szPath) + string("tls\\server.cert"),
        chainCertPath = string(szPath) + string("tls\\chain.cert"),
        dhPath = string(szPath) + string("tls\\dh.pem");
#endif

    if((ssl_ctx = SSL_CTX_new(SSLv23_server_method())) == 0)
    {
        snprintf(szErrorOut, 255, "SSL error while creating CTX");
        if(iTimeout > 0) SetSocketRecvTimeout(sIn, 0);
        return(false);
    }

    const char *cipherList = cfg->Get("ssl_cipher_list");
    if (cipherList != NULL && strlen(cipherList) > 0)
    {
        if (!SSL_CTX_set_cipher_list(ssl_ctx, cipherList))
        {
            db->Log(CMP_CORE, PRIO_WARNING, utils->PrintF("Failed to set SSL cipher list: %s", cipherList));
        }
    }

    const char *cipherSuites = cfg->Get("ssl_ciphersuites");
    if (cipherSuites != NULL && strlen(cipherSuites) > 0)
    {
        if (!SSL_CTX_set_ciphersuites(ssl_ctx, cipherSuites))
        {
            db->Log(CMP_CORE, PRIO_WARNING, utils->PrintF("Failed to set SSL cipher suites: %s", cipherSuites));
        }
    }

    int minVersion = atoi(cfg->Get("ssl_min_version")), maxVersion = atoi(cfg->Get("ssl_max_version"));
    if (!SSL_CTX_set_min_proto_version(ssl_ctx, minVersion))
    {
        db->Log(CMP_CORE, PRIO_WARNING, utils->PrintF("Failed to set SSL min protocol version: %d", minVersion));
    }
    if (!SSL_CTX_set_max_proto_version(ssl_ctx, maxVersion))
    {
        db->Log(CMP_CORE, PRIO_WARNING, utils->PrintF("Failed to set SSL max protocol version: %d", maxVersion));
    }

    SSL_CTX_set_options(ssl_ctx, (SSL_OP_ALL & ~SSL_OP_TLSEXT_PADDING) | SSL_OP_NO_SSLv3 | SSL_OP_NO_SSLv2);
    SSL_CTX_set_default_verify_paths(ssl_ctx);

    if(!SSL_CTX_use_certificate_file(ssl_ctx, certPath.c_str(), SSL_FILETYPE_PEM))
    {
        snprintf(szErrorOut, 255, "SSL error while loading certificate");
        if(iTimeout > 0) SetSocketRecvTimeout(sIn, 0);
        return(false);
    }

    if(utils->FileExists(chainCertPath.c_str()))
    {
        X509 *cChainCert = NULL;
        BIO *bChainCert = BIO_new_file(chainCertPath.c_str(), "rb");

        if(bChainCert != NULL)
        {
            while((cChainCert = PEM_read_bio_X509(bChainCert, NULL, NULL, NULL)) != NULL)
            {
                if(SSL_CTX_add_extra_chain_cert(ssl_ctx, cChainCert) != 1)
                {
                    X509_free(cChainCert);
                    break;
                }
            }
        }

        if(bChainCert != NULL)
            BIO_free_all(bChainCert);
    }

    if(!SSL_CTX_use_PrivateKey_file(ssl_ctx, keyPath.c_str(), SSL_FILETYPE_PEM))
    {
        snprintf(szErrorOut, 255, "SSL error while loading private key");
        if(iTimeout > 0) SetSocketRecvTimeout(sIn, 0);
        return(false);
    }

    if(!SSL_CTX_check_private_key(ssl_ctx))
    {
        snprintf(szErrorOut, 255, "Private key and certificate do not match");
        if(iTimeout > 0) SetSocketRecvTimeout(sIn, 0);
        return(false);
    }

    this->SetTLSDHParams(ssl_ctx, dhPath.c_str());

    SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, 0);

    if((ssl = SSL_new(ssl_ctx)) == 0)
    {
        snprintf(szErrorOut, 255, "SSL error while creating layer");
        if(iTimeout > 0) SetSocketRecvTimeout(sIn, 0);
        return(false);
    }

    fflush(stdout);

    SSL_clear(ssl);
    SSL_set_rfd(ssl, sIn);
    SSL_set_wfd(ssl, sOut);
    SSL_set_accept_state(ssl);

    int iResult = SSL_accept(ssl);
    if(iResult < 1)
    {
        const char *szError = "Unknown error";
        switch(SSL_get_error(ssl, iResult))
        {
        case SSL_ERROR_ZERO_RETURN:
            szError = "Connection terminated";
            break;

        case SSL_ERROR_WANT_WRITE:
        case SSL_ERROR_WANT_CONNECT:
        case SSL_ERROR_WANT_READ:
            szError = "Incomplete transaction";
            break;

        case SSL_ERROR_WANT_X509_LOOKUP:
            szError = "Invalid X509 lookup request";
            break;

        case SSL_ERROR_SYSCALL:
            szError = "Syscall error";
            break;

        case SSL_ERROR_SSL:
            szError = "SSL error";
            break;
        };
        snprintf(szErrorOut, 255, "%s", szError);
        if(iTimeout > 0) SetSocketRecvTimeout(sIn, 0);
        return(false);
    }

    SSL_CTX_set_mode(ssl_ctx, SSL_MODE_AUTO_RETRY);

    snprintf(szErrorOut, 255, "Success");
    if(iTimeout > 0) SetSocketRecvTimeout(sIn, 0);
    return(true);
}

int Utils::GetCoreFeatures()
{
    int iResult = 0;

    // TLS
    if(utils->TLSAvailable())
        iResult |= BMS_CORE_FEATURE_TLS;

    // AlterMIME
    char *szAlterMimePath = utils->GetAlterMimePath();
    if(szAlterMimePath != NULL)
    {
        if(utils->FileExists(szAlterMimePath))
            iResult |= BMS_CORE_FEATURE_ALTERMIME;
        free(szAlterMimePath);
    }

    return(iResult);
}

int Utils::GetTimeZone()
{
    time_t TheTime = time(NULL), TimeA = 0, TimeB = 0;
    struct tm *TimeInfo = NULL;

#ifdef WIN32
    if((TimeInfo = localtime(&TheTime)) != NULL)
#else
    struct tm _TimeInfo;
    if((TimeInfo = localtime_r(&TheTime, &_TimeInfo)) != NULL)
#endif
    {
        TimeInfo->tm_isdst = 0;
        TimeA = mktime(TimeInfo);
    }

#ifdef WIN32
    if((TimeInfo = gmtime(&TheTime)) != NULL)
#else
    if((TimeInfo = gmtime_r(&TheTime, &_TimeInfo)) != NULL)
#endif
    {
        TimeInfo->tm_isdst = 0;
        TimeB = mktime(TimeInfo);
    }

    return((int)(((double)TimeA-(double)TimeB)/(double)36.0));
}

#ifdef WIN32
HANDLE Utils::POpen(const char *command, int *infp, int *outfp)
{
    char *command2 = new char[strlen(command)+1];
    strcpy(command2, command);

    SECURITY_ATTRIBUTES saAttr;
    STARTUPINFOA sInfo;
    PROCESS_INFORMATION pInfo;
    HANDLE hOutRd, hOutWr, hInRd, hInWr;

    memset(&saAttr, 0, sizeof(saAttr));
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = true;
    saAttr.lpSecurityDescriptor = NULL;

    memset(&sInfo, 0, sizeof(sInfo));
    sInfo.cb = sizeof(sInfo);
    sInfo.lpDesktop = "";
    sInfo.dwFlags = STARTF_USESTDHANDLES;

    if(!CreatePipe(&hOutRd, &hOutWr, &saAttr, 0)
        || !SetHandleInformation(hOutRd, HANDLE_FLAG_INHERIT, 0))
    {
        delete[] command2;
        return(INVALID_HANDLE_VALUE);
    }

    if(!CreatePipe(&hInRd, &hInWr, &saAttr, 0)
        || !SetHandleInformation(hInWr, HANDLE_FLAG_INHERIT, 0))
    {
        CloseHandle(hOutRd);
        CloseHandle(hOutWr);
        delete[] command2;
        return(INVALID_HANDLE_VALUE);
    }

    sInfo.hStdInput = hInRd;
    sInfo.hStdOutput = hOutWr;
    sInfo.hStdError = hOutWr;

    if(!CreateProcess(NULL,
        command2,
        NULL,
        NULL,
        true,
        DETACHED_PROCESS,
        NULL,
        NULL,
        &sInfo,
        &pInfo))
    {
        CloseHandle(hInRd);
        CloseHandle(hInWr);
        CloseHandle(hOutRd);
        CloseHandle(hOutWr);
        delete[] command2;
        return(INVALID_HANDLE_VALUE);
    }

    *infp = _open_osfhandle((intptr_t)hInWr, _O_WRONLY);
    *outfp = _open_osfhandle((intptr_t)hOutRd, _O_RDONLY);

    delete[] command2;
    return(pInfo.hProcess);
}
#else
pid_t Utils::POpen(const char *command, int *infp, int *outfp)
{
    int p_stdin[2], p_stdout[2];
    pid_t pid;

    if(pipe(p_stdin) != 0)
        return(-1);

    if(pipe(p_stdout) != 0)
    {
        close(p_stdin[0]);
        close(p_stdin[1]);
        return(-1);
    }

    pid = fork();

    if(pid < 0)
    {
        close(p_stdin[0]);
        close(p_stdin[1]);
        close(p_stdout[0]);
        close(p_stdout[1]);
        return pid;
    }
    else if(pid == 0)
    {
        close(p_stdin[PIPE_WRITE]);
        dup2(p_stdin[PIPE_READ], PIPE_READ);
        close(p_stdout[PIPE_READ]);
        dup2(p_stdout[PIPE_WRITE], PIPE_WRITE);
        close(p_stdin[PIPE_READ]);
        close(p_stdout[PIPE_WRITE]);

        execl("/bin/sh", "sh", "-c", command, NULL);
        _exit(127);
    }

    if(infp == NULL)
        close(p_stdin[PIPE_WRITE]);
    else
        *infp = p_stdin[PIPE_WRITE];

    if(outfp == NULL)
        close(p_stdout[PIPE_READ]);
    else
        *outfp = p_stdout[PIPE_READ];

    close(p_stdin[PIPE_READ]);
    close(p_stdout[PIPE_WRITE]);

    return pid;
}
#endif

b1gMailServer::MySQL_DB *Utils::GetMySQLConnection()
{
    return(db);
}

b1gMailServer::MSGQueue *Utils::CreateMSGQueueInstance()
{
    return(new MSGQueue());
}

b1gMailServer::Mail *Utils::CreateMailInstance()
{
    return(new Mail());
}

string Utils::GetPluginPath()
{
#ifdef WIN32
    static char szResult[255];
    char szSelfFileName[255];

    if(GetModuleFileName(GetModuleHandle(NULL), szSelfFileName, sizeof(szSelfFileName)) > 0)
    {
        char *c = szSelfFileName + strlen(szSelfFileName);
        int i = 0;

        while(c >= szSelfFileName)
        {
            if(*c == '\\')
            {
                i++;

                if(i == 2)
                {
                    *c = '\0';
                    break;
                }
            }

            c--;
        }

        string Result = szSelfFileName;
        Result.append("\\plugins");
        return(Result);
    }

    throw Core::Exception("Cannot locate myself");
    return("");
#else
    return("/opt/b1gmailserver/plugins");
#endif
}

BlobStorageProvider *Utils::CreateBlobStorageProvider(int id, int userID)
{
    BlobStorageProvider *result = NULL;

    switch (id)
    {
    case BMBLOBSTORAGE_SEPARATEFILES:
        result = new BlobStorageProvider_SeparateFiles;
        break;

    case BMBLOBSTORAGE_USERDB:
        result = new BlobStorageProvider_UserDB;
        break;

    default:
        db->Log(CMP_CORE, PRIO_ERROR, utils->PrintF("Unknown blob storage provider: %d", id));
        return NULL;
    }

    if (userID > 0)
    {
        if (!result->open(userID))
        {
            db->Log(CMP_CORE, PRIO_ERROR, utils->PrintF("Failed to open user %d in blob storage provider %d", userID, id));
            delete result;
            result = NULL;
        }
    }

    return result;
}

bool Utils::DeleteBlob(int blobStorageID, int type, int id, int userID)
{
    bool result = false;

    if (strcmp(cfg->Get("enable_blobstorage"), "1") != 0)
        blobStorageID = 0;

    BlobStorageProvider *storage = CreateBlobStorageProvider(blobStorageID, userID);
    if (storage != NULL)
    {
        result = storage->deleteBlob(static_cast<BlobType>(type), id);
        delete storage;
    }

    return result;
}

FILE *Utils::GetMessageFP(int ID, int UserID)
{
    MYSQL_ROW row;
    b1gMailServer::MySQL_Result *res;
    FILE *Result = NULL;

    if (strcmp(cfg->Get("enable_blobstorage"), "1") != 0)
    {
        if (UserID > 0)
            res = db->Query("SELECT `body`,LENGTH(`body`) FROM bm60_mails WHERE `id`=%d AND `userid`=%d",
                    ID,
                    UserID);
        else
            res = db->Query("SELECT `body`,LENGTH(`body`) FROM bm60_mails WHERE `id`=%d",
                    ID);

        if (res->NumRows() != 1)
        {
            Result = NULL;
        }
        else
        {
            row = res->FetchRow();

            if (strcmp(row[0], "file") == 0)
            {
                char *MailPath = utils->MailPath(ID);
                Result = fopen(MailPath, "rb");
                free(MailPath);
            }
            else
            {
                Result = tmpfile();
                fwrite(row[0], 1, atoi(row[1]), Result);
                fseek(Result, 0, SEEK_SET);
            }
        }

        delete res;
    }
    else
    {
        if (UserID > 0)
            res = db->Query("SELECT `blobstorage`,`userid` FROM bm60_mails WHERE `id`=%d AND `userid`=%d",
                ID,
                UserID);
        else
            res = db->Query("SELECT `blobstorage`,`userid` FROM bm60_mails WHERE `id`=%d",
                ID);

        if (res->NumRows() != 1)
        {
            Result = NULL;
        }
        else
        {
            row = res->FetchRow();

            BlobStorageProvider *storage = CreateBlobStorageProvider(atoi(row[0]), atoi(row[1]));
            if (storage != NULL)
            {
                Result = storage->loadBlob(BMBLOB_TYPE_MAIL, ID);
                delete storage;
            }
        }

        delete res;
    }

    // plugins
    if(Result != NULL)
    {
        FOR_EACH_PLUGIN(Plugin)
        {
            FILE *PluginFP = Plugin->OnOpenMailBody(Result);

            if(PluginFP != NULL)
            {
                fclose(Result);
                Result = PluginFP;
                break;
            }
        }
        END_FOR_EACH()
    }

    return(Result);
}

const char *Utils::GetQueueStateFilePath()
{
    static char szResult[512];

    snprintf(szResult, 512, "%s%c%s", GET_QUEUE_DIR(), PATH_SEP, QUEUE_STATE_FILE);

    return((const char *)szResult);
}

const char *Utils::GetQueueDir()
{
    return((const char *)GET_QUEUE_DIR());
}

b1gMailServer::Config *Utils::GetConfig()
{
    return(cfg);
}

int Utils::IO_printf(const char *str, ...)
{
    va_list list;
    va_start(list, str);

    int Result = my_vprintf(str, list);

    va_end(list);

    return(Result);
}

size_t Utils::IO_fwrite(const void *buf, size_t s1, size_t s2, FILE *fp)
{
    return(my_fwrite(buf, s1, s2, fp));
}

char *Utils::IO_fgets(char *buf, int s, FILE *fp)
{
    return(my_fgets(buf, s, fp));
}

size_t Utils::IO_fread(void *buf, size_t s1, size_t s2, FILE *fp)
{
    return(my_fread(buf, s1, s2, fp));
}

string Utils::TimeToString(time_t theTime)
{
    char buffer[128];

    struct tm *t = gmtime(&theTime);

    strftime(buffer, 128, "%a, %d %b %Y %H:%M:%S +0000", t);

    return(string(buffer));
}
