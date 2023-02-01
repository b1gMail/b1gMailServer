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

#include <set>

/*
 * SEARCH command
 */
void IMAP::Search(char *szLine, bool bUID)
{
    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] SEARCH",
        this->strPeer.c_str()));

    IMAPArgList cArgs = IMAPHelper::ParseLine(szLine);

    if(cArgs.size() >= 3)
    {
        vector<IMAPSearchItem> vSearch;

        bool bParsingRequired = false, bReadingRequired = false;

        int iParams = 0;
        string strItem;
        for(int i=2; i<(int)cArgs.size(); i++)
        {
            strItem = cArgs.at(i);
            if(iParams > 0 && !vSearch.empty())
            {
                IMAPSearchItem &cItem = vSearch.at(vSearch.size()-1);
                if(!cItem.bParam1Set)
                {
                    cItem.strParam1 = strItem;
                    cItem.bParam1Set = true;
                }
                else if(!cItem.bParam2Set)
                {
                    cItem.strParam2 = strItem;
                    cItem.bParam2Set = true;
                }
                --iParams;
                continue;
            }

            // search key
            if(strcasecmp(strItem.c_str(), "answered") == 0)
            {
                iParams = 0;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_ANSWERED;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "bcc") == 0)
            {
                iParams = 1;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_BCC;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "before") == 0)
            {
                iParams = 1;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_BEFORE;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "body") == 0)
            {
                bParsingRequired = true;

                iParams = 1;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_BODY;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "cc") == 0)
            {
                iParams = 1;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_CC;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "deleted") == 0)
            {
                iParams = 0;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_DELETED;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "draft") == 0)
            {
                iParams = 0;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_DRAFT;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "flagged") == 0)
            {
                iParams = 0;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_FLAGGED;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "from") == 0)
            {
                iParams = 1;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_FROM;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "header") == 0)
            {
                bParsingRequired = true;

                iParams = 2;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_HEADER;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "keyword") == 0)
            {
                iParams = 1;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_KEYWORD;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "larger") == 0)
            {
                iParams = 1;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_LARGER;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "new") == 0)
            {
                iParams = 0;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_NEW;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "not") == 0)
            {
                iParams = 0;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_NOT;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "old") == 0)
            {
                iParams = 0;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_OLD;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "on") == 0)
            {
                iParams = 1;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_ON;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "or") == 0)
            {
                iParams = 0;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_OR;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "recent") == 0)
            {
                iParams = 0;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_RECENT;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "seen") == 0)
            {
                iParams = 0;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_SEEN;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "sentbefore") == 0)
            {
                bParsingRequired = true;

                iParams = 1;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_SENTBEFORE;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "senton") == 0)
            {
                bParsingRequired = true;

                iParams = 1;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_SENTON;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "sentsince") == 0)
            {
                bParsingRequired = true;

                iParams = 1;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_SENTSINCE;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "since") == 0)
            {
                iParams = 1;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_SINCE;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "smaller") == 0)
            {
                iParams = 1;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_SMALLER;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "subject") == 0)
            {
                iParams = 1;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_SUBJECT;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "text") == 0)
            {
                bReadingRequired = true;

                iParams = 1;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_TEXT;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "to") == 0)
            {
                iParams = 1;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_TO;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "uid") == 0)
            {
                iParams = 1;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_UID;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "unanswered") == 0)
            {
                iParams = 0;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_UNANSWERED;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "undeleted") == 0)
            {
                iParams = 0;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_UNDELETED;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "undraft") == 0)
            {
                iParams = 0;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_UNDRAFT;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "unflagged") == 0)
            {
                iParams = 0;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_UNFLAGGED;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "unkeyword") == 0)
            {
                iParams = 1;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_UNKEYWORD;
                vSearch.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "unseen") == 0)
            {
                iParams = 0;
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_UNSEEN;
                vSearch.push_back(cItem);
            }
            else if(IS_DIGIT(*strItem.c_str()))
            {
                // message set
                IMAPSearchItem cItem;
                cItem.tItemType = IMAPSearchItem::SEARCH_MESSAGESET;
                cItem.strParam1 = strItem;
                vSearch.push_back(cItem);
            }
        }

        // check parameters
        if(iParams != 0)
        {
            db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Invalid parameters for SEARCH",
                this->strPeer.c_str()));

            printf("* BAD Syntax: Missing %d parameters\r\n",
                iParams);
        }
        else
        {
            // search!
            printf("* SEARCH");

            // fetch mails
            int iSeqID = 0, iSeqIDCounter = 0, iLength;
            IMAPRange cSet;
            b1gMailServer::MySQL_Result *res;
            string folderCond = IMAPHelper::FolderCondition(db, this->iSelected);

            if(this->iLimit)
            {
                res = db->Query("SELECT id,flags,cc,von,4,size,betreff,an,fetched,imapuid FROM bm60_mails LEFT JOIN bm60_bms_imapuid ON bm60_bms_imapuid.mailid=bm60_mails.id WHERE (%s) AND userid='%d' ORDER BY id DESC LIMIT %d",
                    folderCond.c_str(),
                    this->iUserID,
                    this->iLimit);
            }
            else
            {
                res = db->Query("SELECT id,flags,cc,von,4,size,betreff,an,fetched,imapuid FROM bm60_mails LEFT JOIN bm60_bms_imapuid ON bm60_bms_imapuid.mailid=bm60_mails.id WHERE (%s) AND userid='%d' ORDER BY id DESC",
                    folderCond.c_str(),
                    this->iUserID);
            }

            unsigned long iNumResults = res->NumRows();
            vector<int> vMatches;

            char **row;
            while((row = res->FetchRow()))
            {
                iSeqID = iNumResults - iSeqIDCounter++;
                int iMailUID = (row[9] == NULL ? IMAPHelper::AddUID(db, atoi(row[0])) : atoi(row[9]));

                bool bMatch = true;
                string strMailText;

                // fetch and parse message
                if(bParsingRequired || bReadingRequired)
                {
                    FILE *fp = utils->GetMessageFP(atoi(row[0]), this->iUserID);
                    if(fp != NULL)
                    {
                        fseek(fp, 0, SEEK_SET);

                        char buffer[4096];
                        while(!feof(fp))
                        {
                            std::size_t readBytes = fread(buffer, 1, 4096, fp);
                            if(readBytes > 0)
                            {
                                strMailText.append(buffer, readBytes);
                            }
                        }

                        fclose(fp);
                    }
                    else
                    {
                        printf("* Internal error\r\n");
                        db->Log(CMP_IMAP, PRIO_WARNING, utils->PrintF("[%s] Cannot open message %d",
                            this->strPeer.c_str(),
                            atoi(row[0])));
                        continue;
                    }
                }

                MailStructItem cRootItem;
                Mail *cMail = NULL;
                if(bParsingRequired)
                {
                    cRootItem = MailParser::Parse(strMailText);
                    cMail = &cRootItem.cHeaders;
                }

                const char *h;

                int iOr = 0, iNot = 0;
                bool bOr1 = false, bOr2 = false;

                // check search keys
                for(int i=0; i<(int)vSearch.size(); i++)
                {
                    int bResult = 2;
                    const IMAPSearchItem &cItem = vSearch.at(i);

                    switch(cItem.tItemType)
                    {
                    case IMAPSearchItem::SEARCH_OR:
                        iOr = 2;
                        break;

                    case IMAPSearchItem::SEARCH_ANSWERED:
                        bResult = FLAGGED(FLAG_ANSWERED, atoi(row[1]));
                        break;

                    case IMAPSearchItem::SEARCH_BCC:
                        bResult = 0;
                        break;

                    case IMAPSearchItem::SEARCH_CC:
                        bResult = IMAPHelper::InString(row[2], cItem.strParam1.c_str());
                        break;

                    case IMAPSearchItem::SEARCH_DELETED:
                        bResult = FLAGGED(FLAG_DELETED, atoi(row[1]));
                        break;

                    case IMAPSearchItem::SEARCH_DRAFT:
                        bResult = FLAGGED(FLAG_DRAFT, atoi(row[1]));
                        break;

                    case IMAPSearchItem::SEARCH_FLAGGED:
                        bResult = FLAGGED(FLAG_FLAGGED, atoi(row[1]));
                        break;

                    case IMAPSearchItem::SEARCH_FROM:
                        bResult = IMAPHelper::InString(row[3], cItem.strParam1.c_str());
                        break;

                    case IMAPSearchItem::SEARCH_LARGER:
                        iLength = atoi(row[5]);
                        bResult = iLength < atoi(cItem.strParam1.c_str());
                        break;

                    case IMAPSearchItem::SEARCH_MESSAGESET:
                        cSet = IMAPHelper::ParseMSGSet(cItem.strParam1.c_str());
                        bResult = IMAPHelper::InMSGSet(cSet, iSeqID);
                        break;

                    case IMAPSearchItem::SEARCH_NEW:
                        bResult = !FLAGGED(FLAG_SEEN, atoi(row[1]))
                            && FLAGGED(FLAG_UNREAD, atoi(row[1]));
                        break;

                    case IMAPSearchItem::SEARCH_NOT:
                        iNot = 1;
                        break;

                    case IMAPSearchItem::SEARCH_OLD:
                        bResult = FLAGGED(FLAG_SEEN, atoi(row[1]));
                        break;

                    case IMAPSearchItem::SEARCH_RECENT:
                        bResult = !FLAGGED(FLAG_SEEN, atoi(row[1]));
                        break;

                    case IMAPSearchItem::SEARCH_SEEN:
                        bResult = !FLAGGED(FLAG_UNREAD, atoi(row[1]));
                        break;

                    case IMAPSearchItem::SEARCH_SMALLER:
                        iLength = atoi(row[5]);
                        bResult = iLength > atoi(cItem.strParam1.c_str());
                        break;

                    case IMAPSearchItem::SEARCH_SUBJECT:
                        bResult = IMAPHelper::InString(row[6], cItem.strParam1.c_str());
                        break;

                    case IMAPSearchItem::SEARCH_TO:
                        bResult = IMAPHelper::InString(row[7], cItem.strParam1.c_str());
                        break;

                    case IMAPSearchItem::SEARCH_UID:
                        cSet = IMAPHelper::ParseMSGSet(cItem.strParam1.c_str());
                        bResult = IMAPHelper::InMSGSet(cSet, iMailUID);
                        break;

                    case IMAPSearchItem::SEARCH_UNANSWERED:
                        bResult = !FLAGGED(FLAG_ANSWERED, atoi(row[1]));
                        break;

                    case IMAPSearchItem::SEARCH_UNDELETED:
                        bResult = !FLAGGED(FLAG_DELETED, atoi(row[1]));
                        break;

                    case IMAPSearchItem::SEARCH_UNDRAFT:
                        bResult = !FLAGGED(FLAG_DRAFT, atoi(row[1]));
                        break;

                    case IMAPSearchItem::SEARCH_UNFLAGGED:
                        bResult = !FLAGGED(FLAG_FLAGGED, atoi(row[1]));
                        break;

                    case IMAPSearchItem::SEARCH_UNSEEN:
                        bResult = FLAGGED(FLAG_UNREAD, atoi(row[1]));
                        break;

                    case IMAPSearchItem::SEARCH_BEFORE:
                        bResult = IMAPHelper::ParseDate(cItem.strParam1.c_str()) > atoi(row[8]);
                        break;

                    case IMAPSearchItem::SEARCH_ON:
                        bResult = atoi(row[8]) >= IMAPHelper::ParseDate(cItem.strParam1.c_str())
                            && atoi(row[8]) <= (IMAPHelper::ParseDate(cItem.strParam1.c_str())+60*60*24);
                        break;

                    case IMAPSearchItem::SEARCH_SINCE:
                        bResult = atoi(row[8]) >= IMAPHelper::ParseDate(cItem.strParam1.c_str());
                        break;

                    case IMAPSearchItem::SEARCH_KEYWORD:
                        iLength = IMAPHelper::FlagMaskFromList(cItem.strParam1.c_str());
                        bResult = (FLAGGED(iLength, atoi(row[1])));
                        break;

                    case IMAPSearchItem::SEARCH_UNKEYWORD:
                        iLength = IMAPHelper::FlagMaskFromList(cItem.strParam1.c_str());
                        bResult = !(FLAGGED(iLength, atoi(row[1])));
                        break;

                    case IMAPSearchItem::SEARCH_TEXT:
                        bResult = IMAPHelper::InString(strMailText.c_str(), cItem.strParam1.c_str());
                        break;

                    case IMAPSearchItem::SEARCH_BODY:
                        bResult = IMAPHelper::InString(cRootItem.strText.c_str(), cItem.strParam1.c_str());
                        break;

                    case IMAPSearchItem::SEARCH_HEADER:
                        bResult = (h = cMail->GetHeader(cItem.strParam1.c_str())) != NULL
                            && IMAPHelper::InString(h, cItem.strParam2.c_str());
                        break;

                    case IMAPSearchItem::SEARCH_SENTBEFORE:
                        bResult = cMail->GetHeader("date") != NULL
                            && (IMAPHelper::ParseRFC822Date(cMail->GetHeader("date")) < IMAPHelper::ParseDate(cItem.strParam1.c_str()));
                        break;

                    case IMAPSearchItem::SEARCH_SENTON:
                        bResult = cMail->GetHeader("date") != NULL
                            && (IMAPHelper::ParseRFC822Date(cMail->GetHeader("date")) >= IMAPHelper::ParseDate(cItem.strParam1.c_str())
                                && IMAPHelper::ParseRFC822Date(cMail->GetHeader("date")) <= (IMAPHelper::ParseDate(cItem.strParam1.c_str())+60*60*24));
                        break;

                    case IMAPSearchItem::SEARCH_SENTSINCE:
                        bResult = cMail->GetHeader("date") != NULL
                            && IMAPHelper::ParseRFC822Date(cMail->GetHeader("date")) >= IMAPHelper::ParseDate(cItem.strParam1.c_str());
                        break;

                    case IMAPSearchItem::SEARCH_INVALID:
                        break;
                    };

                    if(bResult != 2)
                    {
                        if(iOr == 2)
                        {
                            iOr--;
                            if(iNot == 1)
                            {
                                iNot--;
                                bOr1 = bResult != 1;
                            }
                            else
                                bOr1 = bResult == 1;
                        }
                        else if(iOr == 1)
                        {
                            iOr--;
                            if(iNot == 1)
                            {
                                iNot--;
                                bOr2 = bResult != 1;
                            }
                            else
                                bOr2 = bResult == 1;
                            if(!(bOr1 || bOr2))
                                bMatch = false;
                        }
                        else if(iOr == 0)
                        {
                            if(iNot == 1)
                            {
                                iNot--;
                                if(bResult)
                                    bMatch = false;
                            }
                            else
                                if(!bResult)
                                    bMatch = false;
                        }
                    }
                }

                if(bMatch)
                    vMatches.push_back(bUID ? iMailUID : iSeqID);
            }
            delete res;

            // response
            for(vector<int>::reverse_iterator it = vMatches.rbegin();
                it != vMatches.rend();
                ++it)
            {
                printf(" %d", *it);
            }

            // finalize response
            printf("\r\n");
            printf("%s OK SEARCH completed\r\n",
                this->szTag);
        }

        vSearch.clear();
    }
    else
    {
        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Invalid parameters for SEARCH",
            this->strPeer.c_str()));

        printf("* BAD Syntax: SEARCH [keywords]\r\n");
    }
}

