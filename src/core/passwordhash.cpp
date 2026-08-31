/*
 * b1gMailServer
 * Copyright (c) 2002-2026
 *
 * Password verification compatible with b1gMail (legacy MD5 + password_hash/bcrypt/argon2id).
 */

#include <core/utils.h>
#include <core/exception.h>
#include <core/bcrypt/bcrypt.h>
#include <argon2.h>

#include <openssl/rand.h>

using namespace Core;

namespace {

struct PasswordHashPrefs
{
    string algo;
    int cost;

    PasswordHashPrefs()
        : algo("bcrypt"), cost(12)
    {
    }
};

bool IsArgon2Hash(const string &hash)
{
    return hash.length() >= 9 && hash.compare(0, 7, "$argon2") == 0;
}

bool IsBcryptHash(const string &hash)
{
    return hash.length() >= 4 && hash[0] == '$' && hash[1] == '2';
}

PasswordHashPrefs LoadPasswordHashPrefs()
{
    PasswordHashPrefs prefs;

    try
    {
        MySQL_Result *res = db->Query("SELECT pw_hash_li_algo, pw_hash_li_cost FROM bm60_prefs LIMIT 1");
        if(res->NumRows() == 1)
        {
            MYSQL_ROW row = res->FetchRow();
            if(row[0] != NULL && row[0][0] != '\0')
                prefs.algo = row[0];
            if(row[1] != NULL && row[1][0] != '\0')
                prefs.cost = atoi(row[1]);
        }
        delete res;
    }
    catch(Core::Exception &)
    {
        // older b1gMail without pw_hash_* prefs columns
    }

    return prefs;
}

int NormalizeBcryptCost(int cost)
{
    if(cost < 10)
        return 12;
    if(cost > 15)
        return 15;
    return cost;
}

int NormalizeArgon2TimeCost(int cost)
{
    if(cost < 2)
        return 4;
    if(cost > 6)
        return 6;
    return cost;
}

bool CreateBcryptHash(const string &passwordPlain, int cost, string &outHash)
{
    char szSalt[BCRYPT_HASHSIZE], szHash[BCRYPT_HASHSIZE];

    if(bcrypt_gensalt(NormalizeBcryptCost(cost), szSalt) != 0)
        return false;
    if(bcrypt_hashpw(passwordPlain.c_str(), szSalt, szHash) != 0)
        return false;

    outHash = szHash;
    return true;
}

bool CreateArgon2idHash(const string &passwordPlain, int timeCost, string &outHash)
{
    uint8_t salt[16];
    char encoded[256];

    if(RAND_bytes(salt, sizeof(salt)) != 1)
        return false;

    if(argon2id_hash_encoded(
            (uint32_t)NormalizeArgon2TimeCost(timeCost),
            65536,
            1,
            passwordPlain.c_str(),
            passwordPlain.length(),
            salt,
            sizeof(salt),
            32,
            encoded,
            sizeof(encoded)) != ARGON2_OK)
    {
        return false;
    }

    outHash = encoded;
    return true;
}

} // namespace

bool Utils::PasswordIsModern(const string &hash)
{
    return hash.length() >= 4 && hash[0] == '$';
}

bool Utils::LooksLikeMD5Hash(const string &in)
{
    if(in.length() != 32)
        return false;

    for(std::size_t i = 0; i < in.length(); i++)
    {
        char c = in[i];
        if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }

    return true;
}

bool Utils::VerifyModernPassword(const string &passwordPlain, const string &storedHash)
{
    if(storedHash.length() < 4)
        return false;

    if(IsArgon2Hash(storedHash))
    {
        return argon2id_verify(storedHash.c_str(), passwordPlain.c_str(), passwordPlain.length()) == ARGON2_OK;
    }

    if(IsBcryptHash(storedHash))
    {
        return bcrypt_checkpw(passwordPlain.c_str(), storedHash.c_str()) == 0;
    }

    return false;
}

bool Utils::VerifyUserPassword(const string &passwordPlain, const string &storedHash, const string &salt)
{
    if(storedHash.empty())
        return false;

    if(PasswordIsModern(storedHash))
        return VerifyModernPassword(passwordPlain, storedHash);

    string password = LooksLikeMD5Hash(passwordPlain) ? passwordPlain : MD5(passwordPlain);

    if(strcmp(cfg->Get("salted_passwords"), "1") == 0)
        return strcasecmp(storedHash.c_str(), MD5(password + salt).c_str()) == 0;

    return strcasecmp(storedHash.c_str(), MD5(passwordPlain).c_str()) == 0;
}

bool Utils::PasswordNeedsUpgrade(const string &storedHash)
{
    return !PasswordIsModern(storedHash);
}

void Utils::UpgradeUserPasswordIfNeeded(int userID, const string &passwordPlain, const string &storedHash)
{
    if(userID <= 0 || !PasswordNeedsUpgrade(storedHash))
        return;

    PasswordHashPrefs prefs = LoadPasswordHashPrefs();
    string strNewHash;

    if(prefs.algo == "argon2id")
    {
        if(!CreateArgon2idHash(passwordPlain, prefs.cost, strNewHash))
            return;
    }
    else
    {
        if(!CreateBcryptHash(passwordPlain, prefs.cost, strNewHash))
            return;
    }

    db->Query("UPDATE bm60_users SET passwort='%q', passwort_salt='' WHERE id='%d'",
        strNewHash.c_str(),
        userID);
}
