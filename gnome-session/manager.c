/* manager.c - Session manager back end.

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

#include <string.h>
#include <stdlib.h>

#include <libgnome/libgnome.h>
#include <gtk/gtk.h>

#include "manager.h"



/* This is true if we are saving the session.  */
static int saving = 0;

/* This is true if the current save is in response to a shutdown
   request.  */
static int shutting_down = 0;

/* List of all zombie clients.  A zombie client is one that was
   running in the previous session but has not yet been restored to
   life.  */
static GSList *zombie_list = NULL;

/* List of all live clients in the default state.  */
static GSList *live_list = NULL;

/* List of all clients waiting for the interaction token.  The head of
   the list actually has the token.  */
static GSList *interact_list = NULL;

/* List of all clients to which a `save yourself' message has been
   sent.  */
static GSList *save_yourself_list = NULL;

/* List of all clients which have requested a Phase 2 save.  */
static GSList *save_yourself_p2_list = NULL;

/* List of all clients which have been saved.  */
static GSList *save_finished_list = NULL;

/* List of all clients that have exited, but also set their style hint
   to RestartAnyway.  FIXME: should merge this with the zombie list,
   in case one of these clients starts up again.  */
static GSList *anyway_list = NULL;

/* List of clients to which a message has been sent during send_message: 
   needed to cope with io errors. */ 
static GSList *message_sent_list = NULL;



#define APPEND(List,Elt) ((List) = (g_slist_append ((List), (Elt))))
#define REMOVE(List,Elt) ((List) = (g_slist_remove ((List), (Elt))))
#define CONCAT(L1,L2) ((L1) = (g_slist_concat ((L1), (L2))))



typedef void message_func (SmsConn connection);
static void check_session_end (int found);

/* Send a message to every client on LIST.  */
static void
send_message (GSList **list, message_func *message)
{
  /* Protect against io_errors by using static lists */
  while (*list)
    {
      Client *client = (*list)->data;
      REMOVE (*list, client);
      APPEND (message_sent_list, client);
      (*message) (client->connection);
    }
  *list = message_sent_list;
  message_sent_list = NULL;
}



Client *
find_client_by_id (const GSList *list, const char *id)
{
  for (; list; list = list->next)
    {
      Client *client = (Client *) list->data;
      if (! strcmp (client->id, id))
	return client;
    }
  return NULL;
}

static Client *
find_client_by_connection (GSList *list, IceConn connection)
{
  for (; list; list = list->next)
    {
      Client *client = (Client *) list->data;
      if (SmsGetIceConnection (client->connection) == connection)
	return client;
    }
  return NULL;
}



#if 0
/* This is hard to debug using gdb */
static void
free_a_prop (gpointer data, gpointer user_data)
{
  SmProp *sp = (SmProp *) data;
  SmFreeProperty (sp);
}

static void
free_client (Client *client)
{
  if (! client)
    return;

  if (client->id)
    free (client->id);

  g_slist_foreach (client->properties, free_a_prop, NULL);
  g_slist_free (client->properties);

  free (client);
}
#else
static void
free_client (Client *client)
{
 GSList *list; 
 if (! client)
    return;

  if (client->id)
    free (client->id);

  for (list = client->properties; list; list = list->next)
    {
      SmProp *sp = (SmProp *) list->data;
      SmFreeProperty (sp);
    }
  g_slist_free (client->properties);

  free (client);
}
#endif


/* At shutdown time, we forcibly close each client connection.  This
   makes it so we don't fall into an endless loop waiting for losing
   clients which don't exit.  */
static void
kill_client_connection (SmsConn connection)
{
  IceCloseConnection (SmsGetIceConnection (connection));
}



static Status
register_client (SmsConn connection, SmPointer data, char *previous_id)
{
  Client *client = (Client *) data;

  if (previous_id)
    {
      /* Client from existing session.  */
      Client *old_client = find_client_by_id (zombie_list, previous_id);
      if (! old_client)
	{
	  /* Not found.  Let them try again.  */
	  free (previous_id);
	  return 0;
	}
      client->id = previous_id;
      REMOVE (zombie_list, old_client);
      free_client (old_client);
    }
  else
    {
      /* New client.  */
      client->id = SmsGenerateClientID (connection);
    }

  APPEND (live_list, client);

  return SmsRegisterClientReply (connection, client->id);
}