/*
 * FETCH command
 */
void IMAP::Fetch(char *szLine, bool bUID)
{
    char *szCmd = (char *)(bUID ? "UID FETCH" : "FETCH");

    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] %s",
        this->strPeer.c_str(),
        szCmd));

    IMAPArgList cArgs = IMAPHelper::ParseLine(szLine);

    if(cArgs.size() == 4)
    {
        string strMessageSet = cArgs.at(2);
        string strDataItems = cArgs.at(3);

        // parse message set
        IMAPRange cRange = IMAPHelper::ParseMSGSet(strMessageSet.c_str());
        IMAPArgList cDataItems;

        // parse data items
        if(!IMAPHelper::IsList(strDataItems))
        {
            cDataItems.push_back(strDataItems);
        }
        else
        {
            cDataItems = IMAPHelper::ParseList(strDataItems);
        }

        IMAPFetchList cList;
        IMAPFetchItem cItem;
        bool bHaveUID = false, bParsingRequired = false, bReadingRequired = false;

        // force uid if bUID == true
        if(bUID)
        {
            cItem = IMAPFetchItem();
            cItem.tItem = IMAPFetchItem::FETCH_UID;
            cList.push_back(cItem);
            bHaveUID = true;
        }

        for(int i=0; i<(int)cDataItems.size(); i++)
        {
            string strItem = cDataItems.at(i);

            if(strcasecmp(strItem.c_str(), "all") == 0)
            {
                bParsingRequired = true;

                // FLAGS
                cItem = IMAPFetchItem();
                cItem.tItem = IMAPFetchItem::FETCH_FLAGS;
                cItem.bPeek = false;
                cList.push_back(cItem);

                // INTERNALDATE
                cItem = IMAPFetchItem();
                cItem.tItem = IMAPFetchItem::FETCH_INTERNALDATE;
                cItem.bPeek = false;
                cList.push_back(cItem);

                // RFC822.SIZE
                cItem = IMAPFetchItem();
                cItem.tItem = IMAPFetchItem::FETCH_RFC822_SIZE;
                cItem.bPeek = false;
                cList.push_back(cItem);

                // ENVELOPE
                cItem = IMAPFetchItem();
                cItem.tItem = IMAPFetchItem::FETCH_ENVELOPE;
                cItem.bPeek = false;
                cList.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "body") == 0)
            {
                bParsingRequired = true;

                // BODY
                cItem = IMAPFetchItem();
                cItem.tItem = IMAPFetchItem::FETCH_BODY_STRUCTURE_NON_EXTENSIBLE;
                cItem.bPeek = false;
                cList.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "bodystructure") == 0)
            {
                bParsingRequired = true;

                // BODYSTRUCTURE
                cItem = IMAPFetchItem();
                cItem.tItem = IMAPFetchItem::FETCH_BODY_STRUCTURE;
                cItem.bPeek = false;
                cList.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "envelope") == 0)
            {
                bParsingRequired = true;

                // ENVELOPE
                cItem = IMAPFetchItem();
                cItem.tItem = IMAPFetchItem::FETCH_ENVELOPE;
                cItem.bPeek = false;
                cList.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "fast") == 0)
            {
                // FLAGS
                cItem = IMAPFetchItem();
                cItem.tItem = IMAPFetchItem::FETCH_FLAGS;
                cItem.bPeek = false;
                cList.push_back(cItem);

                // INTERNALDATE
                cItem = IMAPFetchItem();
                cItem.tItem = IMAPFetchItem::FETCH_INTERNALDATE;
                cItem.bPeek = false;
                cList.push_back(cItem);

                // RFC822.SIZE
                cItem = IMAPFetchItem();
                cItem.tItem = IMAPFetchItem::FETCH_RFC822_SIZE;
                cItem.bPeek = false;
                cList.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "flags") == 0)
            {
                // FLAGS
                cItem = IMAPFetchItem();
                cItem.tItem = IMAPFetchItem::FETCH_FLAGS;
                cItem.bPeek = false;
                cList.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "full") == 0)
            {
                bParsingRequired = true;

                // FLAGS
                cItem = IMAPFetchItem();
                cItem.tItem = IMAPFetchItem::FETCH_FLAGS;
                cItem.bPeek = false;
                cList.push_back(cItem);

                // INTERNALDATE
                cItem = IMAPFetchItem();
                cItem.tItem = IMAPFetchItem::FETCH_INTERNALDATE;
                cItem.bPeek = false;
                cList.push_back(cItem);

                // RFC822.SIZE
                cItem = IMAPFetchItem();
                cItem.tItem = IMAPFetchItem::FETCH_RFC822_SIZE;
                cItem.bPeek = false;
                cList.push_back(cItem);

                // ENVELOPE
                cItem = IMAPFetchItem();
                cItem.tItem = IMAPFetchItem::FETCH_ENVELOPE;
                cItem.bPeek = false;
                cList.push_back(cItem);

                // BODY
                cItem = IMAPFetchItem();
                cItem.tItem = IMAPFetchItem::FETCH_BODY_STRUCTURE_NON_EXTENSIBLE;
                cItem.bPeek = false;
                cList.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "internaldate") == 0)
            {
                // INTERNALDATE
                cItem = IMAPFetchItem();
                cItem.tItem = IMAPFetchItem::FETCH_INTERNALDATE;
                cItem.bPeek = false;
                cList.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "rfc822") == 0)
            {
                bReadingRequired = true;

                // RFC822
                cItem = IMAPFetchItem();
                cItem.tItem = IMAPFetchItem::FETCH_RFC822;
                cItem.bPeek = false;
                cList.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "rfc822.header") == 0)
            {
                bParsingRequired = true;

                // RFC822.HEADER
                cItem = IMAPFetchItem();
                cItem.tItem = IMAPFetchItem::FETCH_RFC822_HEADER;
                cItem.bPeek = true;
                cList.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "rfc822.size") == 0)
            {
                // RFC822.SIZE
                cItem = IMAPFetchItem();
                cItem.tItem = IMAPFetchItem::FETCH_RFC822_SIZE;
                cItem.bPeek = false;
                cList.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "rfc822.text") == 0)
            {
                bParsingRequired = true;

                // RFC822.TEXT
                cItem = IMAPFetchItem();
                cItem.tItem = IMAPFetchItem::FETCH_RFC822_TEXT;
                cItem.bPeek = false;
                cList.push_back(cItem);
            }
            else if(strcasecmp(strItem.c_str(), "uid") == 0 && !bHaveUID)
            {
                // UID
                bHaveUID = true;
                cItem = IMAPFetchItem();
                cItem.tItem = IMAPFetchItem::FETCH_UID;
                cItem.bPeek = false;
                cList.push_back(cItem);
            }
            else if(strncasecmp(strItem.c_str(), "body[", 5) == 0
                || strncasecmp(strItem.c_str(), "body.peek[", 10) == 0)
            {
                bParsingRequired = true;

                cItem = IMAPFetchItem();
                cItem.tItem = IMAPFetchItem::FETCH_BODY;
                cItem.bPeek = strncasecmp(strItem.c_str(), "body.peek[", 10) == 0;

                // determine type
                char *szItem = mstrdup(strItem.c_str());

                char *szLeftE, *szRightE;
                if((szLeftE = strchr(szItem, '[')) != NULL
                    && (szRightE = strchr(szItem, ']')) != NULL)
                {
                    szLeftE++;
                    *szRightE = '\0';

                    // begins with section?
                    if(IS_DIGIT(*szLeftE))
                    {
                        cItem.strSection = szLeftE;
                        char *szPeriod = szLeftE;
                        while((szPeriod = strchr(szPeriod, '.')) != NULL)
                        {
                            if(!IS_DIGIT(*(szPeriod+1)))
                            {
                                *szPeriod = '\0';
                                szLeftE = szPeriod+1;
                                break;
                            }
                            szPeriod++;
                        }
                    }
                    else
                    {
                        cItem.strSection.clear();
                    }

                    if(strncasecmp(szLeftE, "header.fields.not", 17) == 0)
                    {
                        cItem.tSection = IMAPFetchItem::SECTION_HEADER_FIELDS_NOT;
                        cItem.strHeaderFieldsList = strchr(szLeftE, '(');
                    }
                    else if(strncasecmp(szLeftE, "header.fields", 13) == 0)
                    {
                        cItem.tSection = IMAPFetchItem::SECTION_HEADER_FIELDS;
                        cItem.strHeaderFieldsList = strchr(szLeftE, '(');
                    }
                    else if(strncasecmp(szLeftE, "header", 6) == 0)
                    {
                        cItem.tSection = IMAPFetchItem::SECTION_HEADER;
                    }
                    else if(strncasecmp(szLeftE, "mime", 6) == 0)
                    {
                        cItem.tSection = IMAPFetchItem::SECTION_MIME;
                    }
                    else if(strncasecmp(szLeftE, "text", 4) == 0)
                    {
                        cItem.tSection = IMAPFetchItem::SECTION_TEXT;
                    }
                    else if(strcmp(szLeftE, "") == 0
                        || strcmp(szLeftE, " ") == 0)
                    {
                        if(cItem.strSection.empty())
                            cItem.tSection = IMAPFetchItem::SECTION_EMPTY;
                        else
                            cItem.tSection = IMAPFetchItem::SECTION_SPECIFIC;
                    }
                    else
                    {
                        if(IS_DIGIT(*szLeftE))
                            cItem.tSection = IMAPFetchItem::SECTION_SPECIFIC;
                    }

                    // partial?
                    char *szLeftA = strchr(szRightE+1, '<'),
                        *szRightA = strchr(szRightE+1, '>'),
                        *szPeriodA = strchr(szRightE+1, '.');
                    if(szLeftA != NULL && szRightA != NULL)
                    {
                        if(szPeriodA != NULL)
                        {
                            *szPeriodA++ = '\0';
                            cItem.iPartStart = atoi(szLeftA+1);
                            cItem.iPartLength = atoi(szPeriodA);
                        }
                        else
                        {
                            cItem.iPartStart = atoi(szLeftA+1);
                            cItem.iPartLength = -1;
                        }
                    }
                    else
                    {
                        cItem.iPartStart = -1;
                        cItem.iPartLength = -1;
                    }

                    cList.push_back(cItem);
                }

                delete[] szItem;
            }
        }

        // walk through all messages
        for(int i=0; i<(int)this->vMessages.size(); i++)
        {
            if(IMAPHelper::InMSGSet(cRange, bUID ? vMessages.at(i).iUID : vMessages.at(i).iSequenceID))
            {
                IMAPMsg &cMessage = this->vMessages.at(i);

                // fetch body
                string strMailText;
                const char *szHeader;

                if(bReadingRequired || bParsingRequired)
                {
                    FILE *fp = utils->GetMessageFP(cMessage.iMailID, this->iUserID);
                    if(fp != NULL)
                    {
                        fseek(fp, 0, SEEK_SET);

                        char buffer[4096];
                        while(!feof(fp))
                        {
                            std::size_t readBytes = fread(buffer, 1, 4096, fp);
                            if(readBytes > 0)
                            {
                                strMailText.append(buffer, readBytes);
                            }
                        }

                        fclose(fp);
                    }
                    else
                    {
                        printf("* Internal error\r\n");
                        db->Log(CMP_IMAP, PRIO_WARNING, utils->PrintF("[%s] Cannot open message %d",
                            this->strPeer.c_str(),
                            cMessage.iMailID));
                        continue;
                    }
                }

                // parse mail
                MailStructItem cRootItem;
                Mail *cMail = NULL;

                if(bParsingRequired)
                {
                    db->Log(CMP_IMAP, PRIO_DEBUG, utils->PrintF("[%s] FETCH: Parsing message %d",
                        this->strPeer.c_str(),
                        cMessage.iMailID));

                    cRootItem = MailParser::Parse(strMailText);
                    cMail = &cRootItem.cHeaders;
                }

                printf("* %d FETCH (",
                    cMessage.iSequenceID);

                char szAdd[255];
                int iPartStart = 0;

                bool bPeek = true,
                    bFirst = true;

                for(int j=0; j<(int)cList.size(); j++)
                {
                    IMAPFetchItem &cItem = cList.at(j);
                    if(cItem.iPartStart != -1)
                    {
                        if(cItem.iPartStart < 0)
                            iPartStart = 0;
                        else
                            iPartStart = cItem.iPartStart;
                        snprintf(szAdd, 255, "<%d>", iPartStart);
                    }
                    else
                    {
                        strcpy(szAdd, "");
                        iPartStart = 0;
                    }

                    switch(cItem.tItem)
                    {
                    case IMAPFetchItem::FETCH_BODY:
                        if(!cItem.bPeek)
                            bPeek = false;

                        if(cItem.tSection == IMAPFetchItem::SECTION_EMPTY)
                        {
                            SAFESTART(strMailText.size());
                            printf("%sBODY[]%s {%d}\r\n",
                                bFirst ? "" : " ",
                                szAdd,
                                DETLEN(strMailText.size()));
                            fwrite(START(strMailText.c_str()), DETLEN(strMailText.size()), 1, stdout);
                            if(bFirst)
                                bFirst = false;
                        }
                        else
                        {
                            // search requested part or use default one
                            MailStructItem *cPart = &cRootItem;

                            if(!cItem.strSection.empty())
                            {
                                vector<string> sections;
                                utils->Explode(cItem.strSection, sections, '.');
                                for(vector<string>::iterator it = sections.begin(); it != sections.end(); ++it)
                                {
                                    std::size_t section = strtoul(it->c_str(), NULL, 10);

                                    if(section > 0 && (cPart->vChildItems.size() >= section))
                                    {
                                        cPart = &cPart->vChildItems.at(section-1);
                                    }
                                }
                            }

                            // build answer
                            if(cItem.tSection == IMAPFetchItem::SECTION_HEADER)
                            {
                                SAFESTART(cPart->strHeader2.size());

                                string EncodedResponse = "";
                                EncodedResponse.append(START(cPart->strHeader2.c_str()), DETLEN(cPart->strHeader2.size()));
                                EncodedResponse = IMAPHelper::HeaderEncode(EncodedResponse, "UNKNOWN");

                                printf("%sBODY[%s%sHEADER]%s {%d}\r\n",
                                    bFirst ? "" : " ",
                                    (!cItem.strSection.empty() ? cItem.strSection.c_str() : ""),
                                    (!cItem.strSection.empty() ? "." : ""),
                                    szAdd,
                                    EncodedResponse.length());
                                fwrite(EncodedResponse.c_str(), EncodedResponse.length(), 1, stdout);
                                if(bFirst)
                                    bFirst = false;
                            }
                            else if(cItem.tSection == IMAPFetchItem::SECTION_SPECIFIC
                                || cItem.tSection == IMAPFetchItem::SECTION_TEXT)
                            {
                                SAFESTART(cPart->strText.size());
                                printf("%sBODY[%s%s%s]%s {%d}\r\n",
                                    bFirst ? "" : " ",
                                    (!cItem.strSection.empty() ? cItem.strSection.c_str() : ""),
                                    (!cItem.strSection.empty() ? (cItem.tSection == IMAPFetchItem::SECTION_TEXT ? "." : "") : ""),
                                    (cItem.tSection == IMAPFetchItem::SECTION_TEXT ? "TEXT" : ""),
                                    szAdd,
                                    DETLEN(cPart->strText.size()));
                                if(cPart != NULL)
                                {
                                    fwrite(START(cPart->strText.c_str()), DETLEN(cPart->strText.size()), 1, stdout);
                                }
                                if(bFirst)
                                    bFirst = false;
                            }
                            else if(cItem.tSection == IMAPFetchItem::SECTION_HEADER_FIELDS)
                            {
                                string strData;

                                if(!cItem.strHeaderFieldsList.empty() && cPart != NULL)
                                {
                                    IMAPArgList cFields = IMAPHelper::ParseList(cItem.strHeaderFieldsList.c_str());

                                    for(std::size_t k=0; k < cFields.size(); k++)
                                    {
                                        const char *szFieldValue = cPart->cHeaders.GetRawHeader(utils->StrToLower(cFields.at(k)).c_str());
                                        if(szFieldValue != NULL)
                                        {
                                            strData += cFields.at(k) + ": " + IMAPHelper::HeaderEncode(szFieldValue, "UNKNOWN") + "\r\n";
                                        }
                                    }
                                    strData += "\r\n";
                                }

                                SAFESTART(strData.size());
                                printf("%sBODY[%s%sHEADER.FIELDS %s]%s {%d}\r\n",
                                    bFirst ? "" : " ",
                                    (!cItem.strSection.empty() ? cItem.strSection.c_str() : ""),
                                    (!cItem.strSection.empty() ? "." : ""),
                                    cItem.strHeaderFieldsList.c_str(),
                                    szAdd,
                                    DETLEN(strData.size()));
                                fwrite(START(strData.c_str()), DETLEN(strData.size()), 1, stdout);
                                if(bFirst)
                                    bFirst = false;
                            }
                            else if(cItem.tSection == IMAPFetchItem::SECTION_HEADER_FIELDS_NOT)
                            {
                                string strData;

                                if(!cItem.strHeaderFieldsList.empty() && cPart != NULL)
                                {
                                    IMAPArgList cFields = IMAPHelper::ParseList(cItem.strHeaderFieldsList.c_str());

                                    set<string> excludeFields;
                                    for(IMAPArgList::const_iterator it = cFields.begin(); it != cFields.end(); ++it)
                                    {
                                        excludeFields.insert(utils->StrToLower(*it));
                                    }

                                    vector<string> fields = cPart->cHeaders.GetHeaderFields();
                                    for(vector<string>::const_iterator it = fields.begin(); it != fields.end(); ++it)
                                    {
                                        if(excludeFields.find(*it) != excludeFields.end())
                                        {
                                            continue;
                                        }

                                        const char *szFieldValue = cPart->cHeaders.GetRawHeader(utils->StrToLower(*it).c_str());
                                        if(szFieldValue != NULL)
                                        {
                                            strData += (*it) + ": " + IMAPHelper::HeaderEncode(szFieldValue, "UNKNOWN") + "\r\n";
                                        }
                                    }
                                    strData += "\r\n";
                                }

                                SAFESTART(strData.size());
                                printf("%sBODY[%s%sHEADER.FIELDS.NOT %s]%s {%d}\r\n",
                                    bFirst ? "" : " ",
                                    (!cItem.strSection.empty() ? cItem.strSection.c_str() : ""),
                                    (!cItem.strSection.empty() ? "." : ""),
                                    cItem.strHeaderFieldsList.c_str(),
                                    szAdd,
                                    (DETLEN(strData.size())));
                                fwrite(START(strData.c_str()), DETLEN(strData.size()), 1, stdout);
                                if(bFirst)
                                    bFirst = false;
                            }
                            else if(cItem.tSection == IMAPFetchItem::SECTION_MIME)
                            {
                                // extract mime headers
                                string strData;
                                if(cPart != NULL)
                                {
                                    set<string> includeFields;
                                    includeFields.insert("mime-version");
                                    includeFields.insert("content-type");
                                    includeFields.insert("content-id");
                                    includeFields.insert("content-description");
                                    includeFields.insert("content-transfer-encoding");
                                    includeFields.insert("content-disposition");
                                    includeFields.insert("content-location");
                                    includeFields.insert("content-language");
                                    includeFields.insert("content-md5");

                                    vector<string> fields = cPart->cHeaders.GetHeaderFields();
                                    for(vector<string>::const_iterator it = fields.begin(); it != fields.end(); ++it)
                                    {
                                        if(includeFields.find(*it) == includeFields.end())
                                        {
                                            continue;
                                        }

                                        const char *szFieldValue = cPart->cHeaders.GetRawHeader(utils->StrToLower(*it).c_str());
                                        if(szFieldValue != NULL)
                                        {
                                            strData += (*it) + ": " + IMAPHelper::HeaderEncode(szFieldValue, "UNKNOWN") + "\r\n";
                                        }
                                    }
                                    strData += "\r\n";
                                }

                                SAFESTART(strData.size());
                                printf("%sBODY[%s%sMIME]%s {%d}\r\n",
                                    bFirst ? "" : " ",
                                    (!cItem.strSection.empty() ? cItem.strSection.c_str() : ""),
                                    (!cItem.strSection.empty() ? "." : ""),
                                    szAdd,
                                    DETLEN(strData.size()));
                                fwrite(START(strData.c_str()), DETLEN(strData.size()), 1, stdout);
                                if(bFirst)
                                    bFirst = false;
                            }
                        }
                        break;

                    case IMAPFetchItem::FETCH_BODY_STRUCTURE:
                    case IMAPFetchItem::FETCH_BODY_STRUCTURE_NON_EXTENSIBLE:
                        {
                            bool bExtensionData = cItem.tItem == IMAPFetchItem::FETCH_BODY_STRUCTURE;

                            string strStruct;
                            MailParser::BodyStructure(bExtensionData, cRootItem, strStruct);

                            printf("%s%s %s",
                                bFirst ? "" : " ",
                                bExtensionData ? "BODYSTRUCTURE" : "BODY",
                                strStruct.c_str());
                            if(bFirst)
                                bFirst = false;
                        }
                        break;

                    case IMAPFetchItem::FETCH_RFC822_SIZE:
                        printf("%sRFC822.SIZE %d",
                            bFirst ? "" : " ",
                            cMessage.iSize);
                        if(bFirst)
                            bFirst = false;
                        break;

                    case IMAPFetchItem::FETCH_UID:
                        printf("%sUID %d",
                            bFirst ? "" : " ",
                            cMessage.iUID);
                        if(bFirst)
                            bFirst = false;
                        break;

                    case IMAPFetchItem::FETCH_FLAGS:
                        printf("%sFLAGS (",
                            bFirst ? "" : " ");
                        IMAPHelper::PrintFlags(cMessage.iFlags);
                        printf(")");
                        if(bFirst)
                            bFirst = false;
                        break;

                    case IMAPFetchItem::FETCH_INTERNALDATE:
                        printf("%sINTERNALDATE \"%s\"",
                            bFirst ? "" : " ",
                            IMAPHelper::Date(cMessage.iInternalDate).c_str());
                        bFirst = false;
                        break;

                    case IMAPFetchItem::FETCH_RFC822_HEADER:
                        printf("%sRFC822.HEADER {%d}\r\n%s",
                             bFirst ? "" : " ",
                             (int)cRootItem.strHeader2.size(),
                             cRootItem.strHeader2.c_str());
                        bFirst = false;
                        break;

                    case IMAPFetchItem::FETCH_RFC822_TEXT:
                        printf("%sRFC822.TEXT {%d}\r\n%s",
                            bFirst ? "" : " ",
                            (int)cRootItem.strText.size(),
                            cRootItem.strText.c_str());
                        bFirst = false;
                        break;

                    case IMAPFetchItem::FETCH_ENVELOPE:
                        printf("%sENVELOPE (",
                            bFirst ? "" : " ");
                        bFirst = false;

                        // date
                        if((szHeader = cMail->GetHeader("date")) == NULL)
                            printf("NIL");
                        else
                        {
                            printf("\"%s\"", IMAPHelper::Escape(szHeader, true).c_str());
                        }

                        // subject
                        if((szHeader = cMail->GetHeader("subject")) == NULL)
                            printf(" NIL");
                        else
                        {
                            printf(" \"%s\"", IMAPHelper::Escape(szHeader, true).c_str());
                        }

                        // from
                        if((szHeader = cMail->GetHeader("from")) == NULL)
                            printf(" NIL");
                        else
                            IMAPHelper::AddressStructure(szHeader);

                        // sender
                        if((szHeader = cMail->GetHeader("sender")) == NULL)
                            IMAPHelper::AddressStructure(cMail->GetHeader("from"));
                        else
                            IMAPHelper::AddressStructure(szHeader);

                        // reply-to
                        if((szHeader = cMail->GetHeader("reply-to")) == NULL)
                            IMAPHelper::AddressStructure(cMail->GetHeader("from"));
                        else
                            IMAPHelper::AddressStructure(szHeader);

                        // to
                        if((szHeader = cMail->GetHeader("to")) == NULL)
                            printf(" NIL");
                        else
                            IMAPHelper::AddressStructure(szHeader);

                        // cc
                        if((szHeader = cMail->GetHeader("cc")) == NULL)
                            printf(" NIL");
                        else
                            IMAPHelper::AddressStructure(szHeader);

                        // bcc
                        if((szHeader = cMail->GetHeader("bcc")) == NULL)
                            printf(" NIL");
                        else
                            IMAPHelper::AddressStructure(szHeader);

                        // in-reply-to
                        if((szHeader = cMail->GetHeader("in-reply-to")) == NULL)
                            printf(" NIL");
                        else
                        {
                            printf(" \"%s\"", IMAPHelper::Escape(szHeader, true).c_str());
                        }

                        // message-id
                        if((szHeader = cMail->GetHeader("message-id")) == NULL)
                            printf(" NIL");
                        else
                        {
                            printf(" \"%s\"", IMAPHelper::Escape(szHeader, true).c_str());
                        }

                        printf(")");
                        break;

                    case IMAPFetchItem::FETCH_RFC822:
                        printf("%sRFC822 {%d}\r\n",
                            bFirst ? "" : " ",
                            (int)strMailText.size());
                        fwrite(strMailText.c_str(), strMailText.size(), 1, stdout);
                        if(bFirst)
                            bFirst = false;
                        break;

                    case IMAPFetchItem::FETCH_INVALID:
                        break;
                    };

                }

                printf(")\r\n");

                if(!bPeek && !this->bReadonly && FLAGGED(FLAG_UNREAD, cMessage.iFlags))
                {
                    UNFLAG(FLAG_UNREAD, cMessage.iFlags);
                    IMAPHelper::FlagMessage(db, cMessage.iFlags, cMessage.iMailID, this->iUserID);

                    printf("* %d FETCH (FLAGS(",
                        cMessage.iSequenceID);
                    IMAPHelper::PrintFlags(cMessage.iFlags);
                    printf("))\r\n");
                }
            }
        }

        printf("%s OK FETCH completed\r\n",
            this->szTag);
    }
    else
    {
        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Invalid parameters for %s",
            this->strPeer.c_str(),
            szCmd));

        printf("* BAD Syntax: %s [messageset] [dataitems]\r\n",
            szCmd);
    }
}

