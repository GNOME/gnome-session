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

#include <gnome.h>

#include "manager.h"


/* The save_state gives a description of what we are doing */
static enum {
  MANAGER_IDLE,
  SENDING_MESSAGES,
  SENDING_INTERACT,
  SAVE_PHASE_1,
  SAVE_PHASE_2,
  SAVE_CANCELLED,
  STARTING_SESSION
} save_state;

/* List of sessions which have yet to be loaded. */
static GSList *load_request_list = NULL;

/* List of clients which have yet to be started */
static GSList *start_list = NULL;

/* List of clients which have been started but have yet to register */
static GSList *pending_list = NULL;

/* List of clients which have been purged from the pending list 
   since they have failed to register within our timeout. */
static GSList *purged_list = NULL;

/* List of all live clients in the default state.  */
GSList *live_list = NULL;

/* A queued save yourself request */
typedef struct _SaveRequest
{
  Client*  client;
  gint     save_type;
  gboolean shutdown;
  gint     interact_style;
  gboolean fast;
  gboolean global;
} SaveRequest;

/* List of requested saves */
static GSList *save_request_list = NULL;

/* This is true if a shutdown is expected to follow the current save.  */
static int shutting_down = 0;

/* List of all clients waiting for the interaction token.  The head of
   the list actually has the token.  */
static GSList *interact_list = NULL;

/* List of all clients to which a `save yourself' message has been sent. */
static GSList *save_yourself_list = NULL;

/* List of all clients which have requested a Phase 2 save.  */
static GSList *save_yourself_p2_list = NULL;

/* List of all clients which have been saved.  */
static GSList *save_finished_list = NULL;

/* List of all clients that have exited but set their style hint
   to RestartAnyway - the living dead. */
GSList *zombie_list = NULL;

/* List of clients to which a message has been sent during send_message: 
   needed to cope with io errors. */ 
static GSList *message_sent_list = NULL;



#define APPEND(List,Elt) ((List) = (g_slist_append ((List), (Elt))))
#define REMOVE(List,Elt) ((List) = (g_slist_remove ((List), (Elt))))
#define CONCAT(L1,L2) ((L1) = (g_slist_concat ((L1), (L2))))



static gint compare_interact_request (gconstpointer a, gconstpointer b);
typedef void message_func (SmsConn connection);

static void
save_yourself_request (SmsConn connection, SmPointer data, int save_type,
		       Bool shutdown, int interact_style, Bool fast,
		       Bool global);

static void update_save_state (void);



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
find_client_by_connection (GSList *list, IceConn ice_conn)
{
  for (; list; list = list->next)
    {
      Client *client = (Client *) list->data;
      if (SmsGetIceConnection (client->connection) == ice_conn)
	return client;
    }
  return NULL;
}

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



/* Run a command on a client. */
gint
run_command (const Client* client, const gchar* command)
{
  int argc, envc, envpc, pid = -1;
  gboolean def, envd;
  char **argv, *dir, prefix[1024], **envv, **envp;
  
  if (find_vector_property (client, command, &argc, &argv))
    {
      if (!find_string_property (client, SmCurrentDirectory, &dir))
	dir = NULL;
      
      if (find_vector_property (client, SmEnvironment, &envc, &envv) &&
	  envc > 0 && !(envc | 1))
	{
	  int i;

	  envpc = envc / 2;
	  envp = g_new0 (gchar*, envpc + 1);
	  
	  for (i = 0; i < envpc; i++)
	    envp[i] = g_strconcat (envv[2*i], "=", envv[2*i + 1], NULL);
	  free_vector (envc, envv);
	}
      else
	{
	  envpc = 0;
	  envp = NULL;
	}
      
      pid = gnome_execute_async_with_env (dir, argc, argv, envpc, envp);

      if (envp)
	free_vector (envpc, envp);
      if (dir)
	free (dir);
      free_vector (argc, argv);
    }
  return pid;
}



/* Clean up a client connection following an close connection or session 
   shutdown. */
static void
kill_client_connection (SmsConn connection)
{
  IceConn ice_conn;

  ice_conn = SmsGetIceConnection (connection);
  SmsCleanUp(connection);
  IceSetShutdownNegotiation (ice_conn, False);
  IceCloseConnection (ice_conn);
}

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



