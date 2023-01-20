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

#ifndef _CORE_PROCESS_H_
#define _CORE_PROCESS_H_

#include <stdio.h>
#include <string>

namespace Core
{
    class Process
    {
    public:
        Process(const std::string &commandLine);
        ~Process();

    public:
        static void Init();
        static void UnInit();
        bool Open();
        int Close(int *quitSignal = NULL);
        FILE *GetInputFP();
        FILE *GetOutputFP();
        bool IsOpen();
        bool IsRunning();
        void SetCloseTimeout(int val);

    private:
        int EndProcess(int *quitSignal = NULL);
#ifndef WIN32
        pid_t pid;
#else
        HANDLE pid;
#endif
        int closeTimeout;
        int fdIn;
        int fdOut;
        FILE *fpIn;
        FILE *fpOut;
        std::string commandLine;
        bool open;

        Process(const Process &);
        Process &operator=(const Process &);
    };
};

#endif