/*
 * COPY command
 */
void IMAP::Copy(char *szLine, bool bUID)
{
    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] COPY",
        this->strPeer.c_str()));

    IMAPArgList cArgs = IMAPHelper::ParseLine(szLine);

    if(cArgs.size() == 4)
    {
        string strMessageSet = cArgs.at(2);
        string strMailboxName = IMAPHelper::StrDecode(cArgs.at(3).c_str());

        IMAPRange cRange;

        if(strMailboxName.size() > 0 && strMailboxName[strMailboxName.size()-1] == '/')
        {
            strMailboxName.erase(strMailboxName.size()-1);
        }

        // search mailbox
        IMAPFolder fFolder;
        for(int i=0; i<(int)this->cFolders.size(); i++)
        {
            if(strcasecmp(this->cFolders.at(i).strFullName.c_str(), strMailboxName.c_str()) == 0)
            {
                fFolder = this->cFolders.at(i);
                break;
            }
        }

        // check if mailbox exists
        if(!fFolder)
        {
            db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] COPY: Folder does not exist",
                this->strPeer.c_str()));

            printf("%s NO [TRYCREATE] COPY failed: Invalid destination folder\r\n",
                this->szTag);
        }
        else if(fFolder.iID == this->iSelected ||
            (fFolder.iID == -1 && this->iSelected == 0))
        {
            db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] COPY: Source = target",
                this->strPeer.c_str()));

            printf("%s NO COPY failed: Cannot copy messages to myself\r\n",
                this->szTag);
        }
        else if(fFolder.bIntelligent)
        {
            db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] COPY: Cannot copy to intelligent folders",
                this->strPeer.c_str()));

            printf("%s NO COPY failed: Cannot copy messages to intelligent folders\r\n",
                this->szTag);
        }
        else
        {
            bool bOk = false,
                bDiskFull = false;

            bool haveBlobStorage = strcmp(cfg->Get("enable_blobstorage"), "1") == 0;
            int blobStorage = haveBlobStorage ? atoi(cfg->Get("blobstorage_provider")) : 0;

            // walk through all messages
            for(int i=0; i<(int)this->vMessages.size(); i++)
                if(IMAPHelper::InMSGSet(cRange, bUID ? vMessages.at(i).iUID : vMessages.at(i).iSequenceID))
                {
                    if((IMAPHelper::UserSize(db, this->iUserID) + this->vMessages.at(i).iSize) <= this->iSizeLimit)
                    {
                        b1gMailServer::MySQL_Result *res;

                        // make copy
                        if(haveBlobStorage)
                        {
                            res = db->Query("SELECT id,"            // 0
                                                "userid, "          // 1
                                                "betreff, "         // 2
                                                "von, "             // 3
                                                "an, "              // 4
                                                "cc, "              // 5
                                                "0, "               // 6
                                                "folder, "          // 7
                                                "datum, "           // 8
                                                "trashstamp, "      // 9
                                                "priority, "        // 10
                                                "fetched, "         // 11
                                                "msg_id, "          // 12
                                                "virnam, "          // 13
                                                "trained, "         // 14
                                                "refs, "            // 15
                                                "flags, "           // 16
                                                "size "             // 17
                                                "FROM bm60_mails WHERE id='%d' AND userid='%d' LIMIT 1",
                                this->vMessages.at(i).iMailID,
                                this->iUserID);
                        }
                        else
                        {
                            res = db->Query("SELECT id,"            // 0
                                                "userid, "          // 1
                                                "betreff, "         // 2
                                                "von, "             // 3
                                                "an, "              // 4
                                                "cc, "              // 5
                                                "body, "            // 6
                                                "folder, "          // 7
                                                "datum, "           // 8
                                                "trashstamp, "      // 9
                                                "priority, "        // 10
                                                "fetched, "         // 11
                                                "msg_id, "          // 12
                                                "virnam, "          // 13
                                                "trained, "         // 14
                                                "refs, "            // 15
                                                "flags, "           // 16
                                                "size "             // 17
                                                "FROM bm60_mails WHERE id='%d' AND userid='%d' LIMIT 1",
                                this->vMessages.at(i).iMailID,
                                this->iUserID);
                        }
                        char **row;
                        while((row = res->FetchRow()))
                        {
                            bOk = true;
                            int iMailID = 0;
                            int iFlags = (row[16] == NULL ? 0 : atoi(row[16]));

                            UNFLAG(FLAG_SEEN, iFlags);

                            if(haveBlobStorage)
                            {
                                db->Query("INSERT INTO bm60_mails(userid, betreff, von, an, cc, folder, datum, "
                                                                    "trashstamp, priority, fetched, msg_id, virnam, trained, "
                                                                    "refs, flags, size, blobstorage) "
                                                                "VALUES('%q','%q','%q','%q','%q','%d','%q',"
                                                                        "'%q','%q','%q','%q','%q',"
                                                                        "'%q','%q',%d,'%q',%d)",
                                    row[1] == NULL ? "0" : row[1],
                                    row[2] == NULL ? "0" : row[2],
                                    row[3] == NULL ? "0" : row[3],
                                    row[4] == NULL ? "0" : row[4],
                                    row[5] == NULL ? "0" : row[5],
                                    fFolder.iID,
                                    row[8] == NULL ? "0" : row[8],
                                    row[9] == NULL ? "0" : row[9],
                                    row[10] == NULL ? "0" : row[10],
                                    row[11] == NULL ? "0" : row[11],
                                    row[12] == NULL ? "0" : row[12],
                                    row[13] == NULL ? "0" : row[13],
                                    row[14] == NULL ? "0" : row[14],
                                    row[15] == NULL ? "0" : row[15],
                                    iFlags,
                                    row[17] == NULL ? "0" : row[17],
                                    blobStorage);
                            }
                            else
                            {
                                db->Query("INSERT INTO bm60_mails(userid, betreff, von, an, cc, body, folder, datum, "
                                                                    "trashstamp, priority, fetched, msg_id, virnam, trained, "
                                                                    "refs, flags, size) "
                                                                "VALUES('%q','%q','%q','%q','%q','%q','%d','%q',"
                                                                        "'%q','%q','%q','%q','%q',"
                                                                        "'%q','%q',%d,'%q')",
                                    row[1] == NULL ? "0" : row[1],
                                    row[2] == NULL ? "0" : row[2],
                                    row[3] == NULL ? "0" : row[3],
                                    row[4] == NULL ? "0" : row[4],
                                    row[5] == NULL ? "0" : row[5],
                                    row[6] == NULL ? "0" : row[6],
                                    fFolder.iID,
                                    row[8] == NULL ? "0" : row[8],
                                    row[9] == NULL ? "0" : row[9],
                                    row[10] == NULL ? "0" : row[10],
                                    row[11] == NULL ? "0" : row[11],
                                    row[12] == NULL ? "0" : row[12],
                                    row[13] == NULL ? "0" : row[13],
                                    row[14] == NULL ? "0" : row[14],
                                    row[15] == NULL ? "0" : row[15],
                                    iFlags,
                                    row[17] == NULL ? "0" : row[17]);
                            }
                            iMailID = (int)db->InsertId();

                            if(this->vMessages.at(i).bFile)
                            {
                                FILE *fpSrc = utils->GetMessageFP(this->vMessages.at(i).iMailID, this->iUserID);
                                if(fpSrc != NULL)
                                {
                                    BlobStorageProvider *storage = utils->CreateBlobStorageProvider(blobStorage, this->iUserID);
                                    if(storage != NULL)
                                    {
                                        storage->storeBlob(BMBLOB_TYPE_MAIL, iMailID, fpSrc);
                                        delete storage;
                                    }
                                    fclose(fpSrc);
                                }
                            }

                            IMAPHelper::IncGeneration(db, iUserID, 1, 0);

                            // update space usage
                            db->Query("UPDATE bm60_users SET mailspace_used=mailspace_used+%d WHERE id=%d",
                                atoi(row[17]),
                                this->iUserID);

                            // post event
                            utils->PostEvent(this->iUserID, BMS_EVENT_STOREMAIL, iMailID);
                        }
                        delete res;
                    }
                    else
                    {
                        bDiskFull = true;
                    }
                }

            if(bOk || bUID)
            {
                if(bDiskFull)
                {
                    printf("%s NO [ALERT] COPY failed: Not enough space in mailbox left\r\n",
                        this->szTag);
                }
                else
                {
                    printf("%s OK COPY completed\r\n",
                        this->szTag);
                }
            }
            else
            {
                printf("%s NO COPY failed\r\n",
                    this->szTag);
            }
        }
    }
    else
    {
        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Invalid parameters for COPY",
            this->strPeer.c_str()));

        printf("* BAD Syntax: COPY [messageset] [destmailbox]\r\n");
    }
}

