/* save.c - Code to save session.

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

#include <config.h>
#include <glib.h>
#include <string.h>
#include <gtk/gtk.h>

#include "libgnome/libgnome.h"
#include "libgnomeui/gnome-client.h"
#include "manager.h"
#include "session.h"



/* Used to save xsm style string valued SmDiscardCommand properties. */
#define XsmDiscardCommand SmDiscardCommand "[string]"

/* Used to communicate runlevels for clients with gnome aware apps */
#define GnomePriority "_GSM_Priority"

/* Name of the base session. This must be set before we load any
   clients (so that we can save them) and it must not be in use by
   any other gnome-session process when we set it */
static char *session_name = NULL;

/* See manager.c */
extern gboolean base_loaded;
extern GSList* live_list;
extern GSList* zombie_list;
extern GSList* pending_list;

typedef enum
{
  VECTOR,
  STRING,
  NUMBER
} prop_type;

/* This is used to hold a table of all properties we care to save.  */
typedef struct
{
  const char *name;	      /* Name of property per protocol  */
  prop_type type;	      
  gboolean required;	      /* TRUE if required (by us, not by the spec). */
  const char *save_name;      /* Name of property in config file */
} propsave;

/* These are the only ones which we use at present but eventually we
 * will need to save all the properties (when they have been set)
 * because clients are not obliged to set their properties every time
 * that they run. */
static propsave properties[] =
{
  { SmRestartStyleHint, NUMBER, 0, SmRestartStyleHint },
  { GnomePriority,      NUMBER, 0, "Priority" },
  { SmProgram,          STRING, 0, SmProgram },
  { SmCurrentDirectory, STRING, 0, SmCurrentDirectory },
  { SmDiscardCommand,   STRING, 0, XsmDiscardCommand }, /* for legacy apps */
  { SmDiscardCommand,   VECTOR, 0, SmDiscardCommand },
  { SmRestartCommand,   VECTOR, 1, SmRestartCommand },
  { SmEnvironment,      VECTOR, 0, SmEnvironment }
};

#define NUM_PROPERTIES (sizeof (properties) / sizeof (propsave))
#define APPEND(List,Elt) ((List) = (g_slist_append ((List), (Elt))))



/* Write a single client to the current session.  Return 1 on success,
   0 on failure.  */
static int
write_one_client (const Client *client)
{
  gint  i, vector_count, string_count, number_count, saved;
  gint  argcs[NUM_PROPERTIES];
  gchar **vectors[NUM_PROPERTIES];
  gchar *strings[NUM_PROPERTIES];
  gint  numbers[NUM_PROPERTIES];
  gchar *vector_names[NUM_PROPERTIES];
  gchar *string_names[NUM_PROPERTIES];
  gchar *number_names[NUM_PROPERTIES];

  /* Retrieve each property we care to save.  */
  saved = 1;
  vector_count = string_count = number_count = 0;
  for (i = 0; (i < NUM_PROPERTIES) && saved; ++i)
    {
      gboolean found = 0;

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
	      saved = !(properties[i].name == SmRestartStyleHint &&
			numbers[number_count] == SmRestartNever);
	      
	      number_names[number_count++] = (char*)properties[i].save_name;
	    }
	  break;
	}
      if (properties[i].required && !found)
	saved = 0;
    }

  /* Write each property we found.  */
  if (saved)
    {
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
    free_vector (argcs[i], vectors[i]);
  for (i = 0; i < string_count; ++i)
    free (strings[i]);

  return saved;
}

/* Write our session data.  */
void
write_session (void)
{
  char prefix[1024];
  int i = 0;
  GSList *list;

  delete_session (session_name);

  for (list = zombie_list; list; list = list->next)
    {
      Client *client = (Client *) list->data;
      
      g_snprintf (prefix, sizeof(prefix), "session/%s/%d,", session_name, i);
      gnome_config_push_prefix (prefix);
      i += (write_one_client (client));
      gnome_config_pop_prefix ();
    }

  for (list = live_list; list; list = list->next)
    {
      Client *client = (Client *) list->data;
      
      g_snprintf (prefix, sizeof(prefix), "session/%s/%d,", session_name, i);
      gnome_config_push_prefix (prefix);
      i += (write_one_client (client));
      gnome_config_pop_prefix ();
    }

  for (list = pending_list; list; list = list->next)
    {
      Client *client = (Client *) list->data;
      
      g_snprintf (prefix, sizeof(prefix), "session/%s/%d,", session_name, i);
      gnome_config_push_prefix (prefix);
      i += (write_one_client (client));
      gnome_config_pop_prefix ();
    }

  g_snprintf (prefix, sizeof(prefix), "session/%s/num_clients", session_name);
  gnome_config_set_int (prefix, i);
  gnome_config_sync ();
}

