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

#include <core/dns.h>
#include <algorithm>
#include <sstream>

#define DNS_BUFFERSIZE      4096
#define DNS_HFIXEDSZ        12
#define DNS_QFIXEDSZ        4
#define DNS_RRFIXEDSZ       10
#define NS_T_TLSA           52

/*
 * A lookup
 */
int DNS::ALookup(const string &domain, vector<in_addr_t> &out)
{
    out.clear();

    struct addrinfo hint;
    memset(&hint, 0, sizeof(hint));
    hint.ai_family = AF_INET;
    hint.ai_socktype = 0;
    hint.ai_protocol = 0;
    hint.ai_flags = 0;

    struct addrinfo *res, *rp;
    if(getaddrinfo(domain.c_str(), NULL, &hint, &res) == 0)
    {
        for(rp = res; rp != NULL; rp = rp->ai_next)
        {
            struct sockaddr_in *sAddr = (struct sockaddr_in *)rp->ai_addr;

            if(sAddr->sin_family != AF_INET)
                continue;
            in_addr_t inAddr = sAddr->sin_addr.s_addr;

            bool exists = false;
            for(vector<in_addr_t>::iterator it = out.begin(); it != out.end(); ++it)
            {
                if(*it == inAddr)
                {
                    exists = true;
                    break;
                }
            }

            if(!exists)
                out.push_back(inAddr);
        }

        freeaddrinfo(res);
    }

    return((int)out.size());
}

/*
 * AAA lookup
 */
int DNS::AAALookup(const string &domain, vector<in6_addr> &out)
{
    out.clear();

    struct addrinfo hint;
    memset(&hint, 0, sizeof(hint));
    hint.ai_family = AF_INET6;
    hint.ai_socktype = 0;
    hint.ai_protocol = 0;
    hint.ai_flags = 0;

    struct addrinfo *res, *rp;
    if(getaddrinfo(domain.c_str(), NULL, &hint, &res) == 0)
    {
        for(rp = res; rp != NULL; rp = rp->ai_next)
        {
            struct sockaddr_in6 *sAddr = (struct sockaddr_in6 *)rp->ai_addr;

            if(sAddr->sin6_family != AF_INET6)
                continue;

            in6_addr inAddr;
            memcpy(&inAddr, &sAddr->sin6_addr, sizeof(inAddr));

            bool exists = false;
            for(vector<in6_addr>::iterator it = out.begin(); it != out.end(); ++it)
            {
                in6_addr compareAddr = *it;
                if(memcmp(&inAddr, &compareAddr, sizeof(inAddr)) == 0)
                {
                    exists = true;
                    break;
                }
            }

            if(!exists)
                out.push_back(inAddr);
        }

        freeaddrinfo(res);
    }

    return((int)out.size());
}

/*
 * MX server sort callback
 */
bool DNS::MXServerSort(const MXServer &a, const MXServer &b)
{
    if(a.Preference == b.Preference)            // randomize order of MX servers with same pref (RFC 2821)
        return(a.RandomValue < b.RandomValue);
    else
        return(a.Preference < b.Preference);
}

#ifdef WIN32
/*
 * Lookup PTR record (windows)
 */
int DNS::PTRLookup(const string &ip, vector<string> &out)
{
    out.clear();

    // build search domain
    IPAddress theIP(ip);

    string domain = theIP.toReversedString();

    if(theIP.isIPv6)
    {
        domain += string(".ip6.arpa.");
    }
    else
    {
        domain += string(".in-addr.arpa.");
    }

    // ns vars
    DNS_STATUS iResult;
    PDNS_RECORD cRecords, cRecord;

    // do PTR lookup
    iResult = DnsQuery_A(domain.c_str(), DNS_TYPE_PTR, DNS_QUERY_STANDARD, NULL, &cRecords, NULL);

    // TXT record found?
    if(iResult == NOERROR)
    {
        for(cRecord = cRecords; cRecord != NULL; cRecord = cRecord->pNext)
        {
            if(cRecord->wType != DNS_TYPE_PTR)
                continue;

            PDNS_PTR_DATA cPTRData = (PDNS_PTR_DATA)&cRecord->Data;
            out.push_back(cPTRData->pNameHost);
        }

        DnsRecordListFree(cRecords, DnsFreeRecordList);
    }

    return((int)out.size());
}

