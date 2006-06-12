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
#include <glib/gstdio.h>
#include <string.h>
#include <gtk/gtk.h>

#include <gconf/gconf-client.h>

#include <libgnome/libgnome.h>
#include <libgnome/gnome-desktop-item.h>
#include <libgnomeui/gnome-client.h>

#include "save.h"
#include "session.h"
#include "prop.h"
#include "command.h"
#include "util.h"

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
write_one_client (Client *client)
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

  gsm_verbose ("writing out client \"%s\"\n", client->id ? client->id : "(unknown)");

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

      client->session_saved = TRUE;
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

  gsm_verbose ("Writing out session \"%s\"\n", session_name);

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
      client->session_saved = TRUE; 
      APPEND (list, client);
    }
  return list;
}

/* read .desktop files in a given directory */
static void
read_desktop_entries_in_dir (GHashTable *clients, const gchar *path)
{
  GDir *dir;
  const gchar *name;

  dir = g_dir_open (path, 0, NULL);
  if (!dir)
    return;

  while ((name = g_dir_read_name (dir)))
    {
      Client *client = NULL;
      Client *hash_client;
      SmProp *prop;
      gchar *desktop_file;
      GnomeDesktopItem *ditem;

      if (!g_str_has_suffix (name, ".desktop"))
        continue;

      desktop_file = g_build_filename (path, name, NULL);
      ditem = gnome_desktop_item_new_from_file (desktop_file, GNOME_DESKTOP_ITEM_LOAD_NO_TRANSLATIONS, NULL);
      if (ditem != NULL)
        {
	  const gchar *exec_string;
	  gchar **argv, *hash_key;
	  gint argc, i;

	  exec_string = gnome_desktop_item_get_string (ditem, GNOME_DESKTOP_ITEM_EXEC);
          /* First filter out entries without Exec keys and hidden entries */
	  if ((exec_string == NULL) || 
              !g_shell_parse_argv (exec_string, &argc, &argv, NULL) ||
              (gnome_desktop_item_attr_exists (ditem, GNOME_DESKTOP_ITEM_HIDDEN) &&
               gnome_desktop_item_get_boolean (ditem, GNOME_DESKTOP_ITEM_HIDDEN)))
            {
              gnome_desktop_item_unref (ditem);
              g_free (desktop_file);
              continue;
            }

          /* The filter out entries that are not for GNOME */
          if (gnome_desktop_item_attr_exists (ditem, GNOME_DESKTOP_ITEM_ONLY_SHOW_IN))
              {
                int i;
                char **only_show_in_list;

                only_show_in_list = 
                    gnome_desktop_item_get_strings (ditem,
                                                    GNOME_DESKTOP_ITEM_ONLY_SHOW_IN);

                for (i = 0; only_show_in_list[i] != NULL; i++)
                  {
                    if (g_strcasecmp (only_show_in_list[i], "GNOME") == 0)
                      break;
                  }

                if (only_show_in_list[i] == NULL)
                  {
                    gnome_desktop_item_unref (ditem);
                    g_free (desktop_file);
                    g_strfreev (argv);
                    g_strfreev (only_show_in_list);
                    continue;
                  }
                g_strfreev (only_show_in_list);
              }
          if (gnome_desktop_item_attr_exists (ditem, "NotShowIn"))
              {
                int i;
                char **not_show_in_list;

                not_show_in_list = 
                    gnome_desktop_item_get_strings (ditem, "NotShowIn");

                for (i = 0; not_show_in_list[i] != NULL; i++)
                  {
                    if (g_strcasecmp (not_show_in_list[i], "GNOME") == 0)
                      break;
                  }

                if (not_show_in_list[i] != NULL)
                  {
                    gnome_desktop_item_unref (ditem);
                    g_free (desktop_file);
                    g_strfreev (argv);
                    g_strfreev (not_show_in_list);
                    continue;
                  }
                g_strfreev (not_show_in_list);
              }

          /* finally filter out entries that require a program that's not
           * installed 
           */
          if (gnome_desktop_item_attr_exists (ditem,
                                              GNOME_DESKTOP_ITEM_TRY_EXEC))
            {
              const char *program;
              char *program_path;
              program = gnome_desktop_item_get_string (ditem,
                                                       GNOME_DESKTOP_ITEM_TRY_EXEC);
              program_path = g_find_program_in_path (program);
              if (!program_path)
                {
                  gnome_desktop_item_unref (ditem);
                  g_free (desktop_file);
                  g_strfreev (argv);
                  continue;
                }
              g_free (program_path);
            }

	  client = g_new0 (Client, 1);
	  client->magic = CLIENT_MAGIC;
	  client->id = NULL;
	  client->properties = NULL;
	  client->priority = DEFAULT_PRIORITY;
	  client->handle = command_handle_new ((gpointer) client);
	  client->warning = FALSE;
	  client->get_prop_replies = NULL;
	  client->get_prop_requests = 0;
	  client->command_data = NULL;
	  client->match_rule = MATCH_PROP;

	  prop = (SmProp *) g_new (SmProp, 1);
	  prop->name = g_strdup (SmRestartCommand);
	  prop->type = g_strdup (SmLISTofARRAY8);
	  prop->num_vals = argc;
	  prop->vals = (SmPropValue *) g_new (SmPropValue, argc);

	  for (i = 0; i < argc; i++)
	    {
	      prop->vals[i].length = argv[i] ? strlen (argv[i]) : 0;
	      prop->vals[i].value = argv[i] ? g_strdup (argv[i]) : NULL;
	    }

	  APPEND (client->properties, prop);

	  if (g_hash_table_lookup_extended (clients, name, (gpointer *) &hash_key, (gpointer *) &hash_client))
	    {
	      g_hash_table_remove (clients, name);
	      g_free (hash_key);
	      free_client (hash_client);
	    }

	  /* check the X-GNOME-Autostart-enabled field */
	  if (gnome_desktop_item_attr_exists (ditem, "X-GNOME-Autostart-enabled"))
	    {
	      if (gnome_desktop_item_get_boolean (ditem, "X-GNOME-Autostart-enabled"))
	        g_hash_table_insert (clients, g_strdup (name), client);
	      else
		free_client (client);
	    }
	  else
	    g_hash_table_insert (clients, g_strdup (name), client);

	  g_strfreev (argv);
	  gnome_desktop_item_unref (ditem);
	}

      g_free (desktop_file);
    }

  g_dir_close (dir);
}

