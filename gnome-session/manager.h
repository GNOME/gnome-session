/* manager.h - Definitions for session manager.
   Written by Tom Tromey <tromey@cygnus.com>.  */

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
void save_session (int save_type, Bool shutdown, int interact_style,
		   Bool fast);

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


/*
 * save.c
 */

/* Write session contents to a file.  LIST is a list of `Client*'s.
   SHUTDOWN is true if shutting down.  */
void write_session (GSList *list, int shutdown);

/* Set name of the current session.  This is used to determine which
   file to save to.  */
void set_session_name (char *name);

/* Start a session.  This does *not* shut down the current session; it
   simply adds the new one.  As a side effect it will set the current
   session name if it has not already been set.  If NAME is NULL then
   the `default' session is tried; if that session does not exist,
   then it is created using some internal defaults.  */
void read_session (char *name);


/*
 * ice.c
 */

/* Call this to initialize the ICE part of the session manager.
   Returns 1 on success, 0 on error.  */
int initialize_ice (void);

/* Call this to clean up ICE when exiting.  */
void clean_ice (void);

#endif /* MANAGER_H */