static void
interact_request (SmsConn connection, SmPointer data, int dialog_type)
{
  Client *client = (Client *) data;

  /* FIXME: what to do when this is sent by a client not on the
     save-yourself list?  */

  REMOVE (save_yourself_list, client);
  APPEND (interact_list, client);

  if (interact_list->data == client)
    SmsInteract (connection);
}

static void
interact_done (SmsConn connection, SmPointer data, Bool cancel)
{
  Client *client = (Client *) data;
  int start_next = 0;

  if (interact_list && interact_list->data == client)
    start_next = 1;

  REMOVE (interact_list, client);
  APPEND (save_yourself_list, client);

  /* Protocol does not allow a cancel unless we are shutting down */
  if (shutting_down && cancel)
    {
      /* Cancel whatever we're doing.  */
      saving = 0;
      shutting_down = 0;

      send_message (&interact_list, SmsShutdownCancelled);
      CONCAT (live_list, interact_list);
      interact_list = NULL;

      send_message (&save_yourself_list, SmsShutdownCancelled);
      CONCAT (live_list, save_yourself_list);
      save_yourself_list = NULL;

      send_message (&save_yourself_p2_list, SmsShutdownCancelled);
      CONCAT (live_list, save_yourself_p2_list);
      save_yourself_p2_list = NULL;

      send_message (&save_finished_list, SmsShutdownCancelled);
      CONCAT (live_list, save_finished_list);
      save_finished_list = NULL;
    }
  else if (interact_list && start_next)
    {
      client = (Client *) interact_list->data;
      SmsInteract (client->connection);
    }
}

static void
save_yourself_request (SmsConn connection, SmPointer data, int save_type,
		       Bool shutdown, int interact_style, Bool fast,
		       Bool global)
{
  Client *client = (Client *) data;

  if (saving)
    {
      /* FIXME: should queue the save for later.  */
      return;
    }

  if (! global)
    {
      /* Discard single saves requested for a non-live client.  */
      if (! g_slist_find (live_list, client))
	return;

      saving = 1;
      shutting_down = 0;
      g_assert (!save_yourself_list);
      REMOVE (live_list, client);
      APPEND (save_yourself_list, client);
      /* We ignore `shutdown' when a single-client save requested.  */
      SmsSaveYourself (client->connection, save_type, 0, interact_style,
		       fast);
    }
  else
    {
      /* Global save.  Use same function the rest of gsm uses.  */
      save_session (save_type, shutdown, interact_style, fast);
    }
}

static void
save_yourself_p2_request (SmsConn connection, SmPointer data)
{
  Client *client = (Client *) data;
  gboolean found = (g_slist_find (save_yourself_list, client) != NULL);

  if (found) 
    {
      REMOVE (save_yourself_list, client);
      APPEND (save_yourself_p2_list, client);
      check_session_end (TRUE);
    }
}

/* A helper function.  Finds and executes each shutdown command.  */
static void
run_shutdown_commands (const GSList *list)
{
  for (; list; list = list->next)
    {
      Client *client = (Client *) list->data;
      char **argv;
      int argc;

      if (find_vector_property (client, SmShutdownCommand, &argc, &argv))
	{
	  gnome_execute_async (NULL, argc, argv);
	  free_vector (argc, argv);
	}
    }
}

/* This is a helper function which is run when it might be time to
   actually save the session.  It is run when the save_yourself lists
   are modified.  */
