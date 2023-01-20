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

#ifndef _MSGQUEUE_DELIVERYRULES_H_
#define _MSGQUEUE_DELIVERYRULES_H_

#include <core/core.h>
#include <msgqueue/msgqueue.h>

#define RULE_FLAG_CASELESS          1
#define RULE_FLAG_REGEXP            2

enum eRuleSubject
{
    RULE_SUBJECT_DOMAIN             = 0,
    RULE_SUBJECT_FROM               = 1,
    RULE_SUBJECT_TO                 = 2,
};

class DeliveryRule
{
public:
    DeliveryRule();
    ~DeliveryRule();

public:
    bool Matches(MSGQueueItem *item) const;
    void Compile();
    void ParseTargetRelayServer(RelayServerInfo &rs) const;

public:
    int id;
    eMessageType mailType;
    bool isRegexp;
    eRuleSubject subject;
    string rule;
    pcre *compiledRule;
    eMessageTarget target;
    string targetParam;
    int flags;

private:
    DeliveryRule(const DeliveryRule &);
    DeliveryRule &operator=(const DeliveryRule &);
};

class DeliveryRules
{
public:
    DeliveryRules() {}
    ~DeliveryRules();

public:
    static DeliveryRules *ReadFromDB(MySQL_DB *db);

public:
    const DeliveryRule *FindMatch(MSGQueueItem *item);

private:
    vector<DeliveryRule *> rules;

    DeliveryRules(const DeliveryRules &);
    DeliveryRules &operator=(const DeliveryRules &);
};

#endif
