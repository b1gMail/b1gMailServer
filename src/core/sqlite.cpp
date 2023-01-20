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

#include <core/sqlite.h>

using namespace Core;

SQLite::SQLite(const std::string &_fileName) : busyTimeoutMS(15000), fileName(_fileName), db(NULL)
{
}

SQLite::~SQLite()
{
    close();
}

void SQLite::close()
{
    if (db == NULL)
        return;

    sqlite3_close(db);
    db = NULL;
}

bool SQLite::open()
{
    int rc = sqlite3_open(fileName.c_str(), &db);
    if (rc != 0)
    {
        close();
        return false;
    }
    busyTimeout(busyTimeoutMS);
    return true;
}

bool SQLite::busyTimeout(int ms)
{
    busyTimeoutMS = ms;

    if (db != NULL)
        return sqlite3_busy_timeout(db, ms) == 0;

    return true;
}

SQLiteStmt *SQLite::prepare(const string &q)
{
    sqlite3_stmt *stmt;

    int rc = sqlite3_prepare_v2(db, q.c_str(), static_cast<int>(q.length()), &stmt, NULL);
    if (rc != 0)
        throw Core::Exception("SQLite", sqlite3_errmsg(db));

    return new SQLiteStmt(db, stmt);
}

SQLiteResult *SQLite::query(const string &q)
{
    SQLiteStmt *stmt = prepare(q);
    SQLiteResult *res = stmt->execute();
    delete stmt;

    return res;
}

SQLiteResult::SQLiteResult(sqlite3 *_db, sqlite3_stmt *_stmt) : db(_db), stmt(_stmt), firstFetch(true), done(false)
{
    /*int columnCount = sqlite3_column_count(_stmt);
    for (int i = 0; i < columnCount; ++i)
        columns[i] = string(sqlite3_column_name(_stmt, i));*/
}

SQLiteResult::~SQLiteResult()
{
    sqlite3_finalize(stmt);
}

SQLiteRow SQLiteResult::fetchRow()
{
    SQLiteRow result;

    if (done)
        return result;

    if (firstFetch)
    {
        firstFetch = false;
    }
    else
    {
        int rc = sqlite3_step(stmt);

        switch (rc)
        {
        case SQLITE_BUSY:
            throw Core::Exception("SQLite", "fetchRow failed due to busy timeout");

        case SQLITE_DONE:
            done = true;
            return result;

        case SQLITE_ROW:
            break;

        default:
            throw Core::Exception("SQLite", sqlite3_errmsg(db));
        }
    }

    for (int i = 0; i < sqlite3_column_count(stmt); ++i)
    {
        switch (sqlite3_column_type(stmt, i))
        {
        case SQLITE_BLOB:
            result[i] = string(reinterpret_cast<const char *>(sqlite3_column_blob(stmt, i)), sqlite3_column_bytes(stmt, i));
            break;

        default:
            result[i] = string(reinterpret_cast<const char *>(sqlite3_column_text(stmt, i)), sqlite3_column_bytes(stmt, i));
            break;
        }
    }

    return result;
}

SQLiteStmt::SQLiteStmt(sqlite3 *_db, sqlite3_stmt *_stmt) : db(_db), stmt(_stmt), ownStmt(true)
{

}

SQLiteStmt::~SQLiteStmt()
{
    if (ownStmt)
        sqlite3_finalize(stmt);
}

void SQLiteStmt::bindValue(const std::string &variable, const std::string &text)
{
    int index = sqlite3_bind_parameter_index(stmt, variable.c_str());
    if (index == 0)
        throw Core::Exception("SQLite", "Unknown variable name");

    if (sqlite3_bind_text(stmt, index, text.c_str(), static_cast<int>(text.size()), SQLITE_TRANSIENT) != 0)
        throw Core::Exception("SQLite", "Failed to bind text");
}

void SQLiteStmt::bindValue(const std::string &variable, const char *blob, int blobSize)
{
    int index = sqlite3_bind_parameter_index(stmt, variable.c_str());
    if (index == 0)
        throw Core::Exception("SQLite", "Unknown variable name");

    if (sqlite3_bind_blob(stmt, index, blob, blobSize, SQLITE_TRANSIENT) != 0)
        throw Core::Exception("SQLite", "Failed to bind blob");
}

void SQLiteStmt::bindValue(const std::string &variable, const int value)
{
    int index = sqlite3_bind_parameter_index(stmt, variable.c_str());
    if (index == 0)
        throw Core::Exception("SQLite", "Unknown variable name");

    if (sqlite3_bind_int(stmt, index, value) != 0)
        throw Core::Exception("SQLite", "Failed to bind integer");
}

void SQLiteStmt::bindValue(const std::string &variable, const double value)
{
    int index = sqlite3_bind_parameter_index(stmt, variable.c_str());
    if (index == 0)
        throw Core::Exception("SQLite", "Unknown variable name");

    if (sqlite3_bind_double(stmt, index, value) != 0)
        throw Core::Exception("SQLite", "Failed to bind double");
}

SQLiteResult *SQLiteStmt::execute()
{
    int rc = sqlite3_step(stmt);
    switch (rc)
    {
    case SQLITE_BUSY:
        throw Core::Exception("SQLite", "Query failed due to busy timeout");

    case SQLITE_DONE:
        // everything fine, but no result
        return NULL;

    case SQLITE_ROW:
        ownStmt = false;
        return new SQLiteResult(db, stmt);

    default:
        throw Core::Exception("SQLite", sqlite3_errmsg(db));
    }
}
