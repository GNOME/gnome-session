/* command.c - the _GSM_Command protocol extension.

   Copyright 1999 Free Software Foundation, Inc.

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
   02111-1307, USA. 

   Authors: Felix Bellaby */

#include <gnome.h>
#include "manager.h"
#include "session.h"

/* List of clients that are selecting events on our clients */
GSList* selector_list = NULL;

/* Counter for generating unique handles: */
static guint handle;

/* Hash table for handle lookups: */
static GHashTable* handle_table = NULL;

typedef enum
{
  COMMAND_ACTIVE = 0,
  COMMAND_INACTIVE,
} CommandState;

/* Data maintained for clients that speak the _GSM_Command protocol: */
struct _CommandData
{
  CommandState state;
  GSList *sessions;
};

/* helper functions */
static SmProp*
make_client_event (const gchar* client_handle, const gchar* event)
{
  SmProp *prop = (SmProp*) malloc (sizeof (SmProp));      

  prop->name = strdup (GsmClientEvent);
  prop->type = strdup (SmLISTofARRAY8);
  prop->num_vals = 2;
  prop->vals = (SmPropValue*) malloc (sizeof (SmPropValue) * prop->num_vals);
  prop->vals[0].value = strdup(event);
  prop->vals[0].length = strlen (prop->vals[0].value);
  prop->vals[1].value = strdup(client_handle);
  prop->vals[1].length = strlen (prop->vals[1].value);

  return prop;
}

static SmProp*
make_command (const gchar* command_name, const gchar* argument)
{
  SmProp *prop = (SmProp*) malloc (sizeof (SmProp));      

  prop->name = strdup (GsmCommand);
  prop->type = strdup (SmLISTofARRAY8);
  prop->num_vals = !argument ? 1 : 2;
  prop->vals = (SmPropValue*) malloc (sizeof (SmPropValue) * prop->num_vals);
  prop->vals[0].value = strdup(command_name);
  prop->vals[0].length = strlen (prop->vals[0].value);
  if (argument)
    {
      prop->vals[1].value = strdup(argument);
      prop->vals[1].length = strlen (prop->vals[1].value);
    }
  return prop;
}

static SmProp*
make_client_reasons (const gchar* client_handle, 
		     const guint count, gchar** reasons)
{
  SmProp *prop = (SmProp*) malloc (sizeof (SmProp));      
  guint i;

  prop->name = strdup (GsmClientEvent);
  prop->type = strdup (SmLISTofARRAY8);
  prop->num_vals = 2 + count;
  prop->vals = (SmPropValue*) malloc (sizeof (SmPropValue) * prop->num_vals);
  prop->vals[0].value = strdup (GsmReasons);
  prop->vals[0].length = strlen (prop->vals[0].value);
  prop->vals[1].value = strdup(client_handle);
  prop->vals[1].length = strlen (prop->vals[1].value);
  for (i = 0; i < count; i++)
    {
      prop->vals[i+2].value = strdup(reasons[i]);
      prop->vals[i+2].length = strlen (prop->vals[i+2].value);
    }

  return prop;
}

static SmProp*
prop_dup (const SmProp* old_prop)
{
  SmProp *prop = (SmProp*) malloc (sizeof (SmProp));      
  guint i;

  prop->name = strdup (old_prop->name);
  prop->type = strdup (old_prop->type);
  prop->num_vals = old_prop->num_vals;
  prop->vals = (SmPropValue*) malloc (sizeof (SmPropValue) * prop->num_vals);
  for (i = 0; i < prop->num_vals; i++)
    {
      prop->vals[i].value = strdup(old_prop->vals[i].value);
      prop->vals[i].length = strlen (prop->vals[i].value);
    }
  return prop;
}

void
client_reasons (const gchar* client_handle, gint count, gchar** reasons)
{
  if (count > 0 && client_handle)
    {
      GSList *list;
      for (list = selector_list; list; list = list->next)
	{
	  Client *client = (Client*)list->data;
	  GSList* prop_list = NULL;
	  APPEND (prop_list, make_client_reasons (client_handle, 
						  count, reasons));
	  send_properties (client, prop_list);
	}
    }
}

void
client_event (const gchar* client_handle, const gchar* event)
{
  GSList *list;
  if (client_handle)
    {
      for (list = selector_list; list; list = list->next)
	{
	  Client *client = (Client*)list->data;
	  GSList* prop_list = NULL;
	  
	  APPEND (prop_list, make_client_event (client_handle, event));
	  send_properties (client, prop_list);
	}
    }
}

void
client_property (const gchar* client_handle, int nprops, SmProp** props)
{
  GSList *list;
  if (client_handle)
    {
      for (list = selector_list; list; list = list->next)
	{
	  Client *client = (Client*)list->data;
	  GSList* prop_list = NULL;
	  int i;
	  
	  APPEND (prop_list, make_client_event (client_handle, GsmProperty));
	  for (i = 0; i < nprops; ++i)
	    APPEND (prop_list, prop_dup (props[i]));
	  send_properties (client, prop_list);
	}
    }
}

