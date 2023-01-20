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

#ifndef _CORE_DNS_H_
#define _CORE_DNS_H_

#include <core/core.h>

#ifdef WIN32
#include <windns.h>
#define ns_t_txt    DNS_TYPE_TEXT
#else
#include <resolv.h>
#include <arpa/nameser_compat.h>
#endif

namespace Core
{
    class MXServer
    {
    public:
        MXServer()
            : Preference(0)
            , RandomValue(0)
            , Authenticated(false)
        {}

        string Hostname;
        int Preference;
        int RandomValue;
        bool Authenticated;
    };

    struct TLSARecord
    {
        enum Usage
        {
            TLSA_CA_CONSTRAINT              = 0,
            TLSA_SERVICE_CERT_CONSTRAINT    = 1,
            TLSA_TRUST_ANCHOR_ASSERTION     = 2,
            TLSA_DOMAIN_ISSUED_CERT         = 3
        };

        enum Selector
        {
            TLSA_FULL_CERT                  = 0,
            TLSA_SUBJECT_PK_INFO            = 1
        };

        enum MatchingType
        {
            TLSA_UNHASHED                   = 0,
            TLSA_SHA256                     = 1,
            TLSA_SHA512                     = 2
        };

        Usage usage;
        Selector selector;
        MatchingType matchingType;
        string value;
    };

    class DNS
    {
    public:
        static int ALookup(const string &domain, vector<in_addr_t> &out);
        static int AAALookup(const string &domain, vector<in6_addr> &out);

        static int MXLookup(const string &domain, vector<MXServer> &out, bool implicit = true, bool useDNSSEC = false);
        static bool MXServerSort(const MXServer &a, const MXServer &b);

        static int TXTLookup(const string &domain, vector<string> &out, int type = ns_t_txt);

        static int PTRLookup(const string &ip, vector<string> &out);

        static int TLSALookup(const string &domain, unsigned short port, vector<TLSARecord> &out);

#ifndef WIN32
        static bool ParseMXAnswer(u_char *buffer, int len, vector<MXServer> &out, bool useDNSSEC);
        static bool ParseTXTAnswer(u_char *buffer, int len, vector<string> &out, int type);
        static bool ParsePTRAnswer(u_char *buffer, int len, vector<string> &out);
        static bool ParseTLSAAnswer(u_char *buffer, int len, vector<TLSARecord> &out);
#endif
    };
};

#endif
