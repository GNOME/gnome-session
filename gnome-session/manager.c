/* manager.c - Session manager back end.

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
#include <config.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>

#include <unistd.h>
#include <errno.h>
#ifndef errno
extern int errno;
#endif

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libgnome/libgnome.h>

#include "manager.h"
#include "session.h"
#include "prop.h"
#include "command.h"
#include "splash-widget.h"
#include "remote.h"
#include "save.h"
#include "logout.h"
#include "ice.h"
#include "gsm-protocol.h"
#include "util.h"

/* The save_state gives a description of what we are doing */
typedef enum {
  MANAGER_IDLE,
  SENDING_MESSAGES,
  SENDING_INTERACT,
  SENDING_INTERACT_2,
  SAVE_PHASE_1,
  SAVE_PHASE_2,
  SAVE_CANCELLED,
  STARTING_SESSION,
  SHUTDOWN
} SaveState;

SaveState save_state;

/* List of clients which have been started but have yet to register */
GSList *pending_list = NULL;

/* Lists of clients which have been purged from the pending list: 
   The first list contains clients we throw away at the session end,
   the second contains clients the user wants us to keep. */
GSList *purge_drop_list = NULL;
GSList *purge_retain_list = NULL;

/* List of all live clients in the default state.  */
GSList *live_list = NULL;

/* Timeout for clients that are slow to respond to SmSaveYourselfRequest */
static guint warn_timeout_id = 0;

/* Used to monitor programs that claim to be interacting. */
static gboolean interact_ping_replied = TRUE;

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

/* List of sessions which have yet to be loaded. */
static GSList *load_request_list = NULL;

/* List of clients which have yet to be started */
static GSList *start_list = NULL;

/* List of requested saves */
static GSList *save_request_list = NULL;

/* This is true if a shutdown is expected to follow the current save.  */
static gboolean shutting_down = FALSE;

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

/* A cheap hack to enable us to iterate through the clients */
GSList **client_lists[] = { &zombie_list, &start_list, 
			    &pending_list, &live_list,
			    &purge_drop_list, &purge_retain_list,
			    &save_yourself_list, &save_yourself_p2_list, 
			    &save_finished_list, NULL };
gchar   *events[]       = { GsmInactive, GsmInactive, 
			    GsmStart, GsmConnected,
			    GsmUnknown, GsmUnknown, 
			    GsmSave, GsmSave, 
			    GsmConnected, NULL };

/* whether our current save is global */
static gboolean global_save;

typedef void message_func (SmsConn connection);

static void
close_connection (SmsConn connection, SmPointer data, int count,
		  char **reasons);

static void
save_yourself_request (SmsConn connection, SmPointer data, int save_type,
		       gboolean shutdown, int interact_style, gboolean fast,
		       gboolean global);

static void 
set_properties (SmsConn connection, SmPointer data, 
	        int nprops, SmProp **props);

static void update_save_state (void);

static void print_client (Client *client);

static const char *
get_save_state (void)
{
  switch (save_state)
    {
    case MANAGER_IDLE:
      return "MANAGER_IDLE";
      break;
    case SENDING_MESSAGES:
      return "SENDING_MESSAGES";
      break;
    case SENDING_INTERACT:
      return "SENDING_INTERACT";
      break;
    case SENDING_INTERACT_2:
      return "SENDING_INTERACT_2";
      break;
    case SAVE_PHASE_1:
      return "SAVE_PHASE_1";
      break;
    case SAVE_PHASE_2:
      return "SAVE_PHASE_2";
      break;
    case SAVE_CANCELLED:
      return "SAVE_CANCELLED";
      break;
    case STARTING_SESSION:
      return "STARTING_SESSION";
      break;
    case SHUTDOWN:
      return "SHUTDOWN";
      break;
    default:
      g_assert_not_reached ();
      return "UNKNOWN";
    }
}

static void
save_state_changed (void)
{
  gsm_verbose ("SAVE_STATE -> %s\n", get_save_state ());
}

static void
maybe_write_session (void)
{
  gsm_verbose ("maybe_write_session()\n");
  gsm_verbose_indent (TRUE);

  if (autosave || save_selected || session_save)
    write_session ();

  save_selected = session_save = FALSE;

  gsm_verbose_indent (FALSE);
}

Client *
find_client_by_id (const GSList *list, const char *id)
{
  for (; list; list = list->next)
    {
      Client *client = (Client *) list->data;

      if (client->match_rule != MATCH_PROP)
	{
	  if (client->id && ! strcmp (client->id, id))
	    return client;
	}
    }
  return NULL;
}

static Client *
find_client_by_argv0 (const GSList *list, const char *argv0)
{
  for (; list; list = list->next)
    {
      Client *client = (Client *) list->data;
      if (client->match_rule == MATCH_PROP)
	{
	  SmProp *prop = find_property_by_name (client, SmRestartCommand);
	  if (!strcmp ((prop->vals[0].value), argv0))
	    return client;
	}
    }
  return NULL;
}

static void
set_client_restart_style (Client *client, char style)
{
  guint nprops = 1;
  SmProp *prop = (SmProp*) malloc (sizeof (SmProp));
  SmProp **props = (SmProp**) malloc (sizeof (SmProp*) * nprops);

  gsm_verbose ("set_client_restart_style (%p (connection %p), SmRestartNever)\n", client, client->connection);
  gsm_verbose_indent (TRUE);

  prop->name = strdup (SmRestartStyleHint);
  prop->type = strdup (SmCARD8);
  prop->num_vals = 1;
  prop->vals = (SmPropValue*) malloc (sizeof (SmPropValue));
  prop->vals[0].value = (SmPointer) calloc (sizeof(char), 2);
  ((gchar*)prop->vals[0].value)[0] = style;
  prop->vals[0].length = 1;
  props[0] = prop;
  set_properties(client->connection, (SmPointer)client, nprops, props);

  gsm_verbose_indent (FALSE);
}

/* Entry and exit onto the purge lists is signaled by broadasting changes
 * to the restart style as this reflects the substance of the change. 
 * However, any SmRestartStyleHint in the client->properties is left
 * unchanged since the client will need it if and when it connects. */ 