gchar* 
command_handle_new (gpointer object)
{
  gchar* new_handle = g_strdup_printf ("%d", handle++);  
  if (!handle_table)
    handle_table = g_hash_table_new (g_str_hash, g_str_equal);

  g_hash_table_insert (handle_table, new_handle, object);
  return new_handle;
}

gchar*
command_handle_reassign (gchar* handle, gpointer object)
{
  g_hash_table_remove (handle_table, handle);
  g_hash_table_insert (handle_table, handle, object);
  return handle;
}

static gpointer
command_handle_lookup (gchar* handle)
{
  return g_hash_table_lookup (handle_table, handle);
}

void
command_handle_free (gchar* handle)
{
  g_hash_table_remove (handle_table, handle);
  g_free (handle);
}

void
command_clean_up (Client* client)
{
  REMOVE (selector_list, client);

  if (client->command_data)
    {
      while (client->command_data->sessions)
	{
	  Session *session = (Session*)client->command_data->sessions->data;
	  REMOVE (client->command_data->sessions, session);
	  free_session(session);
	}
      g_free (client->command_data);
      client->command_data = NULL;
    }
}

gboolean
command_active (Client* client)
{
  return (client->command_data && 
	  client->command_data->state != COMMAND_INACTIVE);
}

extern GSList **client_lists[];
extern gchar  *events[];
extern GSList *zombie_list;
extern GSList *purge_drop_list;
extern GSList *purge_retain_list;

static gint		
cmp_args (gconstpointer a, gconstpointer b)
{
  gchar* arg_a = (gchar*)((SmProp*)a)->vals[1].value;
  gchar* arg_b = (gchar*)((SmProp*)b)->vals[1].value;

  return strcmp (arg_a, arg_b);
}

