/* save.c - Code to save session.

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
#include <glib.h>
#include <string.h>
#include <gtk/gtk.h>

#include "libgnome/libgnome.h"
#include "libgnomeui/gnome-client.h"

#include "save.h"
#include "session.h"
#include "prop.h"
#include "command.h"

/* These are the only ones which we use at present but eventually we
 * will need to save all the properties (when they have been set)
 * because clients are not obliged to set their properties every time
 * that they run. */
static propsave properties[] =
{
  { SmRestartStyleHint, NUMBER, 0, SmRestartStyleHint },
  { GsmPriority,        NUMBER, 0, "Priority" },
  { GsmRestartService,  STRING, 0, "RestartService" },
  { SmProgram,          STRING, 0, SmProgram },
  { SmCurrentDirectory, STRING, 0, SmCurrentDirectory },
  { SmDiscardCommand,   STRING, 0, XsmDiscardCommand }, /* for legacy apps */
  { SmDiscardCommand,   VECTOR, 0, SmDiscardCommand },
  { SmCloneCommand,     VECTOR, 0, SmCloneCommand },
  { SmRestartCommand,   VECTOR, 1, SmRestartCommand },
  { SmEnvironment,      VECTOR, 0, SmEnvironment }
};

#define NUM_PROPERTIES (sizeof (properties) / sizeof (propsave))


/* Write a single client to the current session.  Return TRUE on success,
   FALSE on failure.  */

static gint
write_one_client (const Client *client)
{
  gint  i, vector_count, string_count, number_count;
  gboolean saved;
  gint  argcs[NUM_PROPERTIES];
  gchar **vectors[NUM_PROPERTIES];
  gchar *strings[NUM_PROPERTIES];
  gint  numbers[NUM_PROPERTIES];
  gchar *vector_names[NUM_PROPERTIES];
  gchar *string_names[NUM_PROPERTIES];
  gchar *number_names[NUM_PROPERTIES];

  /* Retrieve each property we care to save.  */
  saved = TRUE;
  vector_count = string_count = number_count = 0;
  for (i = 0; (i < NUM_PROPERTIES) && saved; ++i)
    {
      gboolean found = FALSE;

      switch (properties[i].type)
	{
	case VECTOR:
	  found = find_vector_property (client, properties[i].name,
					&argcs[vector_count], 
					&vectors[vector_count]);
	  if (found)
	    vector_names[vector_count++] = (char*) properties[i].save_name;
	  break;
	  
	case STRING:
	  found = find_string_property (client, properties[i].name,
					&strings[string_count]);
      
	  if (found)
	    string_names[string_count++] = (char*)properties[i].save_name;
	  break;

	case NUMBER:
	  found = find_card8_property (client, properties[i].name,
				       &numbers[number_count]);
	  if (found)
	    {
	      saved = !(!strcmp(properties[i].name, SmRestartStyleHint) &&
			numbers[number_count] == SmRestartNever);
	      
	      number_names[number_count++] = (char*)properties[i].save_name;
	    }
	  break;
	default:
	  g_assert_not_reached();
	  break;
	}
      if (properties[i].required && !found)
	saved = FALSE;
    }

  /* Write each property we found.  */
  if (saved)
    {
      if (client->id)
	gnome_config_set_string ("id", client->id);

      for (i = 0; i < number_count; ++i) 
	gnome_config_set_int (number_names[i], numbers[i]);

      for (i = 0; i < string_count; ++i) 
	gnome_config_set_string (string_names[i], strings[i]);

      for (i = 0; i < vector_count; ++i) 
	gnome_config_set_vector (vector_names[i], argcs[i], 
      				 (const char * const *) vectors[i]);
    }

  /* Clean up.  */
  for (i = 0; i < vector_count; ++i)
    g_strfreev (vectors[i]);
  for (i = 0; i < string_count; ++i)
    g_free (strings[i]);

  return saved;
}

