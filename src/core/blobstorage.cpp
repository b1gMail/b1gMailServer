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

#include <core/blobstorage.h>
#include <core/sqlite.h>
#include <core/utils.h>

#include <zlib.h>

using namespace Core;

static const char *g_extensionMap[] = { "msg", "dsk" };

string BlobStorageProvider_SeparateFiles::blobPath(BlobType type, int id) const
{
    const char *ext = g_extensionMap[static_cast<int>(type)];

    char *path = utils->MailPath(id, ext, true);
    if (path == NULL)
        return "";

    string result(path);
    free(path);

    return result;
}

bool BlobStorageProvider_SeparateFiles::storeBlob(BlobType type, int id, const string &data)
{
    string path = blobPath(type, id);
    if (path.empty())
        return false;

    FILE *fpDest = fopen(path.c_str(), "wb");
    if (fpDest == NULL)
    {
        db->Log(CMP_CORE, PRIO_ERROR, utils->PrintF("Failed to create blob file %s",
            path.c_str()));
        return false;
    }

    if (fwrite(data.c_str(), 1, data.size(), fpDest) != data.size())
    {
        db->Log(CMP_CORE, PRIO_WARNING, utils->PrintF("Incomplete write to blob file %s",
            path.c_str()));
    }

    fclose(fpDest);

    chmod(path.c_str(), 0666);

    return true;
}

FILE *BlobStorageProvider_SeparateFiles::loadBlob(BlobType type, int id)
{
    string path = blobPath(type, id);
    if (path.empty())
        return NULL;

    if (!utils->FileExists(path.c_str()))
        return NULL;

    return fopen(path.c_str(), "rb");
}

bool BlobStorageProvider_SeparateFiles::deleteBlob(BlobType type, int id)
{
    string path = blobPath(type, id);
    if (path.empty())
        return false;

    if (utils->FileExists(path.c_str()))
        return unlink(path.c_str()) == 0;

    return true;
}

int BlobStorageProvider_SeparateFiles::getBlobSize(BlobType type, int id)
{
    string path = blobPath(type, id);
    if (path.empty())
        return false;

    return static_cast<int>(utils->FileSize(path.c_str()));
}

BlobStorageProvider_UserDB::~BlobStorageProvider_UserDB()
{
    if (sdb != NULL)
    {
        delete sdb;
        sdb = NULL;
    }
}

string BlobStorageProvider_UserDB::getDBFileName() const
{
    char *path = utils->MailPath(static_cast<int>(userID), "blobdb", true);
    if (path == NULL)
        return "";

    string result(path);
    free(path);

    return result;
}

bool BlobStorageProvider_UserDB::open(long long _userID)
{
    if (!BlobStorageProvider::open(_userID))
        return false;

    if (sdb != NULL)
    {
        try
        {
            delete sdb;
        }
        catch (...)
        {
            db->Log(CMP_CORE, PRIO_WARNING, utils->PrintF("Failed to close blob database"));
        }
        sdb = NULL;
    }

    string dbFileName = getDBFileName();
    if (dbFileName.empty())
        return false;

    try
    {
        sdb = new SQLite(dbFileName);

        if (!sdb->open())
            throw Exception("BlobStorageProvider_UserDB", "open() failed");

        txCounter = 0;

        sdb->query("CREATE TABLE IF NOT EXISTS [blobs] ("
            "   [type] INTEGER,"
            "   [id] INTEGER,"
            "   [flags] INTEGER,"
            "   [size] INTEGER,"
            "   [data] BLOB,"
            "   PRIMARY KEY([type],[id])"
            ")");

        return true;
    }
    catch (const Exception &ex)
    {
        db->Log(CMP_CORE, PRIO_WARNING, utils->PrintF("Failed to open blob database %s: %s",
            dbFileName.c_str(),
            ex.strError.c_str()));
    }

    return false;
}