static void
broadcast_restart_style (Client *client, char style)
{
  char value[2] = { '\0', '\0' };
  SmPropValue val = { 1, NULL };
  SmProp hint = { SmRestartStyleHint, SmCARD8, 1, NULL };
  SmProp *prop = &hint;

  gsm_verbose ("broadcast_restart_style(): client %p (connection %p)\n", client, client->connection);
  gsm_verbose_indent (TRUE);
  value[0] = style;
  val.value = &value;
  hint.vals = &val;
  
  client_property (client->handle, 1, &prop);    
  gsm_verbose_indent (FALSE);
}

static char
get_restart_style (Client *client)
{
  int style;

  if (!find_card8_property (client, SmRestartStyleHint, &style))
    style = SmRestartIfRunning;

  return (char)style;
}

void
free_client (Client *client)
{
  GSList *list; 

  gsm_verbose ("free_client(): %p (%s)\n", client, client->id ? client->id : "(unknown)");

  if(client->magic != CLIENT_MAGIC)
    {
       if(client->magic != CLIENT_DEAD)
               g_warning("free_client: Passed a bogon.");
       else    /* Its that common problem for hungry cannibal lawyers...who live on cheap Czech beer */
               g_warning("free_client: Passed a dead client.");
       g_assert_not_reached ();
    }

  client->magic = CLIENT_DEAD; /* Bang bang, you're dead etc */

  if (client->id != NULL)
    g_free (client->id);
  client->id = NULL; /* sanity */

  if (client->handle != NULL)
    command_handle_free (client->handle);
  client->handle = NULL; /* sanity */

  for (list = client->properties; list; list = list->next)
    {
      SmProp *sp = (SmProp *) list->data;
      list->data = NULL; /* sanity */

      SmFreeProperty (sp);
    }
  g_slist_free (client->properties);
  client->properties = NULL; /* sanity */

  for (list = client->get_prop_replies; list; list = list->next)
    {
      GSList *prop_list = (GSList *) list->data, *list1;
      list->data = NULL; /* sanity */
	
      for (list1 = prop_list; list1; list1 = list1->next)
	{
	  SmProp *prop = (SmProp *) list1->data;
	  list1->data = NULL; /* sanity */

	  SmFreeProperty (prop);
	}
      g_slist_free (prop_list);
    }
  g_slist_free (client->get_prop_replies);
  client->get_prop_replies = NULL; /* sanity */

  if (client->last_discard)
    SmFreeProperty (client->last_discard);
  client->last_discard = NULL;

  if (client->purge_timeout_id != 0)
    {
      g_source_remove (client->purge_timeout_id);
      client->purge_timeout_id = 0;
    }

  g_free (client);
}

/* Run a command on a client. */
static int
run_command_prop (Client     *client, 
		  const char *command, 
		  SmProp     *prop)
{
  int argc, envc, envpc;
  gboolean ok;
  char **argv, *dir, **envv, **envp, *restart_info;
  GPid child_pid;
  GError *error = NULL;
  int pid = -1;

  gsm_verbose ("run_command_prop(): client %p, connection %p, command %s\n", client, client->connection, command);
  gsm_verbose_indent (TRUE);

  if (gsm_parse_vector_prop (prop, &argc, &argv))
    {
      if (!find_string_property (client, SmCurrentDirectory, &dir))
	dir = NULL;
      
      if (find_vector_property (client, SmEnvironment, &envc, &envv)
	  && envc > 0
	  && envc % 2 == 0)
	{
	  int i;

	  envpc = envc / 2;
	  envp = g_new0 (char *, envpc + 1);
	  
	  for (i = 0; i < envpc; i++)
	    envp[i] = g_strconcat (envv[2*i], "=", envv[2*i + 1], NULL);
	}
      else
	{
	  envpc = 0;
	  envp = NULL;
	  /* we do not set envv and envc to NULL since this is
	   * now done correctly in find_vector_property */
	}

      /* We can't run this in the `if' because we might have allocated
	 ENVV in find_vector_property but rejected it for other
	 reasons.  */
      g_strfreev (envv);
      envv = NULL; /* sanity */
      envc = 0;

      splash_update (argv [0]);

      restart_info = NULL;
      find_string_property (client, GsmRestartService, &restart_info);
      ok = remote_start (restart_info, argv, dir, envp, &child_pid, &error);
      if (restart_info)
	g_free (restart_info);

      if (ok)
	pid = (int) child_pid;
      else
	{
	  if (strcmp (command, SmRestartCommand) || client->connection)
	    {
	      gchar *message;
	      message = g_strconcat (argv[0], " : Failed to execute : ",
				     error->message, NULL);
	      client_reasons (client, FALSE, 1, &message); 
	      g_free (message);
	    }
	  g_error_free (error);
	  pid = -1;
	}

      if (envp != NULL)
	g_strfreev (envp);
      if (dir != NULL)
	g_free (dir);
      g_strfreev (argv);
    }
  gsm_verbose_indent (FALSE);

  return pid;
}

int
run_command (Client      *client,
	     const gchar *command)
{
  SmProp *prop;

  prop = find_property_by_name (client, command);
  if (!prop)
    return -1;

  return run_command_prop (client, command, prop);
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

  gsm_verbose ("process_load_request()\n");
  gsm_verbose_indent (TRUE);
     
  /* Eliminate any chance of confusing clients in this client_list with 
   * members of the purge_drop_list by trimming back the purge_drop_list.
   * Bad matches in the purge_retain_list are extremely unlikely as 
   * session aware clients can only get into this list following a user
   * command and always move out of it again as soon as they register. */

  for (list = purge_drop_list; list;)
    {
      Client *client = (Client*)list->data;
      gboolean bad_match = FALSE;

      list = list->next;
	
      if (client->match_rule == MATCH_PROP)
	{
	  SmProp *prop = find_property_by_name (client, SmRestartCommand);
	  char* argv0 = (char*)(prop->vals[0].value);
	  bad_match = (find_client_by_argv0 (client_list, argv0) != NULL);
	}
      else
	bad_match = (find_client_by_id (client_list, client->id) != NULL);

      if (bad_match)
	{
	  gchar *message = _("Wait abandoned due to conflict."); 
	  client_reasons (client, FALSE, 1, &message); 
	  client_event (client->handle, GsmRemove);
	  gsm_verbose ("LIST_CHANGE: removing client %p from purge_drop_list\n", client);
	  REMOVE (purge_drop_list, client);
	  free_client (client);
	}
    }

  gsm_verbose ("LIST_CHANGE: moving client_list to start_list\n");
  start_list = client_list;
  save_state = STARTING_SESSION;
  save_state_changed ();

  update_save_state();
  gsm_verbose_indent (FALSE);
}

