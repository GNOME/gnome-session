/* manager.h - Definitions for session manager.

   Copyright (C) 1998 Tom Tromey

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

#include <X11/SM/SMlib.h>
#include <time.h>

#include "glib.h"

/* Default session name.  */
#define DEFAULT_SESSION "Default"

/* Each client is represented by one of these.  Note that the client's
   state is not kept explicitly.  A client is on only one of several
   linked lists at a given time; the state is implicit in the list.  */
typedef struct
{
  /* Client's session id.  */
  char *id;

  /* Client's connection.  */
  SmsConn connection;

  /* List of all properties for this client.  Each element of the list
     is an `SmProp *'.  */
  GSList *properties;

  /* Used to detect clients which are dying quickly */
  time_t connect_time;

  /* Used to determine order in which clients are started */
  guint priority;

  /* Used to avoid registering clients with ids from default.session */
  gboolean faked_id;
} Client;

/* Milliseconds to wait for clients to register before assuming that
 * they have finished any initialisation needed by other clients. */
extern guint purge_delay;

/*
 * manager.c
 */

/* Start an individual client. */
void start_client (Client* client);

/* Starts a list of clients in order of their priority. */
void load_session (GSList* clients);

/* Run the Discard, Resign or Shutdown command on a client.
 * Returns the pid or -1 if unsuccessful. */
gint run_command (const Client* client, const gchar* command);

/* Call this to initiate a session save, and perhaps a shutdown.
   Save requests are queued internally. */
void save_session (int save_type, gboolean shutdown, int interact_style,
		   gboolean fast);

/* Returns true if shutdown in progress, false otherwise.  Note it is
   possible for this function to return true and then later return
   false -- the shutdown might be cancelled.  */
int shutdown_in_progress_p (void);

/* This is called via ICE when a new client first connects.  */
Status new_client (SmsConn connection, SmPointer data, unsigned long *maskp,
		   SmsCallbacks *callbacks, char **reasons);

/* Find a client from a list */
Client* find_client_by_id (const GSList *list, const char *id);

/* Handler for IO errors on an ICE connection.  This just cleans up
   after the client.  */
void io_error_handler (IceConn connection);

/*
 * save.c
 */

/* Attempts to set the session name (the requested name may be locked).
 * Returns the name that has been assigned to the session. */
gchar* set_session_name (const gchar *name);

/* Releases the lock on the session name when shutting down the session */
void unlock_session (void);

/* Write current session to the config file. */
void write_session (void);

/* Load a session from our configuration by name.
 * The first time that this is called it establishes the name for the
 * base session.
 * An empty base session is filled out with the clients in default.session.
 * Later calls merge the requested session into the base session.
 * Returns TRUE when some clients were loaded. */
int read_session (const char *name);

/* Delete a session from the config file and discard any stale
 * session info saved by clients that were in the session. */
void delete_session (const char *name);

/*
 * ice.c
 */

/* Call this to initialize the ICE part of the session manager.
   Returns 1 on success, 0 on error.  */
int initialize_ice (void);

/* Call this to clean up ICE when exiting.  */
void clean_ice (void);

/*
 * prop.c
 */

/* Call this to find the named property for a client.  Returns NULL if
   not found.  */
SmProp *find_property_by_name (const Client *client, const char *name);

/* Find property NAME attached to CLIENT.  If not found, or type is
   not CARD8, then return FALSE.  Otherwise set *RESULT to the value
   and return TRUE.  */
gboolean find_card8_property (const Client *client, const char *name,
			      int *result);

/* Find property NAME attached to CLIENT.  If not found, or type is
   not ARRAY8, then return FALSE.  Otherwise set *RESULT to the value
   and return TRUE.  *RESULT is malloc()d and must be freed by the
   caller.  */
gboolean find_string_property (const Client *client, const char *name,
			       char **result);

/* Find property NAME attached to CLIENT.  If not found, or type is
   not LISTofARRAY8, then return FALSE.  Otherwise set *ARGCP to the
   number of vector elements, *ARGVP to the elements themselves, and
   return TRUE.  Each element of *ARGVP is malloc()d, as is *ARGVP
   itself.  You can use `free_vector' to free the result.  */
gboolean find_vector_property (const Client *client, const char *name,
			       int *argcp, char ***argvp);

/* Free the return result from find_vector_property.  */
void free_vector (int argc, char **argv);

#endif /* MANAGER_H */