static void
check_session_end (int found)
{
  if (! interact_list && ! save_yourself_list)
    {
      if (save_yourself_p2_list && found)
	{
	  /* Just saw the last ordinary client finish saving.  So tell
	     the Phase 2 clients that they're ok to go.  */
	  /* disable check_session_end tests during the send message: */
	  saving = 0;
	  send_message (&save_yourself_p2_list, SmsSaveYourselfPhase2);
	  saving = 1;
	  /* NB: io errors could wipe out the save_yourself_p2_list */
	}

      if (!save_yourself_p2_list)
	{
	  /* The write_session should include all live_list members 
	     following a SaveYourselfRequest with global == FALSE. 
	     The live_list == NULL in all other cases. */
	  CONCAT (live_list, save_finished_list);
	  
	  /* All clients have responded to the save.  Now shut down or
	     continue as appropriate.  Save the `anyway_list' first,
	     in hopes of getting initialization commands started
	     first.  */
	  write_session (anyway_list, live_list, shutting_down);
	  saving = 0;
	  send_message (&save_finished_list,
			shutting_down ? SmsDie : SmsSaveComplete);
	  save_finished_list = NULL;
	  if (shutting_down)
	    {
	      /* Run each shutdown command.  The manual doesn't make
		 it clear whether this should be done for all clients
		 or only those that have RestartAnyway set.  We run
		 them all.  */
	      run_shutdown_commands (anyway_list);
	      run_shutdown_commands (live_list);
	      
	      /* Make sure that all client connections are closed.  */
	      send_message (&live_list, kill_client_connection);
	      
	      gtk_main_quit ();
	    }
	}
    }
}

static void
save_yourself_done (SmsConn connection, SmPointer data, Bool success)
{
  Client *client = (Client *) data;
  GSList *found;

  /* if a Shutdown Cancelled was sent then saving may be == 0 ! */
  if (saving) {
    found = g_slist_find (save_yourself_list, client);
    REMOVE (save_yourself_list, client);
    REMOVE (save_yourself_p2_list, client);
    APPEND (save_finished_list, client);

    check_session_end (found != NULL);
  }
}

/* FIXME: Display REASONS to user, per spec.  */
static void
close_connection (SmsConn connection, SmPointer data, int count,
		  char **reasons)
{
  Client *client = (Client *) data;
  int interact_next = 0;
  GSList *found;
  int style;

  found = g_slist_find (save_yourself_list, client);

  /* Just try every list.  */
  REMOVE (zombie_list, client);
  REMOVE (live_list, client);

  if (interact_list && interact_list->data == client)
    interact_next = 1;
  REMOVE (interact_list, client);

  REMOVE (save_yourself_list, client);
  REMOVE (save_yourself_p2_list, client);
  REMOVE (save_finished_list, client);

  REMOVE (message_sent_list, client);

  /* Take appropriate action based on the restart style.  The default
     is RestartIfRunning, so if the value isn't found, we just do
     nothing.  */
  if (find_card8_property (client, SmRestartStyleHint, &style))
    {
      if (style == SmRestartImmediately)
	{
	  /* FIXME: re-run the client.  Probably should think of some
	     clever way to avoid thrashing by a losing client.  Don't
	     restart if we are shutting down.  */
	}
      else if (style == SmRestartAnyway)
	{
	  APPEND (anyway_list, client);
	  client = NULL;
	}
    }

  free_client (client);

  if (interact_list && interact_next)
    {
      client = (Client *) interact_list->data;
      SmsInteract (client->connection);
    }

  if (saving)
    check_session_end (found != NULL);

  SmFreeReasons (count, reasons);
}



/* It doesn't matter that this is inefficient.  */
static void
set_properties (SmsConn connection, SmPointer data, int nprops, SmProp **props)
{
  Client *client = (Client *) data;
  int i;

  for (i = 0; i < nprops; ++i)
    {
      SmProp *prop;

      prop = find_property_by_name (client, props[i]->name);
      if (prop)
	{
	  REMOVE (client->properties, prop);
	  SmFreeProperty (prop);
	}

      APPEND (client->properties, props[i]);
    }

  free (props);
}

static void
delete_properties (SmsConn connection, SmPointer data, int nprops,
		   char **prop_names)
{
  Client *client = (Client *) data;
  int i;

  for (i = 0; i < nprops; ++i)
    {
      SmProp *prop;

      prop = find_property_by_name (client, prop_names[i]);
      if (prop)
	{
	  REMOVE (client->properties, prop);
	  SmFreeProperty (prop);
	}
    }

  /* FIXME: free prop_names?? */
}

static void
get_properties (SmsConn connection, SmPointer data)
{
  Client *client = (Client *) data;
  SmProp **props;
  GSList *list;
  int i, len;

  len = g_slist_length (client->properties);
  props = g_new (SmProp *, len);

  i = 0;
  for (list = client->properties; list; list = list->next)
    props[i] = list->data;

  SmsReturnProperties (connection, len, props);
  free (props);
}



/* This is run when a new client connects.  We register all our
   callbacks.  */