/* We need to read clients as well as writing them because the
 * protocol does not oblige clients to specify thier properties
 * every time that they run. */
static void
read_one_client (Client *client)
{
  int i, j;
  gboolean def;

  client->id = gnome_config_get_string ("id=");
  client->properties = NULL;
  client->priority = 50;

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
		  prop->vals[j].length = strlen (vector[j]);
		  prop->vals[j].value = vector[j];
		}
	      APPEND (client->properties, prop);      
	      free (vector);
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
	      prop->vals[0].value = string;
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
	      gchar* value = (gchar*) calloc (sizeof(gchar), 2);
	      value[0] = (gchar) number;
	      prop->name = strdup (properties[i].name);
	      prop->type = strdup (SmCARD8);
	      prop->num_vals = 1;
	      prop->vals = (SmPropValue*) malloc (sizeof (SmPropValue));
	      prop->vals[0].length = 1;
	      prop->vals[0].value = (SmPointer) value;
	      APPEND (client->properties, prop);      

	      if (properties[i].name == GnomePriority)
		client->priority = number;
	    }
	  break;
	}
    }
}



/* Read the session clients recorded in a config file section */
static GSList *
read_clients (const char* file, const char *session, gboolean fake_ids)
{
  int i, num_clients;
  char prefix[1024];
  GSList* list = 0;

  g_snprintf (prefix, sizeof(prefix), "%s/%s/", file, session);

  gnome_config_push_prefix (prefix);
  num_clients = gnome_config_get_int ("num_clients=0");
  gnome_config_pop_prefix ();

  for (i = 0; i < num_clients; ++i)
    {
      Client *client = (Client*)malloc (sizeof(Client));

      g_snprintf (prefix, sizeof(prefix), "%s/%s/%d,", file, session, i);

      gnome_config_push_prefix (prefix);
      read_one_client (client);
      gnome_config_pop_prefix ();
      client->faked_id = fake_ids;
      APPEND (list, client);
    }
  return list;
}



/* frees lock on session at shutdown */
void 
unlock_session (void)
{
  /* FIXME: release lock on the name */

  free (session_name);
  session_name = NULL;
}

/* Attempts to set the session name (the requested name may be locked).
 * Returns the name that has been assigned to the session. */
gchar*
set_session_name (const char *name)
{
  if (! name)
    name = DEFAULT_SESSION;

  /* FIXME: return old name if name is locked by a LIVE gnome-session. */
  /* FIXME: establish lock on the name */

  if (session_name)
      unlock_session ();

  session_name = strdup (name);
  return session_name;
}



/* Load a session from the config file by name. */
gint
read_session (const char *name)
{
  GSList *list;
  gboolean already_locked = (session_name != NULL);
  gboolean use_previous_ids = TRUE;

  if (! name)
    name = DEFAULT_SESSION;

  /* Before loading any clients we need a lock over a base session name 
   * into which we can write the session details: */
  if (! already_locked && ! set_session_name (name))
    return 0;

  list = read_clients ("session", name, FALSE);

  if (! list && ! already_locked)
    {
      /* Fill out empty base sessions: */
      list = read_clients ("=" DEFAULTDIR "/default.session=", name, TRUE);
      use_previous_ids = FALSE;
    }

  load_session (list);
  return list != NULL;
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

  g_snprintf (prefix, sizeof(prefix), "session/%s/num_clients=0", name);
  number = gnome_config_get_int (prefix);

  /* For each client in the deleted session */
  for (i = 0; i < number; ++i)
    {
      Client* cur_client;
      Client* old_client = (Client*)malloc (sizeof(Client));
      
      int old_argc;
      char **old_argv;
      char *old_system;
      
      g_snprintf (prefix, sizeof(prefix), "session/%s/%d,", name, i);
      gnome_config_push_prefix (prefix);
	  
      /* To call these commands in a network independent fashion
	 we need to read almost everything anyway (: */
      read_one_client (old_client);
      
      /* Is this client still part of the running session ? */
      cur_client = find_client_by_id (zombie_list, old_client->id);
      if (!cur_client) 
	cur_client = find_client_by_id (live_list, old_client->id);
      /* Or perhaps one that we are restarting ? */
      if (!cur_client) 
	cur_client = find_client_by_id (pending_list, old_client->id);
      
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
	      
	      free_vector (cur_argc, cur_argv);
	    }
	  
	  if (! ignore) 
	    run_command (old_client, SmDiscardCommand);
	  
	  free_vector (old_argc, old_argv);
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
	      
	      free (cur_system);
	    }	      
	  
	  if (! ignore)
	    system (old_system);
	  
	  free (old_system);
	}

      free_client (old_client);
      gnome_config_pop_prefix ();
    }

  g_snprintf (prefix, sizeof(prefix), "session/%s", name);
  gnome_config_clean_section (prefix);
  gnome_config_sync ();
}