/*
 * Lookup TXT record (windows)
 */
int DNS::TXTLookup(const string &domain, vector<string> &out, int type)
{
    out.clear();

    // do not support other records than TXT at the moment (needs testing)
    if(type != ns_t_txt)
        return(0);

    // ns vars
    DNS_STATUS iResult;
    PDNS_RECORD cRecords, cRecord;

    // do TXT lookup
    iResult = DnsQuery_A(domain.c_str(), (WORD)type, DNS_QUERY_STANDARD, NULL, &cRecords, NULL);

    // TXT record found?
    if(iResult == NOERROR)
    {
        for(cRecord = cRecords; cRecord != NULL; cRecord = cRecord->pNext)
        {
            if(cRecord->wType != (WORD)type)
                continue;

            PDNS_TXT_DATA cTXTData = (PDNS_TXT_DATA)&cRecord->Data;
            PSTR *recordStrings = cTXTData->pStringArray;

            string resultEntry;

            for(DWORD i=0; i<cTXTData->dwStringCount; i++)
            {
                if(recordStrings[i] == NULL)
                    continue;
                resultEntry += string(recordStrings[i]);
            }

            if (!resultEntry.empty())
            {
                out.push_back(resultEntry);
            }
        }

        DnsRecordListFree(cRecords, DnsFreeRecordList);
    }

    return((int)out.size());
}

/*
 * Perform TLSA lookup (windows)
 */
int DNS::TLSALookup(const string &domain, unsigned short port, vector<TLSARecord> &out)
{
#ifdef DNS_TYPE_TLSA
    out.clear();

    // ns vars
    DNS_STATUS iResult;
    PDNS_RECORD cRecords, cRecord;

    // do TXT lookup
    iResult = DnsQuery_A(domain.c_str(), DNS_TYPE_TLSA, DNS_QUERY_STANDARD, NULL, &cRecords, NULL);

    // TXT record found?
    if(iResult == NOERROR)
    {
        for(cRecord = cRecords; cRecord != NULL; cRecord = cRecord->pNext)
        {
            if(cRecord->wType != DNS_TYPE_TLSA)
                continue;

            PDNS_TLSA_DATA cTLSAData = (PDNS_TLSA_DATA)&cRecord->Data;

            TLSARecord entry;
            entry.usage         = static_cast<TLSARecord::Usage>(cTLSAData->bCertUsage);
            entry.selector      = static_cast<TLSARecord::Selector>(cTLSAData->bSelector);
            entry.matchingType  = static_cast<TLSARecord::MatchingType>(cTLSAData->bMatchingType);
            entry.value         = std::string(reinterpret_cast<const char *>(&cTLSAData->bCertificateAssociationData[0]),
                                    static_cast<std::size_t>(cTLSAData->bCertificateAssociationDataLength));
            out.push_back(entry);
        }

        DnsRecordListFree(cRecords, DnsFreeRecordList);
    }

    return((int)out.size());
#else
    return 0;
#endif
}

/*
 * Perform MX lookup (windows)
 */
