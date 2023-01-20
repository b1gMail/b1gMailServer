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

#include <smtp/spf.h>
#include <core/dns.h>
#include <algorithm>
#include <stdint.h>

#define SPF_LOOKUPLIMIT     20

/*
 * Constructor
 */
SPF::SPF(const string &heloDomain, const string &clientIP, const string &clientHost)
{
    this->heloDomain    = heloDomain;
    this->clientIP      = clientIP;
    this->clientHost    = clientHost;
    this->myDomain      = cfg->Get("b1gmta_host");
}

/*
 * SPF check_host function according to rfc 4408
 */
SPFResult SPF::CheckHost(const string &ip, const string &domain, string sender, string &explanation, bool initial)
{
    // check for endless recursion
    if(initial)
    {
        this->lookupCounter = 0;
        this->checkedHosts.clear();
        explanation.clear();
    }
    else
    {
        this->lookupCounter++;
    }
    for(unsigned int i=0; i<this->checkedHosts.size(); i++)
    {
        if(strcasecmp(this->checkedHosts.at(i).c_str(), domain.c_str()) == 0)
        {
            db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("SPF::CheckHost(): Recursion (domain: %s), returning PermError",
                domain.c_str()));
            return(SPF_RESULT_PERMERROR);
        }
    }
    this->checkedHosts.push_back(domain);

    // check for lookup limit
    if(this->lookupCounter > SPF_LOOKUPLIMIT)
    {
        db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("SPF::CheckHost(): Lookup limit reached, returning PermError"));
        return(SPF_RESULT_PERMERROR);
    }

    // checks according to rfc 4408 section 4.3
    if(domain.length() > 63)
    {
        db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("SPF::CheckHost(): Domain %s is too long",
            domain.c_str()));
        return(SPF_RESULT_NONE);
    }

    if(domain.length() == 0)
    {
        db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("SPF::CheckHost(): Domain is empty"));
        return(SPF_RESULT_NONE);
    }

    if(domain.at(domain.length()-1) != '.')
    {
        db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("SPF::CheckHost(): Domain has no zero-length label at its end"));
        return(SPF_RESULT_NONE);
    }

    if(sender.find('@') == string::npos)
        sender = string("postmaster@") + sender;
    else if(sender.find('@') == 0)
        sender = string("postmaster") + sender;

    // lookup
    string spfRecord;
    if(!this->LookupRecord(domain, spfRecord))
    {
        db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("SPF::CheckHost(): No SPF record for domain %s, returning None",
            domain.c_str()));
        return(SPF_RESULT_NONE);
    }

    // split
    bool evaluating = true;
    char resultQualifier = '?';     // default result according to rfc 4408 section 3.7
    string redirectDomain, expDomain;
    vector<string> spfItems;
    utils->ExplodeOutsideOfQuotation(spfRecord, spfItems, ' ');
    for(unsigned int i=0; i<spfItems.size(); i++)
    {
        string item = spfItems.at(i);

        if(item == string("v=spf1"))
            continue;

        if(item.length() > 9 && item.substr(0, 9) == string("redirect="))
        {
            redirectDomain = item.substr(9);
            if(redirectDomain.length() > 0 && redirectDomain.at(redirectDomain.length()-1) != '.')
                redirectDomain.append(1, '.');

            bool syntaxError;
            redirectDomain = this->ExpandMacro(redirectDomain, domain, ip, sender, false, syntaxError);
            if(syntaxError)
            {
                db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("SPF::CheckHost(): Syntax error while expanding redirect= macro, returning PermError"));
                return(SPF_RESULT_PERMERROR);
            }
        }

        else if(item.length() > 4 && item.substr(0, 4) == string("exp="))
        {
            expDomain = item.substr(4);
            if(expDomain.length() > 0 && expDomain.at(expDomain.length()-1) != '.')
                expDomain.append(1, '.');

            bool syntaxError;
            expDomain = this->ExpandMacro(expDomain, domain, ip, sender, false, syntaxError);
            if(syntaxError)
            {
                db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("SPF::CheckHost(): Syntax error while expanding exp= macro, returning PermError"));
                return(SPF_RESULT_PERMERROR);
            }
        }

        else if(evaluating)
        {
            // extract qualifier
            char qualifier = item.at(0);
            switch(qualifier)
            {
            case '+':
            case '-':
            case '~':
            case '?':
                item.erase(0, 1);
                break;

            default:
                qualifier = '+';
                break;
            };

            SPFResult error;
            bool match = this->EntryMatches(item, domain, ip, sender, error);
            if(error != SPF_RESULT_NONE)
            {
                db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("SPF::CheckHost(): Error %d returned by EntryMatches()",
                    (int)error));
                return(error);
            }
            if(match)
            {
                resultQualifier = qualifier;
                evaluating = false;
            }
        }
    }

    // lookup limit?
    if(this->lookupCounter >= SPF_LOOKUPLIMIT)
    {
        db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("SPF::CheckHost(): Lookup limit reached (2), returning PermError"));
        return(SPF_RESULT_PERMERROR);
    }

    // redirect action?
    if(evaluating && redirectDomain.length() > 0)
    {
        return(this->CheckHost(ip, redirectDomain, sender, explanation, false));
    }

    // explanation
    if(initial && resultQualifier == '-' && expDomain.length() > 0)
    {
        vector<string> txtRecords;
        if(DNS::TXTLookup(expDomain, txtRecords) > 0)
        {
            bool syntaxError;

            for(unsigned int i=0; i<txtRecords.size(); i++)
                explanation += txtRecords.at(i);

            explanation = this->ExpandMacro(explanation, domain, ip, sender, true, syntaxError);
            if(syntaxError)
                explanation.clear();
        }
    }

    if(resultQualifier == '+')
    {
        db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("SPF::CheckHost(): Returning Pass"));
        return(SPF_RESULT_PASS);
    }
    else if(resultQualifier == '-')
    {
        db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("SPF::CheckHost(): Returning Fail"));
        return(SPF_RESULT_FAIL);
    }
    else if(resultQualifier == '~')
    {
        db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("SPF::CheckHost(): Returning SoftFail"));
        return(SPF_RESULT_SOFTFAIL);
    }
    else
    {
        db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("SPF::CheckHost(): Returning Neutral"));
        return(SPF_RESULT_NEUTRAL);
    }
}

