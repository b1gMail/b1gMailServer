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

#include <plugin/pluginmgr.h>

#ifndef WIN32
#include <sys/dir.h>
#include <dirent.h>
#include <dlfcn.h>
#endif

using namespace Core;

PluginManager *PluginMgr;

/*
 * Constructor
 */
PluginManager::PluginManager()
{
}

/*
 * Destructor
 */
PluginManager::~PluginManager()
{
    for(unsigned int i=0; i<this->Plugins.size(); i++)
    {
        this->Plugins.at(i).DestroyFunction(this->Plugins.at(i).Instance);
#ifdef WIN32
        FreeLibrary(this->Plugins.at(i).LibHandle);
#else
        dlclose(this->Plugins.at(i).LibHandle);
#endif
    }

    this->Plugins.clear();
}

/*
 * Load all plugins from plugins folder
 */
void PluginManager::Init()
{
    string PluginPath = utils->GetPluginPath();

    db->Log(CMP_CORE, PRIO_DEBUG, utils->PrintF("Plugin path: %s", PluginPath.c_str()));

    if(!utils->FileExists((char *)PluginPath.c_str()))
        return;

    this->GetPluginDB();

#ifndef WIN32
    DIR *PluginDir = opendir(PluginPath.c_str());
    if(PluginDir == NULL)
        return;

    struct dirent *ent;
    while((ent = readdir(PluginDir)) != NULL)
    {
        if(*ent->d_name == '.')
            continue;

        this->LoadPlugin(PluginPath, ent->d_name);
    }

    closedir(PluginDir);
#else
    HANDLE          hList;
    WIN32_FIND_DATA FileData;

    string SearchPath = PluginPath;
    SearchPath += "\\*.dll";

    hList = FindFirstFile(SearchPath.c_str(), &FileData);
    if(hList != INVALID_HANDLE_VALUE)
    {
        bool bFinished = false;

        while(true)
        {
            if((FileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                this->LoadPlugin(PluginPath, FileData.cFileName);

            if(!FindNextFile(hList, &FileData))
                if(GetLastError() == ERROR_NO_MORE_FILES)
                    break;
        }

        FindClose(hList);
    }
#endif

    this->SyncPluginDB();
}

/*
 * Load a specified plugin
 */
bool PluginManager::LoadPlugin(string PluginPath, string FileName)
{
    string PluginFileName = PluginPath;
    PluginFileName += "/";
    PluginFileName += FileName;

    if(!utils->FileExists((char *)PluginFileName.c_str()))
        return(false);

    PluginEntry entry;

#ifdef WIN32
    entry.LibHandle = LoadLibrary(PluginFileName.c_str());
    if(entry.LibHandle == NULL)
    {
        db->Log(CMP_CORE, PRIO_NOTE, utils->PrintF("Cannot load plugin %s: LoadLibrary() failed",
                                                   FileName.c_str()));
        return(false);
    }

    entry.VersionFunction = (b1gMailServer::GetPluginInterfaceVersion_t *)GetProcAddress(entry.LibHandle, "GetPluginInterfaceVersion");
    if(entry.VersionFunction == NULL)
    {
        db->Log(CMP_CORE, PRIO_NOTE, utils->PrintF("Cannot load plugin %s: GetProcAddress failed for GetPluginInterfaceVersion (error %d)",
                                                   FileName.c_str(),
                                                   GetLastError()));
        FreeLibrary(entry.LibHandle);
        return(false);
    }

    entry.CreateFunction = (b1gMailServer::CreatePluginInstance_t *)GetProcAddress(entry.LibHandle, "CreatePluginInstance");
    if(entry.CreateFunction == NULL)
    {
        db->Log(CMP_CORE, PRIO_NOTE, utils->PrintF("Cannot load plugin %s: GetProcAddress failed for CreatePluginInstance (error %d)",
                                                   FileName.c_str(),
                                                   GetLastError()));
        FreeLibrary(entry.LibHandle);
        return(false);
    }

    entry.DestroyFunction = (b1gMailServer::DestroyPluginInstance_t *)GetProcAddress(entry.LibHandle, "DestroyPluginInstance");
    if(entry.DestroyFunction == NULL)
    {
        db->Log(CMP_CORE, PRIO_NOTE, utils->PrintF("Cannot load plugin %s: GetProcAddress failed for DestroyPluginInstance (error %d)",
                                                   FileName.c_str(),
                                                   GetLastError()));
        FreeLibrary(entry.LibHandle);
        return(false);
    }
#else
    const char *DLError;

    entry.LibHandle = dlopen(PluginFileName.c_str(), RTLD_LAZY);
    if(entry.LibHandle == NULL)
    {
        db->Log(CMP_CORE, PRIO_NOTE, utils->PrintF("Cannot load plugin %s: dlopen() failed",
                                                   FileName.c_str()));
        return(false);
    }

    entry.VersionFunction = (b1gMailServer::GetPluginInterfaceVersion_t *)dlsym(entry.LibHandle, "GetPluginInterfaceVersion");
    if((DLError = dlerror()))
    {
        db->Log(CMP_CORE, PRIO_NOTE, utils->PrintF("Cannot load plugin %s: %s",
                                                   FileName.c_str(),
                                                   DLError));
        dlclose(entry.LibHandle);
        return(false);
    }

    entry.CreateFunction = (b1gMailServer::CreatePluginInstance_t *)dlsym(entry.LibHandle, "CreatePluginInstance");
    if(dlerror())
    {
        db->Log(CMP_CORE, PRIO_NOTE, utils->PrintF("Cannot load plugin %s: %s",
                                                   FileName.c_str(),
                                                   DLError));
        dlclose(entry.LibHandle);
        return(false);
    }

    entry.DestroyFunction = (b1gMailServer::DestroyPluginInstance_t *)dlsym(entry.LibHandle, "DestroyPluginInstance");
    if(dlerror())
    {
        db->Log(CMP_CORE, PRIO_NOTE, utils->PrintF("Cannot load plugin %s: %s",
                                                   FileName.c_str(),
                                                   DLError));
        dlclose(entry.LibHandle);
        return(false);
    }
#endif

    if(entry.VersionFunction() != BMS_PLUGIN_INTERFACE_VERSION)
    {
        db->Log(CMP_CORE, PRIO_NOTE, utils->PrintF("Cannot load plugin %s: Incompatible plugin interface version (plugin version: %d, required version: %d)",
                                                   FileName.c_str(),
                                                   entry.VersionFunction(),
                                                   BMS_PLUGIN_INTERFACE_VERSION));
#ifdef WIN32
        FreeLibrary(entry.LibHandle);
#else
        dlclose(entry.LibHandle);
#endif
        return(false);
    }

    entry.Instance = entry.CreateFunction();
    if(entry.Instance == NULL)
    {
        db->Log(CMP_CORE, PRIO_NOTE, utils->PrintF("Cannot load plugin %s: CreatePluginInstance() failed",
                                                   FileName.c_str()));
#ifdef WIN32
        FreeLibrary(entry.LibHandle);
#else
        dlclose(entry.LibHandle);
#endif
        return(false);
    }

    db->Log(CMP_CORE, PRIO_DEBUG, utils->PrintF("Plugin %s (%s) loaded successfully (interface version: %d)",
                                                FileName.c_str(),
                                                entry.Instance->Name.c_str(),
                                                entry.VersionFunction()));

    entry.Instance->BMSUtils = utils;
    entry.Instance->SetFileName(FileName.c_str());

    // in db?
    int DBID = -1;
    for(unsigned int i=0; i<this->PluginDB.size(); i++)
        if(this->PluginDB.at(i).FileName.compare(FileName) == 0)
        {
            DBID = (int)i;
            break;
        }

    // add to db
    if(DBID == -1)
    {
        PluginDBEntry DBEntry;
        DBEntry.FileName        = FileName.c_str();
        DBEntry.Name            = entry.Instance->Name;
        DBEntry.Title           = entry.Instance->Title;
        DBEntry.Version         = entry.Instance->Version;
        DBEntry.Author          = entry.Instance->Author;
        DBEntry.AuthorWebsite   = entry.Instance->AuthorWebsite;
        DBEntry.UpdateURL       = entry.Instance->UpdateURL;
        DBEntry.Active          = true;
        DBEntry.Update          = true;
        this->PluginDB.push_back(DBEntry);

        entry.Active            = true;
    }

    // exists in DB
    else
    {
        PluginDBEntry *DBEntry = &this->PluginDB.at((unsigned int)DBID);

        if(DBEntry->Name.compare(entry.Instance->Name) != 0
           || DBEntry->Title.compare(entry.Instance->Title) != 0
           || DBEntry->Version.compare(entry.Instance->Version) != 0
           || DBEntry->Author.compare(entry.Instance->Author) != 0
           || DBEntry->AuthorWebsite.compare(entry.Instance->AuthorWebsite) != 0
           || DBEntry->UpdateURL.compare(entry.Instance->UpdateURL) != 0)
        {
            DBEntry->Name           = entry.Instance->Name;
            DBEntry->Title          = entry.Instance->Title;
            DBEntry->Version        = entry.Instance->Version;
            DBEntry->Author         = entry.Instance->Author;
            DBEntry->AuthorWebsite  = entry.Instance->AuthorWebsite;
            DBEntry->UpdateURL      = entry.Instance->UpdateURL;
            DBEntry->Update         = true;
        }

        entry.Active            = DBEntry->Active;
    }

    this->Plugins.push_back(entry);

    return(true);
}

/*
 * Get plugin entries from DB
 */
void PluginManager::GetPluginDB()
{
    MySQL_Result *res;
    MYSQL_ROW row;

    // get entries
    res = db->Query("SELECT `filename`,`name`,`title`,`version`,`author`,`author_website`,`update_url`,`active` FROM bm60_bms_mods");
    while((row = res->FetchRow()))
    {
        PluginDBEntry entry;
        entry.FileName          = row[0];
        entry.Name              = row[1];
        entry.Title             = row[2];
        entry.Version           = row[3];
        entry.Author            = row[4];
        entry.AuthorWebsite     = row[5];
        entry.UpdateURL         = row[6];
        entry.Active            = *row[7] == '1';
        entry.Update            = false;
        this->PluginDB.push_back(entry);
    }
    delete res;
}

/*
 * Sync plugin DB
 */
void PluginManager::SyncPluginDB()
{
    string PluginPath = utils->GetPluginPath();

    for(unsigned int i=0; i<this->PluginDB.size(); i++)
    {
        string PluginFileName = PluginPath;
        PluginFileName += "/";
        PluginFileName += this->PluginDB.at(i).FileName;

        if(utils->FileExists((char *)PluginFileName.c_str()))
        {
            if(!this->PluginDB.at(i).Update)
                continue;

            db->Query("REPLACE INTO bm60_bms_mods(`filename`,`name`,`title`,`version`,`author`,`author_website`,`update_url`,`active`) VALUES('%q','%q','%q','%q','%q','%q','%q',%d)",
                      this->PluginDB.at(i).FileName.c_str(),
                      this->PluginDB.at(i).Name.c_str(),
                      this->PluginDB.at(i).Title.c_str(),
                      this->PluginDB.at(i).Version.c_str(),
                      this->PluginDB.at(i).Author.c_str(),
                      this->PluginDB.at(i).AuthorWebsite.c_str(),
                      this->PluginDB.at(i).UpdateURL.c_str(),
                      this->PluginDB.at(i).Active ? 1 : 0);
        }
        else
        {
            db->Query("DELETE FROM bm60_bms_mods WHERE `filename`='%q'",
                      this->PluginDB.at(i).FileName.c_str());
        }
    }
}

/*
 * Show plugin list
 */
void PluginManager::ShowPluginList()
{
    int PluginCount = 0;

    for(unsigned int i=0; i<this->Plugins.size(); i++)
        if(this->Plugins.at(i).Active)
            PluginCount++;

    printf("%d plugin(s) loaded:\n\n",
           PluginCount);

    FOR_EACH_PLUGIN(Plugin)
    {
        printf(" +- %s\n",
               Plugin->Name.c_str());
        printf("     +- Title:   %s\n",
               Plugin->Title.c_str());
        printf("     +- Version: %s\n",
               Plugin->Version.c_str());
        printf("     +- Author:  %s (%s)\n",
               Plugin->Author.c_str(),
               Plugin->AuthorWebsite.c_str());
        printf("     \\- File:    %s\n",
               Plugin->FileName.c_str());
        printf("\n");
    }
    END_FOR_EACH()
}
