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

#include <http/http.h>
#include <core/blobstorage.h>

const char *HTTPParseErrorMessages[] = {
    "No error (valid request)",
    "Invalid HTTP request",
    "Invalid/unsupported protocol in HTTP request",
    "Invalid/unsupported request method"
};

/*
 * Constructor
 */
HTTP::HTTP()
{
  const char *szPeer = utils->GetPeerAddress();
    if(szPeer == NULL)
        this->strPeer = "(unknown)";
    else
        this->strPeer = szPeer;
    request.requestProtocol = "HTTP/1.1";
    iTrafficUsed = 0;
    iTrafficAllowed = -1;
    iBandwithLimit = -1;
}

/*
 * Destructor
 */
HTTP::~HTTP()
{
}

/*
 * Main loop
 */
void HTTP::Run()
{
    char szBuffer[HTTP_LINEMAX+1];

    // parse request
    request.parseStatus = PARSE_INVALID_REQUEST;
    lineNum = 0;
    while(fgets(szBuffer, HTTP_LINEMAX, stdin) != NULL)
        if(!this->ProcessLine(szBuffer))
            break;

    // process request
    if(request.parseStatus == PARSE_OK)
    {
        this->ProcessRequest();
    }
    else
    {
        this->ErrorPage(ERROR_BAD_REQUEST, HTTPParseErrorMessages[request.parseStatus]);
    }
}

/**
 * Find a file on a user's webdisk
 */
WebdiskFileInfo *HTTP::FindWebdiskFile(string strURL, int iUserID)
{
    MySQL_Result *res;
    MYSQL_ROW row;
    int iParentID = 0;

    // get path
    char *szPath = mstrdup(strURL.c_str());

    char *savePtr, *pch = strtok_r(szPath, "/", &savePtr);
    int iCount = 0, iLastFound = 0;
    bool bStopLooking = false;
    while(pch != NULL)
    {
        int iFound = 0;
        if(!bStopLooking)
        {
            res = db->Query("SELECT id FROM bm60_diskfolders WHERE parent='%d' AND titel='%q' LIMIT 1",
                iParentID,
                pch);
            while((row = res->FetchRow()))
            {
                iParentID = atoi(row[0]);
                iFound++;
            }
            delete res;

            if(iFound == 0)
            {
                iLastFound = iCount;
                bStopLooking = true;
            }
        }

        iCount++;
        pch = strtok_r(NULL, "/", &savePtr);
    }
    free(szPath);

    if(iParentID == 0)
        iParentID = -1;

    if(bStopLooking && iLastFound < iCount-1)
        iParentID = -1;

    // get file
    bool haveBlobStorage = strcmp(cfg->Get("enable_blobstorage"), "1") == 0;
    szPath = mstrdup(strURL.c_str());
    WebdiskFileInfo *result = NULL;
    char *szSlash = strrchr(szPath, '/');
    if(szSlash != NULL)
    {
        if(szSlash == (szPath+strlen(szPath)-1) && !bStopLooking)
        {
            // filename ends with slash => search for index.htm/index.html
            if (haveBlobStorage)
            {
                res = db->Query("SELECT id,dateiname,size,contenttype,blobstorage FROM bm60_diskfiles WHERE user='%d' AND ordner='%d' AND (dateiname='index.html' OR dateiname='index.htm') LIMIT 1",
                    iUserID,
                    iParentID);
            }
            else
            {
                res = db->Query("SELECT id,dateiname,size,contenttype FROM bm60_diskfiles WHERE user='%d' AND ordner='%d' AND (dateiname='index.html' OR dateiname='index.htm') LIMIT 1",
                    iUserID,
                    iParentID);
            }
            while((row = res->FetchRow()))
            {
                result = new WebdiskFileInfo;
                result->fileID = atoi(row[0]);
                result->fileSize = atoi(row[2]);
                result->mimeType = row[3];
                result->fileName = row[1];
                result->blobStorage = haveBlobStorage ? atoi(row[4]) : 0;
            }
            delete res;
        }
        else
        {
            // search for file
            szSlash++;
            if (haveBlobStorage)
            {
                res = db->Query("SELECT id,dateiname,size,contenttype,blobstorage FROM bm60_diskfiles WHERE user='%d' AND ordner='%d' AND dateiname='%q' LIMIT 1",
                    iUserID,
                    iParentID,
                    szSlash);
            }
            else
            {
                res = db->Query("SELECT id,dateiname,size,contenttype FROM bm60_diskfiles WHERE user='%d' AND ordner='%d' AND dateiname='%q' LIMIT 1",
                    iUserID,
                    iParentID,
                    szSlash);
            }
            while((row = res->FetchRow()))
            {
                result = new WebdiskFileInfo;
                result->fileID = atoi(row[0]);
                result->fileSize = atoi(row[2]);
                result->mimeType = row[3];
                result->fileName = row[1];
                result->blobStorage = haveBlobStorage ? atoi(row[4]) : 0;
            }
            delete res;
        }
    }
    free(szPath);

    return(result);
}

