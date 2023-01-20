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

#ifndef _CORE_BLOBSTORAGE_H_
#define _CORE_BLOBSTORAGE_H_

#include <stdio.h>
#include <string>

#define BLOB_BUFFERSIZE                 4096
#define BLOB_COMPRESS_LEVEL             8
#define BLOB_COMPRESS_CHUNKSIZE         16384
#define BMBLOBSTORAGE_SEPARATEFILES     0
#define BMBLOBSTORAGE_USERDB            1
#define BMBS_USERDB_FLAG_GZCOMPRESSED   1

namespace Core
{
    enum BlobType
    {
        BMBLOB_TYPE_MAIL = 0,
        BMBLOB_TYPE_WEBDISK = 1
    };

    class BlobStorageProvider
    {
    public:
        virtual ~BlobStorageProvider() { }

    public:
        virtual bool storeBlob(BlobType type, int id, FILE *fp, int limit = -1)
        {
            std::string data;

            size_t byteCount = 0;
            while (!feof(fp))
            {
                char buff[BLOB_BUFFERSIZE];
                size_t readBytes = fread(buff, 1, BLOB_BUFFERSIZE, fp);
                if (limit != -1 && static_cast<int>(byteCount + readBytes) > limit)
                    break;
                if (readBytes == 0)
                    break;
                data.append(buff, readBytes);
                byteCount += readBytes;
            }

            return storeBlob(type, id, data);
        }

        virtual bool storeBlob(BlobType type, int id, const std::string &data) = 0;
        virtual FILE *loadBlob(BlobType type, int id) = 0;
        virtual bool deleteBlob(BlobType type, int id) = 0;
        virtual int getBlobSize(BlobType type, int id) = 0;
        virtual bool open(long long _userID) { userID = _userID; return true; }
        virtual void beginTx() { }
        virtual void endTx() { }

    protected:
        long long userID;
    };

    class BlobStorageProvider_SeparateFiles : public BlobStorageProvider
    {
    public:
        virtual bool storeBlob(BlobType type, int id, const std::string &data);
        virtual FILE *loadBlob(BlobType type, int id);
        virtual bool deleteBlob(BlobType type, int id);
        virtual int getBlobSize(BlobType type, int id);

    private:
        std::string blobPath(BlobType type, int id) const;
    };

    class SQLite;
    class BlobStorageProvider_UserDB : public BlobStorageProvider
    {
    public:
        BlobStorageProvider_UserDB() : sdb(NULL), txCounter(0) { }
        virtual ~BlobStorageProvider_UserDB();

    public:
        virtual bool storeBlob(BlobType type, int id, const std::string &data);
        virtual FILE *loadBlob(BlobType type, int id);
        virtual bool deleteBlob(BlobType type, int id);
        virtual int getBlobSize(BlobType type, int id);
        virtual bool open(long long _userID);
        virtual void beginTx();
        virtual void endTx();

    private:
        void initDB();
        std::string getDBFileName() const;
        std::string uncompressBlob(const std::string &data, int sizeHint = -1) const;
        std::string compressBlob(const std::string &data, int level = BLOB_COMPRESS_LEVEL) const;

    private:
        SQLite *sdb;
        int txCounter;

        BlobStorageProvider_UserDB(const BlobStorageProvider_UserDB &);
        BlobStorageProvider_UserDB &operator=(const BlobStorageProvider_UserDB &);
    };
};

#endif