static gint		
compare_priority (gconstpointer a, gconstpointer b)
{
  Client *client_a = (Client*)a, *client_b = (Client*)b;
  return client_a->priority - client_b->priority;
}

/* Prepare to start clients in order of priority */
void
start_clients (GSList* client_list)
{
  gsm_verbose ("start_clients()\n");
  gsm_verbose_indent (TRUE);
  if (client_list)
    {
      client_list = g_slist_sort (client_list, compare_priority);

      if (save_state != MANAGER_IDLE)
	{
	  APPEND (load_request_list, client_list);
	  goto out;
	}
      else
	process_load_request (client_list);
    }
 out:
  gsm_verbose_indent (FALSE);
}


/* Purges clients from the pending list that are taking excessive
 * periods of time to register.
 * It would be nice to base this test on client CPU time but the 
 * client may be running on a remote host. */
static gint 
purge (gpointer data)
{
  Client* client = (Client*)data;

  gsm_verbose ("TIMEOUT: purge(); purging client %p (connection %p)\n", client, client->connection);
  print_client (client);
  gsm_verbose_indent (TRUE);

  client->purge_timeout_id = 0;

  if (g_slist_find (pending_list, client))
    {
      REMOVE (pending_list, client);
      if (client->match_rule != MATCH_ID)
	{
	  gsm_verbose ("putting in purge_retain_list\n");
	  APPEND (purge_retain_list, client);
	  broadcast_restart_style (client, SmRestartAnyway);
	}
      else
	{
	  APPEND (purge_drop_list, client);
	  gsm_verbose ("putting in purge_drop_list\n");
	  broadcast_restart_style (client, SmRestartNever);
	}
      client_event (client->handle, GsmUnknown);

      update_save_state();
    }
  else
    gsm_verbose ("not found in pending_list\n");
  gsm_verbose_indent (FALSE);
  return 0;
}

static void
reincarnate_client (Client *old_client,
		    Client *new_client)
{
  gsm_verbose ("reincarnate_client (old = %p (connection %p), new = %p (connection %p))\n",
	       old_client, old_client->connection,
	       new_client, new_client->connection);
  gsm_verbose_indent (TRUE);
  new_client->properties    = old_client->properties;
  new_client->priority      = old_client->priority;
  new_client->match_rule    = old_client->match_rule;
  new_client->attempts      = old_client->attempts;
  new_client->connect_time  = old_client->connect_time;
  new_client->command_data  = old_client->command_data;
  new_client->session_saved = old_client->session_saved;
  
  old_client->properties = NULL;

  if (old_client->handle)
    new_client->handle = command_handle_reassign (old_client->handle, new_client);
  old_client->handle = NULL;

  free_client (old_client);	  
  gsm_verbose_indent (FALSE);
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
  int pid;

  gsm_verbose ("start_client (%p, connection %p)\n", client, client->connection);
  gsm_verbose_indent (TRUE);

  client_event (client->handle, GsmStart);

  pid = run_command (client, SmRestartCommand);

  if (pid == -1)
    {
      client_event (client->handle, GsmRemove);
      free_client(client);
      goto out;
    }

  if (client->id || client->match_rule == MATCH_PROP)
    {
      APPEND (pending_list, client);
      if (purge_delay > 0)
	{
	  g_assert (client->purge_timeout_id == 0);
	  client->purge_timeout_id = g_timeout_add (purge_delay, purge, client);
	}
    }
  else
    {
      client_event (client->handle, GsmUnknown);
      broadcast_restart_style (client, SmRestartAnyway);
      APPEND (purge_retain_list, client);
    }
 out:
  gsm_verbose_indent (FALSE);
}

gint 
remove_client (Client* client)
{
  GSList* list;
  int retval;

  gsm_verbose ("remove_client (%p, connection %p)\n", client, client->connection);
  gsm_verbose_indent (TRUE);

  if (g_slist_find (interact_list, client))
    {
      retval = -1;
      goto out;
    }
  
  if (g_slist_find (live_list, client))
    {
      if (save_state != SHUTDOWN)
	{
	  set_client_restart_style (client, SmRestartNever);
	  SmsDie (client->connection);
	}
      else
	{
	  IceConn ice_conn = SmsGetIceConnection (client->connection);
	  
	  IceSetShutdownNegotiation (ice_conn, FALSE);
	  IceCloseConnection (ice_conn);
	  REMOVE (live_list, client);
	  client_event (client->handle, GsmRemove);
	  update_save_state ();
	}
      retval = 1;
      goto out;
    }

  for (list = save_request_list; list;)
    {
      SaveRequest *request = (SaveRequest*)list->data;

      list = list->next;

      if (request->client == client) {
	g_free(request);
	REMOVE (save_request_list, request);
      }
    }
  
  if (g_slist_find (save_yourself_list, client))
    {
      IceConn ice_conn = SmsGetIceConnection (client->connection);

      set_client_restart_style (client, SmRestartNever);
      SmsDie (client->connection);
      IceSetShutdownNegotiation (ice_conn, FALSE);
      IceCloseConnection (ice_conn);
      REMOVE (save_yourself_list, client);
      client_event (client->handle, GsmRemove);
      update_save_state ();
      retval = 1;
      goto out;
    }

  if (g_slist_find (save_yourself_p2_list, client))
    {
      IceConn ice_conn = SmsGetIceConnection (client->connection);

      set_client_restart_style (client, SmRestartNever);
      SmsDie (client->connection);
      IceSetShutdownNegotiation (ice_conn, FALSE);
      IceCloseConnection (ice_conn);
      REMOVE (save_yourself_p2_list, client);
      client_event (client->handle, GsmRemove);
      update_save_state ();
      retval = 1;
      goto out;
    }

  if (g_slist_find (zombie_list, client))
    {
      REMOVE (zombie_list, client);
      run_command (client, SmResignCommand);
      client_event (client->handle, GsmRemove);
      free_client (client);
      update_save_state ();
      retval = 1;
      goto out;
    }

  if (g_slist_find (purge_drop_list, client))
    {
      REMOVE (purge_drop_list, client);
      client_event (client->handle, GsmRemove);
      free_client (client);
      update_save_state ();
      retval = 1;
      goto out;
    }
  
  if (g_slist_find (purge_retain_list, client))
    {
      REMOVE (purge_retain_list, client);
      client_event (client->handle, GsmRemove);
      free_client (client);
      update_save_state ();
      retval = 1;
      goto out;
    }
  
  if (g_slist_find (pending_list, client))
    {
      REMOVE (pending_list, client);
      client_event (client->handle, GsmRemove);
      free_client (client);
      update_save_state ();
      retval = 1;
      goto out;
    }

  if (g_slist_find (start_list, client))
    {
      REMOVE (start_list, client);
      client_event (client->handle, GsmRemove);
      free_client (client);
      update_save_state ();
      retval = 1;
      goto out;
    }

  for (list = load_request_list; list; list = list->next)
    {
      GSList* client_list = (GSList*)list->data;
      
      if (g_slist_find (client_list, client))
	{
	  REMOVE (client_list, client);
	  if (client_list)
	    list->data = client_list;
	  else
	    load_request_list = g_slist_remove_link (load_request_list, list);
	  client_event (client->handle, GsmRemove);
	  free_client (client);
	  update_save_state ();
	  retval = 1;
	  goto out;
	}
    }

  retval = 0;

 out:
  gsm_verbose_indent (FALSE);
  return retval;
}


