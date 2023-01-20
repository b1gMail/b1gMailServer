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

#ifndef _CORE_SQLITE_H_
#define _CORE_SQLITE_H_

#define SQLITE_ASSOC    0
#define SQLITE_NUM      1
#define SQLITE_BOTH     2

#include <sqlite3/sqlite3.h>
#include <core/core.h>

#include <string>
#include <vector>
#include <map>

namespace Core
{
    class SQLiteRow : public std::map<int, std::string>
    {
    public:
        operator bool() const { return !empty(); }
    };

    class SQLiteResult
    {
    public:
        ~SQLiteResult();

    public:
        SQLiteRow fetchRow();

    private:
        void step(bool move);

    private:
        SQLiteResult(sqlite3 *_db, sqlite3_stmt *_stmt);
        SQLiteResult(const SQLiteResult &);
        SQLiteResult &operator=(const SQLiteResult &);

        friend class SQLiteStmt;

        sqlite3 *db;
        sqlite3_stmt *stmt;
        bool firstFetch;
        bool done;
    };

    class SQLiteStmt
    {
    public:
        ~SQLiteStmt();

    public:
        void bindValue(const std::string &variable, const std::string &text);
        void bindValue(const std::string &variable, const char *blob, int blobSize);
        void bindValue(const std::string &variable, const int value);
        void bindValue(const std::string &variable, const double value);
        SQLiteResult *execute();

    private:
        SQLiteStmt(sqlite3 *_db, sqlite3_stmt *_stmt);
        SQLiteStmt(const SQLiteStmt &);
        SQLiteStmt &operator=(const SQLiteStmt &);

        friend class SQLite;

        sqlite3 *db;
        sqlite3_stmt *stmt;
        bool ownStmt;
    };

    class SQLite
    {
    public:
        SQLite(const std::string &_fileName);
        ~SQLite();

    public:
        bool open();
        bool isOpen() const { return db != NULL; }
        void close();
        bool busyTimeout(int ms);
        SQLiteResult *query(const std::string &q);
        SQLiteStmt *prepare(const std::string &q);
        std::string escapeString(const std::string &str);
        long long lastInsertRowID();

    private:
        SQLite(const SQLite &);
        SQLite &operator=(const SQLite &);

        int busyTimeoutMS;
        std::string fileName;
        sqlite3 *db;
    };
};

#endif