int DNS::MXLookup(const string &domain, vector<MXServer> &out, bool implicit, bool useDNSSEC)
{
    string strDomain = domain;
    out.clear();

    // ns vars
    DNS_STATUS iResult;
    PDNS_RECORD cRecords, cRecord;

    // do MX lookup
    iResult = DnsQuery_A(strDomain.c_str(), DNS_TYPE_MX, DNS_QUERY_STANDARD, NULL, &cRecords, NULL);

    // no MX record found? => check for CNAME record
    if(iResult != NOERROR && implicit)
    {
        bool bGotCNAME = false;
        iResult = DnsQuery_A(strDomain.c_str(), DNS_TYPE_CNAME, DNS_QUERY_STANDARD, NULL, &cRecords, NULL);

        if(iResult == NOERROR)
        {
            for(cRecord = cRecords; cRecord != NULL; cRecord = cRecord->pNext)
            {
                if(cRecord->wType != DNS_TYPE_CNAME)
                    continue;

                PDNS_PTR_DATA cPTRData = (PDNS_PTR_DATA)&cRecord->Data;

                strDomain = cPTRData->pNameHost;
                bGotCNAME = true;
                break;
            }

            DnsRecordListFree(cRecords, DnsFreeRecordList);
        }

        // do MX lookup again if a CNAME record was found
        if(bGotCNAME)
        {
            iResult = DnsQuery_A(strDomain.c_str(), DNS_TYPE_MX, DNS_QUERY_STANDARD, NULL, &cRecords, NULL);
        }
        else
            iResult = NOERROR+1;
    }

    // MX record found?
    if(iResult == NOERROR)
    {
        for(cRecord = cRecords; cRecord != NULL; cRecord = cRecord->pNext)
        {
            if(cRecord->wType != DNS_TYPE_MX)
                continue;

            PDNS_MX_DATA cMXData = (PDNS_MX_DATA)&cRecord->Data;

            MXServer mx;
            mx.Hostname = cMXData->pNameExchange;
            mx.Preference = (int)cMXData->wPreference;
            mx.RandomValue = ts_rand();
            mx.Authenticated = false;
            out.push_back(mx);
        }

        DnsRecordListFree(cRecords, DnsFreeRecordList);
    }

    // no MX => use A record
    else if(implicit)
    {
        MXServer mx;
        mx.Hostname = strDomain;
        mx.Preference = 10;
        mx.RandomValue = rand();
        mx.Authenticated = useDNSSEC;
        out.push_back(mx);
    }

    // sort
    sort(out.begin(), out.end(), DNS::MXServerSort);

    return((int)out.size());
}
#else
/*
 * Perform MX lookup (unix)
 */
int DNS::MXLookup(const string &domain, vector<MXServer> &out, bool implicit, bool useDNSSEC)
{
    string strDomain = domain;
    out.clear();

    // prepare res
    struct __res_state res;
    res_ninit(&res);
    if(useDNSSEC)
        res.options |= RES_USE_EDNS0 | RES_USE_DNSSEC;

    // ns vars
    int iResult;
    u_char szAnswerBuffer[DNS_BUFFERSIZE];

    // do MX lookup
    iResult = res_nquery(&res, strDomain.c_str(), ns_c_in, ns_t_mx, szAnswerBuffer, sizeof(szAnswerBuffer));

    // result too big?
    if(iResult > (int)sizeof(szAnswerBuffer))
    {
        return(0);
    }

    // parse
    if(iResult > 0)
    {
        DNS::ParseMXAnswer(szAnswerBuffer, iResult, out, useDNSSEC);
    }

    // no MX record found? => check for CNAME record
    //! @todo DNSSEC for CNAME lookups?
    if(out.size() == 0 && implicit)
    {
        bool bGotCNAME = false;

        struct hostent *aEntry;

#ifdef __APPLE__
        if((aEntry = gethostbyname(strDomain.c_str())) != NULL)
#else
        int err;
        struct hostent aEntryBuff;

        if(gethostbyname_r(strDomain.c_str(), &aEntryBuff, (char *)szAnswerBuffer, sizeof(szAnswerBuffer), &aEntry, &err) == 0
            && aEntry != NULL)
#endif
        {
            if(aEntry->h_aliases[0] != NULL)
            {
                strDomain = aEntry->h_name;
                bGotCNAME = true;
            }
        }

        // do MX lookup again if a CNAME record was found
        if(bGotCNAME)
        {
            iResult = res_nquery(&res, strDomain.c_str(), ns_c_in, ns_t_mx, szAnswerBuffer, sizeof(szAnswerBuffer));

            if(iResult > (int)sizeof(szAnswerBuffer))
            {
                return(0);
            }

            if(iResult > 0)
            {
                DNS::ParseMXAnswer(szAnswerBuffer, iResult, out, useDNSSEC);
            }
        }
    }

    // no MX => use A record
    if(out.size() == 0 && implicit)
    {
        MXServer mx;
        mx.Hostname = strDomain;
        mx.Preference = 10;
        mx.RandomValue = rand();
        mx.Authenticated = useDNSSEC;
        out.push_back(mx);
    }

    // sort
    sort(out.begin(), out.end(), DNS::MXServerSort);

    return((int)out.size());
}

