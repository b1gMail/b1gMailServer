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

#ifndef _UTILS_H_
#define _UTILS_H_

#include <string>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <build.h>

#define VER_STR                 BMS_VER_MAJOR "." BMS_VER_MINOR "." BMS_BUILD

#define IS_HEX_CHAR(cm)         (((cm >= 'A') && (cm <= 'F')) || ((cm >= 'a') && (cm <= 'f')) || ((cm >= '0') && (cm <= '9')))
#define IS_DIGIT(cm)            ((cm >= '0') && (cm <= '9'))
#define IS_UPPER(cm)            ((cm >= 'A') && (cm <= 'F'))

#ifdef x86_64
#   ifndef __APPLE__
#       define ALTERMIME_URL            "ftp://ftp.b1g.de/b1gmailserver/altermime/altermime-current-x86_64.tar.gz"
#   else
#       define ALTERMIME_URL            "ftp://ftp.b1g.de/b1gmailserver/altermime/altermime-current-osx-x86_64.tar.gz"
#   endif
#else
#   ifndef __APPLE__
#       define ALTERMIME_URL            "ftp://ftp.b1g.de/b1gmailserver/altermime/altermime-current-i686.tar.gz"
#   else
#       define ALTERMIME_URL            "ftp://ftp.b1g.de/b1gmailserver/altermime/altermime-current-osx-i686.tar.gz"
#   endif
#endif

class Utils
{
public:
    static std::string getIP();
    static bool installAlterMIME();
    static bool installLibs();
    static bool installFile(std::string srcFile, std::string destFile, mode_t perms);
    static bool fileExists(std::string fileName);
    static std::string md5(std::string str);
    static std::string quotedPrintableDecode(std::string str);
    static std::string base64Encode(std::string str);
    static std::string trim(std::string str, const std::string &drop = "\r\n\t ");
    static std::string strToLower(std::string str);
    static std::string generatePassword(unsigned int length);
};

#endif
