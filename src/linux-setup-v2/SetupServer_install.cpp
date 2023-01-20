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

#include "SetupServer.h"
#include "Utils.h"
#include "mysql.h"
#include <string>
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <sys/stat.h>
#include <string.h>

using namespace std;

void SetupServer::install(string &errorMsg)
{
    if(!this->installPOP3 && !this->installIMAP && !this->installSMTP && !this->installSubmission)
    {
        errorMsg = "No components selected for installation.";
        return;
    }

    MySQL_DB *db = NULL;

    //
    // connect to MySQL DB, set ports
    //
    try
    {
        db = new MySQL_DB(this->mysqlHost.c_str(),
                            this->mysqlUser.c_str(),
                            this->mysqlPass.c_str(),
                            this->mysqlDB.c_str(),
                            this->mysqlSock != "" ? this->mysqlSock.c_str() : NULL);

        db->Query("UPDATE bm60_bms_prefs SET `user_pop3port`=%d,`user_imapport`=%d,`user_smtpport`=%d",
                  this->pop3Port,
                  this->imapPort,
                  this->installSubmission ? this->submissionPort : this->smtpPort);

        delete db;
    }
    catch(runtime_error &ex)
    {
        errorMsg = lang->lang["mysqlError"] + string("<br /><pre>") + ex.what() + string("</pre>");
        return;
    }

    //
    // create directories
    //
    mkdir("/opt", 0755);
    mkdir("/opt/b1gmailserver", 0755);
    mkdir("/opt/b1gmailserver/bin", 0755);
    mkdir("/opt/b1gmailserver/plugins", 0755);
    mkdir("/opt/b1gmailserver/tls", 0755);
    mkdir("/opt/b1gmailserver/setup-backup", 0755);
    mkdir("/opt/b1gmailserver/queue", 0755);
    mkdir("/opt/b1gmailserver/queue/0", 0755);
    mkdir("/opt/b1gmailserver/queue/1", 0755);
    mkdir("/opt/b1gmailserver/queue/2", 0755);
    mkdir("/opt/b1gmailserver/queue/3", 0755);
    mkdir("/opt/b1gmailserver/queue/4", 0755);
    mkdir("/opt/b1gmailserver/queue/5", 0755);
    mkdir("/opt/b1gmailserver/queue/6", 0755);
    mkdir("/opt/b1gmailserver/queue/7", 0755);
    mkdir("/opt/b1gmailserver/queue/8", 0755);
    mkdir("/opt/b1gmailserver/queue/9", 0755);
    mkdir("/opt/b1gmailserver/queue/A", 0755);
    mkdir("/opt/b1gmailserver/queue/B", 0755);
    mkdir("/opt/b1gmailserver/queue/C", 0755);
    mkdir("/opt/b1gmailserver/queue/D", 0755);
    mkdir("/opt/b1gmailserver/queue/E", 0755);
    mkdir("/opt/b1gmailserver/queue/F", 0755);
    if(!Utils::fileExists("/opt/b1gmailserver/bin"))
    {
        errorMsg = "Failed to create install directory.";
        delete db;
        return;
    }

    //
    // back up config files
    //
    Utils::installFile("/etc/services", "/opt/b1gmailserver/setup-backup/etc-services", 0644);
#ifndef __APPLE__
    Utils::installFile("/etc/xinetd.conf", "/opt/b1gmailserver/setup-backup/etc-xinetd.conf", 0644);
#endif

    //
    // install files
    //
    bool copySuccess = Utils::installFile("bin/b1gmailserver", "/opt/b1gmailserver/bin/b1gmailserver", 0755)
#ifndef __APPLE__
                        && Utils::installFile("bin/bms-queue", "/opt/b1gmailserver/bin/bms-queue", 0755)
                        && Utils::installFile("init/bms-queue", "/etc/init.d/bms-queue", 0755)
                        && Utils::installFile("plugins/CLIQueueMgr.so", "/opt/b1gmailserver/plugins/CLIQueueMgr.so", 0755)
#else
                        && Utils::installFile("init/de.b1g.b1gmailserver.queue.plist", "/Library/LaunchDaemons/de.b1g.b1gmailserver.queue.plist", 0644)
                        && Utils::installFile("plugins/CLIQueueMgr.dylib", "/opt/b1gmailserver/plugins/CLIQueueMgr.dylib", 0755)
#endif
                        && Utils::installFile("OPENSSL_LICENSE", "/opt/b1gmailserver/OPENSSL_LICENSE", 0644)
                        && Utils::installFile("MARIADB_LICENSE", "/opt/b1gmailserver/MARIADB_LICENSE", 0644);
    if(!copySuccess)
    {
        errorMsg = "Failed to install b1gMailServer files.";
        return;
    }
    if(this->installSendmailWrapper)
    {
        Utils::installFile("bin/bms-sendmail", "/usr/sbin/bms-sendmail", 0755);
        if(!Utils::fileExists("/usr/sbin/sendmail") && !Utils::fileExists("/usr/bin/sendmail"))
            system("ln -s /usr/sbin/bms-sendmail /usr/sbin/sendmail >/dev/null 2>/dev/null");

        //
        // sendmail config generation
        //
        ofstream smConfigFile("/etc/bms-sendmail.conf", ios::trunc|ios::out);
        if(smConfigFile)
        {
            smConfigFile << "# b1gMailServer sendmail wrapper" << endl
                    << "# Configuration file" << endl
                    << "#" << endl
                    << "# The b1gMailServer sendmail wrapper accepts emails on stdin and" << endl
                    << "# forwards them to an SMTP server (e.g. b1gMailServer's SMTP server" << endl
                    << "# running at the same machine)" << endl
                    << "#" << endl
                    << "# This configuration file has been generated automatically by" << endl
                    << "# the b1gMailServer setup program" << endl
                    << endl;

            smConfigFile << "# relay SMTP server hostname" << endl
                    << "smtp_host = " << ((this->smtpInterface.empty() || this->smtpInterface == "0.0.0.0")
                                            ? "localhost"
                                            : this->smtpInterface) << endl
                    << endl;

            smConfigFile << "# relay SMTP server port" << endl
                    << "smtp_port = " << (this->smtpPort == 0 ? 25 : this->smtpPort) << endl
                    << endl;

            smConfigFile << "# SMTP authentication (1 = enable, 0 = disable)" << endl
                    << "smtp_auth = 0" << endl
                    << endl;

            smConfigFile << "# username for SMTP authentication" << endl
                    << "#smtp_user =" << endl
                    << endl;

            smConfigFile << "# password for SMTP authentication" << endl
                    << "#smtp_pass =" << endl
                    << endl;

            smConfigFile << "# optional address for socket binding in SMTP client connections" << endl
                    << "# (useful in case multiple IPs are hosted on this server)" << endl
                    << "#client_addr = 10.0.0.1" << endl
                    << endl;

            smConfigFile << "# optional hostname for use in SMTP HELO/EHLO command" << endl
                    << "#hostname = host.example.org" << endl
                    << endl;

            smConfigFile.close();
        }
    }

    //
    // install libs
    //
    Utils::installLibs();

    //
    // write version file
    //
    ofstream versionFile("/opt/b1gmailserver/version", ios::trunc|ios::out);
    if(versionFile)
    {
        versionFile << VER_STR;
        versionFile.close();
    }

    //
    // create config file
    //
    ofstream configFile("/opt/b1gmailserver/b1gmailserver.cfg", ios::trunc|ios::out);
    if(configFile)
    {
        configFile << "# b1gMailServer configuration file" << endl;
        configFile << "mysql_host      = " << this->mysqlHost << endl;
        configFile << "mysql_user      = " << this->mysqlUser << endl;
        configFile << "mysql_pass      = " << this->mysqlPass << endl;
        configFile << "mysql_db        = " << this->mysqlDB << endl;

        if(this->mysqlSock.size() > 0)
            configFile << "mysql_sock      = " << this->mysqlSock << endl;

        configFile << "user            = " << this->userName << endl;
        configFile << "group           = " << this->userGroup << endl;

        configFile.close();
    }

    //
    // append services
    //
    fstream servicesFile("/etc/services", ios::ate|ios::out|ios::in);
    if(servicesFile)
    {
        servicesFile << endl;
        servicesFile << "#" << endl;
        servicesFile << "# b1gMailServer" << endl;
        servicesFile << "#" << endl;

        if(this->installSMTP)
            servicesFile << "bms-smtp       " << this->smtpPort << "/tcp" << endl;
        if(this->installSubmission)
            servicesFile << "bms-submission " << this->submissionPort << "/tcp" << endl;
        if(this->installPOP3)
            servicesFile << "bms-pop3       " << this->pop3Port << "/tcp" << endl;
        if(this->installIMAP)
            servicesFile << "bms-imap       " << this->imapPort << "/tcp" << endl;

        servicesFile.close();
    }

    //
    // create xinetd config
    //
    if(this->installSMTP)
    {
        this->createXinetdConfig("bms-smtp",
                                 this->smtpPort,
                                 this->smtpInterface,
                                 "--smtp");
    }
    if(this->installSubmission)
    {
        this->createXinetdConfig("bms-submission",
                                 this->submissionPort,
                                 this->submissionInterface,
                                 "--smtp --submission");
    }
    if(this->installPOP3)
    {
        this->createXinetdConfig("bms-pop3",
                                 this->pop3Port,
                                 this->pop3Interface,
                                 "--pop3");
    }
    if(this->installIMAP)
    {
        this->createXinetdConfig("bms-imap",
                                 this->imapPort,
                                 this->imapInterface,
                                 "--imap");
    }

    //
    // set config permissions
    //
    system("chown root /opt/b1gmailserver/b1gmailserver.cfg >/dev/null 2>/dev/null");
    chmod("/opt/b1gmailserver/b1gmailserver.cfg", 0600);

    //
    // set queue permissions
    //
    string queueCHOwnCommand = string("chown -R ") + this->userName + string(":") + this->userGroup + string(" /opt/b1gmailserver/queue");
    system(queueCHOwnCommand.c_str());

    //
    // install alterMIME
    //
    Utils::installAlterMIME();

#ifndef __APPLE__
    //
    // add service to runlevel
    //

    if(Utils::fileExists("/sbin/chkconfig"))
    {
        system("/sbin/chkconfig --add bms-queue >/dev/null 2>/dev/null");
    }
    else if(Utils::fileExists("/usr/sbin/update-rc.d"))
    {
        system("/usr/sbin/update-rc.d bms-queue start 99 3 4 5 . >/dev/null 2>/dev/null");
    }
    else if(Utils::fileExists("/etc/rc3.d"))
    {
        system("ln -s /etc/init.d/bms-queue /etc/rc3.d/S99bms-queue >/dev/null 2>/dev/null");
        system("ln -s /etc/init.d/bms-queue /etc/rc3.d/K01bms-queue >/dev/null 2>/dev/null");
    }
    else if(Utils::fileExists("/etc/rc.d/rc3.d"))
    {
        system("ln -s /etc/init.d/bms-queue /etc/rc.d/rc3.d/S99bms-queue >/dev/null 2>/dev/null");
        system("ln -s /etc/init.d/bms-queue /etc/rc.d/rc3.d/K01bms-queue >/dev/null 2>/dev/null");
    }
#endif

    //
    // start bms-queue
    //
    system("/etc/init.d/bms-queue start >/dev/null 2>/dev/null");

    //
    // restart xinetd
    //
#ifndef __APPLE__
    system("/etc/init.d/xinetd restart >/dev/null 2>/dev/null");
#else
    system("/bin/launchctl load /Library/LaunchDaemons/de.b1g.b1gmailserver.* >/dev/null 2>/dev/null");
#endif
}