/*
 * Parse DNS MX answer
 */
bool DNS::ParseMXAnswer(u_char *buffer, int len, vector<MXServer> &out, bool useDNSSEC)
{
    bool result = true;
    char tmpBuffer[DNS_BUFFERSIZE];

    if(len < DNS_HFIXEDSZ)
        return(false);

    const HEADER *head = reinterpret_cast<const HEADER *>(buffer);
    unsigned int questionCount = ntohs(head->qdcount),
                 answerCount   = ntohs(head->ancount);

    if(questionCount != 1)
        return(false);

    if(answerCount == 0)
        return(false);

    bool authenticated = (useDNSSEC && head->ad);

    int dnLen = dn_expand(buffer, buffer+len, buffer+DNS_HFIXEDSZ, tmpBuffer, sizeof(tmpBuffer));

    if(dnLen == -1)
        return(false);

    if(buffer + DNS_HFIXEDSZ + dnLen + DNS_QFIXEDSZ > buffer + len)
        return(false);

    const u_char *ptr = buffer + DNS_HFIXEDSZ + dnLen + DNS_QFIXEDSZ;
    for(unsigned int i = 0; i<answerCount; i++)
    {
        dnLen = dn_expand(buffer, buffer+len, ptr, tmpBuffer, sizeof(tmpBuffer));
        if(dnLen == -1)
            break;

        ptr += dnLen;
        if(ptr + DNS_RRFIXEDSZ > buffer + len)
        {
            result = false;
            break;
        }

        int rrType      = (ptr[0] << 8) | ptr[1],
            rrClass     = (ptr[2] << 8) | ptr[3],
            rrLen       = (ptr[8] << 8) | ptr[9];
        ptr += DNS_RRFIXEDSZ;

        if(rrClass == ns_c_in && rrType == ns_t_mx)
        {
            if(rrLen < 2)
            {
                result = false;
                break;
            }

            dnLen = dn_expand(buffer, buffer+len, ptr+sizeof(unsigned short), tmpBuffer, sizeof(tmpBuffer));
            if(dnLen == -1)
                break;

            MXServer entry;
            entry.RandomValue   = rand();
            entry.Preference    = ntohs(*((unsigned short *)ptr));
            entry.Hostname      = tmpBuffer;
            entry.Authenticated = authenticated;
            out.push_back(entry);
        }

        ptr += rrLen;
    }

    return(result);
}

/*
 * Parse TXT DNS record from resolver answer
 */
bool DNS::ParseTXTAnswer(u_char *buffer, int len, vector<string> &out, int type)
{
    bool result = true;
    char tmpBuffer[DNS_BUFFERSIZE];

    if(len < DNS_HFIXEDSZ)
        return(false);

    const HEADER *head = reinterpret_cast<const HEADER *>(buffer);
    unsigned int questionCount = ntohs(head->qdcount),
                 answerCount   = ntohs(head->ancount);

    if(questionCount != 1)
        return(false);

    if(answerCount == 0)
        return(false);

    int dnLen = dn_expand(buffer, buffer+len, buffer+DNS_HFIXEDSZ, tmpBuffer, sizeof(tmpBuffer));

    if(dnLen == -1)
        return(false);

    if(buffer + DNS_HFIXEDSZ + dnLen + DNS_QFIXEDSZ > buffer + len)
        return(false);

    const u_char *ptr = buffer + DNS_HFIXEDSZ + dnLen + DNS_QFIXEDSZ;
    for(unsigned int i = 0; i<answerCount; i++)
    {
        dnLen = dn_expand(buffer, buffer+len, ptr, tmpBuffer, sizeof(tmpBuffer));
        if(dnLen == -1)
            break;

        ptr += dnLen;
        if(ptr + DNS_RRFIXEDSZ > buffer + len)
        {
            result = false;
            break;
        }

        int rrType      = (ptr[0] << 8) | ptr[1],
            rrClass     = (ptr[2] << 8) | ptr[3],
            rrLen       = (ptr[8] << 8) | ptr[9];
        ptr += DNS_RRFIXEDSZ;

        if(rrClass == ns_c_in && rrType == type)
        {
            string resultEntry;

            const u_char *strPtr = ptr;
            while(strPtr < ptr+rrLen)
            {
                u_char strLen = *strPtr++;
                if(strPtr+strLen > ptr+rrLen)
                {
                    result = false;
                    break;
                }

                resultEntry.append((const char *)strPtr, (size_t)strLen);

                strPtr += strLen;
            }

            if (!resultEntry.empty())
            {
                out.push_back(resultEntry);
            }
        }

        ptr += rrLen;
    }

    return(result);
}