/* Write our session data.  */
void
write_session (void)
{
  char prefix[1024];
  int i = 0;
  GSList *list = NULL;

  delete_session (session_name);

  for (list = zombie_list; list; list = list->next)
    {
      Client *client = (Client *) list->data;
      
      g_snprintf (prefix, sizeof(prefix), 
		  CONFIG_PREFIX "%s/%d,", session_name, i);
      gnome_config_push_prefix (prefix);
      i += (write_one_client (client));
      gnome_config_pop_prefix ();
    }

  for (list = live_list; list; list = list->next)
    {
      Client *client = (Client *) list->data;
      
      g_snprintf (prefix, sizeof(prefix),
		  CONFIG_PREFIX "%s/%d,", session_name, i);

      gnome_config_push_prefix (prefix);
      i += (write_one_client (client));
      gnome_config_pop_prefix ();
    }

  for (list = pending_list; list; list = list->next)
    {
      Client *client = (Client *) list->data;
      
      g_snprintf (prefix, sizeof(prefix),
		  CONFIG_PREFIX "%s/%d,", session_name, i);

      gnome_config_push_prefix (prefix);
      i += (write_one_client (client));
      gnome_config_pop_prefix ();
    }

  for (list = purge_drop_list; list; list = list->next)
    {
      Client *client = (Client *) list->data;
      
      g_snprintf (prefix, sizeof(prefix),
		  CONFIG_PREFIX "%s/%d,", session_name, i);
      gnome_config_push_prefix (prefix);
      i += (write_one_client (client));
      gnome_config_pop_prefix ();
    }

  for (list = purge_retain_list; list; list = list->next)
    {
      Client *client = (Client *) list->data;
      
      g_snprintf (prefix, sizeof(prefix),
		  CONFIG_PREFIX "%s/%d,", session_name, i);
      gnome_config_push_prefix (prefix);
      i += (write_one_client (client));
      gnome_config_pop_prefix ();
    }

  if (i > 0)
    {
      g_snprintf (prefix, sizeof(prefix), 
		  CONFIG_PREFIX "%s/" CLIENT_COUNT_KEY, session_name);
      gnome_config_set_int (prefix, i);
      gnome_config_sync ();
    }
}

/* We need to read clients as well as writing them because the
 * protocol does not oblige clients to specify their properties
 * every time that they run. */
static void
read_one_client (Client *client)
{
  int i, j;
  gboolean def;
  gchar* id;

  client->id = NULL;
  client->properties = NULL;
  client->priority = DEFAULT_PRIORITY;
  client->handle = command_handle_new ((gpointer)client);
  client->warning = FALSE;
  client->get_prop_replies = NULL;
  client->get_prop_requests = 0;
  client->command_data = NULL;

  id = gnome_config_get_string ("id");
  if (id)
    {
      client->id = id;
    }
  /* Read each property that we save.  */
  for (i = 0; i < NUM_PROPERTIES; ++i)
    {
      gint  argc;
      gchar **vector;
      gchar *string;
      gint  number;

      switch (properties[i].type)
	{
	case VECTOR:
	  gnome_config_get_vector_with_default (properties[i].save_name, 
						&argc, &vector, &def);
	  if (!def)
	    {
	      SmProp *prop = (SmProp*) malloc (sizeof (SmProp));
	      prop->name = strdup (properties[i].name);
	      prop->type = strdup (SmLISTofARRAY8);
	      prop->num_vals = argc;
	      prop->vals = (SmPropValue*) malloc (sizeof (SmPropValue) * argc);
	      for (j = 0; j < argc; j++)
		{
		  prop->vals[j].length = vector[j] ? strlen (vector[j]) : 0;
		  prop->vals[j].value  = vector[j] ? strdup (vector[j]) : NULL;
		  g_free (vector[j]);
		  vector[j] = NULL; /* sanity */
		}
	      APPEND (client->properties, prop);      
	      g_free (vector);
	    }
	  break;

	case STRING:
	  string =
	    gnome_config_get_string_with_default (properties[i].save_name, 
						  &def);
	  if (!def)
	    {
	      SmProp *prop = (SmProp*) malloc (sizeof (SmProp));
	      prop->name = strdup (properties[i].name);
	      prop->type = strdup (SmARRAY8);
	      prop->num_vals = 1;
	      prop->vals = (SmPropValue*) malloc (sizeof (SmPropValue));
	      prop->vals[0].length = strlen (string);
	      prop->vals[0].value = strdup (string);
	      g_free (string);
	      APPEND (client->properties, prop);      
	    }
	  break;

	case NUMBER:
	  number =
	    gnome_config_get_int_with_default (properties[i].save_name, 
					       &def);
	  if (!def)
	    {
	      SmProp *prop = (SmProp*) malloc (sizeof (SmProp));
	      gchar* value = (gchar*) calloc (sizeof (gchar), 2);
	      value[0] = (gchar) number;
	      prop->name = strdup (properties[i].name);
	      prop->type = strdup (SmCARD8);
	      prop->num_vals = 1;
	      prop->vals = (SmPropValue*) malloc (sizeof (SmPropValue));
	      prop->vals[0].length = 1;
	      prop->vals[0].value = (SmPointer) value;
	      APPEND (client->properties, prop);      

	      if (!strcmp(properties[i].name, GsmPriority))
		client->priority = number;
	    }
	  break;
	default:
	  break;
	}
    }
}



