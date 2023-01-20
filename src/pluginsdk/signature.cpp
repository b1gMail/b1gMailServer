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

using namespace std;

// Plugin class implementation
class SignaturePlugin : public b1gMailServer::Plugin
{
private:
    int _userID;
    bool _installed;

public:
    SignaturePlugin()
    {
        this->Name          = "SignaturePlugin";
        this->Title         = "Signature plugin";
        this->Version       = "1.4";
        this->Author        = "Patrick Schlangen";
        this->AuthorWebsite = "http://my.b1gmail.com/details/54/";
        this->UpdateURL     = "http://service.b1gmail.com/plugin_updates/";

        this->_installed    = false;
    }

    virtual ~SignaturePlugin()
    {
    }

public:
    void Init();
    void UnInit();
    bool GetUserMailSignature(int UserID, const char *Separator, FILE *fpSignature);
};

// Export plugin class "SignaturePlugin" to b1gMailServer
EXPORT_BMS_PLUGIN(SignaturePlugin);

//
// plugin callback implementation
//

void SignaturePlugin::Init()
{
    b1gMailServer::MySQL_DB *db = this->BMSUtils->GetMySQLConnection();
    b1gMailServer::MySQL_Result *res;
    char **row;

    // check if plugin is installed
    res = db->Query("SHOW TABLES");
    while((row = res->FetchRow()))
    {
        if(strcmp(row[0], "bm60_mod_signatures") == 0)
        {
            this->_installed = true;
            break;
        }
    }
    delete res;

    if(!this->_installed)
        return;
}

void SignaturePlugin::UnInit()
{
}

bool SignaturePlugin::GetUserMailSignature(int UserID, const char *Separator, FILE *fpSignature)
{
    if(!this->_installed)
        return(false);

    b1gMailServer::MySQL_DB *db = this->BMSUtils->GetMySQLConnection();
    b1gMailServer::MySQL_Result *res;
    char **row;
    bool result = false;
    int groupID = 0;

    // get group ID
    res = db->Query("SELECT `gruppe` FROM bm60_users WHERE `id`=%d",
            UserID);
    while((row = res->FetchRow()))
    {
        groupID = atoi(row[0]);
    }
    delete res;

    // get signature
    res = db->Query("SELECT `signatureid`,`text` FROM bm60_mod_signatures WHERE "
            "`paused`=0 AND `html`=0 "
            "AND (`groups`='*' OR (`groups`='%d' OR `groups` LIKE '%%,%d,%%' OR `groups` LIKE '%%,%d' OR `groups` LIKE '%d,%%')) "
            "ORDER BY (counter/weight) ASC LIMIT 1",
            groupID, groupID, groupID, groupID);
    while((row = res->FetchRow()))
    {
        fprintf(fpSignature, "\n%s\n%s\n", Separator, row[1]);
        result = true;

        // update stats
        db->Query("UPDATE bm60_mod_signatures SET `counter`=`counter`+1 WHERE `signatureid`=%d",
                atoi(row[0]));
    }
    delete res;

    return(result);
}