FILE *BlobStorageProvider_UserDB::loadBlob(BlobType type, int id)
{
    if (sdb == NULL)
        return NULL;

    FILE *result = NULL;

    SQLiteStmt *stmt = NULL;
    SQLiteResult *res = NULL;
    try
    {
        stmt = sdb->prepare("SELECT [data],[flags],[size] FROM [blobs] WHERE [type]=:type AND [id]=:id");
        stmt->bindValue(":type", static_cast<int>(type));
        stmt->bindValue(":id", id);

        res = stmt->execute();

        SQLiteRow row;
        if(res != NULL && (row = res->fetchRow()))
        {
            result = tmpfile();

            int flags = atoi(row[1].c_str());
            if ((flags & BMBS_USERDB_FLAG_GZCOMPRESSED) != 0)
            {
                string data = uncompressBlob(row[0], atoi(row[2].c_str()));
                fwrite(data.c_str(), 1, data.size(), result);
            }
            else
            {
                fwrite(row[0].c_str(), 1, row[0].size(), result);
            }

            fseek(result, 0, SEEK_SET);
        }
        else
        {
            db->Log(CMP_CORE, PRIO_WARNING, utils->PrintF("Blob %d of type %d for user %d not found",
                id, static_cast<int>(type), static_cast<int>(userID)));
        }
    }
    catch (const Exception &ex)
    {
        db->Log(CMP_CORE, PRIO_ERROR, utils->PrintF("Failed to load blob %d of type %d for user %d from database: %s",
            id, static_cast<int>(type), static_cast<int>(userID), ex.strError.c_str()));
    }

    delete res;
    delete stmt;

    return result;
}

int BlobStorageProvider_UserDB::getBlobSize(BlobType type, int id)
{
    if (sdb == NULL)
        return 0;

    int result = 0;

    SQLiteStmt *stmt = NULL;
    SQLiteResult *res = NULL;
    try
    {
        stmt = sdb->prepare("SELECT [size] FROM [blobs] WHERE [type]=:type AND [id]=:id");
        stmt->bindValue(":type", static_cast<int>(type));
        stmt->bindValue(":id", id);

        res = stmt->execute();

        SQLiteRow row;
        if(res != NULL && (row = res->fetchRow()))
        {
            sscanf(row[0].c_str(), "%d", &result);
        }
        else
        {
            db->Log(CMP_CORE, PRIO_WARNING, utils->PrintF("Blob %d of type %d for user %d not found",
                id, static_cast<int>(type), static_cast<int>(userID)));
        }
    }
    catch (const Exception &ex)
    {
        db->Log(CMP_CORE, PRIO_ERROR, utils->PrintF("Failed to get blob size %d of type %d for user %d from database: %s",
            id, static_cast<int>(type), static_cast<int>(userID), ex.strError.c_str()));
    }

    delete res;
    delete stmt;

    return result;
}

void BlobStorageProvider_UserDB::beginTx()
{
    if (sdb == NULL)
        return;

    if (txCounter == 0)
    {
        try
        {
            sdb->query("BEGIN TRANSACTION");
        }
        catch (const Exception &ex)
        {
            db->Log(CMP_CORE, PRIO_WARNING, utils->PrintF("Failed to start blob database transaction for user %d: %s",
                static_cast<int>(userID), ex.strError.c_str()));
        }
    }
    ++txCounter;
}

void BlobStorageProvider_UserDB::endTx()
{
    if (sdb == NULL)
        return;

    if (--txCounter == 0)
    {
        try
        {
            sdb->query("COMMIT");
        }
        catch (const Exception &ex)
        {
            db->Log(CMP_CORE, PRIO_WARNING, utils->PrintF("Failed to commit blob database transaction for user %d: %s",
                static_cast<int>(userID), ex.strError.c_str()));
        }
    }
    if (txCounter < 0)
        txCounter = 0;
}

bool BlobStorageProvider_UserDB::deleteBlob(BlobType type, int id)
{
    if (sdb == NULL)
        return false;

    bool result = false;

    SQLiteStmt *stmt = NULL;
    try
    {
        stmt = sdb->prepare("DELETE FROM [blobs] WHERE [type]=:type AND [id]=:id");
        stmt->bindValue(":type", static_cast<int>(type));
        stmt->bindValue(":id", id);
        stmt->execute();
        result = true;
    }
    catch (const Exception &ex)
    {
        db->Log(CMP_CORE, PRIO_ERROR, utils->PrintF("Failed to delete blob %d of type %d for user %d from database: %s",
            id, static_cast<int>(type), static_cast<int>(userID), ex.strError.c_str()));
    }
    delete stmt;

    return result;
}