static Status
register_client (SmsConn connection, SmPointer data, char *previous_id)
{
  Client *client = (Client *) data;
  int retval;

  gsm_verbose ("register_client: %p, connection %p\n", client, connection);
  gsm_verbose_indent (TRUE);

  if (previous_id)
    {
      Client *offspring = find_client_by_id (pending_list, previous_id);
      if (! offspring)
	{
	  offspring = find_client_by_id (purge_drop_list, previous_id);
	  if (!offspring) 
	    {
	      offspring = find_client_by_id (purge_retain_list, previous_id);

	      if (!offspring) 
		{
		  /* FIXME: Inform user. */
		  free (previous_id);
		  retval = 0;
		  goto out;
		}
	      else
		REMOVE (purge_retain_list, offspring);
	    }
	  else
	    REMOVE (purge_drop_list, offspring);

	  if (offspring)
	    broadcast_restart_style (offspring, 
				     get_restart_style (offspring));
	}
      else
	REMOVE (pending_list, offspring);

      reincarnate_client (offspring, client);

      if (client->match_rule == MATCH_FAKE_ID ||
	  find_client_by_id (live_list, previous_id) ||
	  find_client_by_id (zombie_list, previous_id))
	{
	  client->match_rule = MATCH_DONE;
	  free (previous_id);
	  retval = 0;
	  goto out;
	}
      client->match_rule = MATCH_DONE;
      client->id = g_strdup (previous_id);
      free (previous_id);
    }
  else
    {
      char *id = SmsGenerateClientID (connection);

      if (id != NULL)
	{
	  client->id = g_strdup (id);
	  free (id);
	}
      else
	{
	  static long int sequence = 0;
	  static char* address = NULL;

	  if (! address)
	    {
	      g_warning ("Host name lookup failure on localhost.");
	      
	      address = g_new (char, 10);
	      srand (time (NULL) + (getpid () <<16));
	      g_snprintf (address, 10, "0%.8x", rand());
	    };

	  /* The typecast there is for 64-bit machines */
	  client->id = g_malloc (43);
	  g_snprintf (client->id, 43, "1%s%.13ld%.10ld%.4ld", address,
		      (long) time(NULL), (long) getpid (), sequence);
	  sequence++;
	  
	  sequence %= 10000;
	}
    }

  if (client->handle)
    client_event (client->handle, GsmConnected);

  if (! SmsRegisterClientReply (connection, client->id))
    g_error ("Could not register new client: %s\n", g_strerror(ENOMEM));

  APPEND (live_list, client);      

  /* SM specs 7.2: */
  if (!previous_id)
    save_yourself_request (connection, (SmPointer) client, 
			   SmSaveLocal, FALSE,
			   SmInteractStyleNone, FALSE, FALSE);
  update_save_state ();
  retval = 1;

 out:
  gsm_verbose_indent (FALSE);
  return retval;
}

static void
interact_request (SmsConn connection, SmPointer data, int dialog_type)
{
  Client *client = (Client *) data;

  gsm_verbose ("interact_request: %p, connection %p\n", client, connection);
  gsm_verbose_indent (TRUE);

  /* These seem to be the only circumstances in which interactions
     can be allowed without breaking the protocol.
     FIXME: Should we inform the user of bogus requests ? */
  if ((save_state == SAVE_PHASE_1 ||
       save_state == SAVE_PHASE_2) &&
      (g_slist_find (save_yourself_list, client) ||
       g_slist_find (save_yourself_p2_list, client)))
    {
      gboolean send = !interact_list;

      interact_list = g_slist_append (interact_list, client);

      if (send) 
	{
	  save_state = (save_state == SAVE_PHASE_1) 
	    ? SENDING_INTERACT
	    : SENDING_INTERACT_2;
	  save_state_changed ();
	  interact_ping_replied = TRUE;
	  SmsInteract (connection);
	  save_state = (save_state == SENDING_INTERACT)
	    ? SAVE_PHASE_1
	    : SAVE_PHASE_2;
	  save_state_changed ();
	}
    }
  gsm_verbose_indent (FALSE);
}

static void 
interact_ping_reply (IceConn ice_conn, IcePointer data)
{
  gsm_verbose ("interact_ping_reply(data %p)\n", data);
  if (interact_list && 
      interact_list->data == data)
    interact_ping_replied = TRUE;
}

/* Used to tell GUI when things are going slowly. The GUI may intervene
 * by removing the clients from the session. Clients that fail to respond
 * to a DIe are always removed. */
