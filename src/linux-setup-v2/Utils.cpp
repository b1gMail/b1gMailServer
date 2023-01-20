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

#include "Utils.h"
#include "md5.h"
#include <sys/stat.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#ifndef __APPLE__
#include <linux/if.h>
#endif
#include <sys/utsname.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;

unsigned char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

#ifndef __APPLE__
string Utils::getIP()
{
    string              Result = "127.0.0.1";
    struct ifreq        ifr, *IFR;
    struct ifconf       ifc;
    char                szBuffer[1024];
    int                 fd;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(fd != -1)
    {
        ifc.ifc_len = sizeof(szBuffer);
        ifc.ifc_buf = szBuffer;
        ioctl(fd, SIOCGIFCONF, &ifc);

        IFR = ifc.ifc_req;

        if(IFR != NULL)
        {
            for(int i=ifc.ifc_len/sizeof(struct ifreq); --i >= 0; IFR++)
            {
                strcpy(ifr.ifr_name, IFR->ifr_name);

                struct sockaddr_in *IFAddr = (struct sockaddr_in *)&IFR->ifr_addr;
                if(IFAddr->sin_addr.s_addr == inet_addr("127.0.0.1"))
                    continue;

                in_addr_t ifIP = IFAddr->sin_addr.s_addr;

                Result = inet_ntoa(IFAddr->sin_addr);

                if((ifIP & inet_addr("255.255.0.0")) != inet_addr("192.168.0.0")
                   && (ifIP & inet_addr("255.240.0.0")) != inet_addr("172.16.0.0")
                   && (ifIP & inet_addr("255.0.0.0")) != inet_addr("10.0.0.0")
                   && (ifIP & inet_addr("255.255.255.0")) != inet_addr("127.0.0.0"))
                {
                    break;
                }
            }
        }

        close(fd);
    }

    return(Result);
}
#else
string Utils::getIP()
{
    // TODO
    return(string("127.0.0.1"));
}
#endif

bool Utils::installAlterMIME()
{
    if(!Utils::fileExists("/opt/b1gmailserver/3rdparty"))
        mkdir("/opt/b1gmailserver/3rdparty", 0755);
    if(!Utils::fileExists("/opt/b1gmailserver/3rdparty/altermime"))
        mkdir("/opt/b1gmailserver/3rdparty/altermime", 0755);

    // get archive using CURL...
    if(Utils::fileExists("/usr/bin/curl"))
    {
        system("/usr/bin/curl " ALTERMIME_URL " > /opt/b1gmailserver/3rdparty/altermime/altermime-current.tar.gz 2>/dev/null");
    }

    // ...or using WGET
    else if(Utils::fileExists("/usr/bin/wget"))
    {
        system("/usr/bin/wget -q --output-document=/opt/b1gmailserver/3rdparty/altermime/altermime-current.tar.gz " ALTERMIME_URL " 2>/dev/null");
    }

    // download success?
    if(Utils::fileExists("/opt/b1gmailserver/3rdparty/altermime/altermime-current.tar.gz"))
    {
        // extract
        system("tar --directory=/opt/b1gmailserver/3rdparty/altermime/ -xzf /opt/b1gmailserver/3rdparty/altermime/altermime-current.tar.gz 2>/dev/null");

        // delete archive
        unlink("/opt/b1gmailserver/3rdparty/altermime/altermime-current.tar.gz");

        // extract success?
        if(Utils::fileExists("/opt/b1gmailserver/3rdparty/altermime/altermime"))
        {
            // create link
            if(!Utils::fileExists("/opt/b1gmailserver/bin/altermime"))
                system("ln -s /opt/b1gmailserver/3rdparty/altermime/altermime /opt/b1gmailserver/bin/altermime");
        }
    }

    return(Utils::fileExists("/opt/b1gmailserver/3rdparty/altermime/altermime")
            && Utils::fileExists("/opt/b1gmailserver/bin/altermime"));
}

bool Utils::installLibs()
{
    bool runLDConfig = false;

    string libDir = "/opt/b1gmailserver/libs";
    if(!Utils::fileExists(libDir))
        mkdir(libDir.c_str(), 0755);

    DIR *srcDir = opendir("libs/");
    if(srcDir == NULL)
        return(false);

    struct dirent *entry;
    while((entry = readdir(srcDir)) != NULL)
    {
        if(strcmp(entry->d_name, ".") == 0
         || strcmp(entry->d_name, "..") == 0
#ifndef __APPLE__
         || strstr(entry->d_name, ".so") == NULL
#else
         || strstr(entry->d_name, ".dylib") == NULL
#endif
         )
            continue;

        string srcFile = string("libs/") + string(entry->d_name), destFile = libDir + string("/") + string(entry->d_name);

        if(Utils::fileExists(destFile))
            unlink(destFile.c_str());

        if(!Utils::fileExists(destFile))
        {
            Utils::installFile(srcFile, destFile, 0755);
            runLDConfig = true;
        }
    }

    if(runLDConfig)
    {
#ifndef __APPLE__
        system("ldconfig -n /opt/b1gmailserver/libs >/dev/null 2>/dev/null");
#endif
    }

    return(true);
}