bool BlobStorageProvider_UserDB::storeBlob(BlobType type, int id, const string &data)
{
    if (sdb == NULL)
        return false;

    bool result = false;

    int flags = 0;
    SQLiteStmt *stmt = NULL;
    try
    {
        stmt = sdb->prepare("REPLACE INTO [blobs]([type],[id],[data],[flags],[size]) VALUES(:type,:id,:data,:flags,:size)");
        stmt->bindValue(":type", static_cast<int>(type));
        stmt->bindValue(":id", id);

        if ((type == BMBLOB_TYPE_MAIL && strcmp(cfg->Get("blobstorage_compress"), "yes") == 0)
            || (type == BMBLOB_TYPE_WEBDISK && strcmp(cfg->Get("blobstorage_webdisk_compress"), "yes") == 0))
        {
            string compressedData = compressBlob(data);
            stmt->bindValue(":data", compressedData.c_str(), static_cast<int>(compressedData.size()));
            flags |= BMBS_USERDB_FLAG_GZCOMPRESSED;
        }
        else
        {
            stmt->bindValue(":data", data.c_str(), static_cast<int>(data.size()));
        }
        stmt->bindValue(":flags", flags);
        stmt->bindValue(":size", static_cast<int>(data.size()));
        stmt->execute();
        result = true;
    }
    catch (const Exception &ex)
    {
        db->Log(CMP_CORE, PRIO_ERROR, utils->PrintF("Failed to store blob of type %d with size %d for user %d in database: %s",
            id, static_cast<int>(type), data.size(), static_cast<int>(userID), ex.strError.c_str()));
    }
    delete stmt;

    return result;
}

string BlobStorageProvider_UserDB::compressBlob(const string &data, int level) const
{
    string result;
    z_stream stream;
    int ret;

    if (!data.empty())
        result.reserve(data.length());

    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.avail_in = static_cast<unsigned int>(data.size());
    stream.next_in = reinterpret_cast<unsigned char *>(const_cast<char *>(data.c_str()));

    ret = deflateInit2(&stream, level, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK)
    {
        db->Log(CMP_CORE, PRIO_WARNING, utils->PrintF("deflateInit2() failed when trying to compress blob"));
        return result;
    }

    do
    {
        unsigned char buff[BLOB_COMPRESS_CHUNKSIZE];
        stream.avail_out = BLOB_COMPRESS_CHUNKSIZE;
        stream.next_out = buff;

        ret = deflate(&stream, Z_FINISH);
        if (ret == Z_STREAM_ERROR)
        {
            inflateEnd(&stream);
            db->Log(CMP_CORE, PRIO_WARNING, utils->PrintF("Stream error while trying to compress blob"));
            return result;
        }

        int have = BLOB_COMPRESS_CHUNKSIZE - stream.avail_out;
        if (have > 0)
            result.append(reinterpret_cast<const char *>(buff), have);
    }
    while (stream.avail_out == 0);

    deflateEnd(&stream);

    return result;
}

string BlobStorageProvider_UserDB::uncompressBlob(const string &data, int sizeHint) const
{
    string result;
    z_stream stream;
    int ret;

    if (sizeHint > 0)
        result.reserve(sizeHint);

    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.avail_in = static_cast<unsigned int>(data.size());
    stream.next_in = reinterpret_cast<unsigned char *>(const_cast<char *>(data.c_str()));

    ret = inflateInit2(&stream, MAX_WBITS + 32);
    if (ret != Z_OK)
    {
        db->Log(CMP_CORE, PRIO_WARNING, utils->PrintF("inflateInit2() failed when trying to uncompress blob"));
        return result;
    }

    do
    {
        unsigned char buff[BLOB_COMPRESS_CHUNKSIZE];
        stream.avail_out = BLOB_COMPRESS_CHUNKSIZE;
        stream.next_out = buff;

        ret = inflate(&stream, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR)
        {
            inflateEnd(&stream);
            db->Log(CMP_CORE, PRIO_WARNING, utils->PrintF("Stream error while trying to uncompress blob"));
            return result;
        }

        switch (ret)
        {
        case Z_NEED_DICT:
        case Z_DATA_ERROR:
        case Z_MEM_ERROR:
            inflateEnd(&stream);
            db->Log(CMP_CORE, PRIO_WARNING, utils->PrintF("Error while trying to uncompress blob: %d", ret));
            return result;

        default:
            break;
        }

        int have = BLOB_COMPRESS_CHUNKSIZE - stream.avail_out;
        if (have > 0)
            result.append(reinterpret_cast<const char *>(buff), have);
    }
    while (stream.avail_out == 0);

    inflateEnd(&stream);

    return result;
}