/* Read the session clients recorded in a config file section */
static GSList *
read_clients (const char* file, const char *session, MatchRule match_rule)
{
  int i, num_clients;
  char prefix[1024];
  GSList* list = NULL;

  g_snprintf (prefix, sizeof(prefix), "%s%s/", file, session);

  gnome_config_push_prefix (prefix);
  num_clients = gnome_config_get_int (CLIENT_COUNT_KEY "=0");
  gnome_config_pop_prefix ();

  for (i = 0; i < num_clients; ++i)
    {
      Client *client = (Client*)g_new0 (Client, 1);
      client->magic = CLIENT_MAGIC;
      g_snprintf (prefix, sizeof(prefix), "%s%s/%d,", file, session, i);

      gnome_config_push_prefix (prefix);
      read_one_client (client);
      gnome_config_pop_prefix ();
      client->match_rule = match_rule;
      APPEND (list, client);
    }
  return list;
}


/* frees lock on session at shutdown */
void 
unlock_session (void)
{
  g_free(session_name);
  session_name = NULL;
}

/* Attempts to set the session name in options file (the requested 
   name may be locked).  
   Returns the name that has been assigned to the session. */

void
set_sessionsave_mode (gboolean sessionsave)
{
      session_save = sessionsave;
}

void 
set_autosave_mode (gboolean auto_save_mode)
{
      autosave = auto_save_mode;
      gnome_config_push_prefix (GSM_OPTION_CONFIG_PREFIX);
      gnome_config_set_bool (AUTOSAVE_MODE_KEY, auto_save_mode);
      gnome_config_pop_prefix ();
      gnome_config_sync (); 
}

void
set_session_name (const char *name)
{
  if (name)
    {
      char *session_name_env;
      session_name = g_strdup(name);
      gnome_config_push_prefix (GSM_OPTION_CONFIG_PREFIX);
      gnome_config_set_string (CURRENT_SESSION_KEY, name);
      gnome_config_pop_prefix ();
      gnome_config_sync ();
      session_name_env = g_strconcat ("GNOME_SESSION_NAME=", g_strdup (name), NULL);
      putenv (session_name_env);
    }
}

gchar*
get_current_session(void) 
{
  if(session_name) {
      return session_name;
  }
  else {
    gchar *current;
    gnome_config_push_prefix (GSM_OPTION_CONFIG_PREFIX);
    current = gnome_config_get_string (CURRENT_SESSION_KEY "=" DEFAULT_SESSION);
    gnome_config_pop_prefix();
    return current;
  } 
}


/* Load a session from the config file by name. */
Session*
read_session (const char *name)
{
  GSList *list = NULL;
  Session *session = g_new0 (Session, 1);

  session->name   = g_strdup (name);
  session->handle = command_handle_new ((gpointer)session);

  if(name) {
    if (!strcmp (name, FAILSAFE_SESSION)) 
      list = read_clients (DEFAULT_CONFIG_PREFIX,DEFAULT_SESSION,MATCH_FAKE_ID);
    else 
      list = read_clients (CONFIG_PREFIX,name,MATCH_ID);
    if (!list)
      list = read_clients (DEFAULT_CONFIG_PREFIX,name,MATCH_FAKE_ID);
  } 
  
  /* If the the specified session from the command line doesn't
   * exist, load the clients off the default session */
  if(!list) 
      list = read_clients (DEFAULT_CONFIG_PREFIX,DEFAULT_SESSION,MATCH_FAKE_ID);  

  session->client_list = list;

  /* See if there is a list of clients that the user has manually
   * created from the session-properties capplet
   */
  list = read_clients (MANUAL_CONFIG_PREFIX, name, MATCH_PROP);
  if (list)
	  session->client_list = g_slist_concat (session->client_list, list);

  return session;
}

