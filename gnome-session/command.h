/* command.h - the _GSM_Command protocol extension.

   Copyright 1999 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA.

   Authors: Felix Bellaby */

#ifndef COMMAND_H
#define COMMAND_H

#include "manager.h"

typedef enum
{
  COMMAND_ACTIVE = 0,
  COMMAND_HANDLE_WARNINGS,
  COMMAND_INACTIVE
} CommandState;

/* Data maintained for clients that speak the _GSM_Command protocol: */
struct _CommandData
{
  CommandState state;
  GSList *sessions;
};

extern GSList **client_lists[];
extern gchar  *events[];
extern GSList *zombie_list;
extern GSList *purge_drop_list;
extern GSList *purge_retain_list;

/* Returns the client that prints up warnings for gnome-session (if any). */
Client* get_warner (void);

/* Log reasons for an event with the client event selectors. */
void client_reasons (Client* client, gboolean confirm,
	gint count, gchar** reasons);

/* Log change in the state of a client with the client event selectors. */
void client_event (const gchar* handle, const gchar* event);

/* Log change in the properties of a client with the client event selectors. */
void client_property (const gchar* handle, int nprops, SmProp** props);

/* Create an object handle for use in the _GSM_Command protocol. */
gchar* command_handle_new (gpointer object);

/* Move an object handle from one object to another. */
gchar* command_handle_reassign (gchar* handle, gpointer new_object);

/* Free an object handle */
void command_handle_free (gchar* handle);

/* Clean up the _GSM_Command protocol for a client. */
void command_clean_up (Client* client);

/* TRUE when the _GSM_Command protocol is enabled for this client. */
gboolean command_active (Client* client);

/* If a logout command exits, exec() it and don't return */
void execute_logout (void);

/* Set the logout command.  */
void set_logout_command (char **command);

/* Process a _GSM_Command protocol message. */
void command (Client* client, int nprops, SmProp** props);

#endif /* COMMAND_H */