static void
process_load_request (GSList *client_list)
{
  GSList* list;
     
  /* Strip out any purged clients that might be confused with clients 
   * in this session. We can only assume that they died before registering, 
   * are badly broken or had corrupt config entries. 
   * We could strip them from the client_list instead but this seems
   * more useful (in theory). */

  for (list = purged_list; list;)
    {
      Client *client = (Client*)list->data;
      
      list = list->next;
      if (find_client_by_id (client_list, client->id))
	{
	  /* FIXME: warn user. */
	  REMOVE (purged_list, client);
	}
    }

  start_list = client_list;
  save_state = STARTING_SESSION;
  
  update_save_state();
}

static gint		
compare_priority (gconstpointer a, gconstpointer b)
{
  Client *client_a = (Client*)a, *client_b = (Client*)b;
  return client_a->priority - client_b->priority;
}

/* Prepare to start clients in order of priority */
void
load_session (GSList* client_list)
{
  if (client_list)
    {
      client_list = g_slist_sort (client_list, compare_priority);

      if (save_state == STARTING_SESSION)
	{
	  APPEND (load_request_list, client_list);
	  return;
	}
      else
	process_load_request (client_list);
    }
}



/* Purges clients from the pending list that are taking excessive
 * periods of time to register.
 * It would be nice to base this test on client CPU time but the 
 * client may be running on a remote host. */
static gint 
purge (gpointer data)
{
  Client* client = (Client*)data;

  if (g_slist_find (pending_list, client))
    {
      fprintf (stderr, "Priority %02d : Purging     Id = %s\n", 
	       client->priority, client->id);

      REMOVE (pending_list, client);
      APPEND (purged_list, client);

      update_save_state();
    }
  return 0;
}

/* We use the SmRestartCommand to start up clients read from session
 * files even when sessions are being merged so that we can match
 * the saved SmProperties against the registering clients.
 *
 * We avoid potential conflicts between client ids by loading one
 * session at a time and refusing to register clients under conflicting
 * ids. This avoids the need to use the vaguely defined SmCloneCommand. */

void
start_client (Client* client)
{
  if (run_command (client, SmRestartCommand) == -1)
    {
      /* FIXME: Inform the user. */
      free_client(client);
      return;
    }

  fprintf (stderr, "Priority %02d : Starting    Id = %s\n", 
	   client->priority, client->id);

  client->connect_time = time (NULL);

  /* ghost clients are not session aware and must become zombies */
  if (! g_strncasecmp ("ghost", client->id, 5))
    APPEND (zombie_list, client);
  else
    {
      APPEND (pending_list, client);
      if (purge_delay)
	gtk_timeout_add (purge_delay, purge, (gpointer)client); 
    }
}



static Status
register_client (SmsConn connection, SmPointer data, char *previous_id)
{
  Client *client = (Client *) data;
  int had_faked_id;

  if (previous_id)
    {
      /* This claims to be a client that we have restarted ... */
      Client *offspring = find_client_by_id (pending_list, previous_id);
      if (! offspring)
	{
	  /* Perhaps it was a slow one ... */
	  offspring = find_client_by_id (purged_list, previous_id);
	  if (! offspring) 
	    {
	      /* Hmm ... not a legitimate child. 
		 FIXME: Inform user. */
	      free (previous_id);
	      return 0;
	    }
	  REMOVE (purged_list, offspring);
	}
      else
	REMOVE (pending_list, offspring);

      client->properties = offspring->properties;
      client->priority = offspring->priority;

      offspring->properties = NULL;

      had_faked_id = offspring->faked_id;
      free_client (offspring);

      if (had_faked_id ||
	  find_client_by_id (live_list, previous_id) ||
	  find_client_by_id (zombie_list, previous_id))
	{
	  fprintf (stderr, "Priority %02d : Cloning     Id = %s\n", 
		   client->priority, previous_id);

	  free (previous_id);
	  return 0;
	}

      client->id = previous_id;
    }
  else
    client->id = SmsGenerateClientID (connection);

  fprintf (stderr, "Priority %02d : Registering Id = %s\n", 
	   client->priority, client->id);

  if (! SmsRegisterClientReply (connection, client->id))
    {
      /* Lost out : SMlib could not assign memory. Very Nasty! */
      free_client (client);
      return 0;
    }

  APPEND (live_list, client);      

  /* SM specs 7.2: */
  if (!previous_id)
      save_yourself_request (connection, (SmPointer) client, 
			     SmSaveLocal, False,
			     SmInteractStyleNone, False, False);
  update_save_state ();
  return 1; 
}