/**
 * Process request
 */
void HTTP::ProcessRequest()
{
    db->Log(CMP_HTTP, PRIO_NOTE, utils->PrintF("[%s] %s - %s %s %s",
        strPeer.c_str(),
        request.requestHost.c_str(),
        request.requestMethod == REQUEST_GET ? "GET" : "HTTP",
        request.requestURL.c_str(),
        request.requestProtocol.c_str()));

    // search for user matching the hostname
    char *szSearchUser = mstrdup(request.requestHost.c_str()),
        *szDomainList = mstrdup(cfg->Get("domains"));

    // transform subdomain to e-mail-address
    char *savePtr, *pch = strtok_r(szDomainList, ":", &savePtr);
    while(pch != NULL)
    {
        if(strlen(szSearchUser) > strlen(pch))
        {
            if(strcasecmp((szSearchUser+(strlen(szSearchUser)-strlen(pch))), pch) == 0)
            {
                *(szSearchUser+(strlen(szSearchUser)-strlen(pch)-1)) = '@';
                break;
            }
        }

        pch = strtok_r(NULL, ":", &savePtr);
    }
    free(szDomainList);

    // search user
    int iGroupID = 0;
    MySQL_Result *res = NULL;

    if(strcmp(cfg->Get("user_space_add"), "1") == 0)
    {
        db->Query("SELECT bm60_users.id, bm60_users.traffic_up, bm60_users.traffic_down, bm60_gruppen.traffic+bm60_users.traffic_add AS traffic, bm60_gruppen.wd_open_kbs, bm60_gruppen.id FROM bm60_users,bm60_gruppen WHERE bm60_users.email='%q' AND bm60_gruppen.id=bm60_users.gruppe AND bm60_gruppen.share='yes' AND bm60_gruppen.webdisk>0",
            szSearchUser);
    }
    else
    {
        db->Query("SELECT bm60_users.id, bm60_users.traffic_up, bm60_users.traffic_down, bm60_gruppen.traffic, bm60_gruppen.wd_open_kbs, bm60_gruppen.id FROM bm60_users,bm60_gruppen WHERE bm60_users.email='%q' AND bm60_gruppen.id=bm60_users.gruppe AND bm60_gruppen.share='yes' AND bm60_gruppen.webdisk>0",
            szSearchUser);
    }
    MYSQL_ROW row;
    int iUserID = -1;
    while((row = res->FetchRow()))
    {
        iUserID = atoi(row[0]);
        iUser = iUserID;
        iTrafficUsed = atoi(row[1]) + atoi(row[2]);
        iTrafficAllowed = atoi(row[3]);
        iBandwithLimit = atoi(row[4]);
        iGroupID = atoi(row[5]);
    }
    delete res;

    // ad sig?
    try
    {
        res = db->Query("SELECT value FROM bm60_groupoptions WHERE `key`='wdhttpadsig' AND gruppe='%d' LIMIT 1",
            iGroupID);
        while((row = res->FetchRow()))
        {
            adSig = row[0];
        }
        delete res;
    }
    catch(Core::Exception e)
    {
        // incompatible version, no signature
        db->Log(CMP_HTTP, PRIO_DEBUG, utils->PrintF("HTTP signature query failed (b1gMail < 6.3.0-PL10?)"));
    }

    // user found?
    if(iUserID > 0)
    {
        // yes -> search requested file
        string myRequestURL = "/www";
        myRequestURL += this->request.requestURL;
        WebdiskFileInfo *requestedFile = this->FindWebdiskFile(myRequestURL, iUserID);

        // file found?
        if(requestedFile != NULL)
        {
            // yes -> send to browser
            this->SendFile(requestedFile);
            delete requestedFile;
        }
        else
        {
            // no -> 404
            this->ErrorPage(ERROR_PAGE_NOT_FOUND, "The requested file does not exist.");
        }
    }
    else
    {
        // no -> 404
        this->ErrorPage(ERROR_PAGE_NOT_FOUND, "The requested user subdomain cannot be found.");
    }

    free(szSearchUser);
}

