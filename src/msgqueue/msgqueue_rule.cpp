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

#include <msgqueue/msgqueue.h>
#include <msgqueue/deliveryrules.h>

/*
 * Process rule-controlled item
 */
void MSGQueue::ProcessRule(MSGQueueItem *item, MSGQueueResult *result, const DeliveryRule *rule)
{
    RelayServerInfo rs;

    switch(rule->target)
    {
    case MESSAGE_TARGET_INBOUND:
        this->ProcessInbound(item, result);
        break;

    case MESSAGE_TARGET_SENDMAIL:
        this->DeliverOutboundToSendmail(item, result, rule->targetParam.c_str());
        break;

    case MESSAGE_TARGET_SMTPRELAY:
        rule->ParseTargetRelayServer(rs);
        this->DeliverOutboundToSMTPRelay(item, result, rs);
        break;

    case MESSAGE_TARGET_DELIVER_SELF:
        this->DeliverOutbound(item, result);
        break;

    default:
        throw DeliveryException("ProcessRule",
            "Unknown rule target.",
            QUEUE_STATUS_TEMPORARY_ERROR,
            "4.3.5");
        break;
    };
}
