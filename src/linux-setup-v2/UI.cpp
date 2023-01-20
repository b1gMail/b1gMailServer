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

#include "UI.h"
#include <dialog.h>
#include <signal.h>

using namespace std;

UI::UI(std::string backTitle)
{
    this->backTitle = backTitle;

    init_dialog(stdin, stderr);
    dialog_vars.dlg_clear_screen = true;
    dialog_vars.backtitle = (char *)this->backTitle.c_str();
    dialog_vars.keep_window = false;

    signal(SIGWINCH, SIG_IGN);
}

UI::~UI()
{
    clear();
    refresh();
    dlg_clear();
    end_dialog();
}

int UI::MessageBox(std::string title, std::string message, int pause, int width, int height)
{
    dlg_clear();
    dlg_put_backtitle();
    return dialog_msgbox(title.c_str(), message.c_str(), height, width, pause);
}

int UI::Question(std::string title, std::string message, int width, int height)
{
    dlg_clear();
    dlg_put_backtitle();
    return dialog_yesno(title.c_str(), message.c_str(), height, width);
}

int UI::Menu(std::string title, std::string message, int menuHeight, int optionCount, const char** options, char** selectedOption, int width, int height)
{
    dlg_clear();
    dlg_put_backtitle();
    int res = dialog_menu(title.c_str(), message.c_str(), height, width, menuHeight, optionCount,
                         (char **)options);
    *selectedOption = dialog_vars.input_result;
    return res;
}

int UI::Gauge(std::string title, std::string message, int percent, int width, int height)
{
    dlg_clear();
    dlg_put_backtitle();
    return dialog_mixedgauge(title.c_str(), message.c_str(), height, width, percent, 0, NULL);
}

void UI::SetLabels(std::string yes, std::string no, std::string cancel)
{
    dialog_vars.yes_label       = (char *)yes.c_str();
    dialog_vars.no_label        = (char *)no.c_str();
    dialog_vars.cancel_label    = (char *)cancel.c_str();
}
