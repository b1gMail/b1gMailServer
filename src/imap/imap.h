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

#ifndef _IMAP_IMAP_H
#define _IMAP_IMAP_H

#include <core/core.h>

enum tIMAPState
{
    IMAP_STATE_NON_AUTHENTICATED,   // 3.1.
    IMAP_STATE_AUTHENTICATED,       // 3.2.
    IMAP_STATE_SELECTED,            // 3.3.
    IMAP_STATE_LOGOUT               // 3.4.
};

struct IMAPSearchItem
{
    enum stItemType
    {
        SEARCH_INVALID,
        SEARCH_MESSAGESET,
        SEARCH_ANSWERED,
        SEARCH_BCC,
        SEARCH_BEFORE,
        SEARCH_BODY,
        SEARCH_CC,
        SEARCH_DELETED,
        SEARCH_DRAFT,
        SEARCH_FLAGGED,
        SEARCH_FROM,
        SEARCH_HEADER,
        SEARCH_KEYWORD,
        SEARCH_LARGER,
        SEARCH_NEW,
        SEARCH_NOT,
        SEARCH_OLD,
        SEARCH_ON,
        SEARCH_OR,
        SEARCH_RECENT,
        SEARCH_SEEN,
        SEARCH_SENTBEFORE,
        SEARCH_SENTON,
        SEARCH_SENTSINCE,
        SEARCH_SINCE,
        SEARCH_SMALLER,
        SEARCH_SUBJECT,
        SEARCH_TEXT,
        SEARCH_TO,
        SEARCH_UID,
        SEARCH_UNANSWERED,
        SEARCH_UNDELETED,
        SEARCH_UNDRAFT,
        SEARCH_UNFLAGGED,
        SEARCH_UNKEYWORD,
        SEARCH_UNSEEN
    } tItemType;

    string strParam1;
    string strParam2;

    bool bParam1Set;
    bool bParam2Set;

    IMAPSearchItem()
        : tItemType(SEARCH_INVALID)
        , bParam1Set(false)
        , bParam2Set(false)
    {}

    operator bool() const
    {
        return tItemType != SEARCH_INVALID;
    }
};

struct IMAPFetchItem
{
    enum stItem
    {
        FETCH_INVALID,
        FETCH_BODY_STRUCTURE_NON_EXTENSIBLE,
        FETCH_BODY,
        FETCH_BODY_STRUCTURE,
        FETCH_ENVELOPE,
        FETCH_FLAGS,
        FETCH_INTERNALDATE,
        FETCH_RFC822,
        FETCH_RFC822_HEADER,
        FETCH_RFC822_SIZE,
        FETCH_RFC822_TEXT,
        FETCH_UID
    } tItem;

    string strSection;
    enum stSection
    {
        SECTION_EMPTY,
        SECTION_SPECIFIC,
        SECTION_HEADER,
        SECTION_HEADER_FIELDS,
        SECTION_HEADER_FIELDS_NOT,
        SECTION_MIME,
        SECTION_TEXT
    } tSection;

    string strHeaderFieldsList;

    int iPartStart;
    int iPartLength;

    bool bPeek;

    IMAPFetchItem()
        : tItem(FETCH_INVALID)
        , tSection(SECTION_EMPTY)
        , iPartStart(0)
        , iPartLength(0)
        , bPeek(false)
    {}

    operator bool() const
    {
        return tItem != FETCH_INVALID;
    }
};

struct IMAPMsg
{
    int iUID;                       // 2.3.1.1. IMAP UID
    int iMailID;                    // b1gMail mail ID
    int iSequenceID;                // 2.3.1.2.
    int iFlags;                     // 2.3.2.
    time_t iInternalDate;           // 2.3.3.
    size_t iSize;                   // 2.3.4.
    bool bFile;
    int iBlobStorage;
};

extern char _szIMAPFolderName_SENT[128],
            _szIMAPFolderName_SPAM[128],
            _szIMAPFolderName_DRAFTS[128],
            _szIMAPFolderName_TRASH[128];

