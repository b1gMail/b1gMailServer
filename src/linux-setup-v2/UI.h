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

#ifndef _UI_H_
#define _UI_H_

#include <string>

class UI
{
public:
    UI(std::string backTitle);
    ~UI();

public:
    int MessageBox(std::string title, std::string message,
                    int pause = 1,
                    int width = 0, int height = 0);
    int Question(std::string title, std::string message,
                    int width = 0, int height = 0);
    int Menu(std::string title, std::string message,
                    int menuHeight, int optionCount, const char **options,
                    char **selectedOption,
                    int width = 0, int height = 0);
    int Gauge(std::string title, std::string message, int percent,
                    int width = 0, int height = 0);
    void SetLabels(std::string yes, std::string no, std::string cancel);

private:
    std::string backTitle;
};

#endif

