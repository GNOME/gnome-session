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

#endif /* MANAGER_H */