/*
 * Lookup TXT record
 */
int DNS::TXTLookup(const string &domain, vector<string> &out, int type)
{
    out.clear();

    int answerLength;
    u_char szAnswerBuffer[DNS_BUFFERSIZE];

    // prepare res
    struct __res_state res;
    res_ninit(&res);

    // lookup
    answerLength = res_nquery(&res, domain.c_str(), ns_c_in, type, szAnswerBuffer, sizeof(szAnswerBuffer));
    if(answerLength <= 0)
        return(0);
    else if(answerLength > (int)sizeof(szAnswerBuffer))
        return(0);

    // parse
    if(!DNS::ParseTXTAnswer(szAnswerBuffer, answerLength, out, type))
        return(0);

    return((int)out.size());
}

/*
 * PTR lookup
 */
int DNS::PTRLookup(const string &ip, vector<string> &out)
{
    out.clear();

    // build search domain
    IPAddress theIP(ip);

    string domain = theIP.toReversedString();

    if(theIP.isIPv6)
    {
        domain += string(".ip6.arpa.");
    }
    else
    {
        domain += string(".in-addr.arpa.");
    }

    // prepare res
    struct __res_state res;
    res_ninit(&res);

    // ns vars
    int iResult;
    u_char szAnswerBuffer[DNS_BUFFERSIZE];

    // do MX lookup
    iResult = res_nquery(&res, domain.c_str(), ns_c_in, ns_t_ptr, szAnswerBuffer, sizeof(szAnswerBuffer));

    // result too big?
    if(iResult > (int)sizeof(szAnswerBuffer))
    {
        return(0);
    }

    // record found?
    if(iResult > 0)
    {
        DNS::ParsePTRAnswer(szAnswerBuffer, iResult, out);
    }

    return((int)out.size());
}

/*
 * Parse PTR DNS record from resolver answer
 */
bool DNS::ParsePTRAnswer(u_char *buffer, int len, vector<string> &out)
{
    bool result = true;
    char tmpBuffer[DNS_BUFFERSIZE];

    if(len < DNS_HFIXEDSZ)
        return(false);

    const HEADER *head = reinterpret_cast<const HEADER *>(buffer);
    unsigned int questionCount = ntohs(head->qdcount),
                 answerCount   = ntohs(head->ancount);

    if(questionCount != 1)
        return(false);

    if(answerCount == 0)
        return(false);

    int dnLen = dn_expand(buffer, buffer+len, buffer+DNS_HFIXEDSZ, tmpBuffer, sizeof(tmpBuffer));

    if(dnLen == -1)
        return(false);

    if(buffer + DNS_HFIXEDSZ + dnLen + DNS_QFIXEDSZ > buffer + len)
        return(false);

    const u_char *ptr = buffer + DNS_HFIXEDSZ + dnLen + DNS_QFIXEDSZ;
    for(unsigned int i = 0; i<answerCount; i++)
    {
        dnLen = dn_expand(buffer, buffer+len, ptr, tmpBuffer, sizeof(tmpBuffer));
        if(dnLen == -1)
            break;

        ptr += dnLen;
        if(ptr + DNS_RRFIXEDSZ > buffer + len)
        {
            result = false;
            break;
        }

        int rrType      = (ptr[0] << 8) | ptr[1],
            rrClass     = (ptr[2] << 8) | ptr[3],
            rrLen       = (ptr[8] << 8) | ptr[9];
        ptr += DNS_RRFIXEDSZ;

        if(rrClass == ns_c_in && rrType == ns_t_ptr)
        {
            if(rrLen < 2)
            {
                result = false;
                break;
            }

            dnLen = dn_expand(buffer, buffer+len, ptr, tmpBuffer, sizeof(tmpBuffer));
            if(dnLen == -1)
                break;

            out.push_back(tmpBuffer);
        }

        ptr += rrLen;
    }

    return(result);
}

