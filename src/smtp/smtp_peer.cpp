/*
 * b1gMailServer
 * Copyright (c) 2002-2025
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

namespace {

bool doesHostentMatchIPs(Core::Utils *utils, struct hostent *hostent, const std::string &matchIPs)
{
    if(matchIPs.empty())
    {
        return true;
    }

    if(hostent->h_addrtype != AF_INET)
    {
        return false;
    }

    struct in_addr **hostentIPs = reinterpret_cast<struct in_addr **>(hostent->h_addr_list);

    std::vector<std::string> ips;
    utils->Explode(matchIPs, ips, ',');

    for(const auto &item : ips)
    {
        std::string matchIP = utils->Trim(item);

        std::vector<std::string> ipParts;
        utils->Explode(matchIP, ipParts, '.');

        if(ipParts.size() != 4)
        {
            continue;
        }

        for(std::size_t ipIndex = 0; hostentIPs[ipIndex] != nullptr; ++ipIndex)
        {
            struct in_addr *hostentIP = hostentIPs[ipIndex];

            bool doesMatch = true;
            for(std::size_t i = 0; i < 4; ++i)
            {
                std::string ipPartStr = utils->Trim(ipParts[i]);
                ipPartStr = utils->Trim(ipPartStr, "[]");

                uint8_t hostentIPPart = (reinterpret_cast<uint8_t *>(hostentIP))[i];

                std::size_t dashPos = ipPartStr.find('-');
                if(dashPos != std::string::npos)
                {
                    unsigned long ipPartFrom = std::stoull(ipPartStr.substr(0, dashPos));
                    unsigned long ipPartTo = std::stoull(ipPartStr.substr(dashPos + 1));

                    if(ipPartTo < ipPartFrom || ipPartFrom > 255 || ipPartTo > 255
                        || hostentIPPart < static_cast<uint8_t>(ipPartFrom)
                        || hostentIPPart > static_cast<uint8_t>(ipPartTo))
                    {
                        doesMatch = false;
                        break;
                    }
                }
                else
                {
                    unsigned long ipPart = std::stoul(ipPartStr);

                    if(ipPart > 255)
                    {
                        doesMatch = false;
                        break;
                    }

                    if(hostentIPPart != static_cast<uint8_t>(ipPart))
                    {
                        doesMatch = false;
                        break;
                    }
                }
            }

            if(doesMatch)
            {
                return true;
            }
        }
    }

    return false;
}

} // anon ns

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
        bool bHaveMatchIPs = strcmp(cfg->Get("enable_dnsbl_matchips"), "1") == 0;
        res = db->Query(bHaveMatchIPs
            ? "SELECT `host`,`classification`,`type`,`match_ips` FROM bm60_bms_dnsbl WHERE `type` IN('ipv4','both') ORDER BY `pos`,`host`"
            : "SELECT `host`,`classification`,`type` FROM bm60_bms_dnsbl WHERE `type` IN('ipv4','both') ORDER BY `pos`,`host`");
        while((row = res->FetchRow()))
        {
            string strLookup = reversedIP;
            strLookup.append(1, '.');
            strLookup.append(row[0]);

            if(row[0][strlen(row[0])-1] != '.')
                strLookup.append(".");

            struct hostent *lookupResult = gethostbyname(strLookup.c_str());
            if(lookupResult != nullptr)
            {
                string strMatchIPs = bHaveMatchIPs ? utils->Trim(std::string(row[3])) : "";
                if (doesHostentMatchIPs(utils, lookupResult, strMatchIPs))
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
