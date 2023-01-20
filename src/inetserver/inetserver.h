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

#ifndef _INETSERVER_H
#define _INETSERVER_H

#include <stdio.h>
#include <winsock2.h>
#include <windows.h>
#include <process.h>

#include "resource.h"
#include "../build.h"

#define BMS_VERSION         "2.8." BMS_BUILD
#define PREFS_REG_KEY       "SOFTWARE\\B1G Software\\b1gMailServer"
#define LISTEN_BACKLOG      128
#define MAX_CONNECTIONS     100

#define MODE_UNKNOWN        0
#define MODE_POP3           1
#define MODE_IMAP           2
#define MODE_SMTP           3
#define MODE_HTTP           4
#define MODE_MSGQUEUE       5

#define PRIO_NOTE           1
#define PRIO_WARNING        2
#define PRIO_ERROR          4
#define PRIO_DEBUG          8

#endif
