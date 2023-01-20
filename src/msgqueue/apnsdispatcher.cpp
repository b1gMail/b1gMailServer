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

#include <msgqueue/apnsdispatcher.h>

#ifndef WIN32
#include <sys/time.h>
#else
#include <sys/timeb.h>
#endif

#include <stdexcept>

#include <openssl/bio.h>

namespace
{
    std::string hexDecode(const std::string &in)
    {
        std::string result = "";

        if (in.length() % 2 != 0)
            return result;

        for (size_t i=0; i<in.length(); i += 2)
        {
            std::string hexByte = in.substr(i, 2);
            char charByte = static_cast<char>(static_cast<int>(strtol(hexByte.c_str(), NULL, 16)));
            result.push_back(charByte);
        }

        return result;
    }
}

APNSConnection::APNSConnection(MySQL_DB *logDb, const std::string &apnsHost, const int apnsPort, const std::string &apnsCertificate, const std::string &apnsPrivateKey)
    : db(logDb), host(apnsHost), port(apnsPort), sslCtx(NULL), certificate(NULL), privateKey(NULL), sock(NULL), ssl(NULL), fd(0), connected(false)
{
    parseCertificate(apnsCertificate);
    parsePrivateKey(apnsPrivateKey);
    initSSLCtx();
}

APNSConnection::~APNSConnection()
{
    disconnect();
    freeInitialData();
}

void APNSConnection::freeInitialData()
{
    if (sslCtx != NULL)
        SSL_CTX_free(sslCtx);

    if (privateKey != NULL)
        EVP_PKEY_free(privateKey);

    if (certificate != NULL)
        X509_free(certificate);
}

void APNSConnection::initSSLCtx()
{
    sslCtx = SSL_CTX_new(SSLv23_client_method());
    if (sslCtx == NULL)
    {
        freeInitialData();
        throw std::runtime_error("Failed to create SSL context");
    }

    if (SSL_CTX_use_certificate(sslCtx, certificate) != 1)
    {
        freeInitialData();
        throw std::runtime_error("Failed to use certificate");
    }

    if (SSL_CTX_use_PrivateKey(sslCtx, privateKey) != 1)
    {
        freeInitialData();
        throw std::runtime_error("Failed to use private key");
    }

    SSL_CTX_set_mode(sslCtx, SSL_MODE_AUTO_RETRY);

    // TODO: Verify CA?
}

void APNSConnection::parseCertificate(const std::string &certificate)
{
    if (this->certificate != NULL)
        throw std::runtime_error("Certificate already loaded");

    BIO *certBio = BIO_new_mem_buf(const_cast<void *>(static_cast<const void *>(certificate.c_str())), certificate.length());
    if (certBio == NULL)
    {
        freeInitialData();
        throw std::runtime_error("Failed to create certificate BIO");
    }
    this->certificate = PEM_read_bio_X509(certBio, NULL, NULL, 0);
    BIO_free(certBio);

    if (this->certificate == NULL)
    {
        freeInitialData();
        throw std::runtime_error("Failed to parse certificate");
    }
}

void APNSConnection::parsePrivateKey(const std::string &privateKey)
{
    if (this->privateKey != NULL)
        throw std::runtime_error("Private key already loaded");

    BIO *pkBio = BIO_new_mem_buf(const_cast<void *>(static_cast<const void *>(privateKey.c_str())), privateKey.length());
    if (pkBio == NULL)
    {
        freeInitialData();
        throw std::runtime_error("Failed to create private key BIO");
    }
    this->privateKey = PEM_read_bio_PrivateKey(pkBio, NULL, NULL, 0);
    BIO_free(pkBio);

    if (this->privateKey == NULL)
    {
        freeInitialData();
        throw std::runtime_error("Failed to parse private key");
    }
}

bool APNSConnection::connect()
{
    if (connected)
    {
        db->Log(CMP_MSGQUEUE, PRIO_DEBUG, utils->PrintF("Already connected"));
        return true;
    }

    sock = BIO_new_ssl_connect(sslCtx);
    if (sock == NULL)
    {
        db->Log(CMP_MSGQUEUE, PRIO_WARNING, utils->PrintF("Failed to create SSL BIO"));
        disconnect();
        return false;
    }

    int res = BIO_set_conn_hostname(sock, host.c_str());
    if (res != 1)
    {
        db->Log(CMP_MSGQUEUE, PRIO_WARNING, utils->PrintF("Failed to set BIO hostname (%d)", res));
        disconnect();
        return false;
    }

    char portStr[32];
    snprintf(portStr, 32, "%d", port);
    res = BIO_set_conn_port(sock, portStr);
    if (res != 1)
    {
        db->Log(CMP_MSGQUEUE, PRIO_WARNING, utils->PrintF("Failed to set BIO port (%d)", res));
        disconnect();
        return false;
    }

    BIO_get_ssl(sock, &ssl);
    if (ssl == NULL)
    {
        db->Log(CMP_MSGQUEUE, PRIO_WARNING, utils->PrintF("Failed to get BIO SSL"));
        disconnect();
        return false;
    }

    res = BIO_do_connect(sock);
    if (res != 1)
    {
        db->Log(CMP_MSGQUEUE, PRIO_WARNING, utils->PrintF("Failed to connect to APNS service (%d)", res));
        disconnect();
        return false;
    }

    res = BIO_get_fd(sock, &fd);
    if (res == -1)
    {
        db->Log(CMP_MSGQUEUE, PRIO_WARNING, utils->PrintF("Failed to get BIO fd"));
        disconnect();
        return false;
    }

    res = BIO_do_handshake(sock);
    if (res != 1)
    {
        db->Log(CMP_MSGQUEUE, PRIO_WARNING, utils->PrintF("SSL handshake with APNS service failed (%d)", res));
        disconnect();
        return false;
    }

    connected = true;
    return true;
}

