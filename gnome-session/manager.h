/* manager.h - Definitions for session manager.

   Copyright (C) 1998, 1999 Tom Tromey

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA.  */

#ifndef MANAGER_H
#define MANAGER_H

#include "headers.h"

/* Start an individual client. */
void start_client (Client* client);

/* Start a list of clients in priority order. */
void start_clients (GSList* client_list);

/* Remove a client from the session (completely).
 * Returns 1 on success, 0 when the client was not found in the current 
 * session and -1 when a save is in progress. */
gint remove_client (Client* client);

/* Free the memory used by a client. */
void free_client (Client* client);

/* Run the Discard, Resign or Shutdown command on a client.
 * Returns the pid or -1 if unsuccessful. */
gint run_command (Client* client, const gchar* command);

/* Call this to initiate a session save, and perhaps a shutdown.
   Save requests are queued internally. */
void save_session (int save_type, gboolean shutdown, int interact_style,
		   gboolean fast);

/* This is called via ICE when a new client first connects.  */
Status new_client (SmsConn connection, SmPointer data, unsigned long *maskp,
		   SmsCallbacks *callbacks, char **reasons);

/* Find a client from a list */
Client* find_client_by_id (const GSList *list, const char *id);

/* This is used to send properties to clients that use the _GSM_Command
 * protocol extension. */
void send_properties (Client* client, GSList* prop_list);

#endif /* MANAGER_H */