bool Utils::installFile(string srcFile, string destFile, mode_t perms)
{
    char buffer[4096];

    FILE *fpSrc = fopen(srcFile.c_str(), "rb");
    if(fpSrc == NULL)
        return(false);

    FILE *fpDest = fopen(destFile.c_str(), "wb");
    if(fpDest == NULL)
    {
        fclose(fpSrc);
        return(false);
    }

    bool success = true;
    while(!feof(fpSrc))
    {
        size_t readBytes = fread(buffer, 1, sizeof(buffer), fpSrc);
        if(readBytes == 0)
            break;
        if(fwrite(buffer, 1, readBytes, fpDest) != readBytes)
        {
            success = false;
            break;
        }
    }

    fclose(fpSrc);
    fclose(fpDest);

    chmod(destFile.c_str(), perms);

    return(success);
}

string Utils::generatePassword(unsigned int length)
{
    const char passwordChars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    string result = "";

    for(unsigned int i=0; i<length; i++)
        result.append(1, passwordChars[ rand() % (sizeof(passwordChars)-1) ]);

    return(result);
}

bool Utils::fileExists(string fileName)
{
    struct stat st;
    return(stat(fileName.c_str(), &st) == 0);
}

string Utils::md5(string str)
{
    md5_state_t pmt;
    md5_byte_t digest[16];

    md5_init(&pmt);
    md5_append(&pmt, (const md5_byte_t *)str.c_str(), str.length());
    md5_finish(&pmt, digest);

    string res;
    for(unsigned int i=0; i<16; i++)
    {
        char buff[3];
        snprintf(buff, 3, "%02x", digest[i]);
        res.append(buff);
    }

    return(res);
}

string Utils::quotedPrintableDecode(string str)
{
    string result;

    for(unsigned int i=0; i<str.length(); i++)
    {
        char c = str.at(i);

        if(str.length() > i+2)
        {
            char c1 = str.at(i+1), c2 = str.at(i+2);
            if(c == '%' && IS_HEX_CHAR(c1) && IS_HEX_CHAR(c2))
            {
                if(IS_DIGIT(c1))
                    c1 -= '0';
                else
                    c1 -= IS_UPPER(c1) ? 'A' - 10 : 'a' - 10;

                if(IS_DIGIT(c2))
                    c2 -= '0';
                else
                    c2 -= IS_UPPER(c2) ? 'A' - 10 : 'a' - 10;

                result.append(1, c2 + (c1<<4));
                i += 2;
            }
            else if(c == '+')
            {
                result.append(1, ' ');
            }
            else
            {
                result.append(1, c);
            }
        }
        else
        {
            result.append(1, c);
        }
    }

    return(result);
}

string Utils::base64Encode(string str)
{
    int strPos = 0, i = 0, j = 0, in_len = (int)str.length(), out_len = (int)(str.length()*2+100);
    string Result;
    unsigned char chunk[3], echunk[4];

    while(in_len-- && out_len)
    {
        chunk[i++] = str.at(strPos++);
        if(i == 3)
        {
            echunk[0] = (chunk[0] & 0xfc) >> 2;
            echunk[1] = ((chunk[0] & 0x03) << 4) + ((chunk[1] & 0xf0) >> 4);
            echunk[2] = ((chunk[1] & 0x0f) << 2) + ((chunk[2] & 0xc0) >> 6);
            echunk[3] = chunk[2] & 0x3f;

            for(i = 0; (i < 4) && out_len--; i++)
                Result.append(1, base64_table[echunk[i]]);
            i = 0;
        }
    }

    if(i)
    {
        for(j = i; j < 3; j++)
            chunk[j] = '\0';

        echunk[0] = (chunk[0] & 0xfc) >> 2;
        echunk[1] = ((chunk[0] & 0x03) << 4) + ((chunk[1] & 0xf0) >> 4);
        echunk[2] = ((chunk[1] & 0x0f) << 2) + ((chunk[2] & 0xc0) >> 6);
        echunk[3] = chunk[2] & 0x3f;

        for (j = 0; (j < i + 1) && out_len--; j++)
            Result.append(1, base64_table[echunk[j]]);

        while((i++ < 3) && out_len--)
            Result.append(1, '=');
    }

    return(Result);
}

string Utils::trim(string str, const string &drop)
{
    string r = str.erase(str.find_last_not_of(drop)+1);
    return(r.erase(0, r.find_first_not_of(drop)));
}

string Utils::strToLower(string str)
{
    string result;

    for(unsigned int i=0; i<str.length(); i++)
        result.append(1, (char)tolower(str.at(i)));

    return(result);
}
