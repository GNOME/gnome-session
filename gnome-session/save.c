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



/* Default session name.  */
#define DEFAULT_SESSION "Default"

/* "Client id" for children which are not session aware */
#define NULL_ID "<none>"

/* Used to save xsm style string valued SmDiscardCommand properties. */
#define XsmDiscardCommand SmDiscardCommand "[string]"

/* Name of current session.  A NULL value means the default.  */
static char *session_name = NULL;

/* This is true if we've ever started a session.  We use this to see
   if we need to run the preloads again.  */
static int session_loaded = 0;

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

  /* Read each property we care to save.  */
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
write_session (const GSList *list1, const GSList *list2)
{
  char prefix[1024];
  int i, step;
  GSList *list;

  delete_session (session_name, list1, list2);

  i = 0;
  step = 0;
  list = (GSList *)list1;
  while (step < 2)
    {
      for (; list; list = list->next)
	{
	  Client *client = (Client *) list->data;

	  g_snprintf (prefix, sizeof(prefix), 
		      "session/%s/%d,", session_name, i);
	  gnome_config_push_prefix (prefix);
	  if (write_one_client (client))
	    ++i;
	  gnome_config_pop_prefix ();
	}
      list = (GSList *)list2;
      ++step;
    }
  
  g_snprintf (prefix, sizeof(prefix), 
	      "session/%s/num_clients", session_name);
  gnome_config_set_int (prefix, i);

  gnome_config_sync ();
}

/* Set current session name.  */
void
set_session_name (const char *name)
{
  if (session_name)
    free (session_name);
  if (name)
    session_name = strdup (name);
  else
    session_name = NULL;
}



/* We need to read clients as well as writing them because the
 * protocol does not oblige clients to specify thier properties
 * every time that they run. */
static void
read_one_client (Client *client)
{
  int i, j;
  gboolean def;

  client->id = gnome_config_get_string ("id=" NULL_ID); 
  client->properties = NULL;

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
	      SmProp *prop = g_new (SmProp, 1);
	      prop->name = strdup (properties[i].name);
	      prop->type = strdup (SmLISTofARRAY8);
	      prop->num_vals = argc;
	      prop->vals = g_new (SmPropValue, argc);
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
	      SmProp *prop = g_new (SmProp, 1);
	      prop->name = strdup (properties[i].name);
	      prop->type = strdup (SmARRAY8);
	      prop->num_vals = 1;
	      prop->vals = g_new (SmPropValue, 1);
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
	      SmProp *prop = g_new (SmProp, 1);
	      gchar* value = g_new0 (gchar, 2);
	      value[0] = (gchar) number;
	      prop->name = strdup (properties[i].name);
	      prop->type = strdup (SmCARD8);
	      prop->num_vals = 1;
	      prop->vals = g_new (SmPropValue, 1);
	      prop->vals[0].length = 1;
	      prop->vals[0].value = (SmPointer) value;
	      APPEND (client->properties, prop);      
	    }
	  break;
	}
    }
}



/* Read the session clients recorded in a config file section */
static GSList *
read_clients (const char *name)
{
  int i, num_clients;
  char prefix[1024];
  GSList* list = 0;

  g_snprintf (prefix, sizeof(prefix), "session/%s/", name);

  gnome_config_push_prefix (prefix);
  num_clients = gnome_config_get_int ("num_clients=0");
  gnome_config_pop_prefix ();

  for (i = 0; i < num_clients; ++i)
    {
      Client *client = g_new0 (Client, 1);

      g_snprintf (prefix, sizeof(prefix), "session/%s/%d,", name, i);

      gnome_config_push_prefix (prefix);
      read_one_client (client);
      gnome_config_pop_prefix ();
      APPEND (list, client);
    }
  return list;
}



/* Return number of preloads.  */
static int
num_preloads (void)
{
  return gnome_config_get_int (PRELOAD_PREFIX PRELOAD_COUNT_KEY "=0");
}

/* Run all the preloads, if required.  */
static void
run_preloads (int count)
{
  int i;

  /* Only run once.  */
  if (session_loaded)
    return;
  session_loaded = 1;

  if (count <= 0)
    return;

  for (i = 0; i < count; ++i)
    {
      char *text, buf[20];
      gboolean def;

      g_snprintf (buf, sizeof(buf), "%d=true", i);
      text = gnome_config_get_string_with_default (buf, &def);
      if (! def)
	gnome_execute_shell (NULL, text);
      g_free (text);
    }
}

