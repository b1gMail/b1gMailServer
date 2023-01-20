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

#include <msgqueue/deliveryrules.h>
#include <stdexcept>
#include <sstream>

DeliveryRule::DeliveryRule()
{
    this->compiledRule = NULL;
}

DeliveryRule::~DeliveryRule()
{
    if(this->compiledRule != NULL)
    {
        pcre_free(this->compiledRule);
    }
}

bool DeliveryRule::Matches(MSGQueueItem *item) const
{
    bool result = false;

    string sbj;
    switch(this->subject)
    {
    case RULE_SUBJECT_FROM:
        sbj = item->from;
        if(sbj.empty())
            sbj = "<>";
        break;

    case RULE_SUBJECT_TO:
        sbj = item->to;
        if(sbj.empty())
            sbj = "<>";
        break;

    case RULE_SUBJECT_DOMAIN:
        sbj = item->domain;
        break;

    default:
        break;
    };

    if(this->compiledRule == NULL)
    {
        if((this->flags & RULE_FLAG_CASELESS) != 0)
            result = (strcasecmp(sbj.c_str(), this->rule.c_str()) == 0);
        else
            result = (sbj == this->rule);
    }
    else
    {
        int res, ovector[3];

        res = pcre_exec(this->compiledRule,
            NULL,
            sbj.c_str(),
            (int)sbj.length(),
            0,
            0,
            ovector,
            3);
        result = (res >= 0);
    }

    return(result);
}

DeliveryRules::~DeliveryRules()
{
    for(vector<DeliveryRule *>::iterator it = rules.begin();
        it != rules.end();
        ++it)
    {
        delete *it;
    }

    rules.clear();
}

void DeliveryRule::Compile()
{
    if(this->compiledRule != NULL)
    {
        pcre_free(this->compiledRule);
        this->compiledRule = NULL;
    }

    if((this->flags & RULE_FLAG_REGEXP) != 0)
    {
        const char *errorDescription;
        int errorOffset;

        this->compiledRule = pcre_compile(this->rule.c_str(),
            (this->flags & RULE_FLAG_CASELESS) != 0 ? PCRE_CASELESS : 0,
            &errorDescription,
            &errorOffset,
            NULL);
        if(this->compiledRule == NULL)
        {
            stringstream ss;
            ss << "Failed to compile delivery rule " << this->id << ": " << errorDescription << " (" << errorOffset << ")";
            throw runtime_error(ss.str());
        }
    }
}

void DeliveryRule::ParseTargetRelayServer(RelayServerInfo &rs) const
{
    rs.host         = "";
    rs.port         = 25;
    rs.requiresAuth = false;
    rs.user         = "";
    rs.pass         = "";

    string authPart, hostPart;
    size_t atPos = this->targetParam.find('@');
    if(atPos != string::npos)
    {
        authPart = this->targetParam.substr(0, atPos);
        hostPart = this->targetParam.substr(atPos+1);
    }
    else
    {
        hostPart = this->targetParam;
    }

    if(!authPart.empty())
    {
        size_t cPos = authPart.find(':');
        if(cPos != string::npos)
        {
            rs.user = authPart.substr(0, cPos);
            rs.pass = authPart.substr(cPos+1);
        }
        else
        {
            rs.user = authPart;
        }
        rs.requiresAuth = true;
    }

    size_t cPos = hostPart.find(':');
    if(cPos != string::npos)
    {
        rs.host = hostPart.substr(0, cPos);
        rs.port = atoi(hostPart.substr(cPos+1).c_str());
        if(rs.port == 0)
            rs.port = 25;
    }
    else
    {
        rs.host = hostPart;
    }
}

DeliveryRules *DeliveryRules::ReadFromDB(MySQL_DB *db)
{
    MySQL_Result *res;
    MYSQL_ROW row;

    DeliveryRules *result = new DeliveryRules;

    res = db->Query("SELECT `deliveryruleid`,`mail_type`,`rule_subject`,`rule`,`target`,`target_param`,`flags` FROM `bm60_bms_deliveryrules` ORDER BY `pos` ASC");
    while((row = res->FetchRow()) != NULL)
    {
        DeliveryRule *rule  = new DeliveryRule;
        rule->id            = atoi(row[0]);
        rule->mailType      = (eMessageType)atoi(row[1]);
        rule->subject       = (eRuleSubject)atoi(row[2]);
        rule->rule          = row[3];
        rule->target        = (eMessageTarget)atoi(row[4]);
        rule->targetParam   = row[5];
        rule->flags         = atoi(row[6]);

        try
        {
            rule->Compile();
            result->rules.push_back(rule);
        }
        catch(runtime_error &ex)
        {
            delete rule;
            db->Log(CMP_MSGQUEUE, PRIO_WARNING, utils->PrintF("%s", ex.what()));
        }
    }
    delete res;

    return(result);
}

const DeliveryRule *DeliveryRules::FindMatch(MSGQueueItem *item)
{
    for(vector<DeliveryRule *>::iterator it = this->rules.begin();
        it != this->rules.end();
        ++it)
    {
        if((*it)->Matches(item))
            return((const DeliveryRule *)(*it));
    }

    return(NULL);
}
