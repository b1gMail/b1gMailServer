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
#include "Language.h"
#include "Template.h"
#include "Utils.h"
#include "mysql.h"
#include "resdata.h"
#include <iostream>
#include <sstream>
#include <stdio.h>
#include <arpa/inet.h>
#include <stdexcept>
#include <sys/types.h>
#include <sys/dir.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>

using namespace std;

SetupServer::SetupServer(int port, string password, Language *lang) : HTTPServer(port)
{
    this->lang              = lang;
    this->step              = 1;
    this->installIMAP       = false;
    this->installPOP3       = false;
    this->installSMTP       = false;
    this->installSubmission = false;
    this->installSendmailWrapper = false;
    this->pop3Port          = 0;
    this->imapPort          = 0;
    this->smtpPort          = 0;
    this->submissionPort    = 0;

    this->password = password;
}

SetupServer::~SetupServer()
{
}

bool SetupServer::processRequest(HTTPRequest &req)
{
    this->request = &req;

    if(!this->checkAuth())
        return(true);

    // resource
    if(request->requestURI.length() > 5 && request->requestURI.substr(0, 5) == "/res/")
    {
        int length = 0;
        const char *resData = NULL, *contentType = "application/octet-stream";

        size_t lastDotPos = request->requestURI.rfind('.');
        if(lastDotPos != string::npos)
        {
            string fileExt = Utils::strToLower(request->requestURI.substr(lastDotPos));

            if(fileExt == ".gif")
                contentType = "image/gif";
            else if(fileExt == ".jpg")
                contentType = "image/jpeg";
            else if(fileExt == ".png")
                contentType = "image/png";
            else if(fileExt == ".js")
                contentType = "text/javascript";
            else if(fileExt == ".css")
                contentType = "text/css";

            resData = getResource(request->requestURI.substr(5).c_str(), &length);
        }

        if(resData == NULL)
        {
            request->errorPage(ERROR_PAGE_NOT_FOUND, "Requested resource not found.");
        }
        else
        {
            request->sendHeaders(200, "OK", length, contentType, "Expires: Thu, 01 Dec 2020 16:00:00 GMT\r\n");
            fwrite(resData, length, 1, request->fp);
        }
    }

    // setup routine
    else if(request->requestURI == "/")
    {
        string errorMsg = "";

        if(request->requestMethod == REQUEST_POST)
        {
            if(request->postFields["step"].length() > 0)
            {
                step = atoi(request->postFields["step"].c_str());

                if(step == 0)
                {
                    step++;
                }
                else if(step == 1)
                {
                    step += 2;
                }
                // NOTE Step 2 used to be the license key input, which has been removed
                else if(step == 3)
                {
                    this->installPOP3 = request->postFields["pop3"] == "1";
                    this->installIMAP = request->postFields["imap"] == "1";
                    this->installSMTP = request->postFields["smtp"] == "1";
                    this->installSubmission = request->postFields["submission"] == "1";
                    this->installSendmailWrapper = request->postFields["sendmailWrapper"] == "1";
                    this->pop3Port = atoi(request->postFields["pop3Port"].c_str());
                    this->imapPort = atoi(request->postFields["imapPort"].c_str());
                    this->smtpPort = atoi(request->postFields["smtpPort"].c_str());
                    this->submissionPort = atoi(request->postFields["submissionPort"].c_str());
                    this->pop3Interface = request->postFields["pop3Address"];
                    this->imapInterface = request->postFields["imapAddress"];
                    this->smtpInterface = request->postFields["smtpAddress"];
                    this->submissionInterface = request->postFields["submissionAddress"];

                    if(!this->installPOP3 && !this->installIMAP && !this->installSMTP && !this->installSubmission)
                    {
                        errorMsg = lang->lang["noServicesError"];
                    }
                    else
                    {
                        if(this->installPOP3 && !this->checkPortStatus(this->pop3Interface, this->pop3Port))
                            errorMsg = lang->lang["portError"];
                        else if(this->installIMAP && !this->checkPortStatus(this->imapInterface, this->imapPort))
                            errorMsg = lang->lang["portError"];
                        else if(this->installSMTP && !this->checkPortStatus(this->smtpInterface, this->smtpPort))
                            errorMsg = lang->lang["portError"];
                        else if(this->installSubmission && !this->checkPortStatus(this->submissionInterface, this->submissionPort))
                            errorMsg = lang->lang["portError"];
                        else
                        {
                            // everything fine!
                            step++;
                        }
                    }
                }
                else if(step == 4)
                {
                    this->mysqlHost = Utils::trim(request->postFields["mysql_host"]);
                    this->mysqlUser = Utils::trim(request->postFields["mysql_user"]);
                    this->mysqlPass = Utils::trim(request->postFields["mysql_pass"]);
                    this->mysqlDB   = Utils::trim(request->postFields["mysql_db"]);
                    this->mysqlSock = Utils::trim(request->postFields["mysql_sock"]);

                    bool connectionOK = false;
                    MySQL_DB *db;

                    if(this->mysqlDB != "")
                    {
                        try
                        {
                            db = new MySQL_DB(this->mysqlHost.c_str(),
                                                 this->mysqlUser.c_str(),
                                                 this->mysqlPass.c_str(),
                                                 this->mysqlDB.c_str(),
                                                 this->mysqlSock != "" ? this->mysqlSock.c_str() : NULL);
                            connectionOK = true;
                        }
                        catch(const runtime_error &ex)
                        {
                            connectionOK = false;
                            errorMsg = lang->lang["mysqlError"] + string("<br /><div class=\"errorMsg\">") + ex.what() + string("</div>");
                        }
                    }
                    else
                    {
                        connectionOK = false;
                        errorMsg = lang->lang["mysqlError"] + string("<br /><div class=\"errorMsg\">MySQL error: No database selected</div>");
                    }

                    if(connectionOK)
                    {
                        bool b1gMailFound = false, adminPluginFound = false;

                        MySQL_Result *res = db->Query("SHOW TABLES");
                        MYSQL_ROW row;
                        while((row = res->FetchRow()))
                        {
                            if(strcmp(row[0], "bm60_prefs") == 0)
                                b1gMailFound = true;
                            else if(strcmp(row[0], "bm60_bms_prefs") == 0)
                                adminPluginFound = true;
                        }
                        delete res;

                        if(!b1gMailFound)
                        {
                            errorMsg = lang->lang["mysqlNob1gMailError"];
                        }
                        else if(!adminPluginFound)
                        {
                            errorMsg = lang->lang["mysqlNoAdminPluginError"];
                        }
                        else
                        {
                            try
                            {
                                res = db->Query("SELECT datafolder,selffolder FROM bm60_prefs LIMIT 1");
                                while((row = res->FetchRow()))
                                {
                                    this->dataFolder = row[0];
                                    this->b1gMailFolder = row[1];

                                    if(!Utils::fileExists(this->dataFolder))
                                    {
                                        errorMsg = lang->lang["dataFolderError"] + string("<br /><div class=\"errorMsg\">") + dataFolder + string("</div>");
                                    }
                                    else if(!Utils::fileExists(this->b1gMailFolder + string("interface/pipe.php")))
                                    {
                                        errorMsg = lang->lang["b1gMailFolderError"] + string("<br /><div class=\"errorMsg\">") + b1gMailFolder + string("</div>");
                                    }
                                }
                                delete res;
                            }
                            catch(const runtime_error &ex)
                            {
                                errorMsg = lang->lang["prefsTableError"] + string("<br /><div class=\"errorMsg\">") + ex.what() + string("</div>");
                            }

                            if(errorMsg == "")
                                step++;
                        }

                        delete db;
                    }
                }
                else if(step == 5)
                {
                    string newUserName = request->postFields["userName"],
                            newUserGroup = request->postFields["userGroup"];

                    if(getpwnam(newUserName.c_str()) == NULL)
                    {
                        errorMsg = lang->lang["userNameError"];
                    }
                    else if(getgrnam(newUserGroup.c_str()) == NULL)
                    {
                        errorMsg = lang->lang["userGroupError"];
                    }
                    else
                    {
                        this->userName = newUserName;
                        this->userGroup = newUserGroup;
                        step++;
                    }
                }
                else if(step == 6)
                {
                    step++;
                }
                else if(step == 7)
                {
                    this->install(errorMsg);
                    step++;
                }
            }
            else
            {
                step = 0;
            }
        }

        Template tpl(this->lang);

        // error message
        if(errorMsg.length() > 0)
        {
            tpl.assign("page", "error.html");
            tpl.assign("errorMsg", errorMsg);
        }

        // language
        else if(step == 0)
        {
            tpl.assign("page", "step0.html");
        }

        // welcome
        else if(step == 1)
        {
#ifndef __APPLE__
            if(!Utils::fileExists("/etc/xinetd.d") && !Utils::fileExists("/etc/xinetd.conf"))
            {
                tpl.assign("page", "error.html");
                tpl.assign("errorMsg", lang->lang["xinetdError"]);
            }
            else
#endif
            {
                tpl.assign("page", "step1.html");
            }
        }

        // ports
        else if(step == 3)
        {
            tpl.assign("imapText", "");
            tpl.assign("imapImg", "<img src=\"/res/load_16.gif\" id=\"imapPortStatus\" border=\"0\" alt=\"\" />");
            tpl.assign("imapDisabled", "");
            tpl.assign("imapChecked", " checked=\"checked\"");
            tpl.assign("page", "step3.html");
        }

        // mysql
        else if(step == 4)
        {
            tpl.assign("page", "step4.html");
        }

        // user/group
        else if(step == 5)
        {
            string userName, userGroup;
            this->determineUserAndGroup(userName, userGroup);

            tpl.assign("userName",  userName);
            tpl.assign("userGroup", userGroup);
            tpl.assign("page",      "step5.html");
        }

        // summary
        else if(step == 6)
        {
            stringstream sstr;
            if(this->installSMTP)
            {
                sstr << " - SMTP (<code>" << this->smtpInterface << ":" << this->smtpPort << "</code>)";

                if(this->smtpPort != 25)
                {
                    sstr << " <img src=\"/res/warning.png\" border=\"0\" alt=\"\" align=\"absmiddle\" /> ";
                    sstr << lang->lang["nonStandardPort"];
                    sstr << " <img src=\"/res/warning.png\" border=\"0\" alt=\"\" align=\"absmiddle\" /> ";
                    sstr << lang->lang["noMXUse"];
                }

                sstr << "<br />" << endl;
            }
            if(this->installSubmission)
            {
                sstr << " - Submission (<code>" << this->submissionInterface << ":" << this->submissionPort << "</code>)";

                if(this->submissionPort != 587)
                {
                    sstr << " <img src=\"/res/warning.png\" border=\"0\" alt=\"\" align=\"absmiddle\" /> ";
                    sstr << lang->lang["nonStandardPort"];
                }

                sstr << "<br />" << endl;
            }
            if(this->installPOP3)
            {
                sstr << " - POP3 (<code>" << this->pop3Interface << ":" << this->pop3Port << "</code>)";

                if(this->pop3Port != 110)
                {
                    sstr << " <img src=\"/res/warning.png\" border=\"0\" alt=\"\" align=\"absmiddle\" /> ";
                    sstr << lang->lang["nonStandardPort"];
                }

                sstr << "<br />" << endl;
            }
            if(this->installIMAP)
            {
                sstr << " - IMAP (<code>" << this->imapInterface << ":" << this->imapPort << "</code>)";

                if(this->imapPort != 143)
                {
                    sstr << " <img src=\"/res/warning.png\" border=\"0\" alt=\"\" align=\"absmiddle\" /> ";
                    sstr << lang->lang["nonStandardPort"];
                }

                sstr << "<br />" << endl;
            }
            if(this->installSendmailWrapper)
                sstr << " - Sendmail-Wrapper<br />" << endl;

            string warnings;
            if(!this->installSMTP || this->smtpPort != 25)
            {
                warnings = string("<p><img src=\"/res/warning.png\" border=\"0\" alt=\"\" align=\"absmiddle\" /> ")
                        + lang->lang["noMXUseExt"]
                        + string("</p>");
            }

            tpl.assign("warnings",
                     warnings);
            tpl.assign("components",
                     sstr.str());
            tpl.assign("mySQL",
                     lang->lang["database"] + string(" <code>") + this->mysqlDB + string("</code> @ <code>") + this->mysqlHost + string("</code>"));
            tpl.assign("userAndGroup",
                     this->userName + string(", ") + this->userGroup);

            tpl.assign("page",      "step6.html");
        }

        // installing
        else if(step == 7)
        {
            tpl.assign("page",      "step7.html");
        }

        // done
        else if(step == 8)
        {
            tpl.assign("page",      "step8.html");
        }

        string page = tpl.fetch("index.html");
        request->sendHeaders(200, "OK", page.length(), "text/html; charset=ISO-8859-15", "Cache-Control: no-cache\r\nPragma: no-cache\r\n");
        fwrite(page.c_str(), page.length(), 1, request->fp);

        if(step == 8)
        {
            sleep(1);
            return(false);
        }
    }

    // AJAX rpc
    else if(request->requestURI == "/rpc" && request->requestMethod == REQUEST_POST)
    {
        string action = request->postFields["action"], response = "";

        if(action == "checkPortStatus")
        {
            response  = request->postFields["svc"];
            response += ":";
            response += this->checkPortStatus(request->postFields["interface"],
                                             atoi(request->postFields["port"].c_str()))
                            ? "1"
                            : "0";
        }

        if(action.length() > 0)
        {
            request->sendHeaders(200, "OK", response.length(), "text/plain", "Cache-Control: no-cache\r\nPragma: no-cache\r\n");
            fwrite(response.c_str(), response.length(), 1, request->fp);
        }
        else
            request->errorPage(ERROR_PAGE_NOT_FOUND, "Invalid RPC action");
    }

    // not found
    else
    {
        request->errorPage(ERROR_PAGE_NOT_FOUND, "Requested page not found.");
    }

    return(true);
}