/*
 * Check if an SPF entry matches
 */
bool SPF::EntryMatches(const string &entry_, const string &domain, const string &ip, const string &sender, SPFResult &error)
{
    IPAddress myIP(ip);
    if(myIP.isMappedIPv4())
        myIP = myIP.mappedIPv4();

    bool result = false, syntaxError;
    error = SPF_RESULT_NONE;

    // lower case conversion
    string entry = entry_;
    for(unsigned int i=0; i<entry.length(); i++)
        entry.at(i) = tolower(entry.at(i));

    // 'all' always matches
    if(entry == string("all"))
        return(true);

    // 'include' calls check_host recursively
    else if(entry.length() > 8 && entry.substr(0, 8) == string("include:"))
    {
        string newDomain = entry.substr(8);

        if(newDomain.at(newDomain.length()-1) != '.')
            newDomain.append(1, '.');

        newDomain = this->ExpandMacro(newDomain, domain, ip, sender, false, syntaxError);
        if(syntaxError)
        {
            db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("SPF::EntryMatches(): Syntax error in include: macro expansion, returning PermError"));
            error = SPF_RESULT_PERMERROR;
            return(false);
        }

        string explanation;
        SPFResult recursiveResult = this->CheckHost(ip, newDomain, sender, explanation, false);

        switch(recursiveResult)
        {
        case SPF_RESULT_PASS:
            result = true;
            break;

        case SPF_RESULT_FAIL:
        case SPF_RESULT_SOFTFAIL:
        case SPF_RESULT_NEUTRAL:
            result = false;
            break;

        case SPF_RESULT_TEMPERROR:
            result = false;
            error = SPF_RESULT_TEMPERROR;
            break;

        case SPF_RESULT_PERMERROR:
        case SPF_RESULT_NONE:
        default:
            result = false;
            error = SPF_RESULT_PERMERROR;
            break;
        };
    }

    // 'a'
    else if(entry.length() >= 1 && entry.substr(0, 1) == string("a"))
    {
        // TODO: support cidr / dual cidr
        if(this->lookupCounter++ < SPF_LOOKUPLIMIT)
        {
            string aDomain = domain;
            if(entry.length() > 1 && entry.substr(0, 2) == string("a:"))
                aDomain = entry.substr(2);

            if(aDomain.length() > 0 && aDomain.at(aDomain.length()-1) != '.')
                aDomain.append(1, '.');

            if(!myIP.isIPv6)
            {
                vector<in_addr_t> ipAddresses;
                DNS::ALookup(aDomain, ipAddresses);

                for(unsigned int i=0; i<ipAddresses.size(); i++)
                {
                    if(ipAddresses.at(i) == myIP.v4Addr.s_addr)
                    {
                        result = true;
                        break;
                    }
                }
            }
            else
            {
                vector<in6_addr> ipAddresses;
                DNS::AAALookup(aDomain, ipAddresses);

                for(vector<in6_addr>::iterator it = ipAddresses.begin(); it != ipAddresses.end(); ++it)
                {
                    in6_addr compareAddr = *it;
                    if(memcmp(&myIP.v6Addr, &compareAddr, sizeof(compareAddr)) == 0)
                    {
                        result = true;
                        break;
                    }
                }
            }
        }
    }

    // 'mx'
    else if(entry.length() >= 2 && entry.substr(0, 2) == string("mx"))
    {
        if(this->lookupCounter++ < SPF_LOOKUPLIMIT)
        {
            string mxDomain = domain;
            if(entry.length() > 2 && entry.substr(0, 3) == string("mx:"))
                mxDomain = entry.substr(3);
            if(mxDomain.length() > 0 && mxDomain.at(mxDomain.length()-1) != '.')
                mxDomain.append(1, '.');

            vector<MXServer> mxServers;
            if(DNS::MXLookup(mxDomain, mxServers, false) > 0)
            {
                for(unsigned int i=0; i<mxServers.size(); i++)
                {
                    if(i >= 10) // rfc 4408 section 5.4. (DoS prevention)
                        break;

                    if(!myIP.isIPv6)
                    {
                        vector<in_addr_t> ipAddresses;
                        DNS::ALookup(mxServers.at(i).Hostname, ipAddresses);
                        for(unsigned int j=0; j<ipAddresses.size(); j++)
                        {
                            in_addr_t mxIP = ipAddresses.at(j);

                            if(myIP.v4Addr.s_addr == mxIP)
                            {
                                result = true;
                                break;
                            }
                        }
                    }
                    else
                    {
                        vector<in6_addr> ipAddresses;
                        DNS::AAALookup(mxServers.at(i).Hostname, ipAddresses);
                        for(vector<in6_addr>::iterator it = ipAddresses.begin(); it != ipAddresses.end(); ++it)
                        {
                            in6_addr mxIP = *it;

                            if(memcmp(&myIP.v6Addr, &mxIP, sizeof(in6_addr)) == 0)
                            {
                                result = true;
                                break;
                            }
                        }
                    }

                    if(result)
                        break;
                }
            }
        }
    }

    // 'ptr'
    else if(entry.length() >= 3 && entry.substr(0, 3) == string("ptr"))
    {
        if(this->lookupCounter++ < SPF_LOOKUPLIMIT)
        {
            string domainSpec;

            if(entry.length() >=4 && entry.substr(0, 4) == string("ptr:"))
                domainSpec = this->ExpandMacro(entry.substr(4), domain, ip, sender, false, syntaxError);
            else
                domainSpec = domain;

            if(domainSpec.length() > 0 && domainSpec.at(domainSpec.length()-1)=='.')
                domainSpec = domainSpec.substr(0, domainSpec.length()-1);

            // PTR lookup
            vector<string> PTRs, validatedPTRs;
            DNS::PTRLookup(ip, PTRs);
            for(unsigned int i=0; i<PTRs.size(); i++)
            {
                if(i >= 10)
                    break;

                if(!myIP.isIPv6)
                {
                    // A lookup
                    vector<in_addr_t> ptrIPs;
                    DNS::ALookup(PTRs.at(i), ptrIPs);

                    for(vector<in_addr_t>::iterator it = ptrIPs.begin(); it != ptrIPs.end(); ++it)
                    {
                        if(*it == myIP.v4Addr.s_addr)
                        {
                            validatedPTRs.push_back(PTRs.at(i));
                            break;
                        }
                    }
                }
                else
                {
                    // AAA lookup
                    vector<in6_addr> ptrIPs;
                    DNS::AAALookup(PTRs.at(i), ptrIPs);

                    for(vector<in6_addr>::iterator it = ptrIPs.begin(); it != ptrIPs.end(); ++it)
                    {
                        in6_addr compareIP = *it;
                        if(memcmp(&myIP.v6Addr, &compareIP, sizeof(compareIP)) == 0)
                        {
                            validatedPTRs.push_back(PTRs.at(i));
                            break;
                        }
                    }
                }
            }

            // check validated PTRs
            for(vector<string>::iterator it = validatedPTRs.begin(); it != validatedPTRs.end(); ++it)
            {
                if(strcasecmp((*it).c_str(), domainSpec.c_str()) == 0)
                {
                    result = true;
                    break;
                }

                if((*it).length() > domainSpec.length())
                {
                    if(strcasecmp((*it).substr( (*it).length()-domainSpec.length() ).c_str(), domainSpec.c_str()) == 0)
                    {
                        result = true;
                        break;
                    }
                }
            }
        }
    }

    // 'ip4'
    else if(entry.length() > 4 && entry.substr(0, 4) == string("ip4:"))
    {
        if(!myIP.isIPv6)
        {
            string validIP = entry.substr(4);
            result = myIP.matches(validIP);
        }
        else
            result = false;
    }

    // 'ip6'
    else if(entry.length() > 4 && entry.substr(0, 4) == string("ip6:"))
    {
        if(myIP.isIPv6)
        {
            // TODO: check RFC
            string validIP = entry.substr(4);
            result = myIP.matches(validIP);
        }
        else
            result = false;
    }

    // 'exists'
    else if(entry.length() > 7 && entry.substr(0, 7) == string("exists:"))
    {
        if(this->lookupCounter++ < SPF_LOOKUPLIMIT)
        {
            string hostName = entry.substr(7);

            hostName = this->ExpandMacro(hostName, domain, ip, sender, false, syntaxError);

            if(!hostName.empty() && hostName.at(hostName.length() - 1) != '.')
                hostName += ".";

            vector<in_addr_t> lookupEntries;
            int lookupResult = DNS::ALookup(hostName, lookupEntries);

            result = (lookupResult > 0);
        }
    }

    // unknown
    else
    {
    }

    return(result);
}