/**
 * Send a file
 */
void HTTP::SendFile(WebdiskFileInfo *file)
{
    // enough traffic?
    if((iTrafficUsed + file->fileSize) > iTrafficAllowed
        && iTrafficAllowed != -1)
    {
        // no
        this->ErrorPage(ERROR_BANDWITH_EXCEEDED, NULL);
    }
    else
    {
        // signature
        bool bSig = false;
        if(file->mimeType.compare("text/html") == 0
            && adSig.length() > 2)
        {
            bSig = true;
            db->Log(CMP_HTTP, PRIO_DEBUG, utils->PrintF("HTTP ad signature enabled"));
        }

        // send headers
        this->PrintHeaders(200, "OK", file->fileSize + (bSig ? adSig.length() : 0), file->mimeType.c_str());
        printf("\r\n");

        // size
        int iBytesTransmitted = 0;

        // get file
        FILE *fp = NULL;
        BlobStorageProvider *storage = utils->CreateBlobStorageProvider(file->blobStorage, this->iUser);
        if (storage != NULL)
            fp = storage->loadBlob(BMBLOB_TYPE_WEBDISK, file->fileID);

        // send file
        db->Log(CMP_HTTP, PRIO_DEBUG, utils->PrintF("Sending file %d from storage provider %d", file->fileID, file->blobStorage));
        if(fp != NULL)
        {
            int iBytesPer5Milliseconds = 4096;
            if(iBandwithLimit != -1)
            {
                iBytesPer5Milliseconds = (iBandwithLimit*1024)/200;
            }

            int iBytes;
            char szBuffer[4096];
            while((iBytes = fread(szBuffer, 1, iBytesPer5Milliseconds, fp)) > 0)
            {
                int numWritten = fwrite(szBuffer, 1, iBytes, stdout);
                iBytesTransmitted += numWritten;
                if(numWritten != iBytes)
                    break;
                fflush(stdout);
                if(iBandwithLimit != -1)
                    utils->MilliSleep(5);
            }
            fclose(fp);

            // ad signature
            if(bSig)
                printf("%s", adSig.c_str());

            // add traffic
            db->Query("UPDATE bm60_users SET traffic_down=traffic_down+%d WHERE id='%d'",
                iBytesTransmitted,
                iUser);
        }

        if (storage != NULL)
            delete storage;
    }
}

/**
 * Send error page
 */
void HTTP::ErrorPage(HTTPError error, const char *szInfo)
{
    // build error strings
    const char *szError = "Unknown error",
        *szDesc = "An unknown error occured.";
    switch(error)
    {
    case ERROR_BAD_REQUEST:
        szError = "Bad Request";
        szDesc = "Your HTTP client made a request this server cannot understand. Please try again with an other HTTP "
                    "client or contact the server\'s administator.";
        break;

    case ERROR_PAGE_NOT_FOUND:
        szError = "Page Not Found";
        szDesc = "The requested page does not exist. Please check the URL and try again or contact the server\'s "
                    "administrator.";
        break;

    case ERROR_UNAUTHORIZED:
        szError = "Unauthorized";
        szDesc = "You are not allowed to access this page. Please try again or contact the servers\'s administator, "
                    "if you think this is an error of the server.";
        break;

    case ERROR_INTERNAL_SERVER_ERROR:
        szError = "Internal Server Error";
        szDesc = "This server cannot process your request now because of internal technical problems. Please try again "
                    "later or contact the server\'s administrator.";
        break;

    case ERROR_BANDWITH_EXCEEDED:
        szError = "Bandwith Limit Exceeded";
        szDesc = "The bandwith limit of this host has exceeded for this month. Please try again next month.";
        break;
    };

    // build page
    string strPage = "<html>\r\n"
                        "<head>\r\n"
                            "<title>";
    strPage += szError;
    strPage +=              "</title>\r\n"
                        "</head>\r\n"
                        "<body>\r\n"
                            "<h1>";
    strPage += szError;
    strPage +=              "</h1>\r\n"
                            "<p>\r\n";
    strPage += szDesc;
    strPage +=              "</p>\r\n"
                            "<p>\r\n";
    strPage += szInfo == NULL ? "There is no further information about this error available." : szInfo;
    strPage +=              "</p>\r\n"
                            "<address>"
                                "b1gMailServer/" BMS_VERSION " (" BMS_BUILD_ARCH ")"
                            "</address>"
                        "</body>\r\n"
                    "</html>\r\n";

    // answer
    db->Log(CMP_HTTP, PRIO_NOTE, utils->PrintF("[%s] Sending error page (Error %d: %s)",
        strPeer.c_str(),
        error,
        szError));
    this->PrintHeaders(error, szError, strPage.length(), "text/html");
    printf("\r\n");
    printf("%s", strPage.c_str());
}