static gint		
compare_interact_request (gconstpointer a, gconstpointer b)
{
  return a > b;
}

static void
interact_request (SmsConn connection, SmPointer data, int dialog_type)
{
  Client *client = (Client *) data;

  /* These seem to be the only circumstances in which interactions
     can be allowed without breaking the protocol.
     FIXME: Should we inform the user of bogus requests ? */
  if (save_state == SAVE_PHASE_1 &&
      (g_slist_find (save_yourself_list, client) ||
       g_slist_find (save_yourself_p2_list, client)))
    {
      gboolean send = !interact_list;

      /* Try to bunch up the interactions for each client */
      interact_list = g_slist_insert_sorted (interact_list, client, 
					     compare_interact_request);
      if (send) 
	{
	  save_state = SENDING_INTERACT;
	  SmsInteract (connection);
	  save_state = SAVE_PHASE_1;
	}
    }
}

static void
interact_done (SmsConn connection, SmPointer data, Bool cancel)
{
  Client *client = (Client *) data;

  /* if client != interact_list->data then libSM will NOT call
     this function when the client at the head of the interact_list
     sends its SmInteractDone. Therefore, we need to remove the
     client at the head of the interact_list REGARDLESS of which
     client sent this message.
     FIXME: Should we inform the user of bogus done messages ? */
     
  REMOVE (interact_list, interact_list->data);

  if (cancel)
    {
      /* We need a state in which we wait for the SaveYourselfDone
         messages from clients following a ShutdownCancelled to 
         avoid misinterpreting those messages as responses to the
         NEXT SaveYourself message which we send to them */
      save_state = SAVE_CANCELLED;
      shutting_down = 0;
      interact_list = NULL;

      send_message (&save_yourself_list, SmsShutdownCancelled);
      send_message (&save_yourself_p2_list, SmsShutdownCancelled);
    }

  if (interact_list)
    {
      save_state = SENDING_INTERACT;
      client = (Client *) interact_list->data;
      SmsInteract (client->connection);
      save_state = SAVE_PHASE_1;
    }
  /* Check in case the client sent a SaveYourselfDone during
     the interaction */ 
  update_save_state ();
}

static void
process_save_request (Client* client, int save_type, gboolean shutdown, 
		      int interact_style, gboolean fast, gboolean global)
{
  save_state = SENDING_MESSAGES;
  if (global)
    {
      shutting_down = shutdown;
      while (live_list) 
	{
	  Client *client = (Client *) live_list->data;
	  REMOVE (live_list, client);
	  APPEND (save_yourself_list, client);
	  SmsSaveYourself (client->connection, save_type, shutting_down,
			   interact_style, fast);
	}
    }
  else
    {
      REMOVE (live_list, client);
      APPEND (save_yourself_list, client);
      SmsSaveYourself (client->connection, 
		       save_type, 0, interact_style, fast);
    }
  save_state = (save_yourself_list != NULL) ? SAVE_PHASE_1 : MANAGER_IDLE;

  update_save_state ();
}

static void
save_yourself_request (SmsConn connection, SmPointer data, int save_type,
		       Bool shutdown, int interact_style, Bool fast,
		       Bool global)
{
  Client *client = (Client *) data;

  if (save_state != MANAGER_IDLE)
    {
      SaveRequest *request = g_new (SaveRequest, 1);
      request->client = client;
      request->save_type = save_type;
      request->shutdown = shutdown;
      request->interact_style = interact_style;
      request->fast = fast;
      request->global = global;
      
      APPEND (save_request_list, request);
    }
  else
    process_save_request (client, save_type, shutdown, 
			  interact_style, fast, global);
}

static void
save_yourself_p2_request (SmsConn connection, SmPointer data)
{
  Client *client = (Client *) data;

  if (g_slist_find (save_yourself_list, client) != NULL) 
    {
      REMOVE (save_yourself_list, client);
      APPEND (save_yourself_p2_list, client);
      update_save_state ();
    }
}

