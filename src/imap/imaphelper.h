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

#ifndef _IMAP_IMAPHELPER_H
#define _IMAP_IMAPHELPER_H

#include <core/core.h>

struct IMAPFolder
{
    IMAPFolder() : iID(-1) {}

    string strFullName;
    string strReference;
    string strName;
    string strAttributes;
    int iID;
    bool bSubscribed;
    bool bIntelligent;

    operator bool() const
    {
        return iID != -1;
    }
};

struct TwoInts
{
    int iFrom;
    int iTo;
};

typedef vector<TwoInts>         IMAPRange;
typedef vector<string>          IMAPArgList;
typedef vector<IMAPFolder>      IMAPFolderList;

class IMAPHelper
{
public:
    static void ReportDelta(MySQL_DB *db, IMAPMsgList &oldList, IMAPMsgList &newList);
    static int AddUID(MySQL_DB *db, int iMailID);
    static bool ShouldHeaderEncode(const string &Header);
    static string HeaderEncode(string Header, const string &Charset);
    static void GetGeneration(MySQL_DB *db, int iUserID, int *iGeneration = NULL, int *iStructureGeneration = NULL);
    static void IncGeneration(MySQL_DB *db, int iUserID, int iGeneration = 0, int iStructureGeneration = 0);
    static int FetchMessageCount(MySQL_DB *db, int iSelected, int iUserID, int iLimit);
    static IMAPMsgList FetchMessages(MySQL_DB *db, int iSelected, int iUserID, int iLimit, bool readOnly = false);
    static void PrintLiteralString(const char *szString);
    static IMAPArgList ParseLine(char *szLine);
    static IMAPArgList ParseList(const char *szLine);
    static IMAPArgList ParseList(const string &strLine)
    {
        return ParseList(strLine.c_str());
    }
    static IMAPFolderList FetchFolders(MySQL_DB *db, int iUserID);
    static string StrEncode(const char *szStr);
    static string StrDecode(const char *szStr);
    static bool Match(const char *szRegexp, const char *szStr);
    static string TransformWildcards(const char *szStr);
    static string Escape(const char *szStr, bool Encode = false);
    static void DeleteFolder(MySQL_DB *db, int iID, int iUserID);
    static bool IsList(const char *szLine);
    static bool IsList(const string &strLine)
    {
        return IsList(strLine.c_str());
    }
    static int CountFlaggedMails(const IMAPMsgList &vMsgs, int iFlags);
    static int CountNotFlaggedMails(const IMAPMsgList &vMsgs, int iFlags);
    static int FlagMask(MySQL_DB *db, int iFolder, int iUserID, int iLimit);
    static int GetLastUID(const IMAPMsgList &vMsgs);
    static int FirstUnseen(const IMAPMsgList &vMsgs);
    static IMAPRange ParseMSGSet(const char *szSet);
    static bool InMSGSet(const IMAPRange &r, int iID);
    static void FlagMessage(MySQL_DB *db, int iFlags, int iID, int iUserID, bool noGenerationInc = false);
    static void PrintFlags(int iFlags);
    static unsigned long long UserSize(MySQL_DB *db, int iUserID);
    static string Date(time_t tTime);
    static void StringReplace(string &str, const char *szSearch, const char *szReplace);
    static string QuotedPrintableDecode(const char *in);
    static int FlagMaskFromList(const char *szList);
    static int FlagMaskFromList(const string &strList)
    {
        return FlagMaskFromList(strList.c_str());
    }
    static void AddressStructure(const char *szAddress);
    static bool InString(const char *szHaystack, const char *szNeedle);
    static time_t ParseDate(const char *szDateIn);
    static time_t ParseRFC822Date(const char *szDate);
    static bool UTF7MustEncode(char c);
    static string FolderCondition(MySQL_DB *db, int iFolderID);

private:
    IMAPHelper();
};

#endif