static gint
no_response_warning (gpointer data)
{
  GSList *list;
  gchar *message;
  gchar *reasons[3];
  Client* warner = get_warner ();

  switch (save_state)
    {
    case SAVE_CANCELLED:
      message = "ShutdownCancelled";
      break;

    case SAVE_PHASE_2:
      message = "SaveYourselfPhase2";
      break;
      
    case SAVE_PHASE_1:
      message = interact_list ? "Interact" : "SaveYourself";
      break;

    case SHUTDOWN:
      message = "Die";
      break;

    default:
      gsm_verbose ("TIMEOUT: no_response_warning in state UNKNOWN\n");
      /* not possible */
      return 0;
    }

  gsm_verbose ("TIMEOUT: no_response_warning in state %s, message %s\n", get_save_state(), message);
  gsm_verbose_indent (TRUE);

  reasons[0] = g_strdup_printf(_("No response to the %s command."), message);
  reasons[1] = _("The program may be slow, stopped or broken.");
  reasons[2] = _("You may wait for it to respond or remove it.");

  if (interact_list)
    {
      Client *client = (Client *)interact_list->data;

      if (interact_ping_replied)
	{
	  IceConn ice_conn;
	  gsm_verbose ("have interact_list; sending ping with data to client %p\n", client);
	  print_client (client);
	  ice_conn = SmsGetIceConnection (client->connection);
	  interact_ping_replied = FALSE;
	  IcePing (ice_conn, interact_ping_reply, (IcePointer)client);
	}
      else
	{
	  gsm_verbose ("have interact_list; not sending ping; calling client_reasons() on client %p instead\n", client);
	  print_client (client);
	  client_reasons (client, TRUE, 3, reasons);	  
	}
    }
  else
    {
      gsm_verbose ("don't have interact_list; calling client_reasons() on clients in the lists\n");
    switch (save_state)
      {
      case SAVE_CANCELLED:
      case SAVE_PHASE_2:
	gsm_verbose ("save_yourself_p2_list\n");
	for (list = save_yourself_p2_list; list; )
	  {
	    Client* client = (Client *) list->data;

	    list = list->next;

	    client_reasons (client, TRUE, 3, reasons);
	  }
	/* fall through */
	
      case SAVE_PHASE_1:
	gsm_verbose ("save_yourself_list\n");
	for (list = save_yourself_list; list;)
	  {
	    Client* client = (Client *) list->data;
	    list = list->next;
	    client_reasons (client, TRUE, 3, reasons);
	  }
	break;
	
      case SHUTDOWN:
	gsm_verbose ("live_list\n");
	for (list = live_list; list;)
	  {
	    Client* client = (Client *) list->data;
	    list = list->next;
	    if (client != warner)
	    client_reasons (client, TRUE, 3, reasons);
	  }
	break;
	
      default:
	/* not possible */
	g_assert_not_reached ();
	break;
      }
    }
  g_free (reasons[0]);
  gsm_verbose_indent (FALSE);
  return 1;
}

static void
interact_done (SmsConn connection, SmPointer data, gboolean cancel)
{
  Client *client = (Client *) data;

  gsm_verbose ("interact_done: %p (connection %p), cancel = %s\n", client, connection, cancel ? "TRUE" : "FALSE");
  gsm_verbose_indent (TRUE);

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
      save_state_changed ();
      shutting_down = FALSE;
      interact_list = NULL;

      send_message (&save_yourself_list, SmsShutdownCancelled);
      send_message (&save_yourself_p2_list, SmsShutdownCancelled);
      send_message (&save_finished_list, SmsShutdownCancelled);
    }

  if (interact_list)
    {
      save_state = (save_state == SAVE_PHASE_1)
	? SENDING_INTERACT
	: SENDING_INTERACT_2;
      save_state_changed ();
      client = (Client *) interact_list->data;
      interact_ping_replied = TRUE;
      SmsInteract (client->connection);
      save_state = (save_state == SENDING_INTERACT)
	? SAVE_PHASE_1
	: SAVE_PHASE_2;
      save_state_changed ();
    }
  /* Check in case the client sent a SaveYourselfDone during
     the interaction */ 
  update_save_state ();
  gsm_verbose_indent (FALSE);
}

static void
print_client (Client *client)
{
  GSList *l;

  gsm_verbose ("  id: %s\n", client->id ? client->id : "(unknown)");
  gsm_verbose ("  handle: %s\n", client->handle);
  gsm_verbose ("  connection: %p\n", client->connection);
  gsm_verbose ("  properties:\n");

  for (l = client->properties; l; l = l->next)
    {
      SmProp *p;
      int v;

      p = l->data;
      gsm_verbose ("    ((name \"%s\") (type \"%s\")", p->name, p->type);
      for (v = 0; v < p->num_vals; v++)
	{
	  int i;
	  guchar *c;

	  gsm_verbose (" (value \"");
	  c = (guchar *) p->vals[v].value;
	  for (i = 0; i < p->vals[v].length; i++)
	    if (c[i] < 0x80)
	      gsm_verbose ("%c", (int) c[i]);
	    else
	      gsm_verbose ("\\x%02x", (int) c[i]);

	  gsm_verbose ("\")");
	} gsm_verbose (")\n");
    }
}

static void
process_save_request (Client* client, int save_type, gboolean shutdown, 
		      int interact_style, gboolean fast, gboolean global)
{
  gsm_verbose ("process_save_request(): client %p (connection %p)\n", client, client->connection);
  gsm_verbose_indent (TRUE);

  gsm_verbose ("\ttype: %d\tshutting down: %d\tinteract: %d\n"
	       "\tfast: %d\tglobal: %d\n",
	       save_type, shutdown, interact_style, fast, global);

  save_state = SENDING_MESSAGES;
  save_state_changed ();
  global_save = global;
  if (global)
    {
      shutting_down = shutdown;
      if (shutting_down
	  && interact_style == SmInteractStyleAny
	  && ! fast
	  && ! maybe_display_gui ())
	{
	  /* Just ignore the save.  */
	  shutting_down = FALSE;
	  if (client && client->magic != CLIENT_MAGIC)
	    g_warning("Oh my god the dead clients are walking again, please report this!");
	}
      else
	{ 
	  if (!shutdown)
	    save_selected = TRUE;

	  /* If the client only requested a Global save (i.e.
	   * only save things like open documents and not
	   * session state), but our user requested that the
	   * session state be saved, we ammend the save type
	   * to a Global save.
	   */
	  if (save_type == SmSaveGlobal && (save_selected || autosave))
	    save_type = SmSaveBoth;

	  while (live_list) 
	    {
	      Client *tmp_client = (Client *) live_list->data;

	      gsm_verbose ("About to save client %p (connection %p):\n", tmp_client, tmp_client->connection);
	      print_client (tmp_client);

	      client_event (tmp_client->handle, GsmSave);
	      REMOVE (live_list, tmp_client);
	      APPEND (save_yourself_list, tmp_client);

	      tmp_client->last_discard = gsm_prop_copy (
			find_property_by_name (tmp_client, SmDiscardCommand));

	      SmsSaveYourself (tmp_client->connection, save_type, shutting_down,
			       interact_style, fast);
	    }
	}
    }
  else
    {
      client_event (client->handle, GsmSave);
      REMOVE (live_list, client);
      APPEND (save_yourself_list, client);

      client->last_discard = gsm_prop_copy (
		find_property_by_name (client, SmDiscardCommand));

      SmsSaveYourself (client->connection, 
		       save_type, 0, interact_style, fast);
    }

  if ((shutting_down || save_yourself_list))
    { 
      /* Give apps extra time on first save as they may call SmcOpenConnection
       * long before entering the select on the resulting connection. */
      gint delay = client->handle ? warn_delay : purge_delay;
      save_state = SAVE_PHASE_1;
      save_state_changed ();
      if (delay > 0)
	{
	  g_assert (warn_timeout_id == 0);
	  warn_timeout_id = g_timeout_add (delay, no_response_warning, NULL);
	}
    }
  else
    {
      save_state = MANAGER_IDLE;
      save_state_changed ();
    }
  update_save_state ();
  gsm_verbose_indent (FALSE);
}

