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

#ifndef _CORE_CORE_H_
#define _CORE_CORE_H_

#ifdef WIN32
#   if defined(_MSC_VER)
#       ifndef _CRT_SECURE_NO_DEPRECATE
#           define _CRT_SECURE_NO_DEPRECATE (1)
#       endif
#   pragma warning(disable : 4996)
#   endif
#   include <winsock2.h>
#endif

#include <stdio.h>
#include <time.h>

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include <openssl/ssl.h>
#include <signal.h>
#include <stdarg.h>

#ifndef SSL_OP_TLSEXT_PADDING
#define SSL_OP_TLSEXT_PADDING 0x00000010L
#endif

// Custom I/O functions
int my_vprintf(const char *str, va_list list);
int my_printf(const char *str, ...);
int my_scanf(const char *str, ...);
size_t my_fwrite(const void *buf, size_t s1, size_t s2, FILE *fp);
char *my_fgets(char *buf, int s, FILE *fp);
size_t my_fread(void *buf, size_t s1, size_t s2, FILE *fp);
#define fwrite          my_fwrite
#define fgets           my_fgets
#define printf          my_printf
#define fread           my_fread

extern bool bIO_SSL;
extern SSL *ssl;

#ifdef WIN32
#define PATH_SEP        '\\'
#define _WINSOCKAPI_
#include <fcntl.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <conio.h>
#include <io.h>
#include <direct.h>
#define getpid                          GetCurrentThreadId
#define strcasecmp                      stricmp
#define strncasecmp                     strnicmp
#define socklen_t                       int
#define popen                           _popen
#define pclose                          _pclose
#define fdopen                          _fdopen
#define usleep(x)                       Sleep(x/1000)
#define mkdir(x,y)                      _mkdir(x)
#define chmod                           _chmod
#define SIGALRM                         512
#define inet_pton                       InetPton
#define inet_ntop                       InetNtop
#define in_addr_t                       unsigned long

#ifndef SIGINT
#define SIGINT              2
#endif

#ifndef SIGKILL
#define SIGKILL             9
#endif

#ifndef SIGTERM
#define SIGTERM             15
#endif

unsigned int alarm(unsigned int seconds);
void SetAlarmSignalCallback(void (*func)(int));
void InterruptFGets();
char *strcasestr(const char *s1, const char *s2);
char *strtok_r(char *str, const char *delim, char **saveptr);
#else
#define SOCKET          int
#define PATH_SEP        '/'
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/wait.h>
#endif

#define PCRE_STATIC
#include <pcre.h>
void CompilePatterns();
void FreePatterns();
extern pcre *pMailPattern, *pIPPattern, *pHeaderPattern, *pMsgIdPattern;
extern size_t iTrafficIn, iTrafficOut;
extern FILE *fpBMSLog;

using namespace std;

#include <stdarg.h>
#include <core/exception.h>
#include <core/utils.h>
#include <core/config.h>
#include <core/mysql.h>
#include <core/socket.h>
#include <plugin/pluginmgr.h>
#include <build.h>

#define TRY()               try {
#define END_TRY()           } catch(Core::Exception e) { e.Output(); exit(1); }
#define END_TRY_MAIN()      } catch(Core::Exception e) { e.Output(); exit(1); }
#define END_TRY_THROW()     } catch(Core::Exception e) { throw(e); }
#define mmalloc(x)          utils->SafeMalloc(x)
#define mstrdup(x)          utils->SafeStrDup(x)

using namespace Core;

#endif
