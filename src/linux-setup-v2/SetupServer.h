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

#ifndef _SETUPSERVER_H_
#define _SETUPSERVER_H_

#include "HTTPServer.h"
#include "Language.h"
#include <string>

#define FEATURE_POP3    1
#define FEATURE_IMAP    2
#define FEATURE_STATS   4

class SetupServer : public HTTPServer
{
public:
    SetupServer(int port, std::string password, Language *lang);
    ~SetupServer();

public:
    virtual bool processRequest(HTTPRequest &request);
    bool checkAuth();
    static bool checkPortStatus(const std::string interface, const int port);
    void determineUserAndGroup(std::string &userName, std::string &userGroup);

private:
    void install(std::string &errorMsg);
    bool createXinetdConfig(std::string serviceName, int servicePort,
                            std::string serviceInterface, std::string args);

private:
    std::string password;
    HTTPRequest *request;
    Language *lang;
    int step;
    bool installPOP3, installIMAP, installSMTP, installSendmailWrapper, installSubmission;
    std::string pop3Interface, imapInterface, smtpInterface, submissionInterface;
    int pop3Port, imapPort, smtpPort, submissionPort;
    std::string mysqlHost, mysqlUser, mysqlPass, mysqlDB, mysqlSock;
    std::string dataFolder, b1gMailFolder, userName, userGroup;

    SetupServer(const SetupServer &);
    SetupServer &operator=(const SetupServer &);
};

#endif