static void
save_yourself_request (SmsConn connection, SmPointer data, int save_type,
		       gboolean shutdown, int interact_style, gboolean fast,
		       gboolean global)
{
  Client *client = (Client *) data;

  gsm_verbose ("save_yourself_request: client %p, connection %p; save_state %s\n", client, connection, get_save_state ());
  gsm_verbose_indent (TRUE);

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

  gsm_verbose_indent (FALSE);
}

static void
save_yourself_p2_request (SmsConn connection, SmPointer data)
{
  Client *client = (Client *) data;

  gsm_verbose ("save_yourself_p2_request: client %p, connection %p\n", client, connection);
  gsm_verbose_indent (TRUE);

  if (g_slist_find (save_yourself_list, client) != NULL) 
    {
      REMOVE (save_yourself_list, client);
      APPEND (save_yourself_p2_list, client);
      update_save_state ();
    }
  gsm_verbose_indent (FALSE);
}

/* A helper function.  Finds and executes each shutdown command.  */
static void
run_shutdown_commands (const GSList *list)
{
  for (; list; list = list->next)
    {
      Client *client = (Client *) list->data;

      gsm_verbose ("run_shutdown_commands on client %p, connection %p\n", client, client->connection);
      run_command (client, SmShutdownCommand);
    }
}

/* This is a helper function which makes sure that the save_state
   variable is correctly set after a change to the save_yourself lists. */
static void
update_save_state (void)
{
  gsm_verbose ("update_save_state() starts with %s\n", get_save_state ());
  gsm_verbose_indent (TRUE);

  if (save_state == STARTING_SESSION)
    {
      unsigned int runlevel = 1000;
      
      if (pending_list)
	{
	  Client* client = (Client*)pending_list->data;
	  if (client->match_rule != MATCH_PROP)
	    goto out;
	}

      if (start_list)
	{
	  while (start_list)
	    {
	      Client* client = (Client*)start_list->data;
	      
	      if (client->priority > runlevel) 
		goto out;
	      
	      REMOVE (start_list, client);
	      if (pending_list)
		runlevel = client->priority;
	      start_client (client);
	    }
	  if (pending_list)
	    goto out;
	}
      save_state = MANAGER_IDLE;
      save_state_changed ();
      splash_stop ();
     }    

  if (save_state == SAVE_PHASE_1)
    {
      if (save_yourself_list || interact_list)
	goto out;

      if (save_yourself_p2_list)
	{
	  save_state = SENDING_MESSAGES;
	  save_state_changed ();
	  send_message (&save_yourself_p2_list, SmsSaveYourselfPhase2);

	  if (warn_timeout_id != 0)
	    {
	      g_source_remove (warn_timeout_id);
	      warn_timeout_id = 0;
	    }
	  if (warn_delay > 0)
	    {
	      g_assert (warn_timeout_id == 0);
	      warn_timeout_id = g_timeout_add (warn_delay,
				               no_response_warning, NULL);
	    }
	}
      save_state = SAVE_PHASE_2;
      save_state_changed ();
    }
 
  if (save_state == SAVE_PHASE_2)
    {
      if (save_yourself_p2_list)
	goto out;

      if (warn_timeout_id != 0)
	{
	  g_source_remove (warn_timeout_id);
	  warn_timeout_id = 0;
	}

      CONCAT (live_list, g_slist_copy (save_finished_list));
	  
      if (shutting_down)
	{
	  Client *warner = get_warner();
	  GSList *list;

	  while (purge_drop_list)
	    {
	      Client *client = (Client*)purge_drop_list->data;

	      client_event (client->handle, GsmRemove);
	      REMOVE (purge_drop_list, client);
	      free_client (client);
	    }

	  maybe_write_session ();

	  save_finished_list
	    = g_slist_reverse (g_slist_sort (save_finished_list,
					     compare_priority));
	  for (list = save_finished_list; list;)
	    {
	      Client *client = list->data;

	      list = list->next;
	      if (client != warner)
		SmsDie (client->connection);
	    }
	  g_slist_free (save_finished_list);
	  save_finished_list = NULL;
	  save_state = SHUTDOWN;
	  save_state_changed ();
	  /* Run each shutdown command. These commands are only strictly
	   * needed by zombie clients. */
	  run_shutdown_commands (zombie_list);
	  run_shutdown_commands (live_list);

	  if (suicide_delay > 0)
	    {
	      g_assert (warn_timeout_id == 0);
	      warn_timeout_id = g_timeout_add (suicide_delay,
					       no_response_warning, NULL);
	    }
	}
      else
	{
	  maybe_write_session ();

	  save_state = SENDING_MESSAGES;
	  save_state_changed ();
	  send_message (&save_finished_list, SmsSaveComplete);
	  g_slist_free (save_finished_list);
	  save_finished_list = NULL;
	  save_state = MANAGER_IDLE;
	  save_state_changed ();
	}
    }

  if (save_state == SHUTDOWN)
    {
      if (! live_list)
	gtk_main_quit ();
      else if (!live_list->next)
	{
	  Client* client = (Client *) live_list->data;
	  if (client == get_warner())
	    SmsDie (client->connection);
	}
    }

  if (save_state == SAVE_CANCELLED)
    {
      if (save_yourself_list || save_yourself_p2_list)
	goto out;

      if (warn_timeout_id != 0)
	{
	  g_source_remove (warn_timeout_id);
	  warn_timeout_id = 0;
	}

      CONCAT (live_list, save_finished_list);
      save_finished_list = NULL;
      maybe_write_session ();

      save_state = MANAGER_IDLE;
      save_state_changed ();
    }

  if (save_state == MANAGER_IDLE)
    {
      if (save_request_list)
	{
	  SaveRequest *request = save_request_list->data;
	  
	  REMOVE (save_request_list, request);
	  process_save_request (request->client, request->save_type, 
				request->shutdown, request->interact_style, 
				request->fast, request->global);
	  g_free (request);
	}
    }

  if (save_state == MANAGER_IDLE)
    {
      if (load_request_list)
	{
	  GSList* client_list = (GSList*)load_request_list->data;
	  
	  REMOVE (load_request_list, client_list);
	  process_load_request (client_list);
	}
    }

 out:
  gsm_verbose ("ends with %s\n", get_save_state ());
  gsm_verbose_indent (FALSE);
}

