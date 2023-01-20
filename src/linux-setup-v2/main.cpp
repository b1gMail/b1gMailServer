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

#include <iostream>
#include <fstream>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/stat.h>

#include "SetupServer.h"
#include "Language.h"
#include "UI.h"
#include "Utils.h"
#include "mysql.h"

#define _(x)        (lang == NULL ? "NULL" : lang->lang[x])

using namespace std;

UI ui("Setup - b1gMailServer " VER_STR " (" BMS_BUILD_ARCH ")");
Language *lang = NULL;

bool doUpdate()
{
    bool queueWasRunning = false;

    //
    // shut down queue process, if running
    //
#ifndef __APPLE__
    ifstream pidFile("/opt/b1gmailserver/queue/pid");
    if(pidFile)
    {
        pid_t queuePID;
        pidFile >> queuePID;
        pidFile.close();

        // running?
        if(kill(queuePID, 0) == 0)
        {
            queueWasRunning = true;

            ui.Gauge(_("update"), _("quittingQueue"), 0);

            // send SIGINT
            kill(queuePID, SIGINT);

            // wait for process to quit
            int seconds = 0;
            while(kill(queuePID, 0) == 0)
            {
                seconds++;
                if(seconds > 20)
                    break;
                sleep(1);
                ui.Gauge(_("update"), _("quittingQueue"), seconds*4);
            }

            // still running? => SIGTERM
            if(kill(queuePID, 0) == 0)
            {
                kill(queuePID, SIGTERM);
                sleep(1);
            }

            ui.Gauge(_("update"), _("quittingQueue"), 100);

            // still running? => fail
            if(kill(queuePID, 0) == 0)
            {
                ui.MessageBox(_("error"), _("quittingQueueFail"));
                return(false);
            }
        }
    }
#else
    system("launchctl unload /Library/LaunchDaemons/de.b1g.b1gmailserver.queue.plist >/dev/null 2>/dev/null");
    queueWasRunning = true;
#endif

    //
    // shut down b1gMailServer connections
    //
    ui.Gauge(_("update"), _("quittingConnections"), 0);
    system("killall b1gmailserver >/dev/null 2>/dev/null");
    sleep(1);
    ui.Gauge(_("update"), _("quittingConnections"), 40);
    sleep(1);
    ui.Gauge(_("update"), _("quittingConnections"), 80);
    usleep(500000);
    system("killall -9 b1gmailserver >/dev/null 2>/dev/null");
    ui.Gauge(_("update"), _("quittingConnections"), 100);

    //
    // update files
    //
    ui.MessageBox(_("update"), _("updating"), 0);

    // install new main binary
    if(unlink("/opt/b1gmailserver/bin/b1gmailserver") != 0
     || !Utils::installFile("bin/b1gmailserver", "/opt/b1gmailserver/bin/b1gmailserver", 0755))
    {
        ui.MessageBox(_("error"), _("fileInUse"));
        return(false);
    }

    // install plugins
    mkdir("/opt/b1gmailserver/plugins", 0755);
#ifndef __APPLE__
    unlink("/opt/b1gmailserver/plugins/CLIQueueMgr.so");
    unlink("/opt/b1gmailserver/plugins/SendmailIface.so");
    Utils::installFile("plugins/CLIQueueMgr.so", "/opt/b1gmailserver/plugins/CLIQueueMgr.so", 0755);
#else
    unlink("/opt/b1gmailserver/plugins/CLIQueueMgr.dylib");
    unlink("/opt/b1gmailserver/plugins/SendmailIface.dylib");
    Utils::installFile("plugins/CLIQueueMgr.dylib", "/opt/b1gmailserver/plugins/CLIQueueMgr.dylib", 0755);
#endif
    chmod("/opt/b1gmailserver/plugins", 0755);

    // install sendmail wrapper
    Utils::installFile("bin/bms-sendmail", "/usr/sbin/bms-sendmail", 0755);
    if(!Utils::fileExists("/etc/bms-sendmail.conf"))
        Utils::installFile("conf/bms-sendmail.conf", "/etc/bms-sendmail.conf", 0644);
    if(!Utils::fileExists("/usr/sbin/sendmail") && !Utils::fileExists("/usr/bin/sendmail"))
        system("ln -s /usr/sbin/bms-sendmail /usr/sbin/sendmail >/dev/null 2>/dev/null");

    // install libraries
    Utils::installLibs();

    // install updated init script
#ifndef __APPLE__
    unlink("/etc/init.d/bms-queue");
    Utils::installFile("init/bms-queue", "/etc/init.d/bms-queue", 0755);
#else
    Utils::installFile("init/de.b1g.b1gmailserver.queue.plist", "/Library/LaunchDaemons/de.b1g.b1gmailserver.queue.plist", 0644);
#endif

    // install other files
    Utils::installFile("OPENSSL_LICENSE", "/opt/b1gmailserver/OPENSSL_LICENSE", 0644);
    Utils::installFile("MARIADB_LICENSE", "/opt/b1gmailserver/MARIADB_LICENSE", 0644);

    // set config permissions
    //system("chown root /opt/b1gmailserver/b1gmailserver.cfg >/dev/null 2>/dev/null");
    //chmod("/opt/b1gmailserver/b1gmailserver.cfg", 0600);

    // write version file
    ofstream versionFile("/opt/b1gmailserver/version", ios::trunc|ios::out);
    if(versionFile)
    {
        versionFile << VER_STR;
        versionFile.close();
    }

    // install alterMIME
    Utils::installAlterMIME();

    //
    // start queue process
    //
    ui.MessageBox(_("update"), _("startingQueue"), 0);
    if(queueWasRunning)
    {
#ifndef __APPLE__
        system("/etc/init.d/bms-queue start >/dev/null 2>/dev/null");
#else
        system("launchctl load /Library/LaunchDaemons/de.b1g.b1gmailserver.queue.plist >/dev/null 2>/dev/null");
#endif
    }

    return(true);
}

