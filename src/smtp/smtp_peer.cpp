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

#include <smtp/smtp.h>

/*
 * classify peer
 */
void SMTP::ClassifyPeer()
{
    // get peer address (binary)
#ifdef WIN32
    SOCKET sSock = (SOCKET)GetStdHandle(STD_INPUT_HANDLE);
#else
    int sSock = fileno(stdin);
#endif
    struct sockaddr_storage sPeer;
    int iNameLen = sizeof(sPeer);
    if(getpeername(sSock, (struct sockaddr *)&sPeer, (socklen_t *)&iNameLen) != 0)
    {
        // unknown peer -> default origin
        this->iPeerOrigin = SMTP_PEER_ORIGIN_DEFAULT;
        return;
    }

    IPAddress peerAddr(0);
    if(sPeer.ss_family == AF_INET6)
    {
        peerAddr = IPAddress(((struct sockaddr_in6 *)&sPeer)->sin6_addr);
        if(peerAddr.isMappedIPv4())
            peerAddr = peerAddr.mappedIPv4();
    }
    else
    {
        peerAddr = IPAddress(((struct sockaddr_in *)&sPeer)->sin_addr);
    }

    // plugins
    FOR_EACH_PLUGIN(Plugin)
    {
        if(peerAddr.isIPv6)
            this->iPeerOrigin = Plugin->ClassifySMTPPeer(((struct sockaddr_in6 *)&sPeer)->sin6_addr.s6_addr);
        else
            this->iPeerOrigin = Plugin->ClassifySMTPPeer((int)((struct sockaddr_in *)&sPeer)->sin_addr.s_addr);

        if(this->iPeerOrigin != SMTP_PEER_ORIGIN_UNKNOWN)
            break;
    }
    END_FOR_EACH()

    // check peer angainst subnets...
    MySQL_Result *res;
    MYSQL_ROW row;
    if(this->iPeerOrigin == SMTP_PEER_ORIGIN_UNKNOWN)
    {
        res = db->Query("SELECT `ip`,`mask`,`classification` FROM bm60_bms_subnets");
        while((row = res->FetchRow()))
        {
            int iClassification = atoi(row[2]);

            // IPv6 rule
            if(strchr(row[0], ':') != NULL && peerAddr.isIPv6)
            {
                if(peerAddr.matches(row[0], row[1]))
                {
                    db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("[%s] Peer classified as %d by subnet rule",
                        this->strPeer.c_str(),
                        iClassification));
                    this->iPeerOrigin = iClassification;
                    break;
                }
            }

            // IPv4 rule
            else if(strchr(row[0], '.') != NULL && !peerAddr.isIPv6)
            {
                if(peerAddr.matches(row[0], row[1]))
                {
                    db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("[%s] Peer classified as %d by subnet rule",
                        this->strPeer.c_str(),
                        iClassification));
                    this->iPeerOrigin = iClassification;
                    break;
                }
            }
        }
        delete res;
    }

    // ...and DNSBLs (IPv4)
    if(this->iPeerOrigin == SMTP_PEER_ORIGIN_UNKNOWN && !peerAddr.isIPv6)
    {
        // flip IP
        string reversedIP = peerAddr.toReversedString();

        // check against DNSBLs
        res = db->Query("SELECT `host`,`classification`,`type` FROM bm60_bms_dnsbl WHERE `type` IN('ipv4','both') ORDER BY `pos`,`host`");
        while((row = res->FetchRow()))
        {
            string strLookup = reversedIP;
            strLookup.append(1, '.');
            strLookup.append(row[0]);

            if(row[0][strlen(row[0])-1] != '.')
                strLookup.append(".");

            if(gethostbyname(strLookup.c_str()) != NULL)
            {
                int iClassification = atoi(row[1]);

                db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("[%s] Peer classified as %d by IPv4 dnsbl server %s",
                    this->strPeer.c_str(),
                    iClassification,
                    row[0]));
                this->iPeerOrigin = iClassification;
                break;
            }
        }
        delete res;
    }

    // ...and IPv6 DNSBLs
    else if(this->iPeerOrigin == SMTP_PEER_ORIGIN_UNKNOWN && peerAddr.isIPv6)
    {
        // flip IP
        string reversedIP = peerAddr.toReversedString();

        // check against DNSBLs
        res = db->Query("SELECT `host`,`classification`,`type` FROM bm60_bms_dnsbl WHERE `type` IN('ipv6','both') ORDER BY `pos`,`host`");
        while((row = res->FetchRow()))
        {
            string strLookup = reversedIP;
            strLookup.append(1, '.');
            strLookup.append(row[0]);

            if(row[0][strlen(row[0])-1] != '.')
                strLookup.append(".");

            if(gethostbyname(strLookup.c_str()) != NULL)
            {
                int iClassification = atoi(row[1]);

                db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("[%s] Peer classified as %d by IPv6 dnsbl server %s",
                    this->strPeer.c_str(),
                    iClassification,
                    row[0]));
                this->iPeerOrigin = iClassification;
                break;
            }
        }
        delete res;
    }

    // no result => default
    if(this->iPeerOrigin == SMTP_PEER_ORIGIN_UNKNOWN)
        this->iPeerOrigin = SMTP_PEER_ORIGIN_DEFAULT;
}