void SetupServer::determineUserAndGroup(string &userName, string &userGroup)
{
    userName    = "";
    userGroup   = "";

    string fileName = this->dataFolder;

    DIR *d = opendir(this->dataFolder.c_str());
    if(d != NULL)
    {
        struct dirent *entry;
        while((entry = readdir(d)) != NULL)
        {
            string entryName = entry->d_name;
            if(entryName.length() == 0 || entryName.at(0) == '.')
                continue;

            if(Utils::strToLower(entryName).find(".dsk") != string::npos
             || Utils::strToLower(entryName).find(".msg") != string::npos)
            {
                fileName = this->dataFolder + entryName;
            }
        }
        closedir(d);
    }

    struct stat st;
    if(stat(fileName.c_str(), &st) == 0)
    {
        struct passwd *pwdEnt;
        struct group *grpEnt;

        if((pwdEnt = getpwuid(st.st_uid)) != NULL
         && (grpEnt = getgrgid(st.st_gid)) != NULL)
        {
            userName    = pwdEnt->pw_name;
            userGroup   = grpEnt->gr_name;
        }
    }
}

bool SetupServer::checkPortStatus(const string interface, const int port)
{
    if(port <= 0)
        return(false);

    bool result = false;

    int sock = socket(AF_INET, SOCK_STREAM, 0), optVal = 1;

    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(optVal));

    struct sockaddr_in inAddr;
    memset(&inAddr, 0, sizeof(inAddr));
    inAddr.sin_port = htons(port);
    inAddr.sin_addr.s_addr = (interface == "0.0.0.0" ? INADDR_ANY : inet_addr(interface.c_str()));
    inAddr.sin_family = AF_INET;

    result = bind(sock, (struct sockaddr *)&inAddr, sizeof(inAddr)) >= 0;

    close(sock);

    return(result);
}

bool SetupServer::checkAuth()
{
    bool result = false;

    string authString = request->headerFields["authorization"];

    if(authString.length() > 6)
    {
        if(strcasecmp(authString.substr(0, 6).c_str(), "basic ") == 0)
        {
            string givenAuthToken       = authString.substr(6),
                    correctAuthToken    = Utils::base64Encode("admin:" + this->password);

            if(givenAuthToken == correctAuthToken)
                result = true;
        }
    }

    if(!result)
    {
        string errorPage = "<h1>Authentication failed / Authentifizierung fehlgeschlagen</h1>";
        errorPage += "<p>Please log in with the username and password which was shown at the console "
                        "after launching the b1gMailServer setup program.</p>"
                        "<p>Bitte loggen Sie sich mit dem Benutzernamen/Passwort ein, dass "
                        "beim Start des b1gMailServer-Setups auf der Konsole angezeigt wurde.</p>";

        request->sendHeaders(ERROR_UNAUTHORIZED, "Unauthorized", errorPage.length(),
                         "text/html",
                         "WWW-Authenticate: Basic realm=\"b1gMailServer-Setup\"\r\n");
        fprintf(request->fp, "%s", errorPage.c_str());
    }

    return(result);
}
