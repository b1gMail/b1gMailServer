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

/*
 * Process inbound item
 */
void MSGQueue::ProcessInbound(MSGQueueItem *item, MSGQueueResult *result)
{
    InboundProcess *proc = NULL;
    FILE *stream = NULL;

    try
    {
        // open message
        stream = fopen(this->QueueFileNameStr(item->id).c_str(), "rb");
        if(stream == NULL)
        {
            throw DeliveryException("ProcessInbound",
                "Failed to open queue message file.",
                QUEUE_STATUS_TEMPORARY_ERROR,
                "4.3.0");
        }

        // get process
        proc = this->inboundPool->getInboundProcess();
        proc->beginSession();

        // actually deliver
        proc->deliver(item->from, item->to, item->flags, stream);

        // close message
        fclose(stream);
        stream = NULL;

        // put back process
        this->inboundPool->putBackInboundProcess(proc);

        result->status      = QUEUE_STATUS_SUCCESS;
        result->statusInfo  = "Delivered to b1gMail pipe.";
        result->deliveredTo = "local user";
    }
    catch(DeliveryException &ex)
    {
        if(stream != NULL)
            fclose(stream);
        if(proc != NULL)
            delete proc;
        throw(ex);
    }
}
