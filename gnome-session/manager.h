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

#include "glib.h"

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
} Client;


/*
 * manager.c
 */

/* Call this to initiate a session save, and perhaps a shutdown.  This
   will do nothing if a save is already in progress.  */
void save_session (int save_type, gboolean shutdown, int interact_style,
		   gboolean fast);

/* Returns true if shutdown in progress, false otherwise.  Note it is
   possible for this function to return true and then later return
   false -- the shutdown might be cancelled.  */
int shutdown_in_progress_p (void);

/* This is called via ICE when a new client first connects.  */
Status new_client (SmsConn connection, SmPointer data, unsigned long *maskp,
		   SmsCallbacks *callbacks, char **reasons);

/* Handler for IO errors on an ICE connection.  This just cleans up
   after the client.  */
void io_error_handler (IceConn connection);

/* Declare a new zombie that was read from an init file.  */
void add_zombie (const char *id);

/*
 * save.c
 */

/* Write session contents to a file.  LIST1 and LIST2 are lists of
   `Client*'s.  Either can be NULL.  SHUTDOWN is true if shutting
   down.  */
void write_session (const GSList *list1, const GSList *list2, int shutdown);

/* Set name of the current session.  This is used to determine which
   file to save to.  */
void set_session_name (const char *name);

/* Start a session.  This does *not* shut down the current session; it
   simply adds the new one.  As a side effect it will set the current
   session name if it has not already been set.  If NAME is NULL then
   the `default' session is tried; if that session does not exist,
   then it is created using some internal defaults.  Returns 1 if
   any client was started, 0 otherwise.  */
int read_session (const char *name);

/* Delete a session as saved on disk.  This has no effect on the
   currently running applications.  */
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

/*
 * exec.c
 */

/* Execute some command in the background and immediately return.  DIR
   is the directory in which the command should be run; NULL means
   inherit the current directory from this process.  */
void execute_async (const char *dir, int argc, char *argv[]);

#endif /* MANAGER_H */