/*
 * STORE command
 */
void IMAP::Store(char *szLine, bool bUID)
{
    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] STORE",
        this->strPeer.c_str()));

    IMAPArgList cArgs = IMAPHelper::ParseLine(szLine);

    if(cArgs.size() == 5)
    {
        string strMessageSet = cArgs.at(2);
        string strDataItem = cArgs.at(3);
        string strValue = cArgs.at(4);

        bool bDone = false;
        IMAPRange cRange = IMAPHelper::ParseMSGSet(strMessageSet.c_str());

        // set new flags
        if(strcasecmp(strDataItem.c_str(), "FLAGS") == 0
            || strcasecmp(strDataItem.c_str(), "FLAGS.SILENT") == 0)
        {
            int iNewFlags = IMAPHelper::FlagMaskFromList(strValue.c_str());

            for(int i=0; i<(int)this->vMessages.size(); i++)
                if(IMAPHelper::InMSGSet(cRange, bUID ? vMessages.at(i).iUID : vMessages.at(i).iSequenceID))
                {
                    this->vMessages.at(i).iFlags = iNewFlags;
                    IMAPHelper::FlagMessage(db,
                        iNewFlags,
                        this->vMessages.at(i).iMailID,
                        this->iUserID);
                    bDone = true;

                    if(strcasecmp(strDataItem.c_str(), "FLAGS") == 0)
                    {
                        printf("* %d FETCH (FLAGS (",
                            this->vMessages.at(i).iSequenceID);
                        IMAPHelper::PrintFlags(this->vMessages.at(i).iFlags);
                        printf("))\r\n");
                    }
                }
        }

        // add or remove flags
        else if(strcasecmp(strDataItem.c_str(), "+FLAGS") == 0
            || strcasecmp(strDataItem.c_str(), "+FLAGS.SILENT") == 0
            || strcasecmp(strDataItem.c_str(), "-FLAGS") == 0
            || strcasecmp(strDataItem.c_str(), "-FLAGS.SILENT") == 0)
        {
            for(int i=0; i<(int)this->vMessages.size(); i++)
                if(IMAPHelper::InMSGSet(cRange, bUID ? vMessages.at(i).iUID : vMessages.at(i).iSequenceID))
                {
                    int iNewFlags = this->vMessages.at(i).iFlags;
                    IMAPArgList vFlags = IMAPHelper::ParseList(strValue.c_str());
                    for(int j=0; j<(int)vFlags.size(); j++)
                    {
                        if(strcasecmp(vFlags.at(j).c_str(), "seen") == 0)
                            UNFLICKFLAG(strDataItem.c_str(), FLAG_UNREAD, iNewFlags);
                        else if(strcasecmp(vFlags.at(j).c_str(), "answered") == 0)
                            FLICKFLAG(strDataItem.c_str(), FLAG_ANSWERED, iNewFlags);
                        else if(strcasecmp(vFlags.at(j).c_str(), "deleted") == 0)
                            FLICKFLAG(strDataItem.c_str(), FLAG_DELETED, iNewFlags);
                        else if(strcasecmp(vFlags.at(j).c_str(), "draft") == 0)
                            FLICKFLAG(strDataItem.c_str(), FLAG_DRAFT, iNewFlags);
                        else if(strcasecmp(vFlags.at(j).c_str(), "recent") == 0)
                            UNFLICKFLAG(strDataItem.c_str(), FLAG_SEEN, iNewFlags);
                        else if(strcasecmp(vFlags.at(j).c_str(), "flagged") == 0)
                            FLICKFLAG(strDataItem.c_str(), FLAG_FLAGGED, iNewFlags);
                    }

                    this->vMessages.at(i).iFlags = iNewFlags;
                    IMAPHelper::FlagMessage(db,
                        iNewFlags,
                        this->vMessages.at(i).iMailID,
                        this->iUserID);
                    bDone = true;

                    if(strDataItem.at(6) != '.')
                    {
                        printf("* %d FETCH (FLAGS (",
                            this->vMessages.at(i).iSequenceID);
                        IMAPHelper::PrintFlags(this->vMessages.at(i).iFlags);
                        printf("))\r\n");
                    }
                }
        }

        if(bDone || bUID)
        {
            printf("%s OK STORE completed\r\n",
                this->szTag);
        }
        else
        {
            printf("%s NO STORE failed\r\n",
                this->szTag);
        }
    }
    else
    {
        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] Invalid parameters for STORE",
            this->strPeer.c_str()));

        printf("* BAD Syntax: STORE [messageset] [dataitem] [value]\r\n");
    }
}

