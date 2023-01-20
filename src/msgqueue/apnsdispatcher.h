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

#ifndef _MSGQUEUE_APNSDISPATCHER_H_
#define _MSGQUEUE_APNSDISPATCHER_H_

#include <core/core.h>

#include <deque>
#include <queue>
#include <string>

#include <stdint.h>

#include <openssl/ssl.h>
#include <pthread.h>

struct APNSMessage
{
    uint32_t id;
    uint32_t expiry;
    std::string deviceToken;
    std::string payload;

    APNSMessage()
    {
        id = 0;
        expiry = 0;
    }
};

#define APNS_ERR_NO_ERROR               0
#define APNS_ERR_PROCESSING             1
#define APNS_ERR_MISSING_DEVTOKEN       2
#define APNS_ERR_MISSING_TOPIC          3
#define APNS_ERR_MISSING_PAYLOAD        4
#define APNS_ERR_INVALID_TOKEN_SIZE     5
#define APNS_ERR_INVALID_TOPIC_SIZE     6
#define APNS_ERR_INVALID_PAYLOAD_SIZE   7
#define APNS_ERR_INVALID_TOKEN          8
#define APNS_ERR_SHUTDOWN               10
#define APNS_ERR_PROTOCOL               128
#define APNS_ERR_UNKNOWN                255

#define APNS_SENT_BUFFER_SIZE           128

struct APNSError
{
    uint8_t status;
    uint32_t id;
};

class APNSConnection
{
public:
    APNSConnection(MySQL_DB *logDb, const std::string &apnsHost, const int apnsPort, const std::string &apnsCertificate, const std::string &apnsPrivateKey);
    ~APNSConnection();

public:
    bool connect();
    void disconnect();
    bool sendMessage(const APNSMessage &msg);
    bool checkForErrors(APNSError &error);

private:
    void initSSLCtx();
    void parseCertificate(const std::string &certificate);
    void parsePrivateKey(const std::string &privateKey);
    void freeInitialData();

private:
    MySQL_DB *db;

    std::string host;
    int port;

    SSL_CTX *sslCtx;
    X509 *certificate;
    EVP_PKEY *privateKey;
    BIO *sock;
    SSL *ssl;
    int fd;

    bool connected;

    APNSConnection(const APNSConnection &);
    APNSConnection &operator=(const APNSConnection &);
};

class APNSDispatcher
{
public:
    APNSDispatcher(MySQL_DB *logDb, const std::string &apnsHost, const int apnsPort, const std::string &apnsCertificate, const std::string &apnsPrivateKey);
    ~APNSDispatcher();

public:
    void enqueue(const APNSMessage &msg);
    void reenqueueAfterError(const uint32_t failedId);
    void run();
    void quit();

private:
    MySQL_DB *db;
    APNSConnection connection;
    int idCounter;
    bool isRunning;

    pthread_mutex_t queueLock;
    pthread_cond_t queueSignal;
    std::queue<APNSMessage> sendQueue;
    std::deque<APNSMessage> sentMessages;

    APNSDispatcher(const APNSDispatcher &);
    APNSDispatcher &operator=(const APNSDispatcher &);
};

#endif