/* A helper function.  Finds and executes each shutdown command.  */
static void
run_shutdown_commands (const GSList *list)
{
  for (; list; list = list->next)
    {
      Client *client = (Client *) list->data;

      run_command (client, SmShutdownCommand);
    }
}

/* This is a helper function which makes sure that the save_state
   variable is correctly set after a change to the save_yourself lists. */
static void
update_save_state ()
{
  if (save_state == STARTING_SESSION)
    {
      int runlevel = 1000;
      
      if (pending_list)
	return;

      if (start_list)
	{
	  while (start_list)
	    {
	      Client* client = (Client*)start_list->data;
	      
	      if (client->priority > runlevel) 
		return;
	      
	      runlevel = client->priority;
	      REMOVE (start_list, client);
	      start_client (client);
	    }
	  return;
	}
      save_state = MANAGER_IDLE;
     }    

  if (save_state == SAVE_PHASE_1)
    {
      if (save_yourself_list || interact_list)
	return;

      if (save_yourself_p2_list)
	{
	  save_state = SENDING_MESSAGES;
	  send_message (&save_yourself_p2_list, SmsSaveYourselfPhase2);
	}
      save_state = SAVE_PHASE_2;
    }
 
  if (save_state == SAVE_PHASE_2)
    {
      if (save_yourself_p2_list)
	return;

      CONCAT (live_list, save_finished_list);
	  
      write_session ();

      save_state = SENDING_MESSAGES;

      send_message (&save_finished_list,
		    shutting_down ? SmsDie : SmsSaveComplete);
      
      if (shutting_down)
	{
	  /* Run each shutdown command. These commands are only strictly
	   * needed by zombie clients. */
	  run_shutdown_commands (zombie_list);
	  run_shutdown_commands (live_list);

	  unlock_session ();
	  gtk_main_quit ();
	}
      save_finished_list = NULL;
      save_state = MANAGER_IDLE;
    }

  if (save_state == SAVE_CANCELLED)
    {
      if (save_yourself_list || save_yourself_p2_list)
	return;

      CONCAT (live_list, save_finished_list);

      write_session ();

      save_finished_list = NULL;
      save_state = MANAGER_IDLE;
    }

  if (save_state == MANAGER_IDLE && save_request_list)
    {
      SaveRequest *request = save_request_list->data;
      
      REMOVE (save_request_list, request);
      process_save_request (request->client, request->save_type, 
			    request->shutdown, request->interact_style, 
			    request->fast, request->global);
      free (request);
    }
      
  if (save_state == MANAGER_IDLE && load_request_list)
    {
      GSList* client_list = (GSList*)load_request_list->data;

      REMOVE (load_request_list, client_list);
      process_load_request (client_list);
    }
}

static void
save_yourself_done (SmsConn connection, SmPointer data, Bool success)
{
  Client *client = (Client *) data;

  if (save_state == MANAGER_IDLE || save_state == STARTING_SESSION ||
      (g_slist_find (save_yourself_list, client) == NULL &&
       g_slist_find (save_yourself_p2_list, client) == NULL))
    {
      /* A SmSaveYourselfDone from a broken client. 
	 FIXME: Should we inform the user ? */
      return;
    }

  REMOVE (save_yourself_list, client);
  REMOVE (save_yourself_p2_list, client);
  APPEND (save_finished_list, client);      
  
  update_save_state ();
}

static void
display_reasons (gint count, gchar** reasons)
{
  if (count > 0)
    {
      GtkWidget* dialog;
      gchar* message = reasons [0];
      int i;

      for (i = 1; i < count; i++)
	{
	  message = g_strjoin ("\n", message, reasons[i], NULL);
	}

      /* This is warning about a fatality rather than a fatal error. */
      dialog = gnome_warning_dialog (message);

      if (count > 1)
	free (message);
    }
}

