/*
 * b1gMailServer
 * Copyright (c) 2002-2026
 *
 * App-password authentication for IMAP/POP3/SMTP (parity with BMAppPassword).
 */

#include <core/utils.h>
#include <core/exception.h>

using namespace Core;

namespace {

struct AppPasswordPrefs
{
    bool globallyEnabled;
    bool mailEnabled;
    string mailMode;

    // Defaults match b1gMail BMAppPassword when prefs columns are absent:
    // global feature on, mail scopes off, mode off → no mail app-password path.
    AppPasswordPrefs()
        : globallyEnabled(true), mailEnabled(false), mailMode("off")
    {
    }

    bool mailScopesActive() const
    {
        return globallyEnabled && mailEnabled;
    }
};

bool PrefIsYes(const char *value)
{
    return value != NULL && strcmp(value, "yes") == 0;
}

string NormalizeMailMode(const char *value)
{
    if(value == NULL || value[0] == '\0')
        return "off";
    if(strcmp(value, "warn") == 0 || strcmp(value, "enforce") == 0 || strcmp(value, "strict") == 0)
        return value;
    return "off";
}

char FailbanTypeForScope(const string &scope)
{
    if(scope == "pop3")
        return FAILBAN_POP3LOGIN;
    if(scope == "smtp")
        return FAILBAN_SMTPLOGIN;
    return FAILBAN_IMAPLOGIN;
}

AppPasswordPrefs LoadAppPasswordPrefs()
{
    // Cached for the lifetime of the process (BMS workers are short/long-lived
    // but prefs rarely change; avoids a prefs SELECT on every AUTH).
    static bool loaded = false;
    static AppPasswordPrefs cached;

    if(loaded)
        return cached;

    AppPasswordPrefs prefs;
    try
    {
        MySQL_Result *res = db->Query(
            "SELECT `app_password_enable`,`app_password_mail_enable`,`app_password_mail_mode` "
            "FROM bm60_prefs LIMIT 1");
        if(res->NumRows() == 1)
        {
            MYSQL_ROW row = res->FetchRow();
            prefs.globallyEnabled = PrefIsYes(row[0]);
            prefs.mailEnabled = PrefIsYes(row[1]);
            prefs.mailMode = NormalizeMailMode(row[2]);
        }
        delete res;
    }
    catch(Core::Exception &)
    {
        // older b1gMail without app-password prefs columns → defaults above
    }

    cached = prefs;
    loaded = true;
    return cached;
}

bool ScopeInList(const string &scopeCsv, const string &requiredScope)
{
    size_t start = 0;
    while(start <= scopeCsv.length())
    {
        size_t comma = scopeCsv.find(',', start);
        string part = (comma == string::npos)
            ? scopeCsv.substr(start)
            : scopeCsv.substr(start, comma - start);
        if(part == requiredScope)
            return true;
        if(comma == string::npos)
            break;
        start = comma + 1;
    }
    return false;
}

void SafeAuthLog(int severity, char *message)
{
    if(message == NULL)
        return;
    try
    {
        db->Log(CMP_CORE, severity, message);
    }
    catch(...)
    {
        free(message);
    }
}

bool VerifyAppPasswordRows(int userID, const string &passwordPlain, const string &requiredScope, int *matchedID)
{
    if(matchedID != NULL)
        *matchedID = 0;

    int matchID = 0;
    time_t now = time(NULL);
    MySQL_Result *res = db->Query(
        "SELECT `id`,`password_hash`,`scope`,`expires`,`revoked_at` FROM bm60_app_passwords WHERE `user`=%d",
        userID);

    MYSQL_ROW row;
    while((row = res->FetchRow()))
    {
        // Cheap rejects first — avoid bcrypt/Argon2 unless the row is a candidate.
        if((row[4] ? atol(row[4]) : 0) != 0)
            continue;
        time_t expires = row[3] ? (time_t)atol(row[3]) : 0;
        if(expires != 0 && expires < now)
            continue;
        if(!ScopeInList(row[2] ? row[2] : "", requiredScope))
            continue;

        string hash = row[1] ? row[1] : "";
        if(!utils->VerifyModernPassword(passwordPlain, hash))
            continue;

        // Keep iterating for timing parity among candidate rows.
        if(matchID == 0)
            matchID = row[0] ? atoi(row[0]) : 0;
    }
    delete res;

    if(matchID > 0 && matchedID != NULL)
        *matchedID = matchID;
    return matchID > 0;
}

bool AcceptAccountPassword(int userID, const string &passwordPlain, const string &storedHash, const string &salt)
{
    if(!utils->VerifyUserPassword(passwordPlain, storedHash, salt))
        return false;
    try
    {
        utils->UpgradeUserPasswordIfNeeded(userID, passwordPlain, storedHash);
    }
    catch(...)
    {
    }
    return true;
}

} // namespace

bool Utils::AreMailAppPasswordsEnabled()
{
    return LoadAppPasswordPrefs().mailScopesActive();
}

string Utils::MailAppPasswordMode()
{
    return LoadAppPasswordPrefs().mailMode;
}