static gboolean
hash_table_remove_cb (gpointer key, gpointer value, gpointer user_data)
{
  GSList **list = (GSList **) user_data;

  *list = g_slist_prepend (*list, value);
  g_free (key);

  return TRUE;
}

/* read the list of system-wide autostart apps */
static GSList *
read_autostart_dirs (void)
{
  const char * const * system_dirs;
  char *path;
  gint i;
  GHashTable *clients;
  GSList *list = NULL;

  clients = g_hash_table_new (g_str_hash, g_str_equal);

  /* read directories */
  system_dirs = g_get_system_data_dirs ();
  for (i = 0; system_dirs[i] != NULL; i++)
    {
      path = g_build_filename (system_dirs[i], "gnome", "autostart", NULL);
      read_desktop_entries_in_dir (clients, path);
      g_free (path);
    }

  /* support old place (/etc/xdg/autostart) */
  system_dirs = g_get_system_config_dirs ();
  for (i = 0; system_dirs[i] != NULL; i++)
    {
      path = g_build_filename (system_dirs[i], "autostart", NULL);
      read_desktop_entries_in_dir (clients, path);
      g_free (path);
    }

  path = g_build_filename (g_get_user_config_dir (), "autostart", NULL);
  read_desktop_entries_in_dir (clients, path);
  g_free (path);

  /* convert the hash table into a GSList */
  g_hash_table_foreach_remove (clients, (GHRFunc) hash_table_remove_cb, &list);
  g_hash_table_destroy (clients);

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
      gconf_client_set_bool (gsm_get_conf_client (), AUTOSAVE_MODE_KEY, auto_save_mode, NULL);
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

static gboolean
compare_restart_command (GSList *props1, GSList *props2)
{
  SmProp *p1, *p2;
  GSList *node1, *node2;

  for (node1 = props1; node1 != NULL; node1 = node1->next)
    {
      p1 = node1->data;
      if (!strcmp (p1->name, SmRestartCommand))
        {
	  for (node2 = props2; node2 != NULL; node2 = node2->next)
	    {
	      p2 = node2->data;
	      if (!strcmp (p2->name, SmRestartCommand))
                {
                  if ((p1->vals[0].value == NULL) || 
                      (p2->vals[0].value == NULL))
                    return FALSE;
                  return strcmp (p1->vals[0].value, p2->vals[0].value) == 0;
                }
	    }
	  break;
        }
    }

  return FALSE;
}

static GSList *
remove_duplicated_clients (GSList *clients)
{
  GSList *node;

  for (node = clients; node != NULL; node = node->next)
    {
      GSList *sl;
      Client *client = node->data;

      for (sl = node->next; sl != NULL; sl = sl->next)
        {
	  Client *tmp_client = sl->data;
	  GSList *props, *tmp_props;

	  props = client->properties;
	  tmp_props = tmp_client->properties;

	  if (compare_restart_command (props, tmp_props))
	    {
	      REMOVE (clients, tmp_client);
	      free_client (tmp_client);
	      sl = node;
	    }
	}
    }

  return clients;
}

/* Load a session from the config file by name. */
Session*
read_session (const char *name)
{
  GSList *list = NULL;
  Session *session = g_new0 (Session, 1);

  session->name   = g_strdup (name);
  session->handle = command_handle_new ((gpointer)session);

  if (name) {
    if (!strcmp (name, FAILSAFE_SESSION)) 
      list = read_clients (
		DEFAULT_CONFIG_PREFIX, DEFAULT_SESSION, MATCH_FAKE_ID);
    else 
      list = read_clients (CONFIG_PREFIX, name, MATCH_ID);

    if (!list)
      list = read_clients (DEFAULT_CONFIG_PREFIX,name,MATCH_FAKE_ID);

  } 

  if (!list) 
    list = read_clients (
		DEFAULT_CONFIG_PREFIX, DEFAULT_SESSION, MATCH_FAKE_ID);

  session->client_list = list;

  /* See if there is a list of clients that the user has manually
   * created from the session-properties capplet
   */
  list = read_clients (MANUAL_CONFIG_PREFIX, name, MATCH_PROP);
  if (list) {
    char *path, *user_autostart_dir;

    user_autostart_dir = g_build_filename (g_get_user_config_dir (), "autostart", NULL);
    if (!g_file_test (user_autostart_dir, G_FILE_TEST_IS_DIR))
        g_mkdir_with_parents (user_autostart_dir, S_IRWXU);

    /* migrate to newly desktop-based autostart system */
    while (list) {
      SmProp *prop;
      Client *client = (Client *) list->data;
    
      prop = find_property_by_name (client, SmRestartCommand);
      if (prop) {
        GnomeDesktopItem *ditem;
	int i;
	char *basename, *orig_filename, *filename;
	GString *args = g_string_new ((const char *) prop->vals[0].value);

	for (i = 1; i < prop->num_vals; i++) {
	  args = g_string_append_c (args, ' ');
	  args = g_string_append (args, (const char *) prop->vals[i].value);
	}

	ditem = gnome_desktop_item_new ();
	gnome_desktop_item_set_string (ditem, GNOME_DESKTOP_ITEM_EXEC, args->str);

	basename = g_path_get_basename (prop->vals[0].value);
	orig_filename = g_strconcat (user_autostart_dir, G_DIR_SEPARATOR_S, basename, ".desktop", NULL);
	filename = g_strdup (orig_filename);
	i = 2;
	while (g_file_exists (filename)) {
	  char *tmp = g_strdup_printf ("%s" G_DIR_SEPARATOR_S "%s-%d.desktop", user_autostart_dir, basename, i);

	  g_free (filename);
	  filename = tmp;
	  i++;
	}
	g_free (basename);
	
	if (!gnome_desktop_item_save (ditem, filename, TRUE, NULL))
  	  g_warning ("Could not save %s file\n", filename);

	gnome_desktop_item_unref (ditem);
	g_string_free (args, TRUE);
	g_free (orig_filename);
	g_free (filename);
      }

      REMOVE (list, client);
      free_client (client);
    }

    g_free (user_autostart_dir);

    path = g_build_filename (g_get_home_dir (), ".gnome2/session-manual", NULL);
    g_remove ((const char *) path);
    g_free (path);
  }

  /* Get applications to be autostarted */
  list = read_autostart_dirs ();
  if (list)
    session->client_list = g_slist_concat (session->client_list, list);

  session->client_list = remove_duplicated_clients (session->client_list);

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
		  ignore = gsm_compare_commands (old_argc, old_argv,
						 cur_argc, cur_argv);
		  
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