void
start_session (Session* session)
{
  /* Do not copy discard commands between sessions. */
  if (strcmp (session->name, session_name))
    {
      GSList* list = session->client_list;

      for (; list; list = list->next)
	{
	  Client* client = (Client*)list->data;
	  GSList* prop_list = client->properties;

	  for (; prop_list; prop_list = prop_list->next)
	    {
	      SmProp* prop = (SmProp*)prop_list->data;
	      
	      if (!strcmp (prop->name, SmDiscardCommand))
		{
		  REMOVE (client->properties, prop);
		  break;
		}
	    }
	}
    }

  start_clients (session->client_list);
  session->client_list = NULL;
  free_session (session);
}

void
free_session (Session* session)
{
  while (session->client_list)
    {
      Client *client1 = (Client*)session->client_list->data;
      REMOVE (session->client_list, client1);
      free_client (client1);		  
    }	      
  command_handle_free (session->handle);  
  session->handle = NULL;  /* sanity */
  g_free (session->name);
  session->name = NULL; /* sanity */
  g_free (session);
}

/* Delete a session. 
 * This needs to be done carefully because we have to discard stale
 * session data and only a few of the clients may have conducted
 * a save since the session was last written */
void
delete_session (const char *name)
{
  int i, number;
  gboolean ignore;
  char prefix[1024];

  if (! name)
    return;

  /* FIXME: need to obtain a lock on a session name before deleting it! */

  g_snprintf (prefix, sizeof(prefix), 
	      CONFIG_PREFIX "%s/" CLIENT_COUNT_KEY "=0", name);
  number = gnome_config_get_int (prefix);

  /* For each client in the deleted session */
  for (i = 0; i < number; ++i)
    {
      Client* cur_client;
      Client* old_client = (Client*)g_new0 (Client, 1);
      
      int old_argc;
      char **old_argv;
      char *old_system;
      
      old_client->magic = CLIENT_MAGIC;
      
      g_snprintf (prefix, sizeof(prefix), CONFIG_PREFIX "%s/%d,", name, i);
      gnome_config_push_prefix (prefix);
	  
      /* To call these commands in a network independent fashion
	 we need to read almost everything anyway (: */
      read_one_client (old_client);

      /* Only delete data for clients that were connected. */
      if (old_client->id)
	{	  
	  /* Is this client still part of the running session ? */
	  cur_client = find_client_by_id (zombie_list, old_client->id);
	  if (!cur_client) 
	    cur_client = find_client_by_id (live_list, old_client->id);
	  /* Or perhaps one that we are restarting ? */
	  if (!cur_client) 
	    cur_client = find_client_by_id (pending_list, old_client->id);
	  if (!cur_client) 
	    cur_client = find_client_by_id (purge_drop_list, old_client->id);
	  if (!cur_client) 
	    cur_client = find_client_by_id (purge_retain_list, old_client->id);
	  
	  if (find_vector_property (old_client, SmDiscardCommand, 
				    &old_argc, &old_argv))
	    {
	      int cur_argc;
	      char **cur_argv;
	      
	      /* Do not call discard commands on running clients which 
	       * have not changed their discard commands. These clients
	       * did not take part in the save or are broken in some way. */
	      ignore = FALSE;
	      
	      if (cur_client && 
		  find_vector_property (cur_client, SmDiscardCommand, 
					&cur_argc, &cur_argv))
		{
		  int j;
		  ignore = (old_argc == cur_argc);
		  
		  if (ignore)
		    for (j = 0; j < old_argc; j++)
		      if (strcmp (old_argv[j], cur_argv[j]))
			{ 
			  ignore = FALSE;
			  break;
			}
		  
		  g_strfreev (cur_argv);
		}
	      
	      if (! ignore) 
		run_command (old_client, SmDiscardCommand);
	      
	      g_strfreev (old_argv);
	    }
	  
	  /* Now, repeat the whole process for apps with SmARRAY8 commands:
	   * This code is a direct crib from xsm because we are only doing
	   * this for backwards compatibility purposes. */
	  if (find_string_property (old_client, XsmDiscardCommand, 
				    &old_system))
	    {
	      char *cur_system;
	      
	      ignore = FALSE;
	      
	      if (cur_client &&
		  find_string_property (cur_client, XsmDiscardCommand, 
					&cur_system))
		{
		  ignore = !strcmp (cur_system, old_system);
		  
		  g_free (cur_system);
		}	      
	      
	      if (! ignore)
		system (old_system);
	  
	      g_free (old_system);
	    }
	}      
      free_client (old_client);
      gnome_config_pop_prefix ();
    }

  g_snprintf (prefix, sizeof(prefix), CONFIG_PREFIX "%s", name);
  gnome_config_clean_section (prefix);
  gnome_config_sync ();
}