void APNSConnection::disconnect()
{
    if (sock != NULL)
    {
        BIO_free_all(sock);
        sock = NULL;
    }

    ssl = NULL;
    fd = 0;
    connected = false;
}

bool APNSConnection::sendMessage(const APNSMessage &msg)
{
    bool result = false;

    if (!connected || ssl == NULL)
        return result;

    if (msg.deviceToken.length() % 2 != 0)
        return result;

    std::string decDeviceToken = hexDecode(msg.deviceToken);

    const int messageBuffSize = sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint32_t)
                                    + sizeof(uint16_t) + decDeviceToken.length()
                                    + sizeof(uint16_t) + msg.payload.length();
    char *messageBuff = new char[messageBuffSize];
    char *ptr = messageBuff;

    const uint8_t command = 1;
    *ptr++ = command;

    uint32_t networkOrderMsgId = htons(msg.id);
    memcpy(ptr, &networkOrderMsgId, sizeof(uint32_t));
    ptr += sizeof(uint32_t);

    uint32_t networkOrderExpiry = htons(msg.expiry);
    memcpy(ptr, &networkOrderExpiry, sizeof(uint32_t));
    ptr += sizeof(uint32_t);

    uint16_t networkOrderTokenLength = htons(decDeviceToken.length());
    memcpy(ptr, &networkOrderTokenLength, sizeof(uint16_t));
    ptr += sizeof(uint16_t);
    memcpy(ptr, decDeviceToken.c_str(), decDeviceToken.length());
    ptr += decDeviceToken.length();

    uint16_t networkOrderPayloadLength = htons(msg.payload.length());
    memcpy(ptr, &networkOrderPayloadLength, sizeof(uint16_t));
    ptr += sizeof(uint16_t);
    memcpy(ptr, msg.payload.c_str(), msg.payload.length());
    ptr += msg.payload.length();

    int len = ptr - messageBuff;
    if (SSL_write(ssl, messageBuff, len) == len)
        result = true;

    delete[] messageBuff;

    return result;
}

bool APNSConnection::checkForErrors(APNSError &error)
{
    error.status = APNS_ERR_NO_ERROR;
    error.id = 0;

    fd_set fdSet;

    FD_ZERO(&fdSet);
    FD_SET(fd, &fdSet);

    struct timeval tm;
    tm.tv_sec = 0;
    tm.tv_usec = 0;

    int res = select(fd + 1, &fdSet, NULL, NULL, &tm);
    if (res == -1)
        return true;

    if (FD_ISSET(fd, &fdSet))
    {
        char errBuff[6];

        int bytesRead = SSL_read(ssl, errBuff, sizeof(errBuff));

        int sslError;
        switch (sslError = SSL_get_error(ssl, bytesRead))
        {
        case SSL_ERROR_NONE:
            if (errBuff[0] == 8)
            {
                error.status = errBuff[1];
                error.id = ntohs(*reinterpret_cast<uint32_t *>(errBuff+2));
            }
            else
            {
                error.status = APNS_ERR_UNKNOWN;
            }
            return true;

        case SSL_ERROR_WANT_READ:
        case SSL_ERROR_WANT_WRITE:
        case SSL_ERROR_ZERO_RETURN:
        case SSL_ERROR_SYSCALL:
        default:
            db->Log(CMP_MSGQUEUE, PRIO_NOTE, utils->PrintF("Unknown error while reading data from APNS (%d)", sslError));
            error.status = APNS_ERR_UNKNOWN;
            return true;
        }
    }

    return false;
}

APNSDispatcher::APNSDispatcher(MySQL_DB *logDb, const std::string &apnsHost, const int apnsPort, const std::string &apnsCertificate, const std::string &apnsPrivateKey)
    : db(logDb), connection(db, apnsHost, apnsPort, apnsCertificate, apnsPrivateKey), idCounter(0), isRunning(true)
{
    pthread_mutex_init(&this->queueLock, NULL);
    pthread_cond_init(&this->queueSignal, NULL);
}

APNSDispatcher::~APNSDispatcher()
{
    pthread_cond_destroy(&this->queueSignal);
    pthread_mutex_destroy(&this->queueLock);
}