static void
close_connection (SmsConn connection, SmPointer data, int count,
		  char **reasons)
{
  Client *client = (Client *) data;
  int style;
  GSList *list;

  /* The client appear be one of several live lists: */
  REMOVE (live_list, client);
  REMOVE (save_yourself_list, client);
  REMOVE (save_yourself_p2_list, client);
  REMOVE (save_finished_list, client);
  REMOVE (message_sent_list, client);
  
  /* It also may have queued interact requests ... */
  while (g_slist_find (interact_list, client))
    REMOVE (interact_list, client);

  /* ... or save requests */
  for (list = save_request_list; list;)
    {
      SaveRequest *request = (SaveRequest*)list->data;

      list = list->next;
      if (request->client == client) {
	g_free(request);
	REMOVE (save_request_list, request);
      }
    }

  if (!find_card8_property (client, SmRestartStyleHint, &style))
    style = SmRestartIfRunning;

  /* FIXME: The display reasons dialog should reflect
   * the restart style hint and whether we are actually restarting
   * the restart immediately clients */

  display_reasons (count, reasons);

  switch (style)
    {
    case SmRestartImmediately:

      if (! shutting_down && 
	  time(NULL) > client->connect_time + 60)
	{
	  run_command (client, SmRestartCommand);
	  APPEND (pending_list, client);
	}
      else
	free_client (client);
      break;

    case SmRestartAnyway:
      
      APPEND (zombie_list, client);
      break;

    default:

      free_client (client);
      break;
    }

  if (save_state == SENDING_INTERACT)
    {
      if (interact_list)
	{
	  client = (Client *) interact_list->data;
	  SmsInteract (client->connection);
	}
      save_state = SAVE_PHASE_1;
    }

  SmFreeReasons (count, reasons);

  kill_client_connection (connection);

  update_save_state ();
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

  /* libSM allocates the reasons and property names in identically: */
  SmFreeReasons (nprops, prop_names);
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

  if (shutting_down)
    {
      *reasons  = g_strdup (_("A session shutdown is in progress."));
      return 0;
    }

  client = g_new (Client, 1);
  client->id = NULL;
  client->connection = connection;
  client->properties = NULL;
  client->priority = 50;
  client->connect_time = time (NULL);

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
   performs a global save */
void
save_session (int save_type, gboolean shutdown, int interact_style,
	      gboolean fast)
{
  if (save_state != MANAGER_IDLE)
    {
      SaveRequest *request = g_new (SaveRequest, 1);
      request->client = NULL;
      request->save_type = save_type;
      request->shutdown = shutdown;
      request->interact_style = interact_style;
      request->fast = fast;
      request->global = TRUE;
      
      APPEND (save_request_list, request);
    }
  else
    process_save_request (NULL, save_type, shutdown, 
			  interact_style, fast, TRUE);
}



int
shutdown_in_progress_p (void)
{
  return save_state != MANAGER_IDLE && shutting_down;
}



/* This is called when an IO error occurs on some client connection.
   This means the client has shut down in a losing way.  */
void
io_error_handler (IceConn ice_conn)
{
  Client *client;
  gchar* program = NULL;
  gchar* reason;

  /* Find the client on any list.  */
  client = find_client_by_connection (live_list, ice_conn);
  if (! client)
    client = find_client_by_connection (save_yourself_list, ice_conn);
  if (! client)
    client = find_client_by_connection (save_yourself_p2_list, ice_conn);
  if (! client)
    client = find_client_by_connection (save_finished_list, ice_conn);
  if (! client)
    client = find_client_by_connection (message_sent_list, ice_conn);

  /* Try to work out who just departed this world ... */
  if (client)
    { 
      if (! find_string_property (client, SmProgram, &program))
	{
	  int argc;
	  char **argv;

	  if (find_vector_property (client, SmRestartCommand, &argc, &argv))
	    {
	      program = strdup (argv[0]);
	      free_vector (argc,argv);
	    }
	}
    }
  
  if (program)
    {
      reason = 
	g_strdup_printf (_("The Gnome Session Manager unexpectedly\n"
			   "lost contact with program \"%s\"."), program);
      free(program);
    }
  else
    reason = g_strdup(_("The Gnome Session Manager unexpectedly\n"
	       "lost contact with an unnamed program."));

#if 0
  /* stupid annoying useless message */	  
  display_reasons (1, &reason);
#endif

  free (reason);
  
  /* if client were a gtk object... */
  if (client)
    {
      close_connection (client->connection, (SmPointer)client, 0, NULL);
    }
  else
    {
      IceSetShutdownNegotiation (ice_conn, False);
      IceCloseConnection (ice_conn);
    }
}
