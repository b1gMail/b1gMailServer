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

AppPasswordPrefs LoadAppPasswordPrefs()
{
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
        // older b1gMail without app-password prefs
    }

    return prefs;
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
    int now = (int)time(NULL);

    MySQL_Result *res = db->Query(
        "SELECT `id`,`password_hash`,`scope`,`expires`,`revoked_at` FROM bm60_app_passwords WHERE `user`=%d",
        userID);

    MYSQL_ROW row;
    while((row = res->FetchRow()))
    {
        string hash = row[1] ? row[1] : "";
        string scopeCsv = row[2] ? row[2] : "";
        int expires = row[3] ? atoi(row[3]) : 0;
        int revokedAt = row[4] ? atoi(row[4]) : 0;
        int id = row[0] ? atoi(row[0]) : 0;

        bool ok = utils->VerifyModernPassword(passwordPlain, hash);
        if(!ok)
            continue;
        if(revokedAt != 0)
            continue;
        if(expires != 0 && expires < now)
            continue;
        if(!ScopeInList(scopeCsv, requiredScope))
            continue;

        // Keep iterating for timing parity with BMAppPassword::Verify().
        if(matchID == 0)
            matchID = id;
    }
    delete res;

    if(matchID > 0)
    {
        if(matchedID != NULL)
            *matchedID = matchID;
        return true;
    }

    return false;
}

bool AcceptAccountPassword(int userID,
                           const string &passwordPlain,
                           const string &storedHash,
                           const string &salt)
{
    if(!utils->VerifyUserPassword(passwordPlain, storedHash, salt))
        return false;

    try
    {
        utils->UpgradeUserPasswordIfNeeded(userID, passwordPlain, storedHash);
    }
    catch(...)
    {
        // Rehash is best-effort; never fail a valid login because of it.
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
        if(!totpSecret.empty())
            return true;

        return false;
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
    // MODE_OFF: app passwords are disabled for mail — account password only.
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

    string ipTrim = ip;
    if(ipTrim.length() > 64)
        ipTrim = ipTrim.substr(0, 64);

    string scopeTrim = scope;
    if(scopeTrim.length() > 16)
        scopeTrim = scopeTrim.substr(0, 16);

    try
    {
        db->Query("UPDATE bm60_app_passwords SET `last_used`=%d, `last_ip`='%q', `last_scope`='%q' WHERE `id`=%d",
            (int)time(NULL),
            ipTrim.c_str(),
            scopeTrim.c_str(),
            id);
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

    AppPasswordPrefs prefs;
    try
    {
        prefs = LoadAppPasswordPrefs();
    }
    catch(...)
    {
    }

    // MODE_OFF or mail subsystem inactive: account password only (no app-password path).
    const bool allowAppPasswords = prefs.mailScopesActive() && prefs.mailMode != "off";

    if(allowAppPasswords)
    {
        try
        {
            int appID = 0;
            if(VerifyAppPasswordRows(userID, passwordPlain, scope, &appID))
            {
                try
                {
                    TouchAppPassword(appID, peerIP, scope);
                }
                catch(...)
                {
                }
                SafeAuthLog(PRIO_NOTE, PrintF(
                    "Mail auth user=%d scope=%s via app password id=%d",
                    userID, scope.c_str(), appID));
                return true;
            }
        }
        catch(...)
        {
            // App-password DB/crypto failures must not block account password.
        }

        bool mfaReady = false;
        try
        {
            mfaReady = UserHasMfaLoginReady(userID);
        }
        catch(...)
        {
        }

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
            // Logged only after account password succeeds (below).
            if(AcceptAccountPassword(userID, passwordPlain, storedHash, salt))
            {
                SafeAuthLog(PRIO_WARNING, PrintF(
                    "Mail auth user=%d via account password despite active MFA (deprecated; scope=%s)",
                    userID, scope.c_str()));
                return true;
            }
            return false;
        }
    }

    return AcceptAccountPassword(userID, passwordPlain, storedHash, salt);
}