void APNSDispatcher::enqueue(const APNSMessage &msg)
{
    pthread_mutex_lock(&this->queueLock);
    sendQueue.push(msg);
    pthread_cond_signal(&this->queueSignal);
    pthread_mutex_unlock(&this->queueLock);
}

void APNSDispatcher::reenqueueAfterError(const uint32_t failedId)
{
    if (sentMessages.empty())
        return;

    pthread_mutex_lock(&this->queueLock);

    // remove all sent entries up to including the failed one
    while (!sentMessages.empty())
    {
        bool stop = (sentMessages.front().id == failedId);
        sentMessages.pop_front();
        if (stop) break;
    }

    // now re-enqueue the rest
    while (!sentMessages.empty())
    {
        sendQueue.push(sentMessages.front());
        sentMessages.pop_front();
    }

    pthread_cond_signal(&this->queueSignal);
    pthread_mutex_unlock(&this->queueLock);
}

void APNSDispatcher::run()
{
    int sleepMS = 1000;
    bool first = true;

    this->isRunning = true;
    while(this->isRunning)
    {
        if (first)
        {
            first = false;
        }
        else
        {
            const int sleepStep = 10;
            for (int i=0; i < (sleepMS/sleepStep) && this->isRunning; ++i)
            {
                utils->MilliSleep(sleepStep);
            }
            if (!this->isRunning)
                break;
            sleepMS *= 2;

            // limit delay to 1 hour
            if (sleepMS > 3600*1000)
                sleepMS = 3600*1000;
        }

        db->Log(CMP_MSGQUEUE, PRIO_DEBUG, utils->PrintF("Connecting to APNS service"));

        if (!connection.connect())
        {
            db->Log(CMP_MSGQUEUE, PRIO_NOTE, utils->PrintF("Failed to connect to APNS service"));
            continue;
        }

        while(this->isRunning)
        {
            APNSMessage job;
            bool jobValid = false;

            pthread_mutex_lock(&queueLock);
            if (sendQueue.empty())
            {
                struct timespec waitTime;

#ifndef WIN32
                struct timeval sysTime;

                if(gettimeofday(&sysTime, NULL) == 0)
                {
                    waitTime.tv_sec     = sysTime.tv_sec;
                    waitTime.tv_nsec    = sysTime.tv_usec * 1000 + 10000000;
                    if (waitTime.tv_nsec >= 1000000000)
                    {
                        ++waitTime.tv_sec;
                        waitTime.tv_nsec -= 1000000000;
                    }
                    pthread_cond_timedwait(&queueSignal, &queueLock, &waitTime);
                }
                else
                {
                    pthread_cond_wait(&queueSignal, &queueLock);
                }
#else
                struct __timeb64 tb;
                _ftime64(&tb);

                waitTime.tv_nsec    = tb.millitm * 1000000 + 10000000;
                waitTime.tv_sec     = tb.time;
                if (waitTime.tv_nsec >= 1000000000)
                {
                    ++waitTime.tv_sec;
                    waitTime.tv_nsec -= 1000000000;
                }
                pthread_cond_timedwait(&queueSignal, &queueLock, &waitTime);
#endif
            }
            if (!sendQueue.empty())
            {
                job = sendQueue.front();
                sendQueue.pop();
                jobValid = true;
            }
            pthread_mutex_unlock(&queueLock);

            if (!this->isRunning)
                break;

            APNSError error;
            if (connection.checkForErrors(error))
            {
                if (error.id > 0)
                {
                    db->Log(CMP_MSGQUEUE, PRIO_NOTE, utils->PrintF("APNS service rejected push notification #%d with status %d, re-enqueueing and re-connecting",
                        (int)error.id, (int)error.status));
                }
                else
                {
                    db->Log(CMP_MSGQUEUE, PRIO_NOTE, utils->PrintF("APNS connection error %d, re-connecting",
                        (int)error.status));
                }

                if (jobValid)
                    enqueue(job);

                if (error.id > 0)
                    reenqueueAfterError(error.id);

                connection.disconnect();
                break;
            }

            if (jobValid)
            {
                if (job.id == 0)
                    job.id = ++idCounter;

                if (connection.sendMessage(job))
                {
                    db->Log(CMP_MSGQUEUE, PRIO_DEBUG, utils->PrintF("Sent push notification #%d to device <%s>: %s",
                        (int)job.id, job.deviceToken.c_str(), job.payload.c_str()));

                    sentMessages.push_back(job);

                    if (sentMessages.size() > APNS_SENT_BUFFER_SIZE)
                        sentMessages.pop_front();

                    sleepMS = 1000;
                }
                else
                {
                    db->Log(CMP_MSGQUEUE, PRIO_DEBUG, utils->PrintF("Failed to send push notification #%d to device <%s>, re-enqueueing and re-connecting",
                        (int)job.id, job.deviceToken.c_str()));

                    enqueue(job);
                    connection.disconnect();
                    break;
                }
            }
        }
    }
}

void APNSDispatcher::quit()
{
    this->isRunning = false;
    pthread_cond_signal(&this->queueSignal);
}