/*
 * EXPUNGE command
 */
void IMAP::Expunge(bool silent)
{
    if(!silent)
    {
        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] EXPUNGE",
            this->strPeer.c_str()));
    }

    // expunge, reloaded
    bool haveBlobStorage = strcmp(cfg->Get("enable_blobstorage"), "1") == 0;
    size_t freedSpace = 0;
    bool mailboxModified = false;
lblTop:
    for(int i=0; i<(int)this->vMessages.size(); i++)
    {
        if(FLAGGED(FLAG_DELETED, this->vMessages.at(i).iFlags))
        {
            int iID = this->vMessages.at(i).iSequenceID;

            // delete message
            if (haveBlobStorage || this->vMessages.at(i).bFile)
            {
                int blobStorage = haveBlobStorage ? this->vMessages.at(i).iBlobStorage : 0;
                utils->DeleteBlob(blobStorage, BMBLOB_TYPE_MAIL, this->vMessages.at(i).iMailID, this->iUserID);
            }

            db->Query("DELETE FROM bm60_mails WHERE id='%d' AND userid='%d'",
                this->vMessages.at(i).iMailID,
                this->iUserID);
            unsigned int affectedRows = db->AffectedRows();

            if(affectedRows == 1)
            {
                freedSpace += this->vMessages.at(i).iSize;

                db->Query("DELETE FROM bm60_attachments WHERE `mailid`=%d AND `userid`=%d",
                          this->vMessages.at(i).iMailID,
                          this->iUserID);
            }

            // post event
            utils->PostEvent(iUserID, BMS_EVENT_DELETEMAIL, this->vMessages.at(i).iMailID);

            // remove from vector
            this->vMessages.erase(vMessages.begin()+i);

            // sequence ids
            for(int j=0; j<(int)this->vMessages.size(); j++)
                if(this->vMessages.at(j).iSequenceID > iID)
                    this->vMessages.at(j).iSequenceID--;

            mailboxModified = true;

            if(!silent)
            {
                printf("* %d EXPUNGE\r\n",
                    iID);
            }

            goto lblTop;
        }
    }
    if (mailboxModified)
        IMAPHelper::IncGeneration(db, iUserID, 1, 0);

    // update space usage
    if(freedSpace > 0)
        db->Query("UPDATE bm60_users SET mailspace_used=mailspace_used-LEAST(mailspace_used,%d) WHERE id=%d",
            (int)freedSpace,
            this->iUserID);

    if(!silent)
    {
        printf("%s OK EXPUNGE completed\r\n",
            this->szTag);
    }
}

/*
 * CLOSE command
 */
void IMAP::Close()
{
    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] CLOSE",
        this->strPeer.c_str()));

    this->Expunge(true);

    this->vMessages.clear();
    this->iSelected = -1;
    this->iState = IMAP_STATE_AUTHENTICATED;

    printf("%s OK CLOSE completed\r\n",
        this->szTag);
}

/*
 * CHECK command
 */
void IMAP::Check()
{
    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] CHECK",
        this->strPeer.c_str()));

    printf("%s OK CHECK completed\r\n",
        this->szTag);
}
