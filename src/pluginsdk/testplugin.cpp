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

// Plugin class implementation
class TestPlugin : public b1gMailServer::Plugin
{
public:
    TestPlugin()
    {
        this->Name          = "TestPlugin";
        this->Title         = "b1gMailServer Test Plugin";
        this->Version       = "1.0.0";
        this->Author        = "B1G Software";
        this->AuthorWebsite = "http://www.b1g.de";
    }

    ~TestPlugin()
    {
    }

    void Init()
    {
        printf("TestPlugin: My file name is %s\n",
               this->FileName.c_str());
        printf("TestPlugin: Init()\n");
    }

    void UnInit()
    {
        printf("TestPlugin: UnInit()\n");
    }

    void OnLoginUser(int UserID, std::string EMail, std::string Password)
    {
        printf("TestPlugin: OnLoginUser(%d, %s, %s)\n", UserID, EMail.c_str(), Password.c_str());
    }

    void OnLogoutUser()
    {
        printf("TestPlugin: OnLogoutUser()\n");
    }

    FILE *OnOpenMailBody(FILE *RawMessage)
    {
        FILE *fp = tmpfile();
        fprintf(fp, "This is TESTFUNCTION!!!11\n");
        fseek(fp, 0, SEEK_SET);
        return(fp);
    }

    void OnDisplayModeHelp()
    {
        printf("   --test-plugin     Test plugin\n");
    }

    bool OnCallWithoutValidMode(int argc, char **argv, int &ResultCode)
    {
        bool Continue = false;

        for(int i=0; i<argc; i++)
            if(strcmp(argv[i], "--test-plugin") == 0)
            {
                Continue = true;
                break;
            }

        if(!Continue)
            return(false);

        printf("TestPlugin: b1gMailServer called with --test-plugin\n");
        return(true);
    }
};

// Export plugin class "TestPlugin" to b1gMailServer
EXPORT_BMS_PLUGIN(TestPlugin);
