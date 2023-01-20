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

#include <imap/imap.h>

void IMAP::StartTLS(char *szLine)
{
    db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] STARTTLS",
                                               this->strPeer.c_str()));

    if(bTLSMode)
    {
        printf("%s NO Already in TLS mode\r\n",
               this->szTag);
        return;
    }

    printf("%s OK STARTTLS completed\r\n",
           this->szTag);

    // initialize TLS mode
    char szError[255];
    if(utils->BeginTLS(ssl_ctx, szError))
    {
        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] TLS negotiation succeeded",
                                                   this->strPeer.c_str()));

        bTLSMode = true;
        bIO_SSL = true;
    }
    else
    {
        bIO_SSL = false;

        db->Log(CMP_IMAP, PRIO_NOTE, utils->PrintF("[%s] TLS negotiation failed (%s)",
                                                   this->strPeer.c_str(),
                                                   szError));
        printf("%s NO %s\r\n",
               this->szTag,
               szError);
    }
}