bool SetupServer::createXinetdConfig(string serviceName, int servicePort, string serviceInterface, string args)
{
#ifndef __APPLE__
    bool result = false;
    fstream *s = NULL;

    if(Utils::fileExists("/etc/xinetd.d"))
    {
        s = new fstream(string(string("/etc/xinetd.d/") + serviceName).c_str(),
                        ios::trunc|ios::out|ios::in);
    }
    else if(Utils::fileExists("/etc/xinetd.conf"))
    {
        s = new fstream("/etc/xinetd.conf",
                        ios::ate|ios::out|ios::in);
    }

    if(s == NULL)
        return(false);

    fstream &str = *s;

    if(str)
    {
        str << endl << endl;
        str << "# b1gMailServer " << serviceName << endl;
        str << "service " << serviceName << endl;
        str << "{" << endl;
        str << "    disable = no" << endl;
        str << "    port = " << servicePort << endl;
        str << "    socket_type = stream" << endl;
        str << "    protocol = tcp" << endl;
        str << "    wait = no" << endl;
        str << "    user = root" << endl;
        str << "    server = /opt/b1gmailserver/bin/b1gmailserver" << endl;
        str << "    server_args = " << args << endl;
        if(serviceInterface != "0.0.0.0")
            str << "    bind = " << serviceInterface << endl;
        str << "}" << endl;

        result = true;
    }

    s->close();
    delete s;

    return(result);
#else
    bool result = false;
    fstream *s = new fstream(string(string("/Library/LaunchDaemons/de.b1g.b1gmailserver.") + serviceName + string(".plist")).c_str(),
        ios::trunc|ios::out|ios::in);

    if(s == NULL)
        return(false);

    fstream &str = *s;

    if(str)
    {
        str << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << endl;
        str << "<!DOCTYPE plist PUBLIC \"-//Apple Computer//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">" << endl;
        str << "<plist version=\"1.0\">" << endl;
        str << "<dict>" << endl;
        str << "    <key>Label</key>" << endl;
        str << "    <string>de.b1g.b1gmailserver." << serviceName << "</string>" << endl;
        str << "    <key>OnDemand</key><false />" << endl;
        str << "    <key>Program</key>" << endl;
        str << "    <string>/opt/b1gmailserver/bin/b1gmailserver</string>" << endl;
        str << "    <key>ProgramArguments</key>" << endl;
        str << "    <array>" << endl;
        str << "        <string>/opt/b1gmailserver/bin/b1gmailserver</string>" << endl;

        char *tmp = strdup(args.c_str());
        if(tmp != NULL)
        {
            char *pch = strtok(tmp, " ");
            while(pch != NULL)
            {
                str << "        <string>" << pch << "</string>" << endl;
                pch = strtok(NULL, " ");
            }
            free(temp);
        }

        str << "    </array>" << endl;
        str << "    <key>Sockets</key>" << endl;
        str << "    <dict>" << endl;
        str << "        <key>Listeners</key>" << endl;
        str << "        <dict>" << endl;
        if(serviceInterface != "0.0.0.0")
        {
            str << "            <key>SockNodeName</key>" << endl;
            str << "            <string>" << serviceInterface << "</string>" << endl;
        }
        str << "            <key>SockServiceName</key>" << endl;
        str << "            <string>" << serviceName << "</string>" << endl;
        str << "        </dict>" << endl;
        str << "    </dict>" << endl;
        str << "    <key>inetdCompatibility</key>" << endl;
        str << "    <dict>" << endl;
        str << "        <key>Wait</key><false />" << endl;
        str << "    </dict>" << endl;
        str << "    <key>WorkingDirectory</key>" << endl;
        str << "    <string>/opt/b1gmailserver/bin</string>" << endl;
        str << "</dict>" << endl;
        str << "</plist>" << endl;

        result = true;
    }

    s->close();
    delete s;

    return(result);
#endif
}
