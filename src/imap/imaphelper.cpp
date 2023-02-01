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
#include <utility>
#include <algorithm>
#include <set>
#include <iostream>
#include <stack>

static const char *szMonths[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

#define BMLINK_AND              1
#define BMLINK_OR               2

#define BMOP_EQUAL              1       // filter ops
#define BMOP_NOTEQUAL           2
#define BMOP_CONTAINS           3
#define BMOP_NOTCONTAINS        4
#define BMOP_STARTSWITH         5
#define BMOP_ENDSWITH           6
#define MAILFIELD_SUBJECT       1       // mail fields
#define MAILFIELD_FROM          2
#define MAILFIELD_TO            3
#define MAILFIELD_CC            4
#define MAILFIELD_BCC           5
#define MAILFIELD_READ          6
#define MAILFIELD_ANSWERED      7
#define MAILFIELD_FORWARDED     8
#define MAILFIELD_PRIORITY      9
#define MAILFIELD_ATTACHMENT    10
#define MAILFIELD_FLAGGED       11
#define MAILFIELD_FOLDER        12
#define MAILFIELD_ATTACHLIST    13
#define MAILFIELD_COLOR         14
#define MAILFIELD_DONE          15

void IMAPHelper::ReportDelta(MySQL_DB *db, IMAPMsgList &oldList, IMAPMsgList &newList)
{
    unsigned int oldSize = oldList.size();
    int oldRecent = IMAPHelper::CountNotFlaggedMails(oldList, FLAG_SEEN);

    // Report mails which have been deleted
    bool expunged = false;
    IMAPMsgList::iterator newIt = newList.begin();
    IMAPMsgList::iterator oldIt = oldList.begin();
    for(; newIt != newList.end() && oldIt != oldList.end(); ++newIt, ++oldIt)
    {
        const IMAPMsg &newMsg = *newIt;

        while(oldIt != oldList.end()
                && (*oldIt).iUID != newMsg.iUID)
        {
            db->Log(CMP_IMAP, PRIO_DEBUG, utils->PrintF("compare uid(%d, %d) seq(%d, %d)",
                (*oldIt).iUID, newMsg.iUID,
                (*oldIt).iSequenceID, newMsg.iSequenceID));

            printf("* %d EXPUNGE\r\n",
                (*oldIt).iSequenceID);

            for(IMAPMsgList::iterator it = oldIt; it != oldList.end(); ++it)
                --((*it).iSequenceID);

            oldIt = oldList.erase(oldIt);

            expunged = true;
        }

        if(oldIt == oldList.end())
            break;
    }
    if(oldList.size() > newList.size())
    {
        int seqId = -1;

        for(IMAPMsgList::iterator it = oldList.begin()+newList.size(); it != oldList.end(); )
        {
            if(seqId == -1)
                seqId = (*it).iSequenceID;
            printf("* %d EXPUNGE\r\n",
                seqId);

            it = oldList.erase(it);

            expunged = true;
        }
    }

    // Report changed flags
    newIt = newList.begin();
    oldIt = oldList.begin();
    for(; newIt != newList.end() && oldIt != oldList.end(); ++newIt, ++oldIt)
    {
        if((*newIt).iUID != (*oldIt).iUID)
        {
            db->Log(CMP_IMAP, PRIO_WARNING, utils->PrintF("[HELPER] Internal error: Unexpected UID deviation in synchronized message list"));
            break;
        }

        if((*newIt).iSequenceID != (*oldIt).iSequenceID)
        {
            db->Log(CMP_IMAP, PRIO_WARNING, utils->PrintF("[HELPER] Internal error: Unexpected sequence number deviation in synchronized message list"));
            break;
        }

        if((*newIt).iFlags != (*oldIt).iFlags)
        {
            printf("* %d FETCH (FLAGS (", (*newIt).iSequenceID);
            PrintFlags((*newIt).iFlags);
            printf("))\r\n");
        }
    }

    // Report changed mail count
    if(newList.size() != oldSize || expunged)
    {
        printf("* %d EXISTS\r\n",
            newList.size());
    }

    // Report changed count of mails flagged with Recent
    int newRecent = IMAPHelper::CountNotFlaggedMails(newList, FLAG_SEEN);
    if(newRecent != oldRecent)
    {
        printf("* %d RECENT\r\n",
            newRecent);
    }
}

int IMAPHelper::AddUID(MySQL_DB *db, int iMailID)
{
    int iUID = 0;

    db->Query("LOCK TABLES bm60_bms_imapuid WRITE");

    MySQL_Result *res = db->Query("SELECT `imapuid` FROM bm60_bms_imapuid WHERE `mailid`=%d",
        iMailID);
    if(res->NumRows() > 0)
    {
        MYSQL_ROW row = res->FetchRow();
        iUID = atoi(row[0]);
    }
    delete res;

    if (iUID == 0)
    {
        db->Query("INSERT INTO bm60_bms_imapuid(`mailid`) VALUES(%d)",
            iMailID);
        iUID = db->InsertId();
    }

    db->Query("UNLOCK TABLES");

    return iUID;
}

string IMAPHelper::FolderCondition(MySQL_DB *db, int iFolderID)
{
    char szBuff[255];
    snprintf(szBuff, 255, "folder='%d'", iFolderID);
    string result = szBuff;

    if(iFolderID > 0)
    {
        // this may be an intelligent folder
        bool isIntelligent = false;
        int linkType = BMLINK_AND;
        MySQL_Result *res = db->Query("SELECT `intelligent`,`intelligent_link` FROM bm60_folders WHERE `id`='%d'",
                iFolderID);
        MYSQL_ROW row;
        while((row = res->FetchRow()))
        {
            isIntelligent = strcmp(row[0], "1") == 0;
            if(isIntelligent)
            {
                linkType = atoi(row[1]);
                if(linkType != BMLINK_AND && linkType != BMLINK_OR)
                    linkType = BMLINK_AND;
            }
        }
        delete res;

        // is it?
        if(isIntelligent)
        {
            result.clear();

            res = db->Query("SELECT `field`,`op`,`val` FROM bm60_folder_conditions WHERE `folder`='%d'",
                    iFolderID);
            while((row = res->FetchRow()))
            {
                int field = atoi(row[0]), op = atoi(row[1]);
                bool valueIsNo = strcmp(row[2], "no") == 0;
                string condition, sqlField, sqlVal = db->Escape(row[2]);

                if(field > 0 && field <= MAILFIELD_DONE
                        && op > 0 && op <= BMOP_ENDSWITH)
                {
                    switch(field)
                    {
                    case MAILFIELD_SUBJECT:
                        sqlField = "betreff";
                        break;

                    case MAILFIELD_FROM:
                        sqlField = "von";
                        break;

                    case MAILFIELD_TO:
                        sqlField = "an";
                        break;

                    case MAILFIELD_CC:
                        sqlField = "cc";
                        break;

                    case MAILFIELD_READ:
                        condition = string("(flags&1)") + (valueIsNo?string("!=0"):string("=0"));
                        break;

                    case MAILFIELD_ANSWERED:
                        condition = string("(flags&2)") + (valueIsNo?string("=0"):string("!=0"));
                        break;

                    case MAILFIELD_FORWARDED:
                        condition = string("(flags&4)") + (valueIsNo?string("=0"):string("!=0"));
                        break;

                    case MAILFIELD_PRIORITY:
                        sqlField = "priority";
                        break;

                    case MAILFIELD_ATTACHMENT:
                        condition = string("(flags&64)") + (valueIsNo?string("=0"):string("!=0"));
                        break;

                    case MAILFIELD_FLAGGED:
                        condition = string("(flags&16)") + (valueIsNo?string("=0"):string("!=0"));
                        break;

                    case MAILFIELD_DONE:
                        condition = string("(flags&4096)") + (valueIsNo?string("=0"):string("!=0"));
                        break;

                    case MAILFIELD_FOLDER:
                        sqlField = "folder";
                        break;

                    case MAILFIELD_COLOR:
                        sqlField = "color";
                        break;

                    default:
                        sqlField = "0";
                        break;
                    };

                    if(condition.empty())
                    {
                        string sqlOp;

                        switch(op)
                        {
                        case BMOP_EQUAL:
                            sqlOp = "=";
                            break;

                        case BMOP_NOTEQUAL:
                            sqlOp = "!=";
                            break;

                        case BMOP_CONTAINS:
                            sqlOp = " LIKE ";
                            sqlVal = string("%") + sqlVal + string("%");
                            break;

                        case BMOP_NOTCONTAINS:
                            sqlOp = " NOT LIKE ";
                            sqlVal = string("%") + sqlVal + string("%");
                            break;

                        case BMOP_STARTSWITH:
                            sqlOp = " LIKE ";
                            sqlVal = sqlVal + string("%");
                            break;

                        case BMOP_ENDSWITH:
                            sqlOp = " LIKE ";
                            sqlVal = string("%") + sqlVal;
                            break;

                        default:
                            sqlOp = "=";
                            sqlVal = "1";
                            break;
                        };

                        condition = string("`") + sqlField + string("`") + sqlOp + string("'") + sqlVal + string("'");
                    }

                    result += condition;
                    if(linkType == BMLINK_OR)
                        result += " OR ";
                    else if(linkType == BMLINK_AND)
                        result += " AND ";
                }
            }
            delete res;

            if(result.length() >= 4 && linkType == BMLINK_OR)
                result.erase(result.end()-4, result.end());
            else if(result.length() >= 5 && linkType == BMLINK_AND)
                result.erase(result.end()-5, result.end());
        }
    }


    db->Log(CMP_IMAP, PRIO_DEBUG, utils->PrintF("[HELPER] Folder query WHERE clause for folder %d: %s",
        iFolderID,
        result.c_str()));

    return(result);
}

/*
 * checks if 8-bit encoding is required
 */
bool IMAPHelper::ShouldHeaderEncode(const string &Header)
{
    for(unsigned int i=0; i<Header.length(); i++)
    {
        char c = Header.at(i);
        if((c < 32 || c > 126) && c != '\r' && c != '\n' && c != '\t')
            return(true);
    }

    return(false);
}

/*
 * encode 8-bit chars in header field
 */
string IMAPHelper::HeaderEncode(string Header, const string &Charset)
{
    if(!IMAPHelper::ShouldHeaderEncode(Header))
        return(Header);

    // explode by \n if Header is multi-line
    size_t nlPos = Header.find('\n');
    if(nlPos != string::npos)
    {
        string result = "";
        do
        {
            nlPos = Header.find('\n');

            string Line = Header.substr(0, nlPos-1);
            Header.erase(0, nlPos+1);

            if(Line.length() > 0)
            {
                result.append(IMAPHelper::HeaderEncode(Line, Charset));
                result.append(1, '\n');
            }
        }
        while(nlPos != string::npos);

        return(result);
    }

    // otherwise encode
    vector<string> words;
    vector< pair<bool, string> > fieldParts;
    unsigned int j = 0;

    utils->ExplodeOutsideOfQuotation(Header, words, ' ', true);
    for(unsigned int i=0; i<words.size(); i++)
    {
        string word = words.at(i);
        bool encode = IMAPHelper::ShouldHeaderEncode(word);

        if(fieldParts.size() >= j+1)
        {
            if(fieldParts.at(j).first == encode)
            {
                fieldParts.at(j).second.append(1, ' ');
                fieldParts.at(j).second.append(word);
            }
            else
            {
                fieldParts.push_back(make_pair(encode, word));
                j++;
            }
        }
        else
        {
            fieldParts.push_back(make_pair(encode, word));
        }
    }

    string encodedText = "";
    for(unsigned int i=0; i<fieldParts.size(); i++)
    {
        if(fieldParts.at(i).first)
        {
            encodedText.append(" =?");
            encodedText.append(Charset);
            encodedText.append("?B?");

            string rTrimmedWord = utils->RTrim(fieldParts.at(i).second), suffix = "";

            if(rTrimmedWord.length() < fieldParts.at(i).second.length())
                suffix = fieldParts.at(i).second.substr(rTrimmedWord.length());

            char *encodedPart = utils->Base64Encode(rTrimmedWord.c_str());
            if(encodedPart != NULL)
            {
                encodedText.append(encodedPart);
                encodedText.append("?=");
                encodedText.append(suffix);
                free(encodedPart);
            }
            else
                encodedText.append("?=");
        }
        else
        {
            encodedText.append(1, ' ');
            encodedText.append(utils->Trim(fieldParts.at(i).second));
        }
    }

    return(utils->Trim(encodedText));
}

/*
 * Increment mailbox generation
 */
void IMAPHelper::IncGeneration(MySQL_DB *db, int iUserID, int iGeneration, int iStructureGeneration)
{
    db->Query("UPDATE bm60_users SET mailbox_generation=mailbox_generation+%d,mailbox_structure_generation=mailbox_structure_generation+%d WHERE id='%d'",
        iGeneration,
        iStructureGeneration,
        iUserID);
}

/*
 * Get mailbox generation
 */
void IMAPHelper::GetGeneration(MySQL_DB *db, int iUserID, int *iGeneration, int *iStructureGeneration)
{
    int iGenRes = 0, iStructRes = 0;

    MYSQL_ROW row;
    MySQL_Result *res = db->Query("SELECT mailbox_generation,mailbox_structure_generation FROM bm60_users WHERE id='%d'",
        iUserID);
    if(res->NumRows() == 1)
    {
        row = res->FetchRow();
        iGenRes = atoi(row[0]);
        iStructRes = atoi(row[1]);
    }
    delete res;

    if(iGeneration != NULL)
        *iGeneration = iGenRes;
    if(iStructureGeneration != NULL)
        *iStructureGeneration = iStructRes;
}

/*
 * Parse RFC822 date
 */
time_t IMAPHelper::ParseRFC822Date(const char *szDate)
{
    int iDay, iYear, iHour, iMinute, iSecond, iMonth = 0;
    char szMonth[10];

    if(sscanf(szDate, "%*s %d %4s %d %d:%d:%d",
               &iDay,
               szMonth,
               &iYear,
               &iHour,
               &iMinute,
               &iSecond) == 6)
    {
        // month?
        for(int i=0; i<12; i++)
            if(strcasecmp(szMonths[i], szMonth) == 0)
                iMonth = i;

        // convert to timestamp
        time_t iTime = time(NULL);
#ifdef WIN32
        struct tm *tTime = localtime(&iTime);
#else
        struct tm _tTime, *tTime = localtime_r(&iTime, &_tTime);
#endif
        tTime->tm_hour = iHour;
        tTime->tm_min = iMinute;
        tTime->tm_sec = iSecond;
        tTime->tm_mday = iDay;
        tTime->tm_mon = iMonth;
        tTime->tm_year = iYear - 1900;
        iTime = mktime(tTime);

        return(iTime);
    }
    else
    {
        return(0);
    }
}

/*
 * Parse date
 */
time_t IMAPHelper::ParseDate(const char *szDateIn)
{
    char *szDate = mstrdup(szDateIn);
    char *szFirstHyphen = strchr(szDate, '-'),
        *szSecondHyphen = strrchr(szDate, '-'),
        *szFirstSpace = strchr(szDate, ' ');

    if(szFirstHyphen == szSecondHyphen
        || szFirstHyphen == NULL
        || szSecondHyphen == NULL)
        return(0);

    *szFirstHyphen++ = '\0';
    *szSecondHyphen++ = '\0';

    int iDay = atoi(szDate),
        iYear = atoi(szSecondHyphen),
        iMonth = 0,
        iHour = 0,
        iMinute = 0,
        iSecond = 0;
    char *szMonth = szFirstHyphen;

    if(iYear < 1900)
        iYear += 1900;

    // month?
    for(int i=0; i<12; i++)
    {
        if(strcasecmp(szMonths[i], szMonth) == 0)
        {
            iMonth = i;
            break;
        }
    }

    // time?
    if(szFirstSpace != NULL
        && strlen(szFirstSpace) > 8)
    {
        sscanf(szFirstSpace+1, "%02d:%02d:%02d",
               &iHour,
               &iMinute,
               &iSecond);
    }

    // convert to timestamp
    time_t iTime = time(NULL);
#ifdef WIN32
    struct tm *tTime = localtime(&iTime);
#else
    struct tm _tTime, *tTime = localtime_r(&iTime, &_tTime);
#endif
    tTime->tm_hour = iHour;
    tTime->tm_min = iMinute;
    tTime->tm_sec = iSecond;
    tTime->tm_mday = iDay;
    tTime->tm_mon = iMonth;
    tTime->tm_year = iYear - 1900;
    iTime = mktime(tTime);

    // clean up
    free(szDate);

    // return
    return(iTime);
}

/*
 * Check if string occurs in another string
 */
bool IMAPHelper::InString(const char *szHaystack, const char *szNeedle)
{
    return(strcasestr(szHaystack, szNeedle) != NULL);
}

/**
 * Fetch count of messages
 */
int IMAPHelper::FetchMessageCount(MySQL_DB *db, int iSelected, int iUserID, int iLimit)
{
    int i = -1;
    b1gMailServer::MySQL_Result *res;
    string folderCond = IMAPHelper::FolderCondition(db, iSelected);

    if(iLimit)
    {
        res = db->Query("SELECT COUNT(*) FROM bm60_mails WHERE (%s) AND userid='%d' ORDER BY id DESC LIMIT %d",
            folderCond.c_str(),
            iUserID,
            iLimit);
    }
    else
    {
        res = db->Query("SELECT COUNT(*) FROM bm60_mails WHERE (%s) AND userid='%d'",
            folderCond.c_str(),
            iUserID);
    }

    char **row;
    while((row = res->FetchRow()))
        i = atoi(row[0]);
    delete res;

    db->Log(CMP_IMAP, PRIO_DEBUG, utils->PrintF("[HELPER] [%d] messages in folder [%d]",
        i,
        iSelected));

    return(i);
}

/**
 * Callback to sort IMAP Message List by UID
 */
static bool UIDSortCallback(const IMAPMsg &a, const IMAPMsg &b)
{
    return(a.iUID < b.iUID);
}

/*
 * Fetch messages
 */
IMAPMsgList IMAPHelper::FetchMessages(MySQL_DB *db, int iSelected, int iUserID, int iLimit, bool readOnly)
{
    db->Log(CMP_IMAP, PRIO_DEBUG, utils->PrintF("[HELPER] Fetching messages from folder [%d]",
        iSelected));

    IMAPMsgList vMessages;
    b1gMailServer::MySQL_Result *res;
    string folderCond = IMAPHelper::FolderCondition(db, iSelected);
    bool haveBlobStorage = strcmp(cfg->Get("enable_blobstorage"), "1") == 0;

    if (haveBlobStorage)
    {
        res = db->Query("SELECT flags,fetched,blobstorage,id,datum,size,imapuid FROM bm60_mails LEFT JOIN bm60_bms_imapuid ON bm60_bms_imapuid.mailid=bm60_mails.id WHERE (%s) AND userid='%d'",
            folderCond.c_str(),
            iUserID);
    }
    else
    {
        res = db->Query("SELECT flags,fetched,LENGTH(body),id,datum,size,imapuid FROM bm60_mails LEFT JOIN bm60_bms_imapuid ON bm60_bms_imapuid.mailid=bm60_mails.id WHERE (%s) AND userid='%d'",
            folderCond.c_str(),
            iUserID);
    }

    vMessages.reserve(res->NumRows());

    char **row;
    while((row = res->FetchRow()))
    {
        IMAPMsg cMsg;

        // set flags
        cMsg.iFlags = atoi(row[0]);

        // internal date
        cMsg.iInternalDate = atoi(row[1]);
        if(cMsg.iInternalDate == 0)
            cMsg.iInternalDate = atoi(row[4]);

        // size
        cMsg.iSize = atoi(row[5]);

        // mail ID
        cMsg.iMailID = atoi(row[3]);

        // imap UID
        cMsg.iUID = (row[6] == NULL ? IMAPHelper::AddUID(db, cMsg.iMailID) : atoi(row[6]));

        // file?
        cMsg.bFile = haveBlobStorage || (atoi(row[2]) == 4);

        // storage provider
        cMsg.iBlobStorage = haveBlobStorage ? atoi(row[2]) : 0;

        vMessages.push_back(cMsg);

        // set FLAG_SEEN, if not set
        if(!readOnly)
        {
            if(!FLAGGED(FLAG_SEEN, cMsg.iFlags))
                IMAPHelper::FlagMessage(db, cMsg.iFlags | FLAG_SEEN, atoi(row[3]), iUserID, true);
        }
    }
    delete res;

    // First sort by UID
    sort(vMessages.begin(), vMessages.end(), UIDSortCallback);

    // Then enforce msg number limit
    if(iLimit && (int)vMessages.size() > iLimit)
        vMessages.erase(vMessages.begin(), vMessages.begin() + (vMessages.size() - iLimit + 1));

    // Finally assign sequence numbers
    int i = 0;
    for(IMAPMsgList::iterator it = vMessages.begin(); it != vMessages.end(); ++it)
    {
        it->iSequenceID = ++i;
    }

    return(vMessages);
}

/*
 * Print address structure
 */
void IMAPHelper::AddressStructure(const char *szAddress)
{
    if(szAddress == NULL)
    {
        printf(" NIL");
        return;
    }

    // extract address
    char *szAddr = Mail::ExtractMailAddress(szAddress),
        *szAddr2 = mstrdup(szAddress),
        *szName = NULL,
        *szQuote = NULL;

    if(szAddr != NULL)
    {
        char *szAt = strrchr(szAddr, '@');

        if(szAt != NULL)
            *szAt++ = '\0';

        if((szQuote = strchr(szAddr2, '"')) != NULL)
        {
            szName = szQuote+1;
            if((szQuote = strchr(szName, '"')) != NULL)
                *szQuote = '\0';
            else if((szQuote = strchr(szName, '<')) != NULL)
                *szQuote = '\0';
        }
        else
        {
            if((szQuote = strchr(szAddr2, '<')) != NULL)
            {
                *szQuote = '\0';
                szName = szAddr2;
            }
        }

        if(szName != NULL)
        {
            printf(" ((\"%s\" NIL \"%s\" \"%s\"))",
                IMAPHelper::Escape(utils->Trim(szName).c_str()).c_str(),
                szAddr,
                szAt);
        }
        else
        {
            printf(" ((NIL NIL \"%s\" \"%s\"))",
                szAddr,
                szAt);
        }
    }
    else
    {
        printf(" ((\"%s\" NIL NIL NIL))",
            IMAPHelper::Escape(szAddress).c_str());
    }

    free(szAddr);
    free(szAddr2);
}

/*
 * Decode quoted-printable encoded string
 */
string IMAPHelper::QuotedPrintableDecode(const char *in)
{
    char c, c1, c2;
    string ret;
    for(std::size_t i=0; i < strlen(in); i++)
    {
        c = in[i];
        if(i + 2 < strlen(in))
        {
            c1 = in[i+1];
            c2 = in[i+2];
            if(c == '=' && IS_HEX_CHAR(c1) && IS_HEX_CHAR(c2))
            {
                if(IS_DIGIT(c1))
                {
                    c1 -= '0';
                }
                else
                {
                    c1 -= IS_UPPER(c1) ? 'A' - 10 : 'a' - 10;
                }
                if(IS_DIGIT(c2))
                {
                    c2 -= '0';
                }
                else
                {
                    c2 -= IS_UPPER(c2) ? 'A' - 10 : 'a' - 10;
                }
                ret.append(1, c2 + (c1 << 4));
                i += 2;
            }
            else if(c == '_')
            {
                ret.append(1, ' ');
            }
            else
            {
                ret.append(1, c);
            }
        }
        else
        {
            ret.append(1, c);
        }
    }
    return(ret);
}

/*
 * Replace in string
 */
void IMAPHelper::StringReplace(string &str, const char *szSearch, const char *szReplace)
{
    size_t f = str.find(szSearch);
    if(f != str.npos)
    {
        if(strlen(szReplace) > 0)
        {
            str.replace(f, strlen(szSearch), szReplace);
        }
        else
        {
            str.erase(f, strlen(szSearch));
        }
    }
}

/*
 * Format date
 */
string IMAPHelper::Date(time_t tTime)
{
    char szDateBuff[40];

#ifdef WIN32
    struct tm *t = gmtime(&tTime);
#else
    struct tm _t, *t = gmtime_r(&tTime, &_t);
#endif

    snprintf(szDateBuff, 40, "%02d-%s-%04d %02d:%02d:%02d +0000",
        t->tm_mday,
        szMonths[t->tm_mon],
        t->tm_year+1900,
        t->tm_hour,
        t->tm_min,
        t->tm_sec);

    return(string(szDateBuff));
}

/*
 * Get user's disk usage
 */
unsigned long long IMAPHelper::UserSize(MySQL_DB *db, int iUserID)
{
    unsigned long long iSize = 0;

    MySQL_Result *res = db->Query("SELECT mailspace_used FROM bm60_users WHERE id='%d'",
        iUserID);
    MYSQL_ROW row;
    while((row = res->FetchRow()))
    {
        sscanf(row[0], "%llu", &iSize);
    }
    delete res;

    db->Log(CMP_IMAP, PRIO_DEBUG, utils->PrintF("[HELPER] User size of [%d] is [%a]",
        iUserID,
        iSize));

    return(iSize);
}

/*
 * Generate flag mask from flag list
 */
int IMAPHelper::FlagMaskFromList(const char *szList)
{
    IMAPArgList vFlags = IMAPHelper::ParseList(szList);

    int iFlags = 0 | FLAG_UNREAD;
    for(std::size_t i=0; i < vFlags.size(); i++)
    {
        if(strcasecmp(vFlags.at(i).c_str(), "seen") == 0)
            UNFLAG(FLAG_UNREAD, iFlags);
        else if(strcasecmp(vFlags.at(i).c_str(), "answered") == 0)
            FLAG(FLAG_ANSWERED, iFlags);
        else if(strcasecmp(vFlags.at(i).c_str(), "deleted") == 0)
            FLAG(FLAG_DELETED, iFlags);
        else if(strcasecmp(vFlags.at(i).c_str(), "draft") == 0)
            FLAG(FLAG_DRAFT, iFlags);
        else if(strcasecmp(vFlags.at(i).c_str(), "recent") == 0)
            UNFLAG(FLAG_SEEN, iFlags);
        else if(strcasecmp(vFlags.at(i).c_str(), "flagged") == 0)
            FLAG(FLAG_FLAGGED, iFlags);
    }

    return(iFlags);
}

/*
 * Print flags
 */
void IMAPHelper::PrintFlags(int iFlags)
{
    bool bFirst = true;

    if(!FLAGGED(FLAG_UNREAD, iFlags))
    {
        if(bFirst)
        {
            printf("\\Seen");
            bFirst = false;
        }
        else
            printf(" \\Seen");
    }

    if(FLAGGED(FLAG_ANSWERED, iFlags))
    {
        if(bFirst)
        {
            printf("\\Answered");
            bFirst = false;
        }
        else
            printf(" \\Answered");
    }

    if(FLAGGED(FLAG_DELETED, iFlags))
    {
        if(bFirst)
        {
            printf("\\Deleted");
            bFirst = false;
        }
        else
            printf(" \\Deleted");
    }

    if(FLAGGED(FLAG_DRAFT, iFlags))
    {
        if(bFirst)
        {
            printf("\\Draft");
            bFirst = false;
        }
        else
            printf(" \\Draft");
    }

    if(!FLAGGED(FLAG_SEEN, iFlags))
    {
        if(bFirst)
        {
            printf("\\Recent");
            bFirst = false;
        }
        else
            printf(" \\Recent");
    }

    if(FLAGGED(FLAG_FLAGGED, iFlags))
    {
        if(bFirst)
        {
            printf("\\Flagged");
            bFirst = false;
        }
        else
            printf(" \\Flagged");
    }
}

/*
 * Flag a message
 */
void IMAPHelper::FlagMessage(MySQL_DB *db, int iFlags, int iID, int iUserID, bool noGenerationInc)
{
    db->Log(CMP_IMAP, PRIO_DEBUG, utils->PrintF("[HELPER] Setting flags of message [%d] to [%d]",
        iID,
        iFlags));

    // set flags
    db->Query("UPDATE bm60_mails SET flags='%d' WHERE id='%d' AND userid='%d'",
        iFlags,
        iID,
        iUserID);

    // inc generation
    if (!noGenerationInc)
        IMAPHelper::IncGeneration(db, iUserID, 1, 0);

    // post event
    utils->PostEvent(iUserID, BMS_EVENT_CHANGEMAILFLAGS, iID, iFlags);
}

/*
 * Check if id is in range
 */
bool IMAPHelper::InMSGSet(const IMAPRange &r, int iID)
{
    for(std::size_t i=0; i< r.size(); i++)
    {
        if((iID >= r.at(i).iFrom) && (iID <= r.at(i).iTo))
        {
            return true;
        }
    }
    return false;
}

/*
 * Parse MSGSet
 */
IMAPRange IMAPHelper::ParseMSGSet(const char *szSet)
{
    IMAPRange result;

    vector<string> ranges;
    utils->Explode(szSet, ranges, ',');

    result.reserve(ranges.size());

    for(vector<string>::iterator it = ranges.begin(); it != ranges.end(); ++it)
    {
        string range = utils->Trim(*it);
        if(range.empty())
        {
            continue;
        }

        TwoInts ti;

        std::size_t dotPos = range.find(':');
        if(dotPos != string::npos)
        {
            string from = range.substr(0, dotPos);
            string to = range.substr(dotPos + 1);

            ti.iFrom = atoi(from.c_str());
            ti.iTo = to == "*" ? 2147483646 : atoi(to.c_str());
        }
        else
        {
            ti.iFrom = ti.iTo = atoi(range.c_str());
        }

        result.push_back(ti);
    }

    return(result);
}

/*
 * Get sequence number of first unseen message in iFolder
 */
int IMAPHelper::FirstUnseen(const IMAPMsgList &vMsgs)
{
    int iResult = -1;

    for(IMAPMsgList::const_iterator it = vMsgs.begin();
        it != vMsgs.end();
        ++it)
    {
        if(FLAGGED(FLAG_UNREAD, it->iFlags))
        {
            iResult = it->iSequenceID;
            break;
        }
    }

    return(iResult);
}

/*
 * Get UID of most recent mail in iFolder
 */
int IMAPHelper::GetLastUID(const IMAPMsgList &vMsgs)
{
    if(vMsgs.empty())
        return(-1);

    return(vMsgs.back().iUID);
}

/*
 * Count mails flagged with iFlags
 */
int IMAPHelper::CountFlaggedMails(const IMAPMsgList &vMsgs, int iFlags)
{
    if(vMsgs.empty())
        return(0);

    int iResult = 0;

    for(IMAPMsgList::const_iterator it = vMsgs.begin();
        it != vMsgs.end();
        ++it)
    {
        if(FLAGGED(iFlags, it->iFlags))
            ++iResult;
    }

    return(iResult);
}

/*
 * Count mails NOT flagged with iFlags
 */
int IMAPHelper::CountNotFlaggedMails(const IMAPMsgList &vMsgs, int iFlags)
{
    if(vMsgs.empty())
        return(0);

    int iResult = 0;

    for(IMAPMsgList::const_iterator it = vMsgs.begin();
        it != vMsgs.end();
        ++it)
    {
        if(NOTFLAGGED(iFlags, it->iFlags))
            ++iResult;
    }

    return(iResult);
}

/*
 * Recursively delete a folder
 */
void IMAPHelper::DeleteFolder(MySQL_DB *db, int iID, int iUserID)
{
    bool haveBlobStorage = strcmp(cfg->Get("enable_blobstorage"), "1") == 0;

    // delete child folders
    b1gMailServer::MySQL_Result *res = db->Query("SELECT id FROM bm60_folders WHERE parent='%d' AND userid='%d'",
        iID,
        iUserID);
    MYSQL_ROW row;
    while((row = res->FetchRow()))
        IMAPHelper::DeleteFolder(db, atoi(row[0]), iUserID);
    delete res;

    // delete message files
    size_t freedSpace = 0;

    if (haveBlobStorage)
    {
        res = db->Query("SELECT id,blobstorage,size FROM bm60_mails WHERE folder='%d' AND userid='%d'",
            iID,
            iUserID);
    }
    else
    {
        res = db->Query("SELECT id,LENGTH(body),size FROM bm60_mails WHERE folder='%d' AND userid='%d'",
            iID,
            iUserID);
    }

    while((row = res->FetchRow()))
    {
        freedSpace += atoi(row[2]);

        if (haveBlobStorage || atoi(row[1]) == 4)
        {
            int blobStorage = haveBlobStorage ? atoi(row[1]) : 0;
            utils->DeleteBlob(blobStorage, BMBLOB_TYPE_MAIL, atoi(row[0]), iUserID);
        }

        // post event
        utils->PostEvent(iUserID, BMS_EVENT_DELETEMAIL, atoi(row[0]));
    }
    delete res;

    // delete messages
    db->Query("DELETE FROM bm60_mails WHERE folder='%d' AND userid='%d'",
        iID,
        iUserID);
    db->Query("DELETE FROM bm60_attachments WHERE `mailid`=%d AND `userid`=%d",
        iID,
        iUserID);

    // update space
    if(freedSpace > 0)
        db->Query("UPDATE bm60_users SET mailspace_used=mailspace_used-LEAST(mailspace_used,%d) WHERE id=%d",
            (int)freedSpace,
            iUserID);

    // delete folder
    db->Query("DELETE FROM bm60_folder_conditions WHERE folder='%d'",
        iID);
    db->Query("DELETE FROM bm60_folders WHERE id='%d' AND userid='%d'",
        iID,
        iUserID);
    IMAPHelper::IncGeneration(db, iUserID, 1, 1);

    db->Log(CMP_IMAP, PRIO_DEBUG, utils->PrintF("[HELPER] Folder [%d] deleted",
        iID));
}

/*
 * Print out a literal string
 */
void IMAPHelper::PrintLiteralString(const char *szString)
{
    printf("{%d}\r\n", strlen(szString));
    fwrite(szString, strlen(szString), 1, stdout);
}

/*
 * Must encode char?
 */
bool IMAPHelper::UTF7MustEncode(char c)
{
    if(((c >= 0x20 && c <= 0x7E)))
        return(false);
    if(c == '\r' || c == '\n' || c == '\0' || c == ' ' || c == '\t')
        return(false);
    return(true);
}

/*
 * Decode a string in modified UTF7
 */
string IMAPHelper::StrDecode(const char *szStr)
{
    if(szStr == NULL)
        return("");
    string strOut(""), strTemp;
    char c, cNext;
    bool bDecodeMode = false;

    std::size_t strLength = strlen(szStr);
    for(std::size_t i=0; i < strLength; i++)
    {
        c = *(szStr+i);
        cNext = *(szStr+i+1);

        if(!bDecodeMode)
        {
            if(c == '&')
            {
                if(cNext == '-')
                {
                    strOut.append(1, '&');
                    i++;
                }
                else
                {
                    strTemp = "";
                    bDecodeMode = true;
                }
                continue;
            }
            else
                strOut.append(1, c);
        }
        else
        {
            if(c == '-')
            {
                bDecodeMode = false;
                char *szTemp = utils->Base64Decode((char *)strTemp.c_str(), true);

                int j = 0;
                while(true)
                {
                    if(*(szTemp+j) == '\0'
                        && *(szTemp+j+1) == '\0')
                        break;
                    else
                        if(*(szTemp+j) != '\0')
                            strOut.append(1, *(szTemp+j));
                    j++;
                }

                strOut.append(szTemp);
                free(szTemp);
                strTemp = "";
            }
            else
            {
                strTemp.append(1, c);
            }
        }
    }

    return(strOut);
}

/*
 * Encode a string in modified UTF7
 */
string IMAPHelper::StrEncode(const char *szStr)
{
    if(szStr == NULL)
        return("");
    string strOut(""), strTemp;
    char c, cNext;
    bool bEncodeMode = false;

    if(IMAPHelper::UTF7MustEncode(*szStr))
    {
        strOut.append("&");
        bEncodeMode = true;
    }

    std::size_t strLength = strlen(szStr);
    for(std::size_t i=0; i < strLength; i++)
    {
        c = *(szStr+i);
        cNext = *(szStr+i+1);

        if(bEncodeMode)
        {
            strTemp.append(1, '\0');
            strTemp.append(1, c);
        }
        else
        {
            if(c == 0x26)
                strOut.append("&-");
            else
                strOut.append(1, c);
        }

        if(IMAPHelper::UTF7MustEncode(cNext))
        {
            if(!bEncodeMode)
            {
                strOut.append("&");
                strTemp = "";
                bEncodeMode = true;
            }
        }
        else if(bEncodeMode)
        {
            char *szTemp = utils->Base64Encode((char *)strTemp.c_str(), true, (int)strTemp.length());
            strOut.append(szTemp);
            strOut.append("-");
            strTemp = "";
            free(szTemp);
            bEncodeMode = false;
        }
    }

    return(strOut);
}

/*
 * Is string a list?
 */
bool IMAPHelper::IsList(const char *szLine)
{
    if(strlen(szLine) > 2)
    {
        return((*szLine == '(') && (szLine[strlen(szLine)-1] == ')'));
    }
    else
    {
        return(false);
    }
}

/*
 * Parse a list
 */
IMAPArgList IMAPHelper::ParseList(const char *szLine)
{
    IMAPArgList result;
    string strElem;
    int iOpen = 0;

    std::size_t lineLength = strlen(szLine);
    for(std::size_t i=0; i < lineLength; i++)
    {
        char c = *(szLine+i);

        switch(c)
        {
        case '[':
        case '(':
            if(iOpen > 0)
                strElem.append(1, c);
            iOpen++;
            break;

        case ']':
        case ')':
            if(iOpen == 0)
            {
                strElem.append(1, c);
            }
            else
            {
                iOpen--;
                if(iOpen != 0)
                {
                    strElem.append(1, c);
                }
                else
                {
                    result.push_back(strElem);
                    strElem.clear();
                }
            }
            break;

        case '\r':
            break;

        case ' ':
            if(iOpen > 1)
            {
                strElem.append(1, ' ');
            }
            else
            {
                result.push_back(strElem);
                strElem.clear();
            }
            break;

        default:
            strElem.append(1, c);
            break;
        }
    }

    return(result);
}

/*
 * Parse a line
 */
IMAPArgList IMAPHelper::ParseLine(char *szLine)
{
    IMAPArgList result;
    string strElem;
    int iOpen = 0;
    bool bSlash = false, bQuote = false, bBracket = false, bNoEmpty = false;

Again:
    std::size_t lineLength = strlen(szLine);
    for(std::size_t i=0; i < lineLength; i++)
    {
        char c = *(szLine+i);

        switch(c)
        {
        case '\\':
            if(bSlash)
            {
                bSlash = false;
                strElem.append(1, '\\');
            }
            else
                bSlash = true;
            break;

        case '"':
            if(bSlash)
            {
                bSlash = false;
                strElem.append(1, '"');
            }
            else
                bQuote = !bQuote;
            break;

        case ' ':
        case '\n':
            bSlash = false;
            if(bQuote || (c == ' ' && iOpen > 0))
            {
                strElem.append(1, ' ');
            }
            else
            {
                if(!bNoEmpty || strElem.size() > 0)
                {
                    result.push_back(strElem);
                    strElem.clear();
                }
                bNoEmpty = false;
            }
            break;

        case '\r':
            break;

        case '(':
            if(!bQuote && !bSlash)
            {
                iOpen++;
                strElem.append(1, '(');
            }
            else
            {
                strElem.append(1, '(');
            }
            bSlash = false;
            break;

        case ')':
            if(!bQuote && !bSlash)
            {
                if(iOpen != 0)
                    iOpen--;
                strElem.append(1, ')');
            }
            else
            {
                strElem.append(1, ')');
            }
            bSlash = false;
            break;

        case '{':
            if(!bQuote && !bSlash)
            {
                bBracket = true;
            }
            else
            {
                strElem.append(1, '{');
            }
            bSlash = false;
            break;

        case '}':
            if(!bQuote && !bSlash && bBracket)
            {
                int iBytes = atoi(strElem.c_str());

                int iMaxBytes = atoi(cfg->Get("smtp_size_limit"));
                if(iMaxBytes < 1024*1024*20)
                    iMaxBytes = 1024*1024*20;

                if(iBytes <= 0 || iBytes > iMaxBytes)
                {
                    printf("* NO Parameter too big\r\n");
                    result.push_back("");
                    strElem.clear();
                    bNoEmpty = true;
                    bBracket = false;
                }
                else
                {
                    printf("+ Ok, reading %d bytes\r\n",
                        iBytes);

                    // read bytes
                    char *szElemBuffer = new char[iBytes+1];
                    memset(szElemBuffer, 0, iBytes+1);
                    fread(szElemBuffer, iBytes, 1, stdin);

                    // go on
                    result.push_back(szElemBuffer);
                    delete[] szElemBuffer;

                    strElem.clear();
                    if(fgets(szLine, IMAP_MAXLINE, stdin) == NULL)
                        strcpy(szLine, "\r\n");
                    szLine++;
                    bNoEmpty = true;
                    bBracket = false;
                    goto Again;
                }
            }
            else
            {
                strElem.append(1, '}');
            }
            bSlash = false;
            break;

        default:
            bSlash = false;
            strElem.append(1, c);
            break;
        }
    }

    return(result);
}

/*
 * Fetch folder list for an user
 */
IMAPFolderList IMAPHelper::FetchFolders(MySQL_DB *db, int iUserID)
{
    // fetch all folders of user and store by parent
    map<int, vector<IMAPFolder> > foldersByParent;
    MySQL_Result *res = db->Query("SELECT titel,id,parent,subscribed,intelligent FROM bm60_folders WHERE userid='%d' %sORDER BY titel ASC",
                                  iUserID,
                                  (strcmp(cfg->Get("imap_intelligentfolders"), "1") == 0 ? "" : "AND intelligent=0 "));
    MYSQL_ROW row;
    while((row = res->FetchRow()))
    {
        IMAPFolder f;
        f.iID = atoi(row[1]);
        f.strName = row[0];

        size_t pos;
        while((pos = f.strName.find('/')) != string::npos)
            f.strName[pos] = '-';

        f.bSubscribed = atoi(row[3]) == 1;
        f.bIntelligent = atoi(row[4]) == 1;
        f.strAttributes = "";

        int parentID = atoi(row[2]);
        foldersByParent[parentID].push_back(f);
    }
    delete res;

    IMAPFolderList result;

    // inbox folder
    IMAPFolder fInbox;
    fInbox.iID = 0;
    fInbox.strReference = "";
    fInbox.strName = "INBOX";
    fInbox.strFullName = "INBOX";
    fInbox.strAttributes = "";
    fInbox.bSubscribed = true;
    fInbox.bIntelligent = false;
    result.push_back(fInbox);

    // fetch user folders
    stack<pair<int, string> > parents;
    set<int> visited;
    parents.push(pair<int, string>(-1, ""));

    while(!parents.empty())
    {
        pair<int, string> parent = parents.top();
        parents.pop();

        visited.insert(parent.first);

        map<int, vector<IMAPFolder> >::iterator it = foldersByParent.find(parent.first);
        if(it != foldersByParent.end())
        {
            for(vector<IMAPFolder>::iterator folderIt = it->second.begin(); folderIt != it->second.end(); ++folderIt)
            {
                IMAPFolder f = *folderIt;
                f.strReference = parent.second;
                f.strFullName = f.strReference.empty() ? f.strName : (f.strReference + "/" + f.strName);
                result.push_back(f);

                if(visited.find(f.iID) == visited.end())
                {
                    parents.push(pair<int, string>(f.iID, f.strFullName));
                }
            }
        }
    }

    // sent folder
    IMAPFolder fSent;
    fSent.iID = -2;
    fSent.strReference = "";
    fSent.strName = SENT;
    fSent.strFullName = SENT;
    fSent.strAttributes = "\\Sent";
    fSent.bSubscribed = true;
    fSent.bIntelligent = false;
    result.push_back(fSent);

    // drafts folder
    IMAPFolder fDrafts;
    fDrafts.iID = -3;
    fDrafts.strReference = "";
    fDrafts.strName = DRAFTS;
    fDrafts.strFullName = DRAFTS;
    fDrafts.strAttributes = "\\Drafts";
    fDrafts.bSubscribed = true;
    fDrafts.bIntelligent = false;
    result.push_back(fDrafts);

    // spam folder
    IMAPFolder fSpam;
    fSpam.iID = -4;
    fSpam.strReference = "";
    fSpam.strName = SPAM;
    fSpam.strFullName = SPAM;
    fSpam.strAttributes = "\\Junk";
    fSpam.bSubscribed = true;
    fSpam.bIntelligent = false;
    result.push_back(fSpam);

    // trash folder
    IMAPFolder fTrash;
    fTrash.iID = -5;
    fTrash.strReference = "";
    fTrash.strName = TRASH;
    fTrash.strFullName = TRASH;
    fTrash.strAttributes = "\\Trash";
    fTrash.bSubscribed = true;
    fTrash.bIntelligent = false;
    result.push_back(fTrash);

    db->Log(CMP_IMAP, PRIO_DEBUG, utils->PrintF("[HELPER] Fetched [%d] folders of user [%d]",
        result.size(),
        iUserID));

    return(result);
}

/*
 * Check if regexp matches
 */
bool IMAPHelper::Match(const char *szRegexp, const char *szStr)
{
    pcre *p;
    bool matches;
    const char *error_str;
    int error_nr, c, ovector[3];

    p = pcre_compile(
        szRegexp,
        0,
        &error_str,
        &error_nr,
        NULL
    );
    if(p == NULL)
        return(false);
    c = pcre_exec(
        p,
        NULL,
        szStr,
        (int)strlen(szStr),
        0,
        0,
        ovector,
        3);
    matches = (c >= 0);
    pcre_free(p);
    return(matches);
}

/*
 * Transform wildcards
 */
string IMAPHelper::TransformWildcards(const char *szStr)
{
    string result;

    char c;
    for(std::size_t i=0; i < strlen(szStr); i++)
    {
        c = *(szStr+i);
        switch(c)
        {
        case '*':
            result += "(.*)";
            break;

        case '%':
            result += "([^\\/]+)";
            break;

        case '.':
        case '\\':
        case '+':
        //case '*':     -- should not be escaped, it's our wildcard handled above
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
        case '<':
        case '>':
        case '|':
        case ':':
            result += "\\";
            result.append(1, c);
            break;

        default:
            result.append(1, c);
            break;
        }
    }

    return(result);
}

/*
 * Escape string
 */
string IMAPHelper::Escape(const char *szStr, bool Encode)
{
    string Str = szStr;

    if(Encode)
    {
        Str = IMAPHelper::HeaderEncode(Str, "UNKNOWN");
        szStr = Str.c_str();
    }

    string result;

    char c;
    std::size_t strLength = strlen(szStr);
    for(std::size_t i=0; i < strLength; i++)
    {
        c = *(szStr+i);
        switch(c)
        {
        case '"':
            result += "\\";
            result.append(1, c);
            break;

        default:
            result.append(1, c);
            break;
        }
    }

    return(result);
}