/*
 * TLSA lookup
 */
int DNS::TLSALookup(const string &domain, unsigned short port, vector<TLSARecord> &out)
{
    out.clear();

    std::stringstream sstr;
    sstr << "_" << port << "._tcp." << domain;

    std::string lookupDomain = sstr.str();

    // prepare res
    struct __res_state res;
    res_ninit(&res);

    // enable DNSSEC since without it, TLSA does not make sense
    res.options |= RES_USE_EDNS0 | RES_USE_DNSSEC;

    // ns vars
    int answerLength;
    u_char szAnswerBuffer[DNS_BUFFERSIZE];

    // do TLSA lookup
    answerLength = res_nquery(&res, lookupDomain.c_str(), ns_c_in, NS_T_TLSA, szAnswerBuffer, sizeof(szAnswerBuffer));
    if(answerLength <= 0)
        return(0);
    else if(answerLength > (int)sizeof(szAnswerBuffer))
        return(0);

    // record found?
    DNS::ParseTLSAAnswer(szAnswerBuffer, answerLength, out);

    return((int)out.size());
}

/*
 * Parse TLSA DNS record from resolver answer
 */
bool DNS::ParseTLSAAnswer(u_char *buffer, int len, vector<TLSARecord> &out)
{
    bool result = true;
    char tmpBuffer[DNS_BUFFERSIZE];

    if(len < DNS_HFIXEDSZ)
        return(false);

    const HEADER *head = reinterpret_cast<const HEADER *>(buffer);
    unsigned int questionCount = ntohs(head->qdcount),
                 answerCount   = ntohs(head->ancount);

    if(questionCount != 1)
        return(false);

    if(answerCount == 0)
        return(false);

    // check AD flag which indicates DNSSEC validation
    if(!head->ad)
        return(false);

    int dnLen = dn_expand(buffer, buffer+len, buffer+DNS_HFIXEDSZ, tmpBuffer, sizeof(tmpBuffer));

    if(dnLen == -1)
        return(false);

    if(buffer + DNS_HFIXEDSZ + dnLen + DNS_QFIXEDSZ > buffer + len)
        return(false);

    const u_char *ptr = buffer + DNS_HFIXEDSZ + dnLen + DNS_QFIXEDSZ;
    for(unsigned int i = 0; i<answerCount; i++)
    {
        dnLen = dn_expand(buffer, buffer+len, ptr, tmpBuffer, sizeof(tmpBuffer));
        if(dnLen == -1)
            break;

        ptr += dnLen;
        if(ptr + DNS_RRFIXEDSZ > buffer + len)
        {
            result = false;
            break;
        }

        int rrType      = (ptr[0] << 8) | ptr[1],
            rrClass     = (ptr[2] << 8) | ptr[3],
            rrLen       = (ptr[8] << 8) | ptr[9];
        ptr += DNS_RRFIXEDSZ;

        if(rrClass == ns_c_in && rrType == NS_T_TLSA)
        {
            if(rrLen < 3)
            {
                result = false;
                break;
            }

            TLSARecord entry;
            entry.usage = static_cast<TLSARecord::Usage>(*(ptr+0));
            entry.selector = static_cast<TLSARecord::Selector>(*(ptr+1));
            entry.matchingType = static_cast<TLSARecord::MatchingType>(*(ptr+2));
            entry.value = std::string(reinterpret_cast<const char *>(ptr + 3), static_cast<std::size_t>(rrLen - 3));
            out.push_back(entry);
        }

        ptr += rrLen;
    }

    return(result);
}
#endif