/*
 * The basic rule here is that every time we
 * 'forget' a RestartCommand we need to run
 * the DiscardCommand.
 */
static void
maybe_run_discard_commands (Client *client)
{
  SmProp *discard;

  gsm_verbose ("maybe_run_discard_commands (client %p, connection %p)\n", client, client->connection);
  gsm_verbose_indent (TRUE);

  discard = find_property_by_name (client, SmDiscardCommand);

  /* Broken Client: not saving its session data on a
   * per-SaveYourself basis.
   * See 'Advice to Implementors' under the SaveYourself
   * section in the XSMP spec.
   */
   if (gsm_prop_compare (client->last_discard, discard))
     goto out;

   if (client->last_discard && !client->session_saved)
     run_command_prop (client, SmDiscardCommand, client->last_discard);

   if (discard && shutting_down && !(autosave || save_selected))
     run_command_prop (client, SmDiscardCommand, discard);

 out:
   gsm_verbose_indent (FALSE);
}

static void
save_yourself_done (SmsConn connection, SmPointer data, gboolean success)
{
  Client *client = (Client *) data;
  GSList *prop_list = NULL;

  gsm_verbose ("save_yourself_done: client %p, connection %p\n", client, connection);
  gsm_verbose_indent (TRUE);

  if (save_state == MANAGER_IDLE || save_state == STARTING_SESSION ||
      (g_slist_find (save_yourself_list, client) == NULL &&
       g_slist_find (save_yourself_p2_list, client) == NULL))
    {
      /* The use of timeouts in the save process introduces the possibility
       * that these are messages which the user was too impatient to wait
       * for ... the clients concerned should have received die messages... */
      goto out;
    }

  if (client->match_rule == MATCH_WARN)
    {
      client_event (client->handle, GsmRemove);
      command_handle_free (client->handle);
      client->handle = NULL;
    }

  /* Must delay assignment of handle for gsm protocol extensions until
   * the initial save is complete on external clients so that we can
   * match them against MATCH_PROP clients using properties. */
  if (!client->handle)
    {
      Client *offspring = NULL;
      SmProp *prop = find_property_by_name (client, SmRestartCommand);

      if (prop)
	{
	  gchar* argv0 = (gchar*)prop->vals[0].value;
	  offspring = find_client_by_argv0 (pending_list, argv0);
	  if (! offspring)
	    {
	      offspring = find_client_by_argv0 (purge_drop_list, argv0);
	      if (!offspring) 
		{
		  offspring = find_client_by_argv0 (purge_retain_list, argv0);
		  
		  if (offspring) 
		    REMOVE (purge_retain_list, offspring);		    
		}
	      else
		REMOVE (purge_drop_list, offspring);

	      if (offspring)
		broadcast_restart_style (offspring, 
					 get_restart_style (offspring));
	    }
	  else
	    REMOVE (pending_list, offspring);
	}

      /* Save properties that have NOT been reported under the gsm client
       * event protocol because the client has had no handle. */
      prop_list = client->properties;
      client->properties = NULL;

      if (offspring)
	{
	  reincarnate_client (offspring, client);
	  client->match_rule = MATCH_DONE;
	}
      else
	client->handle = command_handle_new (client);
    }

  client_event (client->handle, GsmConnected);

  if (prop_list)
    {
      GSList *list;
      SmProp **props;
      guint i, nprops = g_slist_length (prop_list);
      
      /* Set any properties missed by the client event protocol (overriding
       * the properties set in the GsmAddClient command as appropriate). */
      props = (SmProp**) malloc (sizeof (SmProp*)*nprops);
      for (list = prop_list, i = 0; list; list=list->next, i++)
	props[i] = (SmProp*)list->data;
      g_slist_free (prop_list);
      set_properties(client->connection, (SmPointer)client, nprops, props);
    }

  maybe_run_discard_commands (client);

  REMOVE (save_yourself_list, client);
  REMOVE (save_yourself_p2_list, client);
  APPEND (save_finished_list, client);      
  
  update_save_state ();

 out:
  gsm_verbose_indent (FALSE);
}

/* We call IceCloseConnection in response to SmcCloseConnection to prevent
 * any use of the connection until we exit IceProcessMessages.
 * The actual clean up occurs in client_clean_up when the IceConn closes. */
static void
close_connection (SmsConn connection, SmPointer data, int count,
		  char **reasons)
{
  Client *client = (Client *) data;
  IceConn ice_conn = SmsGetIceConnection (connection);

  gsm_verbose ("close_connection: client %p, connection %p\n", client, connection);
  gsm_verbose_indent (TRUE);

  client_reasons (client, FALSE, count, reasons);
  SmFreeReasons (count, reasons);

  IceSetShutdownNegotiation (ice_conn, FALSE);
  IceCloseConnection (ice_conn);

  gsm_verbose_indent (FALSE);
}

/* This clean up handler is called when the IceConn for the client is closed
 * for some reason. This seems to be the only way to avoid memory leaks 
 * without the risk of segmentation faults. */