/* Run the default session.  */
static int
run_default_session (void)
{
  int count;

  gnome_config_push_prefix ("=" DEFAULTDIR "/default.session=/Default/");
  count = gnome_config_get_int ("num_preloads");
  run_preloads (count);
  gnome_config_pop_prefix ();

  return 1;
}



/* Load a session from the config file by name.
 * This never closes any existing clients.
 * The first time that it is called it establishes the name for the
 * current session and sets this to a default value when a NULL name is 
 * passed. 
 * In order to avoid client id conflicts the loaded session is cloned
 * rather than restarted when there is already a session running. */
int
read_session (const char *name)
{
  int preloads;
  char prefix[1024];
  GSList *list;

  gboolean clone = (session_name != NULL);

  if (! name)
    name = DEFAULT_SESSION;

  if (! clone)
    set_session_name (name);

  list = read_clients (name);

  preloads = num_preloads ();

  if (! list && ! preloads)
    {
      /* Nothing to run - use the fallback */
      if (! strcmp (name, DEFAULT_SESSION))
	return run_default_session ();
      return 0;
    }

  gnome_config_push_prefix (PRELOAD_PREFIX);
  run_preloads (preloads);
  gnome_config_pop_prefix ();

  for (; list; list = list->next)
    {
      Client *client = (Client*)list->data;

      start_client (client, clone);
    }
  return 1;
}



/* Delete a session. 
 * This needs to be done carefully because we have to discard stale
 * session data and only a few of the clients may have conducted
 * a save since the session was last written */
void
delete_session (const char *name, const GSList* list1, const GSList* list2)
{
  int number;
  gboolean ignore;
  char prefix[1024];
  int i;

  if (! name)
    name = DEFAULT_SESSION;

  g_snprintf (prefix, sizeof(prefix), "session/%s/num_clients=-1", name);
  number = gnome_config_get_int_with_default (prefix, &ignore);

  if (ignore)
    return;

  /* For each client in the deleted session */
  for (i = 0; i < number; ++i)
    {
      char *id;
      Client *client;
      int old_argc, old_envc, old_envpc;
      char **old_argv, *old_dir, **old_envv, **old_envp, *old_system;

      g_snprintf (prefix, sizeof(prefix), "session/%s/%d,", name, i);
      gnome_config_push_prefix (prefix);

      /* Is it a client in the current session ? */
      id = gnome_config_get_string ("id");
      if (!(client = find_client_by_id (list1, id))) 
	client = find_client_by_id (list2, id);
      g_free(id);

      gnome_config_get_vector_with_default (SmDiscardCommand, 
					    &old_argc, &old_argv,
					    &ignore);
      if (! ignore)
	{
	  /* Do not call discard commands on running clients which 
	   * have not changed their discard commands. */
	  int cur_argc;
	  char **cur_argv;
	      
	  if (client && find_vector_property (client, SmDiscardCommand, 
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
	    {
	      old_dir = gnome_config_get_string (SmCurrentDirectory);
	      
	      gnome_config_get_vector_with_default (SmEnvironment, 
						    &old_envc, &old_envv, 
						    &ignore);
	      if (! ignore && old_envc > 0 && !(old_envc | 1))
		{
		  int i;

		  old_envpc = old_envc / 2;
		  old_envp = g_new0 (gchar*, old_envpc + 1);
		  
		  for (i = 0; i < old_envpc; i++)
		    old_envp[i] = g_strconcat (old_envv[2*i], "=", 
					       old_envv[2*i+1], NULL);

		  free_vector (old_envc, old_envv);
		}
	      else
		{
		  old_envpc = 0;
		  old_envp = NULL;
		}

	      gnome_execute_async_with_env (old_dir, old_argc, old_argv, 
					    old_envc, old_envp);

	      if (old_envp)
		free_vector (old_envpc, old_envp);
	      if (old_dir)
		free (old_dir);	  
	    }
	}
      free_vector (old_argc, old_argv);

      /* Now, repeat the whole process for apps with SmARRAY8 commands:
       * This code is a direct crib from xsm because we are only doing
       * this for compatibility purposes. */
      old_system = gnome_config_get_string_with_default (XsmDiscardCommand, 
							 &ignore);
      if (! ignore)
	{
	  char *cur_system;
	  
	  if (client && find_string_property (client, XsmDiscardCommand, 
					      &cur_system))
	    {
	      ignore = !strcmp (cur_system, old_system);
	      
	      free (cur_system);
	    }	      

	  if (! ignore)
	    system (old_system);
	}
      free (old_system);

      gnome_config_pop_prefix ();
    }

  g_snprintf (prefix, sizeof(prefix), "session/%s", name);
  gnome_config_clean_section (prefix);
  gnome_config_sync ();
}