int update()
{
    char buff[512];

    if(!Utils::fileExists("/opt/b1gmailserver/version"))
    {
        ui.MessageBox(_("error"), _("nonUpdateableVersion"));
        return(1);
    }
    else
    {
        string installedVersion;
        ifstream versionFile("/opt/b1gmailserver/version");
        versionFile >> installedVersion;
        versionFile.close();

        int vMajor = 0, vMinor = 0 , vBuild = 0;
        if(sscanf(installedVersion.c_str(), "%d.%d.%d", &vMajor, &vMinor, &vBuild) == 3
         && vMajor == 2)
        {
            if(vBuild == atoi(BMS_BUILD))
            {
                ui.MessageBox(_("error"), _("alreadyInstalled"));
                return(1);
            }
            else if(vBuild > atoi(BMS_BUILD))
            {
                ui.MessageBox(_("error"), _("newerVersionInstalled"));
                return(1);
            }
            else if(vMinor < 5)
            {
                ui.MessageBox(_("error"), _("versionTooOld"));
                return(1);
            }
            else
            {
                snprintf(buff, 512, _("updateQuestion").c_str(), vMajor, vMinor, vBuild, VER_STR);
                if(ui.Question(_("update"), buff) != 0)
                    return(0);

                ui.MessageBox(_("update"), _("pleaseWait"), 0);

                if(doUpdate())
                {
                    ui.MessageBox(_("update"), _("updateDone"));
                    return(0);
                }
                else
                {
                    ui.MessageBox(_("update"), _("updateFailed"));
                    return(1);
                }
            }
        }
        else
        {
            ui.MessageBox(_("error"), _("unknownVersion"));
            return(1);
        }
    }
}

int install()
{
    char buff[1024];

    int httpPort = 8089;
    string httpPass = Utils::generatePassword(8), httpAddress = Utils::getIP();

    while(!SetupServer::checkPortStatus("0.0.0.0", httpPort))
        httpPort++;

    snprintf(buff, 1024, _("installerLaunchedText").c_str(),
         '\n', '\n',
         httpAddress.c_str(), httpPort,
         '\n', '\n',
         '\n', '\n', '\n',
         "admin", '\n',
         httpPass.c_str(),
         '\n', '\n',
         httpPort, '\n', '\n', '\n');

    ui.MessageBox(_("setup"), buff, 0);

    SetupServer setupSrv(httpPort, httpPass, lang);
    setupSrv.run();

    return(0);
}

int main(int argc, char **argv)
{
    srand((unsigned int)time(NULL));

    // root check
    if(getuid() != 0)
    {
        ui.MessageBox("Error / Fehler",
                     "This program has to be started as \"root\" user.\n\n"
                        "Dieses Programm muss als \"root\" ausgefuehrt werden.");
        return(1);
    }

    MySQL_DB::LibraryInit();

    // language
    const char *languageOptions[] = { "de", "Deutsch", "en", "English" };
    char *selectedLanguage = NULL;
    int res = ui.Menu("Language / Sprache",
                     "Please select your language.\n\nBitte waehlen Sie Ihre Sprache aus.",
                     2,
                     2,
                     languageOptions,
                     &selectedLanguage);

    if(res != 0)
        return(0);
    lang = new Language(selectedLanguage);
    ui.SetLabels(_("yes"), _("no"), _("cancel"));

    // check for existing versions
    int result = 1;
    if(Utils::fileExists("/opt/b1gmailserver"))
    {
        result = update();
    }
    else
    {
        result = install();
    }

    MySQL_DB::LibraryEnd();

    return(result);
}