static void
client_clean_up (Client* client)
{
  int style;
  GSList *list;
  
  gsm_verbose ("client_clean_up: client %p, connection %p\n", client, client->connection);
  gsm_verbose_indent (TRUE);

  command_clean_up (client);
  
  SmsCleanUp (client->connection);
  
  client->connection = NULL;

  REMOVE (live_list, client);
  REMOVE (save_yourself_list, client);
  REMOVE (save_yourself_p2_list, client);
  REMOVE (save_finished_list, client);
  REMOVE (message_sent_list, client);
  
  while (g_slist_find (interact_list, client))
    REMOVE (interact_list, client);
  
  for (list = save_request_list; list;)
    {
      SaveRequest *request = (SaveRequest*)list->data;
      list = list->next;
      
      if (request->client == client) {
	g_free(request);
	REMOVE (save_request_list, request);
      }
    }
  
  style = shutting_down ? SmRestartNever : get_restart_style (client);
  
  switch (style)
    {
    case SmRestartImmediately:
      {
	time_t now = time (NULL);
	
	if (now > client->connect_time + 120)
	  {
	    client->attempts = 0;
	    client->connect_time = now;
	  }
	
	if (client->attempts++ < 10)
	  start_client (client);
	else
	  {
	    gchar *message = _("Restart abandoned due to failures.");
	    /* This is a candidate for a confirm = TRUE. */
	    client_reasons (client, FALSE, 1, &message); 
	    client_event (client->handle, GsmRemove);
	    free_client (client);
	  }
      }
      break;
      
    case SmRestartAnyway:
      
      client_event (client->handle, GsmInactive);
      APPEND (zombie_list, client);
      break;
      
    default:
      
      client_event (client->handle, GsmRemove);
      free_client (client);
      break;
    }
  
  if (save_state == SENDING_INTERACT || save_state == SENDING_INTERACT_2)
    {
      if (interact_list)
	{
	  client = (Client *) interact_list->data;
	  interact_ping_replied = TRUE;
	  SmsInteract (client->connection);
	}
      save_state = (save_state == SENDING_INTERACT)
	? SAVE_PHASE_1
	: SAVE_PHASE_2;
      save_state_changed ();
    }
  
  update_save_state ();

  gsm_verbose_indent (FALSE);
}

/* This extends the standard SmsSetPropertiesProc by interpreting attempts
 * to set properties with the name GsmCommand as commands. These
 * properties are not added to the clients property list and using them
 * results in a change to the semantics of the SmsGetPropertiesProc
 * (until a GsmSuspend command is received). */
static void
set_properties (SmsConn connection, SmPointer data, int nprops, SmProp **props)
{
  Client *client = (Client *) data;
  int i;

  gsm_verbose ("set_properties: client %p, connection %p\n", client, connection);
  gsm_verbose_indent (TRUE);

  if (!nprops)
    goto out;

  if (!strcmp (props[0]->name, GsmCommand))
    {
      command (client, nprops, props);
    }
  else
    {
      client_property (client->handle, nprops,  props);

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

 out:
  gsm_verbose_indent (FALSE);
}

static void
delete_properties (SmsConn connection, SmPointer data, int nprops,
		   char **prop_names)
{
  Client *client = (Client *) data;
  int i;

  gsm_verbose ("delete_properties: client %p, connection %p\n", client, connection);
  gsm_verbose_indent (TRUE);

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

  gsm_verbose_indent (FALSE);
}

/* While the GsmCommand protocol is active the properties that are
 * returned represent the return values from the commands. All commands
 * return themselves as the first property returned. Commands need not
 * return values in the order in which they were invoked. The standard 
 * SmsGetPropertiesProc behavior may be restored at any time by suspending
 * the protocol using a GsmSuspend command. The next GsmCommand will 
 * then resume the protocol as if nothing had happened. */
static void
get_properties (SmsConn connection, SmPointer data)
{
  Client *client = (Client *) data;
  GSList *prop_list, *list;

  gsm_verbose ("get_properties: client %p, connection %p\n", client, connection);
  gsm_verbose_indent (TRUE);

  client->get_prop_requests++;

  if (! command_active (client))
    send_properties (client, client->properties);
  else if (client->get_prop_replies)
    {
      prop_list = (GSList*) client->get_prop_replies->data;

      send_properties (client, prop_list);
      REMOVE (client->get_prop_replies, prop_list);

      for (list = prop_list; list; list = list->next)
	{
	  SmProp *prop = (SmProp *) list->data;
	  SmFreeProperty (prop);
	}
      g_slist_free (prop_list);
    }

  gsm_verbose_indent (FALSE);
}

void
send_properties (Client* client, GSList *prop_list)
{
  gsm_verbose ("send_properties: client %p, connection %p\n", client, client->connection);
  gsm_verbose_indent (TRUE);

  if (! client->get_prop_requests)
    APPEND (client->get_prop_replies, prop_list);
  else
    {
      GSList *list;
      SmProp **props;
      int i, len;
      
      len = g_slist_length (prop_list);
      props = (SmProp**)g_new0 (SmProp *, len);
      
      for (i = 0, list = prop_list; list; i++, list = list->next)
	props[i] = list->data;
      
      SmsReturnProperties (client->connection, len, props);
      g_free (props);      
      
      client->get_prop_requests--;
    }

  gsm_verbose_indent (FALSE);
}


/* This is run when a new client connects.  We register all our
   callbacks.  */
Status
new_client (SmsConn connection, SmPointer data, unsigned long *maskp,
	    SmsCallbacks *callbacks, char **reasons)
{
  Client *client;
  int retval;

  gsm_verbose ("new_client(): connection %p\n", connection);
  gsm_verbose_indent (TRUE);

  if (shutting_down)
    {
      *reasons  = strdup (_("A session shutdown is in progress."));
      retval = 0;
      goto out;
    }

  client = (Client*)g_new0 (Client, 1);
  client->magic = CLIENT_MAGIC;
  client->priority = DEFAULT_PRIORITY;
  client->connection = connection;
  client->attempts = 1;
  client->connect_time = time (NULL);
  client->warning = FALSE;

  ice_set_clean_up_handler (SmsGetIceConnection (connection),
			    (GDestroyNotify)client_clean_up, (gpointer)client);

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

  retval = 1;

  gsm_verbose ("created client %p, connection %p\n", client, client->connection);

 out:
  gsm_verbose_indent (FALSE);
  return retval;
}


/* This function is exported to the rest of gsm.  It sets up and
   performs a global save */
void
save_session (int save_type, gboolean shutdown, int interact_style,
	      gboolean fast)
{
  gsm_verbose ("save_session()\n");
  gsm_verbose_indent (TRUE);

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

      gsm_verbose ("delaying because we are not idle\n");
    }
  else
    {
      gsm_verbose ("initiating session save\n");
      process_save_request (NULL, save_type, shutdown, 
			    interact_style, fast, TRUE);
    }

  gsm_verbose_indent (FALSE);
}
