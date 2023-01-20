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

#define ENABLE_BMS_IO
#include "bmsplugin.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <openssl/md5.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/pkcs7.h>
#include <openssl/x509.h>

using namespace std;

// Plugin class implementation
class TCCleverMailEncryption : public b1gMailServer::Plugin
{
private:
    int _userID;
    bool _installed;
    EVP_PKEY *_privKey;
    X509 *_cert;

public:
    TCCleverMailEncryption()
    {
        this->Name          = "TCCleverMailEncryption";
        this->Title         = "CleverMailEncryption";
        this->Version       = "1.0.0";
        this->Author        = "ThinkClever GmbH";
        this->AuthorWebsite = "http://www.thinkclever.ch";
        this->UpdateURL     = "http://service.b1gmail.com/plugin_updates/";

        this->_userID       = 0;
        this->_installed    = false;
        this->_privKey      = NULL;
        this->_cert         = NULL;
    }

    ~TCCleverMailEncryption()
    {
    }

public:
    void Init();
    void UnInit();
    void OnLoginUser(int UserID, const char *EMail, const char *Password);
    void OnLogoutUser();
    FILE *OnOpenMailBody(FILE *RawMessage);

private:
    bool _loadPrivateKey(const char *Password);
    bool _loadCert();
    string _getXORSalt();
    string _getXORCryptKey(const char *Password);
    string _getUserPref(const char *Key);
    string _md5(string Input);
};

// Export plugin class "TCCleverMailEncryption" to b1gMailServer
EXPORT_BMS_PLUGIN(TCCleverMailEncryption);

//
// plugin callback implementation
//

void TCCleverMailEncryption::Init()
{
    b1gMailServer::MySQL_DB *db = this->BMSUtils->GetMySQLConnection();
    b1gMailServer::MySQL_Result *res;
    char **row;

    // check if plugin is installed
    res = db->Query("SHOW TABLES");
    while((row = res->FetchRow()))
    {
        if(strcmp(row[0], "bm60_tccme_plugin_settings") == 0)
        {
            this->_installed = true;
            break;
        }
    }
    delete res;

    if(!this->_installed)
        return;

    // init openssl algorithms
    OpenSSL_add_all_algorithms();
}

void TCCleverMailEncryption::UnInit()
{
    // log out?
    if(this->_userID != 0)
        this->OnLogoutUser();

    // uninit openssl algorithms
    EVP_cleanup();
}

void TCCleverMailEncryption::OnLoginUser(int UserID, const char *EMail, const char *Password)
{
    if(!this->_installed)
        return;

    this->_userID = UserID;

    this->_loadPrivateKey(Password) && this->_loadCert();
}

void TCCleverMailEncryption::OnLogoutUser()
{
    if(!this->_installed)
        return;

    this->_userID = 0;

    if(this->_privKey != NULL)
    {
        EVP_PKEY_free(this->_privKey);
        this->_privKey = NULL;
    }

    if(this->_cert != NULL)
    {
        X509_free(this->_cert);
        this->_cert = NULL;
    }
}

FILE *TCCleverMailEncryption::OnOpenMailBody(FILE *RawMessage)
{
    if(!this->_installed || this->_privKey == NULL || this->_cert == NULL)
        return(NULL);

    FILE *Result = NULL;
    long FilePos = ftell(RawMessage);

    // check if msg is crypted
    char Buff[34];
    memset(Buff, 0, sizeof(Buff));
    fseek(RawMessage, 0, SEEK_SET);
    fread(Buff, 1, 33, RawMessage);

    if(strcmp(Buff, "X-EncodedBy: CleverMailEncryption") == 0)
    {
        BIO *in = BIO_new_fp(RawMessage, BIO_NOCLOSE);
        if(in != NULL)
        {
            FILE *DecryptedMessage = tmpfile();
            BIO *out = BIO_new_fp(DecryptedMessage, BIO_NOCLOSE);
            if(out != NULL)
            {
                BIO *datain = NULL;

                PKCS7 *p7 = SMIME_read_PKCS7(in, &datain);
                if(p7 != NULL)
                {
                    if(PKCS7_decrypt(p7, this->_privKey, this->_cert, out, PKCS7_DETACHED))
                    {
                        Result = DecryptedMessage;
                    }

                    PKCS7_free(p7);
                    if(datain != NULL)
                        BIO_free(datain);
                }

                BIO_free(out);
                fseek(DecryptedMessage, 0, SEEK_SET);
            }

            if(Result == NULL && DecryptedMessage != NULL)
                fclose(DecryptedMessage);

            BIO_free(in);
        }
    }

    // reset fp
    fseek(RawMessage, FilePos, SEEK_SET);

    return(Result);
}

//
// internal helper functions
//

string TCCleverMailEncryption::_md5(string Input)
{
    unsigned char Hash[16];
    char HexBuff[3];
    string Result = "";

    if(MD5((const unsigned char *)Input.c_str(), (unsigned long)Input.length(), Hash) != NULL)
    {
        for(unsigned int i=0; i<sizeof(Hash); i++)
        {
            sprintf(HexBuff, "%02x", (unsigned int)Hash[i]);
            Result.append(HexBuff);
        }
    }

    return(Result);
}

string TCCleverMailEncryption::_getUserPref(const char *Key)
{
    string Result = "";
    b1gMailServer::MySQL_DB *db = this->BMSUtils->GetMySQLConnection();
    b1gMailServer::MySQL_Result *res;
    char **row;

    res = db->Query("SELECT `value` FROM bm60_userprefs WHERE `userID`=%d AND `key`='%q'",
                    this->_userID,
                    Key);
    while((row = res->FetchRow()))
    {
        Result = row[0];
    }
    delete res;

    return(Result);
}

string TCCleverMailEncryption::_getXORSalt()
{
    string Result = this->_getUserPref("XORKeySalt");

    if(!Result.empty())
    {
        int Len = (int)Result.length();

        char *Decoded = this->BMSUtils->Base64Decode(Result.c_str(), false, &Len);
        Result.clear();

        if(Decoded != NULL && Len >= 64)
        {
            Result.append(Decoded, 64);
            free(Decoded);
        }
    }

    return(Result);
}

string TCCleverMailEncryption::_getXORCryptKey(const char *Password)
{
    string Result = "", Salt = this->_getXORSalt(), ToHash = "";

    ToHash  = Password;
    ToHash += Salt;

    Result = this->_md5(ToHash);

    return(Result);
}

bool TCCleverMailEncryption::_loadPrivateKey(const char *Password)
{
    if(this->_privKey != NULL)
        return(false);

    string XORCryptKey = this->_getXORCryptKey(Password),
            PrivKeyData = this->_getUserPref("tccme_privateKey");
    BIO *in = BIO_new_mem_buf((char *)PrivKeyData.c_str(), PrivKeyData.length());
    if(in != NULL)
    {
        this->_privKey = PEM_read_bio_PrivateKey(in, NULL, 0, (char *)XORCryptKey.c_str());
        BIO_free(in);
    }

    return(this->_privKey != NULL);
}

bool TCCleverMailEncryption::_loadCert()
{
    if(this->_cert != NULL)
        return(false);

    string CertData = this->_getUserPref("tccme_cert");
    BIO *in = BIO_new_mem_buf((char *)CertData.c_str(), CertData.length());
    if(in != NULL)
    {
        this->_cert = PEM_read_bio_X509(in, NULL, NULL, 0);
        BIO_free(in);
    }

    return(this->_cert != NULL);
}