/*
 * Expand SPF macro
 */
string SPF::ExpandMacro(const string &in, const string &domain, const string &ip, const string &sender, bool exp, bool &syntaxError)
{
    string result;
    syntaxError = false;

    for(unsigned int i=0; i<in.length(); i++)
    {
        char c = in.at(i), c2 = 0;

        if(i+1 < in.length())
            c2 = in.at(i+1);

        if(c == '%')
        {
            string macroLetters;

            if(c2 == '_')
            {
                result.append(1, ' ');
                i++;
            }
            else if(c2 == '-')
            {
                result.append("%20");
                i++;
            }
            else if(c2 == '%')
            {
                result.append(1, '%');
                i++;
            }
            else if(c2 == 'x')
            {
                i++;

                if(i+2 < in.length())
                {
                    char hexStr[3];
                    hexStr[0] = in.at(i+1);
                    hexStr[1] = in.at(i+2);
                    hexStr[2] = 0;

                    int theChar;
                    if(sscanf(hexStr, "%x", &theChar) == 1
                        && ((theChar >= 0x21 && theChar <= 0x24)
                            || (theChar >= 0x26 && theChar <= 0x7e)))
                    {
                        result.append(1, (unsigned char)theChar);
                    }
                    else
                    {
                        db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("SPF::ExpandMacro(): Syntax error (%%x)"));
                        syntaxError = true;
                        break;
                    }

                    i += 2;
                }
            }
            else if(c2 == '{')
            {
                i++;

                for(i=i+1; i<in.length(); i++)
                {
                    if(in.at(i) == '}')
                        break;
                    macroLetters.append(1, in.at(i));
                }
            }
            else if(c2 != 0)
            {
                macroLetters.append(1, c2);
            }

            if(macroLetters.length() > 0)
            {
                size_t atPos;
                string expansionResult;
                char macroLetter = macroLetters.at(0);

                switch(macroLetter)
                {
                case 's':
                    expansionResult = sender;
                    break;

                case 'l':
                    atPos = sender.find('@');
                    if(atPos == string::npos)
                    {
                        expansionResult = sender;
                    }
                    else if(atPos > 0)
                    {
                        expansionResult = sender.substr(0, atPos);
                    }
                    break;

                case 'o':
                    atPos = sender.find('@');
                    if(atPos == string::npos)
                    {
                        expansionResult = sender;
                    }
                    else if(atPos > 0)
                    {
                        expansionResult = sender.substr(atPos+1);
                    }
                    break;

                case 'd':
                    expansionResult = domain;
                    if(expansionResult.length() > 0 && expansionResult.at(expansionResult.length()-1) == '.')
                        expansionResult.erase(expansionResult.length()-1);
                    break;

                case 'i':
                    expansionResult = ip;
                    break;

                case 'p':
                    expansionResult = this->clientHost;
                    break;

                case 'v':
                    if(ip.find(':') != string::npos)
                        expansionResult = "ip6";
                    else
                        expansionResult = "in-addr";
                    break;

                case 'h':
                    expansionResult = this->heloDomain;
                    break;

                case 'c':
                    if(exp)
                        expansionResult = this->clientIP;
                    else
                    {
                        db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("SPF::ExpandMacro(): Syntax error (%%c in non-exp mode)"));
                        syntaxError = true;
                    }
                    break;

                case 'r':
                    if(exp)
                        expansionResult = this->myDomain;
                    else
                    {
                        db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("SPF::ExpandMacro(): Syntax error (%%r in non-exp mode)"));
                        syntaxError = true;
                    }
                    break;

                case 't':
                    if(exp)
                    {
                        char timestampString[16];
                        snprintf(timestampString, 16, "%u", (unsigned int)time(NULL));
                        expansionResult = timestampString;
                    }
                    else
                    {
                        db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("SPF::ExpandMacro(): Syntax error (%%t in non-exp mode)"));
                        syntaxError = true;
                    }
                    break;

                default:
                    syntaxError = true;
                    break;
                };

                if(syntaxError)
                    break;

                if(macroLetters.length() > 1)
                {
                    macroLetters.erase(0, 1);

                    string numbers, delimiters;
                    bool doReverse = false, doSplit = false;
                    while(macroLetters.length() > 0 && !syntaxError)
                    {
                        macroLetter = macroLetters.at(0);
                        macroLetters.erase(0, 1);

                        if(macroLetter >= '0' && macroLetter <= '9')
                        {
                            numbers.append(1, macroLetter);
                            doSplit = true;
                        }
                        else switch(macroLetter)
                        {
                        // valid delimiters
                        case '.':
                        case '-':
                        case '+':
                        case ',':
                        case '/':
                        case '_':
                        case '=':
                            delimiters.append(1, macroLetter);
                            doSplit = true;
                            break;

                        // 'r'
                        case 'r':
                            // reverse expansion result
                            doReverse = true;
                            doSplit = true;
                            break;

                        // garbage
                        default:
                            db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("SPF::ExpandMacro(): Syntax error (invalid char in macroLetters)"));
                            syntaxError = true;
                            break;
                        };
                    }

                    if(doSplit)
                    {
                        if(delimiters.length() == 0)
                            delimiters = string(".");

                        // split
                        vector<string> splitItems;
                        char *strBuffer = mstrdup(expansionResult.c_str());
                        char *savePtr, *pch = strtok_r(strBuffer, delimiters.c_str(), &savePtr);
                        while(pch != NULL)
                        {
                            splitItems.push_back(string(pch));
                            pch = strtok_r(NULL, delimiters.c_str(), &savePtr);
                        }
                        free(strBuffer);

                        // reverse?
                        if(doReverse)
                            reverse(splitItems.begin(), splitItems.end());

                        // remove parts?
                        if(numbers.length() > 0)
                        {
                            unsigned int num;
                            if(sscanf(numbers.c_str(), "%u", &num) == 1 && num > 0)
                            {
                                if(num > splitItems.size())
                                    num = splitItems.size();
                                int numDel = splitItems.size() - num;
                                while(numDel > 0)
                                {
                                    splitItems.erase(splitItems.begin());
                                    numDel--;
                                }
                            }
                        }

                        // implode
                        expansionResult.clear();
                        for(unsigned int j=0; j<splitItems.size(); j++)
                        {
                            if(expansionResult.length() > 0)
                                expansionResult.append(1, '.');
                            expansionResult += splitItems.at(j);
                        }
                    }
                }

                result += expansionResult;
            }
            else
            {
                db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("SPF::ExpandMacro(): macroLetters empty"));
                syntaxError = true;
            }
        }
        else
        {
            result.append(1, c);
        }
    }

    return(result);
}

/*
 * Lookup SPF record for domain: search TXT and SPF records and return first valid SPF string (v=spf1) found
 */
bool SPF::LookupRecord(const string &domain, string &result)
{
    vector<string> recordList;

    // lookup TXT first as the chance to have a TXT record seems to be higher
    int res = DNS::TXTLookup(domain, recordList, ns_t_txt);
    if(res == 0)
    {
        // no TXT, try SPF
        res = DNS::TXTLookup(domain, recordList, 99 /* rfc 4408 3.1.1 */);
    }
    if(res == 0)
    {
        db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("SPF::LookupRecord(): Neither TXT nor SPF record found for domain %s",
            domain.c_str()));
        return(false);
    }

    // find SPF record
    bool recordFound = false;
    for(unsigned int i=0; i<recordList.size(); i++)
    {
        if(recordList.at(i).length() < 7)
            continue;
        if(recordList.at(i).substr(0, 7) != "v=spf1 ")
            continue;

        recordFound = true;
        result = recordList.at(i);
        break;
    }

    if(!recordFound)
    {
        db->Log(CMP_SMTP, PRIO_DEBUG, utils->PrintF("SPF::LookupRecord(): No SPF record found in TXT/SPF response"));
        return(false);
    }

    return(recordFound);
}