typedef vector<IMAPMsg>                         IMAPMsgList;
typedef vector<IMAPFetchItem>                       IMAPFetchList;
#define HVAL(x,y)                                   cMail.GetHeader(x) == NULL ? y : cMail.GetHeader(x)
#define START(x)                                    x+iPartStart
#define SAFESTART(x)                                if(iPartStart >= (int)x) iPartStart = (int)x;
#define DETLEN(x)                                   (cItem.iPartLength == -1 ? ((int)x - iPartStart) : (cItem.iPartLength >= (int)x-iPartStart) ? (int)x-iPartStart : cItem.iPartLength)
#define FLICKFLAG(szDataItem, flag, iNewFlags)      (*szDataItem == '+') ? (!FLAGGED(flag, iNewFlags) ? (iNewFlags |= flag) : false) : (FLAGGED(flag, iNewFlags) ? (iNewFlags &= ~(flag)) : false)
#define UNFLICKFLAG(szDataItem, flag, iNewFlags)    (*szDataItem == '+') ? (FLAGGED(flag, iNewFlags) ? (iNewFlags &= ~(flag)) : false) : (!FLAGGED(flag, iNewFlags) ? (iNewFlags |= flag) : false)
#define IMAP_MAXLINE                                8192    // rfc 2683
#define FLAG(flag, x)                               (x |= flag)
#define UNFLAG(flag, x)                             (x &= ~(flag))
#define FLAGGED(flag, x)                            ((x & flag) == flag)
#define NOTFLAGGED(flag, x)                         ((x & flag) != flag)
#define FLAG_UNREAD                                 1
#define FLAG_ANSWERED                               2
#define FLAG_DELETED                                8
#define FLAG_FLAGGED                                16
#define FLAG_SEEN                                   32
#define FLAG_DRAFT                                  1024
#define SENT                                        _szIMAPFolderName_SENT
#define SPAM                                        _szIMAPFolderName_SPAM
#define DRAFTS                                      _szIMAPFolderName_DRAFTS
#define TRASH                                       _szIMAPFolderName_TRASH
#define LASTLOGIN_UPDATE_INTERVAL                   60

#include <imap/imaphelper.h>
#include <imap/mail.h>
#include <imap/mailparser.h>

class IMAP
{
public:
    IMAP();
    ~IMAP();

public:
    void Run();

private:
    bool ProcessLine(char *szLine);
    void DoLogin();
    void Authenticate(char *szLine);
    void StartTLS(char *szLine);
    void Login(char *szLine);
    void Append(char *szLine);
    void List(char *szLine, bool bLSUB = false);
    void Create(char *szLine);
    void Rename(char *szLine);
    void Subscribe(char *szLine, bool bUNSUBSCRIBE = false);
    void Delete(char *szLine);
    void Status(char *szLine);
    void Select(char *szLine, bool bEXAMINE = false);
    void Store(char *szLine, bool bUID = false);
    void Copy(char *szLine, bool bUID = false);
    void Fetch(char *szLine, bool bUID = false);
    void Search(char *szLine, bool bUID = false);
    void GetQuota(char *szLine);
    void GetQuotaRoot(char *szLine);
    void SetQuota();
    void Idle();
    void XApplePushService(char *szLine);
    void Close();
    void Expunge(bool silent = false);
    void Capability();
    void Noop();
    void Check();
    void Logout();

public:
    int iUserID;
    bool bTLSMode;
    bool bTimeout;
    bool bQuit;
    bool bBanned;
    SSL_CTX *ssl_ctx;

private:
    IMAPFolderList cFolders;
    IMAPMsgList vMessages;
    int iBadTries;
    int iState;
    int iCommands;
    int iSelected;
    int iGeneration;
    int iStructureGeneration;
    unsigned long long iSizeLimit;
    bool bReadonly;
    string strPeer;
    char szTag[IMAP_MAXLINE+1];
    string strUser;
    bool bAPNS;
    bool bAutoExpunge;
    int iLimit;
    time_t iLastLoginUpdate;

    IMAP(const IMAP &);
    IMAP &operator=(const IMAP &);
};

extern IMAP *cIMAP_Instance;

#endif