Status
new_client (SmsConn connection, SmPointer data, unsigned long *maskp,
	    SmsCallbacks *callbacks, char **reasons)
{
  Client *client;

  client = g_new (Client, 1);
  client->id = NULL;
  client->connection = connection;
  client->properties = NULL;

  *maskp = 0;

  *maskp |= SmsRegisterClientProcMask;
  callbacks->register_client.callback = register_client;
  callbacks->register_client.manager_data = (SmPointer) client;

  *maskp |= SmsInteractRequestProcMask;
  callbacks->interact_request.callback = interact_request;
  callbacks->interact_request.manager_data = (SmPointer) client;

  *maskp |= SmsInteractDoneProcMask;
  callbacks->interact_done.callback = interact_done;
  callbacks->interact_done.manager_data = (SmPointer) client;

  *maskp |= SmsSaveYourselfRequestProcMask;
  callbacks->save_yourself_request.callback = save_yourself_request;
  callbacks->save_yourself_request.manager_data = (SmPointer) client;

  *maskp |= SmsSaveYourselfP2RequestProcMask;
  callbacks->save_yourself_phase2_request.callback = save_yourself_p2_request;
  callbacks->save_yourself_phase2_request.manager_data = (SmPointer) client;

  *maskp |= SmsSaveYourselfDoneProcMask;
  callbacks->save_yourself_done.callback = save_yourself_done;
  callbacks->save_yourself_done.manager_data = (SmPointer) client;

  *maskp |= SmsCloseConnectionProcMask;
  callbacks->close_connection.callback = close_connection;
  callbacks->close_connection.manager_data = (SmPointer) client;

  *maskp |= SmsSetPropertiesProcMask;
  callbacks->set_properties.callback = set_properties;
  callbacks->set_properties.manager_data = (SmPointer) client;

  *maskp |= SmsDeletePropertiesProcMask;
  callbacks->delete_properties.callback = delete_properties;
  callbacks->delete_properties.manager_data = (SmPointer) client;

  *maskp |= SmsGetPropertiesProcMask;
  callbacks->get_properties.callback = get_properties;
  callbacks->get_properties.manager_data = (SmPointer) client;

  return 1;
}



/* This function is exported to the rest of gsm.  It sets up and
   performs a session save, possibly followed by a shutdown.  */
void
save_session (int save_type, gboolean shutdown, int interact_style,
	      gboolean fast)
{
  if (saving)
    return;

  shutting_down = shutdown;

  g_assert (!save_yourself_list);
  /* Protect against io_errors by using static lists */
  while (live_list) 
    {
      Client *client = (Client *) live_list->data;
      REMOVE (live_list, client);
      APPEND (save_yourself_list, client);
      SmsSaveYourself (client->connection, save_type, shutting_down,
		       interact_style, fast);
    }
  /* enable check_session_end tests AFTER sending the last save yourself */
  saving = 1;
  check_session_end (FALSE);
}



int
shutdown_in_progress_p (void)
{
  return saving && shutting_down;
}



/* This is called when an IO error occurs on some client connection.
   This means the client has shut down in a losing way.  */
void
io_error_handler (IceConn connection)
{
  Client *client;

  /* Find the client on any list.  */
  client = find_client_by_connection (live_list, connection);
  if (! client)
    client = find_client_by_connection (interact_list, connection);
  if (! client)
    client = find_client_by_connection (save_yourself_list, connection);
  if (! client)
    client = find_client_by_connection (save_yourself_p2_list, connection);
  if (! client)
    client = find_client_by_connection (save_finished_list, connection);
  if (! client)
    client = find_client_by_connection (message_sent_list, connection);

  /* The client might not be found.  For instance this could happen if
     the client exited before registering.  */
  if (client)
    {
      /* FIXME: error message.  The problem is, how to free it?
	 close_connection can't do it, since SmFreeReasons is opaque.
	 */
      close_connection (client->connection, (SmPointer) client, 0, NULL);
    }

  /* Close the connection regardless of whether we discovered a
     client.  */
  IceCloseConnection (connection);
}



/* Register a new zombie.  */
void
add_zombie (const char *id)
{
  Client *client;

  client = g_new (Client, 1);
  client->id = strdup (id);
  client->connection = NULL;
  client->properties = NULL;

  APPEND (zombie_list, client);
}