void
command (Client* client, int nprops, SmProp** props)
{
  SmProp* prop = props[0];
  gchar* arg = NULL;

  if (! client->command_data)
    {
      client->command_data = g_new0 (CommandData, 1);
    }

  if (client->command_data->state == COMMAND_INACTIVE)
    client->command_data->state = COMMAND_ACTIVE;

  if (prop->num_vals > 1)
    arg = (gchar*)prop->vals[1].value;

  if (!strcmp (prop->vals[0].value, GsmSuspend))
    client->command_data->state = COMMAND_INACTIVE;
  else if (!strcmp (prop->vals[0].value, GsmSelectClientEvents))
    APPEND (selector_list, client);
  else if (!strcmp (prop->vals[0].value, GsmDeselectClientEvents))
    REMOVE (selector_list, client);
  else if (!strcmp (prop->vals[0].value, GsmGetLastSession))
    {
      GSList* prop_list = NULL;

      APPEND (prop_list, make_command (GsmGetLastSession, 
				       get_last_session ()));
      send_properties (client, prop_list);      
    }
  else if (!strcmp (prop->vals[0].value, GsmListSessions))
    {
      GSList *prop_list = NULL;
      gchar  *section;
      void   *iter;

      if (! failsafe)
	{
	  iter = gnome_config_init_iterator_sections (CONFIG_PREFIX);
	  
	  while ((iter = gnome_config_iterator_next(iter, &section, NULL)))
	    {
	      if (strcasecmp (section, GSM_CONFIG_SECTION))
		{
		  SmProp* prop = make_command (GsmReadSession, section);
		  
		  prop_list = g_slist_insert_sorted (prop_list, prop,cmp_args);
		}
	    }
	}
      iter = gnome_config_init_iterator_sections (DEFAULT_CONFIG_PREFIX);

      while ((iter = gnome_config_iterator_next(iter, &section, NULL)))
	{
	  if (strcasecmp (section, CHOOSER_SESSION))
	    {
	      SmProp* prop = make_command (GsmReadSession, section);
	      
	      if (!g_slist_find_custom (prop_list, prop, cmp_args))
		prop_list = g_slist_insert_sorted (prop_list, prop, cmp_args);
	      else
		SmFreeProperty (prop);
	    }
	}
      send_properties (client, prop_list);      
    }
  else if (!strcmp (prop->vals[0].value, GsmListClients))
    {
      gchar  **event = events;
      GSList ***client_list = client_lists;
      GSList *prop_list = NULL;
      GSList *list;
      
      for (; *event; event++, client_list++)
	for (list = **client_list; list; list = list->next)
	  {
	    Client *client1 = (Client*)list->data;

	    if (client1->handle)
	      APPEND (prop_list, make_client_event (client1->handle, *event));
	  }	
      send_properties (client, prop_list);
    }
  else if (!strcmp (prop->vals[0].value, GsmAddClient))
    {
      if (nprops > 1)
	{
	  Client* client1 = (Client*)calloc (1, sizeof(Client));
	  int i;
	  
	  for (i = 1; i < nprops; ++i)
	    APPEND (client1->properties, props[i]);

	  if (find_property_by_name (client1, SmRestartCommand))
	    {
	      GSList *prop_list = NULL, *one_client = NULL;

	      client1->priority = 50;
	      client1->handle = command_handle_new (client1);
	      client1->match_rule = MATCH_PROP;
	      
	      APPEND (prop_list, make_command (GsmAddClient, client1->handle));
	      send_properties (client, prop_list);

	      client_event (client1->handle, GsmInactive);
	      client_property (client1->handle, nprops - 1, &props[1]);

	      APPEND (one_client, client1);
	      start_clients (one_client);
	    }
	  else
	    free_client(client1);
	}
    }
  else if (!strcmp (prop->vals[0].value, GsmSetSessionName))
    {
      gchar* name;
      GSList* prop_list = NULL;
      
      choosing = FALSE;
      name = set_session_name (arg);
      
      APPEND (prop_list, make_command (GsmSetSessionName, name));
      send_properties (client, prop_list);      
    }
  else if (arg && !strcmp (prop->vals[0].value, GsmReadSession))
    {
      GSList  *prop_list = NULL;
      Session *session;
      GSList  *list;
      
      session = read_session (arg);
      APPEND (client->command_data->sessions, session);
      APPEND (prop_list, make_command (GsmStartSession, session->handle));
      APPEND (prop_list, make_command (GsmFreeSession, session->handle));
      
      for (list = session->client_list; list; list = list->next)
	{
	  Client *client1 = (Client*)list->data;
	  APPEND (prop_list, make_client_event (client1->handle, GsmInactive));
	}
      send_properties (client, prop_list);
    }
  else if (arg && !strcmp (prop->vals[0].value, GsmStartSession))
    {
      GSList *list;

      for (list = client->command_data->sessions; list; list = list->next)
	{
	  Session *session = (Session*)list->data;
	  if (!strcmp (session->handle, arg))
	    {
	      start_session (session);
	      REMOVE (client->command_data->sessions, session);
	      break;
	    }
	}
    }
  else if (arg && !strcmp (prop->vals[0].value, GsmFreeSession))
    {
      GSList *list;
      
      for (list = client->command_data->sessions; list; list = list->next)
	{
	  Session *session = (Session*)list->data;
	  if (!strcmp (session->handle, arg))
	    {
	      free_session (session);
	      REMOVE (client->command_data->sessions, session);
	      break;
	    }
	}
    }
  else if (arg && !strcmp (prop->vals[0].value, GsmListProperties))
    {
      Client* client1 = (Client*) command_handle_lookup (arg);
      gboolean purge_drop = g_slist_find (purge_drop_list, client1) != NULL;
      gboolean purged = (purge_drop || 
			 g_slist_find (purge_retain_list,  client1) != NULL);

      if (client1)
	{
	  GSList* prop_list = NULL;
	  GSList* list;

	  APPEND (prop_list, make_client_event (arg, GsmProperty));
	  for (list = client1->properties; list; list = list->next)
	    {
	      SmProp* prop = (SmProp*)list->data;

	      if (! purged || strcmp (prop->name, SmRestartStyleHint))
		APPEND (prop_list, prop_dup (prop));
	    }
		
	  if (purged)
	    {
	      char value[2] = { SmRestartAnyway, '\0' };
	      SmPropValue val = { 1, &value };
	      SmProp hint = { SmRestartStyleHint, SmCARD8, 1, &val };
	      SmProp *prop = &hint;

	      if (purge_drop)
		value[0] = SmRestartNever;

	      APPEND (prop_list, prop_dup (prop));
	    }
	  send_properties (client, prop_list);      
	}
    }
  else if (arg && !strcmp (prop->vals[0].value, GsmRemoveClient))
    {
      Client* client1 = (Client*) command_handle_lookup (arg);

      if (client1)
	{
	  if (!remove_client (client1))
	    {
	      /* Attempt to remove a client from a non active session.
	       * In order to implement this usefully we need support for
	       * writing the session (with DEFAULT_CONFIG_PREFIX support),
	       * and a means to add clients. Hmm... */
	    }
	}
    }
  else if (arg && !strcmp (prop->vals[0].value, GsmChangeProperties))
    {
      Client* client1 = (Client*) command_handle_lookup (arg);
      
      if (client1 && nprops  > 1)
	{
	  int i;
	  for (i = 1; i < nprops; ++i)
	    {
	      SmProp *prop1 = find_property_by_name (client1, props[i]->name);
							
	      if (! strcmp (props[i]->name, SmRestartStyleHint))
		{
		  char *style = (gchar*)props[i]->vals[0].value;

		  if (g_slist_find (purge_drop_list, client1))
		    {
		      if (*style == SmRestartAnyway)
			{
			  REMOVE (purge_drop_list, client1);
			  APPEND (purge_retain_list, client1);
			}
		      else 
			*style = SmRestartNever;
		      continue;
		    }
		  else if (g_slist_find (purge_retain_list, client1))
		    {
		      if (*style == SmRestartNever)
			{
			  REMOVE (purge_retain_list, client1);
			  APPEND (purge_drop_list, client1);
			}
		      else
			*style = SmRestartAnyway;
		      continue;
		    }
		}
	      if (prop1)
		{
		  REMOVE (client1->properties, prop1);
		  SmFreeProperty (prop1);
		}
	      APPEND (client1->properties, props[i]);
	    }

	  client_property (client1->handle, nprops - 1, &props[1]);
	}      
    }

  SmFreeProperty (prop);
  free (props);
  return;
}

