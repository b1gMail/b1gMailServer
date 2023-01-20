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

#include <core/mysql.h>

using namespace Core;

MySQL_Result::MySQL_Result(MYSQL_RES *res)
{
    this->result = res;
}

MySQL_Result::~MySQL_Result()
{
    if(this->result != NULL)
        mysql_free_result(this->result);
}

char **MySQL_Result::FetchRow()
{
    return((char **)mysql_fetch_row(this->result));
}

unsigned long MySQL_Result::NumRows()
{
    return((unsigned long)mysql_num_rows(this->result));
}

unsigned long MySQL_Result::NumFields()
{
    return((unsigned long)mysql_num_fields(this->result));
}

MYSQL_FIELD *MySQL_Result::FetchFields()
{
    return(mysql_fetch_fields(this->result));
}

unsigned long *MySQL_Result::FetchLengths()
{
    return(mysql_fetch_lengths(this->result));
}