/**
 * Print out headers
 */
void HTTP::PrintHeaders(int statusCode, const char *statusInfo, int contentLength, const char *contentType)
{
    printf("%s %03d %s\r\n",
        request.requestProtocol.c_str(),
        statusCode,
        statusInfo);
    printf("Server: b1gMailServer/" BMS_VERSION " (" BMS_BUILD_ARCH ")\r\n");
    printf("Connection: close\r\n");
    printf("Content-Length: %d\r\n",
        contentLength);
    printf("Content-Type: %s\r\n",
        contentType);
}

/*
 * Process a line
 */
bool HTTP::ProcessLine(char *szLine)
{
    // end of request?
    if(strcmp(szLine, "") == 0 || strcmp(szLine, "\r") == 0 || strcmp(szLine, "\r\n") == 0 || strcmp(szLine, "\n") == 0)
    {
        return(false);
    }

    // first line?
    if(lineNum == 0)
    {
        // get request method
        if(strncasecmp(szLine, "GET ", 4) == 0)
        {
            // GET
            request.requestMethod = REQUEST_GET;
        }
        else if(strncasecmp(szLine, "POST ", 5) == 0)
        {
            // POST
            request.requestMethod = REQUEST_POST;
        }
        else
        {
            // abort parsing - invalid request method
            request.parseStatus = PARSE_INVALID_REQUEST_METHOD;
            return(false);
        }

        // read request URL and protocol
        char *szURL = new char[strlen(szLine)+1],
            *szProtocol = new char[strlen(szLine)+1];
        if(sscanf(szLine, "%*s %s %s", szURL, szProtocol) == 2)
        {
            if(strcasecmp(szProtocol, "HTTP/1.0") == 0
                || strcasecmp(szProtocol, "HTTP/1.1") == 0)
            {
                // everyting OK, go on
                request.requestURL = szURL;
                request.requestProtocol = szProtocol;
                request.parseStatus = PARSE_OK;
            }
            else
            {
                // abort parsing - invalid protocol
                request.parseStatus = PARSE_INVALID_PROTOCOL;
            }
        }
        else
        {
            // abort parsing - invalid request
            request.parseStatus = PARSE_INVALID_REQUEST;
        }
        delete[] szURL;
        delete[] szProtocol;

        // ok?
        if(request.parseStatus != PARSE_OK)
            return(false);
    }

    // other lines
    else
    {
        char *szKey = new char[strlen(szLine)+1],
            *szValue = new char[strlen(szLine)+1];
        if(sscanf(szLine, "%s %s", szKey, szValue) == 2
            && szKey[strlen(szKey)-1] == ':')
        {
            // get real value
            strcpy(szValue, szLine+strlen(szKey)+1);
            while(szValue[strlen(szValue)-1] == '\r'
                    || szValue[strlen(szValue)-1] == '\n'
                    || szValue[strlen(szValue)-1] == ' ')
            {
                szValue[strlen(szValue)-1] = '\0';
            }

            // host
            if(strcasecmp(szKey, "Host:") == 0)
            {
                char *szDDot = strchr(szValue, ':');
                if(szDDot != NULL)
                    *szDDot = '\0';
                request.requestHost = szValue;
            }
            else if(strcasecmp(szKey, "User-Agent:") == 0)
            {
                request.userAgent = szValue;
            }
        }
        else
        {
            // strange line, don't care about it
        }
        delete[] szKey;
        delete[] szValue;
    }

    // increment line number
    lineNum++;

    // go on
    return(true);
}
