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

#ifndef _PLUGINMGR_H
#define _PLUGINMGR_H

#include <core/core.h>
#include <plugin/plugin.h>

#define PLUGIN_FUNCTION(function)                   for(unsigned int __i=0; __i<PluginMgr->Plugins.size(); __i++) { if(!PluginMgr->Plugins.at(__i).Active) continue; PluginMgr->Plugins.at(__i).Instance->function(); }
#define FOR_EACH_PLUGIN(varname)                    for(unsigned int __i=0; __i<PluginMgr->Plugins.size(); __i++) { if(!PluginMgr->Plugins.at(__i).Active) continue; b1gMailServer::Plugin *varname = PluginMgr->Plugins.at(__i).Instance;
#define END_FOR_EACH()                              }

namespace Core
{
    class PluginEntry
    {
    public:
        b1gMailServer::Plugin *Instance;
        b1gMailServer::GetPluginInterfaceVersion_t *VersionFunction;
        b1gMailServer::CreatePluginInstance_t *CreateFunction;
        b1gMailServer::DestroyPluginInstance_t *DestroyFunction;
        bool Active;
#ifdef WIN32
        HMODULE LibHandle;
#else
        void *LibHandle;
#endif
    };

    class PluginDBEntry
    {
    public:
        string FileName;
        string Name;
        string Title;
        string Version;
        string Author;
        string AuthorWebsite;
        string UpdateURL;
        bool Active;
        bool Update;
    };

    class PluginManager
    {
    public:
        PluginManager();
        ~PluginManager();

    public:
        void Init();
        bool LoadPlugin(string PluginPath, string FileName);
        void ShowPluginList();

    private:
        void GetPluginDB();
        void SyncPluginDB();

    public:
        vector<PluginEntry> Plugins;
        vector<PluginDBEntry> PluginDB;
    };
};

extern Core::PluginManager *PluginMgr;

#endif