bool Utils::UserHasMfaLoginReady(int userID)
{
    if(userID <= 0)
        return false;
    try
    {
        MySQL_Result *res = db->Query(
            "SELECT `enabled`,`totp_enabled`,`totp_secret`,`email_enabled`,`recovery_mode` "
            "FROM bm60_mfa_accounts WHERE `account_type`='user' AND `account_id`=%d LIMIT 1",
            userID);
        if(res->NumRows() != 1)
        {
            delete res;
            return false;
        }
        MYSQL_ROW row = res->FetchRow();
        string enabled = row[0] ? row[0] : "";
        string totpEnabled = row[1] ? row[1] : "";
        string totpSecret = row[2] ? row[2] : "";
        string emailEnabled = row[3] ? row[3] : "";
        string recoveryMode = row[4] ? row[4] : "";
        delete res;

        if(enabled != "yes")
            return false;
        if(!totpSecret.empty() && totpEnabled == "yes")
            return true;
        if(emailEnabled == "yes")
            return true;
        if(recoveryMode == "altmail")
            return true;
        // Parity with BMMfa::RequiresMfaVerifyAtLogin(): enabled row with a TOTP
        // secret still counts if flags are inconsistent (e.g. aborted setup).
        return !totpSecret.empty();
    }
    catch(Core::Exception &)
    {
        return false;
    }
}

bool Utils::VerifyAppPassword(int userID, const string &passwordPlain, const string &requiredScope, int *matchedID)
{
    if(matchedID != NULL)
        *matchedID = 0;
    if(userID <= 0 || passwordPlain.empty() || requiredScope.empty())
        return false;

    AppPasswordPrefs prefs = LoadAppPasswordPrefs();
    if(!prefs.mailScopesActive() || prefs.mailMode == "off")
        return false;
    if(requiredScope != "imap" && requiredScope != "pop3" && requiredScope != "smtp")
        return false;

    try
    {
        return VerifyAppPasswordRows(userID, passwordPlain, requiredScope, matchedID);
    }
    catch(Core::Exception &)
    {
        return false;
    }
}

void Utils::TouchAppPassword(int id, const string &ip, const string &scope)
{
    if(id <= 0)
        return;
    string ipTrim = ip.length() > 64 ? ip.substr(0, 64) : ip;
    string scopeTrim = scope.length() > 16 ? scope.substr(0, 16) : scope;
    try
    {
        // Store unix time as 64-bit-safe decimal string for MySQL INT/BIGINT columns.
        char szNow[32];
        snprintf(szNow, sizeof(szNow), "%lld", (long long)time(NULL));
        db->Query("UPDATE bm60_app_passwords SET `last_used`=%s, `last_ip`='%q', `last_scope`='%q' WHERE `id`=%d",
            szNow, ipTrim.c_str(), scopeTrim.c_str(), id);
    }
    catch(Core::Exception &)
    {
    }
}

bool Utils::AuthenticateMailPassword(int userID,
                                     const string &passwordPlain,
                                     const string &scope,
                                     const string &peerIP,
                                     const string &storedHash,
                                     const string &salt)
{
    if(userID <= 0 || passwordPlain.empty())
        return false;

    // Throttle / reject before bcrypt/Argon2 (and before app-password loops).
    // Fail-open on failban errors so a DB issue cannot lock out all mail logins.
    if(!peerIP.empty() && peerIP != "(unknown)")
    {
        try
        {
            if(!Failban_AllowExpensiveAuth(IPAddress(peerIP), FailbanTypeForScope(scope)))
                return false;
        }
        catch(...)
        {
        }
    }

    AppPasswordPrefs prefs = LoadAppPasswordPrefs();
    const bool allowAppPasswords = prefs.mailScopesActive() && prefs.mailMode != "off";

    if(allowAppPasswords)
    {
        try
        {
            int appID = 0;
            if(VerifyAppPasswordRows(userID, passwordPlain, scope, &appID))
            {
                try { TouchAppPassword(appID, peerIP, scope); } catch(...) {}
                SafeAuthLog(PRIO_NOTE, PrintF(
                    "Mail auth user=%d scope=%s via app password id=%d",
                    userID, scope.c_str(), appID));
                return true;
            }
        }
        catch(...)
        {
        }

        bool mfaReady = false;
        try { mfaReady = UserHasMfaLoginReady(userID); } catch(...) {}

        SafeAuthLog(PRIO_NOTE, PrintF(
            "Mail auth decision user=%d scope=%s mode=%s mfa_ready=%s",
            userID, scope.c_str(), prefs.mailMode.c_str(), mfaReady ? "yes" : "no"));

        if(prefs.mailMode == "strict")
        {
            SafeAuthLog(PRIO_WARNING, PrintF(
                "Mail auth user=%d rejected: strict mode, app password required (scope=%s)",
                userID, scope.c_str()));
            return false;
        }
        if(mfaReady && prefs.mailMode == "enforce")
        {
            SafeAuthLog(PRIO_WARNING, PrintF(
                "Mail auth user=%d rejected: MFA is active, app password required (scope=%s)",
                userID, scope.c_str()));
            return false;
        }
        if(mfaReady && prefs.mailMode == "warn")
        {
            if(!AcceptAccountPassword(userID, passwordPlain, storedHash, salt))
                return false;
            SafeAuthLog(PRIO_WARNING, PrintF(
                "Mail auth user=%d via account password despite active MFA (deprecated; scope=%s)",
                userID, scope.c_str()));
            return true;
        }
    }

    return AcceptAccountPassword(userID, passwordPlain, storedHash, salt);
}
