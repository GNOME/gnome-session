/* gsm-protocol.c - gnome interface to gnome-session protocol extensions.

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

#include <config.h>

#include <libgnome/libgnome.h>
#include <libgnomeui/libgnomeui.h>
#include <X11/SM/SMlib.h>

#include "session.h"
#include "gsm-protocol.h"

#define GSM_IS_CLIENT_EVENT(event) (!strncmp (((SmProp*)(event))->name, GsmClientEvent, strlen (GsmClientEvent))) 
#define GSM_CLIENT_TYPE(event) ((gchar*)((SmProp*)event)->vals[0].value)
#define GSM_CLIENT_HANDLE(event) ((gchar*)((SmProp*)event)->vals[1].value)

static void    commandv (SmProp* prop0, va_list args);
static void    command (SmProp* prop, ...);
static gint    request_event (gpointer data);
static void    prop_free (SmProp* prop);

static gchar*  gsm_prop_to_sh (SmProp* prop);
static GSList* gsm_prop_to_list (SmProp* prop);
static SmProp* gsm_sh_to_prop (gchar* name, gchar* sh);
static SmProp* gsm_list_to_prop (gchar* name, GSList* list);
static SmProp* gsm_int_to_prop (gchar* name, gint value);
static SmProp* gsm_args_to_propv (gchar* name, va_list args);
static SmProp* gsm_args_to_prop (gchar* name, ...);

#define GSM_IS_COMMAND(command) (!strncmp (((SmProp*)(command))->name, GsmCommand, strlen (GsmCommand)))
#define GSM_COMMAND_ARG0(command) ((gchar*)((SmProp*)command)->vals[0].value)
#define GSM_COMMAND_ARG1(command) ((gchar*)((SmProp*)command)->vals[1].value)

#define GSM_PROP_NAME(prop) ((gchar*)((SmProp*)prop)->name)
#define GSM_PROP_IS_INT(prop) (!strcmp(((SmProp*)prop)->type, SmCARD8))
#define GSM_PROP_IS_STRING(prop) (!strcmp(((SmProp*)prop)->type, SmARRAY8))
#define GSM_PROP_IS_LIST(prop) (!strcmp(((SmProp*)prop)->type, SmLISTofARRAY8))
#define GSM_PROP_INT(prop) (((gchar*)((SmProp*)prop)->vals[0].value)[0])
#define GSM_PROP_STRING(prop) ((gchar*)((SmProp*)prop)->vals[0].value)
#define GSM_PROP_LIST(prop) (gsm_prop_list((SmProp*)prop))

/* Translation tables - hmmm... probably a temporary measure */
static const guint styles[] =
{
  SmRestartIfRunning,
  SmRestartImmediately,
  SmRestartNever,
  SmRestartAnyway
};

static const GsmStyle r_styles[] =
{
  GSM_NORMAL,
  GSM_SETTINGS,
  GSM_RESPAWN,
  GSM_TRASH,
};

/* GSM_CLIENT object */

static void gsm_client_destroy (GtkObject *o);

enum {
  REMOVE,
  REASONS,
  COMMAND,
  STATE,
  STYLE,
  ORDER,
  NSIGNALS
};

static gint gsm_client_signals[NSIGNALS];
static GtkObjectClass *parent_class = NULL;

static void
gsm_client_class_init (GsmClientClass *klass)
{
  GtkObjectClass *object_class = (GtkObjectClass*) klass;

  if (!parent_class)
    parent_class = gtk_type_class (gtk_object_get_type ());

  gsm_client_signals[REMOVE] =
    gtk_signal_new ("remove",
		    GTK_RUN_LAST,
		    object_class->type,
		    GTK_SIGNAL_OFFSET (GsmClientClass, remove),
		    gtk_signal_default_marshaller,
		    GTK_TYPE_NONE, 0); 
  gsm_client_signals[REASONS] =
    gtk_signal_new ("reasons",
		    GTK_RUN_LAST,
		    object_class->type,
		    GTK_SIGNAL_OFFSET (GsmClientClass, reasons),
		    gtk_marshal_NONE__POINTER,
		    GTK_TYPE_NONE, 1, GTK_TYPE_POINTER);
  gsm_client_signals[COMMAND] =
    gtk_signal_new ("command",
		    GTK_RUN_LAST,
		    object_class->type,
		    GTK_SIGNAL_OFFSET (GsmClientClass, command),
		    gtk_marshal_NONE__POINTER,
		    GTK_TYPE_NONE, 1, GTK_TYPE_POINTER);
  gsm_client_signals[STATE] =
    gtk_signal_new ("state",
		    GTK_RUN_LAST,
		    object_class->type,
		    GTK_SIGNAL_OFFSET (GsmClientClass, state),
		    gtk_marshal_NONE__INT,
		    GTK_TYPE_NONE, 1, GTK_TYPE_INT);
  gsm_client_signals[STYLE] =
    gtk_signal_new ("style",
		    GTK_RUN_LAST,
		    object_class->type,
		    GTK_SIGNAL_OFFSET (GsmClientClass, style),
		    gtk_marshal_NONE__INT,
		    GTK_TYPE_NONE, 1, GTK_TYPE_INT);
  gsm_client_signals[ORDER] =
    gtk_signal_new ("order",
		    GTK_RUN_LAST,
		    object_class->type,
		    GTK_SIGNAL_OFFSET (GsmClientClass, order),
		    gtk_marshal_NONE__INT,
		    GTK_TYPE_NONE, 1, GTK_TYPE_INT);
 
  gtk_object_class_add_signals (object_class, gsm_client_signals, NSIGNALS);
  
  object_class->destroy = gsm_client_destroy;
  
  klass->remove   = NULL;
  klass->reasons  = NULL;
  klass->command  = NULL;
  klass->state    = NULL;
  klass->style    = NULL;
  klass->order    = NULL;
}

static void
gsm_client_object_init (GsmClient* client)
{
  client->handle = NULL;
  client->order = 50;
  client->style = GSM_NORMAL;
  client->state = GSM_INACTIVE;
  client->command = NULL;
}

GtkTypeInfo gsm_client_info = 
{
  "GsmClient",
  sizeof (GsmClient),
  sizeof (GsmClientClass),
  (GtkClassInitFunc) gsm_client_class_init,
  (GtkObjectInitFunc) gsm_client_object_init,
  (GtkArgSetFunc) NULL,
  (GtkArgGetFunc) NULL,
  (GtkClassInitFunc) NULL
};

guint
gsm_client_get_type (void)
{
  static guint type = 0;

  if (!type)
    type = gtk_type_unique (gtk_object_get_type (), &gsm_client_info);
  return type;
}

/* These variables are session manager specific but this code only supports
 * a single session manager: */
/* Clients which we know to be in the session stored by protocol handle: */
static GHashTable* handle_table = NULL;

/* List of clients which we are attempting to add to the session: */
static GSList* adds = NULL;

/* List of clients which we are attempting to remove from the session: */
static GSList* removes = NULL;

GtkObject* 
gsm_client_new (void)
{
  GsmClient* client = gtk_type_new(gsm_client_get_type());
  return GTK_OBJECT (client);
}

static void 
gsm_client_destroy (GtkObject *o)
{
  GsmClient* client;
  guint i, j;

  g_return_if_fail(o != NULL);
  g_return_if_fail(GSM_IS_CLIENT(o));

  client = GSM_CLIENT(o);

  if (client->handle)
      g_hash_table_remove (handle_table, client->handle);
  g_free (client->command);
  g_free (client->handle);
  (*(GTK_OBJECT_CLASS (parent_class)->destroy))(o);
}

void
gsm_client_commit_remove (GsmClient *client)
{
  command (gsm_args_to_prop (GsmCommand, GsmRemoveClient,
			     client->handle, NULL), NULL);
  removes = g_slist_append (removes, client);
}

void
gsm_client_commit_add (GsmClient *client)
{
  command (gsm_args_to_prop (GsmCommand, GsmAddClient, NULL),
	   gsm_sh_to_prop (SmRestartCommand, client->command), 
	   gsm_int_to_prop (SmRestartStyleHint, styles[client->style]), 
	   gsm_int_to_prop (GsmPriority, client->order), 
	   NULL);
  gtk_object_ref ((GtkObject*)client);
  adds = g_slist_append (adds, client);
}

void      
gsm_client_commit_style (GsmClient *client)
{
  command (gsm_args_to_prop (GsmCommand, GsmChangeProperties, 
			     client->handle, NULL),
	   gsm_int_to_prop (SmRestartStyleHint, 
			    styles[client->style]), NULL);
}

void      
gsm_client_commit_order (GsmClient *client)
{
  command (gsm_args_to_prop (GsmCommand, GsmChangeProperties, 
			     client->handle, NULL),
	   gsm_int_to_prop (GsmPriority, client->order), NULL);
}

/* GSM_SELECTOR object */

static void gsm_selector_destroy (GtkObject *o);

enum {
  INITIALIZED,
  NSIGNALS2
};

static gint gsm_selector_signals[NSIGNALS2];

static void
gsm_selector_class_init (GsmSelectorClass *klass)
{
  GtkObjectClass *object_class = (GtkObjectClass*) klass;

  if (!parent_class)
    parent_class = gtk_type_class (gtk_object_get_type ());

  gsm_selector_signals[INITIALIZED] =
    gtk_signal_new ("initialized",
		    GTK_RUN_LAST,
		    object_class->type,
		    GTK_SIGNAL_OFFSET (GsmSelectorClass, initialized),
		    gtk_signal_default_marshaller,
		    GTK_TYPE_NONE, 0); 
 
  gtk_object_class_add_signals (object_class, gsm_selector_signals, NSIGNALS2);
  
  object_class->destroy = gsm_selector_destroy;
  
  klass->initialized = NULL;
}

GtkTypeInfo gsm_selector_info = 
{
  "GsmSelector",
  sizeof (GsmSelector),
  sizeof (GsmSelectorClass),
  (GtkClassInitFunc) gsm_selector_class_init,
  (GtkObjectInitFunc) NULL,
  (GtkArgSetFunc) NULL,
  (GtkArgGetFunc) NULL,
  (GtkClassInitFunc) NULL
};

guint
gsm_selector_get_type (void)
{
  static guint type = 0;

  if (!type)
    type = gtk_type_unique (gtk_object_get_type (), &gsm_selector_info);
  return type;
}

static gboolean
gnome_session_client (GnomeClient* gnome_client)
{
  return !strcmp(SmcVendor((SmcConn)gnome_client->smc_conn), GsmVendor);
}

static gboolean selector_created = FALSE;

GtkObject* 
gsm_selector_new (GnomeClient* gnome_client, GsmClientFactory client_factory, 
		  gpointer data)
{
  GsmSelector* selector;

  g_return_val_if_fail (! selector_created, NULL);
  g_return_val_if_fail (gnome_client, NULL);
  g_return_val_if_fail (GNOME_CLIENT_CONNECTED (gnome_client), NULL);
  g_return_val_if_fail (gnome_session_client (gnome_client), NULL);

  selector = gtk_type_new(gsm_selector_get_type());
  selector->gnome_client   = gnome_client;
  selector->client_factory = client_factory;
  selector->data           = data;
  selector->waiting        = -1;
  selector_created = TRUE;

  gtk_object_ref (GTK_OBJECT (selector->gnome_client));

  command (gsm_args_to_prop (GsmCommand, GsmSelectClientEvents, NULL),
	   NULL);
  command (gsm_args_to_prop (GsmCommand, GsmListClients, NULL),
	   NULL);
  gtk_idle_add (request_event, (gpointer)selector);

  return GTK_OBJECT (selector);
}

static void 
gsm_selector_destroy (GtkObject *o)
{
  GsmSelector* selector;
  guint i, j;

  g_return_if_fail(o != NULL);
  g_return_if_fail(GSM_IS_SELECTOR(o));

  selector = GSM_SELECTOR(o);

  gtk_object_unref (GTK_OBJECT (selector->gnome_client));
  selector_created = FALSE;

  command (gsm_args_to_prop (GsmCommand, GsmDeselectClientEvents, NULL),
	   NULL);

  (*(GTK_OBJECT_CLASS (parent_class)->destroy))(o);
}

/* Public utilities */
gboolean   gsm_sh_quotes_balance (gchar* sh)
{
  SmProp* prop = gsm_sh_to_prop (SmRestartCommand, sh);
  gboolean complete = prop != NULL;
  prop_free (prop);
  return complete;
}

/* Interal utilities */
static void
commandv (SmProp* prop0, va_list args)
{
  SmcConn smc_conn = (SmcConn) gnome_master_client()->smc_conn;
  SmProp** props;
  gint i, nprops = 1;
  va_list args2;

  G_VA_COPY (args2, args);

  while (va_arg (args, gchar*)) nprops++;

  props = (SmProp**) malloc (sizeof (SmProp*) * nprops);
  props[0] = prop0;

  for (i = 1; i < nprops; i++)
    props[i] = (SmProp*)va_arg (args2, SmProp*);

  va_end (args2);

  SmcSetProperties (smc_conn, nprops, props);
  free (props);
}

static void
command (SmProp* prop0, ...)
{
  va_list args;

  va_start (args, prop0);
  commandv (prop0, args);
  va_end (args);
}

static GsmClient* 
find_client (SmProp* prop, GsmSelector *selector)
{
  GsmClient* client = NULL;

  if (!handle_table)
    handle_table = g_hash_table_new (g_str_hash, g_str_equal);

  client = (GsmClient*)g_hash_table_lookup (handle_table, 
					    GSM_CLIENT_HANDLE(prop));
  if (!client)
    {
      client = GSM_CLIENT (selector->client_factory (selector->data));
      client->handle  = g_strdup (GSM_CLIENT_HANDLE (prop));
      g_hash_table_insert (handle_table, client->handle, client);
      command (gsm_args_to_prop (GsmCommand, GsmListProperties, 
				     client->handle, NULL), 
	       NULL);
    }
  return client;
}     

/* Event loop implementation: */
static void
dispatch_event (SmcConn smc_conn, SmPointer data, 
		   int nprops, SmProp **props)
{
  GsmSelector *selector = (GsmSelector*)data;

  if(nprops > 0)
    {
      if (GSM_IS_CLIENT_EVENT (props[0]))
	{
	  if (!strcmp (GSM_CLIENT_TYPE (props[0]), GsmProperty))
	    {
	      gint i;
	      gchar *restart_command = NULL, *command = NULL;
	      GsmClient* client = find_client (props[0], data);
	      prop_free (props[0]);

	      if (!g_slist_find(removes, client))
		{
		  for (i = 1; i < nprops; i++)
		    {
		      gchar* name = GSM_PROP_NAME (props[i]);
		      
		      if (!strcmp (name, GsmPriority))
			{
			  guint order = GSM_PROP_INT (props[i]);
			  gtk_signal_emit (GTK_OBJECT (client), 
					   gsm_client_signals[ORDER], order); 
			  client->order = order;
			}
		      else if (!strcmp (name, SmRestartStyleHint))
			{
			  guint style = r_styles[(int)GSM_PROP_INT (props[i])];
			  gtk_signal_emit (GTK_OBJECT (client), 
					   gsm_client_signals[STYLE], style); 
			  client->style = style;
			}
		      else if (!strcmp (name, SmCloneCommand))
			{
			  command = gsm_prop_to_sh (props[i]);
			}
		      else if (!strcmp (name, SmRestartCommand))
			{
			  if (! command && ! client->command)
			    {
			      restart_command = gsm_prop_to_sh (props[i]);
			      command = restart_command;
			    }
			}
		      prop_free (props[i]);
		    }

		  if (command)
		    {		      
		      gtk_signal_emit (GTK_OBJECT (client), 
				       gsm_client_signals[COMMAND],
				       command);
		      g_free (client->command);
		      client->command = command;

		      if (restart_command != command)
			g_free (restart_command);
		    }
		  
		  if (selector->waiting)
		    {
		      selector->waiting--;
		      if (!selector->waiting)
			gtk_signal_emit (GTK_OBJECT (selector),
					 gsm_selector_signals[INITIALIZED]);
		    }
		}
	    }
	  else
	    {
	      gint i, j;
	      for (i = 0; i < nprops; i++)
		{
		  GsmClient* client = find_client (props[i], data);
		  gchar* type = GSM_CLIENT_TYPE (props[i]);

		  if (!strcmp (type, GsmReasons))
		    {
		      GSList *list = gsm_prop_to_list (props[i]);
		      
		      list = g_slist_remove (list, list->data);
		      list = g_slist_remove (list, list->data);
		      gtk_signal_emit (GTK_OBJECT (client), 
				       gsm_client_signals[REASONS], list);
		      g_slist_free (list);
		    }
		  else if (!strcmp (type, GsmRemove))
		    {
		      gtk_signal_emit (GTK_OBJECT (client), 
				       gsm_client_signals[REMOVE]);
		      removes = g_slist_remove(removes, client);
		      gtk_object_unref (GTK_OBJECT (client));
		    }
		  else
		    {
		      GsmState state = GSM_INACTIVE;

		      if (! strcmp (type, GsmStart))
			state = GSM_START;
		      else if (! strcmp (type, GsmConnected))
			state = GSM_CONNECTED;
		      else if (! strcmp (type, GsmSave))
			state = GSM_SAVE;
		      else if (! strcmp (type, GsmUnknown))
			state = GSM_UNKNOWN;
		      
		      gtk_signal_emit (GTK_OBJECT (client), 
				       gsm_client_signals[STATE], state);
		      client->state = state;

		      if (selector->waiting == -1)
			selector->waiting = 1;
		      else if (selector->waiting)
			selector->waiting++;
		    }
		  prop_free (props[i]);
		}
	    }
	}
      else if (GSM_IS_COMMAND (props[0]))
	{
	  if (!strcmp (GSM_COMMAND_ARG0 (props[0]), GsmAddClient) && adds)
	    {
	      GsmClient* client = (GsmClient*) adds->data;
	      client->handle = GSM_COMMAND_ARG1 (props[0]);
	      g_hash_table_insert (handle_table, client->handle, client);
	      adds    = g_slist_remove (adds, client);
	    }
	  /* Dispatch other things ... */
	}
      free (props);
    }
  gtk_idle_add (request_event, data);
}

/* libSM prevents us form calling SmcGetProperties within its own callback
* (dispatch_event) so we have to use an idle routine to make the call*/
static gint
request_event (gpointer data)
{
  SmcConn smc_conn = (SmcConn) gnome_master_client()->smc_conn;
  SmcGetProperties (smc_conn, dispatch_event, (SmPointer)data);
  return FALSE;
}

static void
prop_free (SmProp* prop)
{
  if (prop)
    SmFreeProperty (prop);
} 

/* Property converters */
static SmProp*
gsm_args_to_propv (gchar* name, va_list args)
{
  SmProp* prop = (SmProp*) malloc (sizeof (SmProp));
  gint i;
  va_list args2;

  G_VA_COPY (args2, args);

  prop->name = strdup (name);
  prop->type = strdup (SmLISTofARRAY8);
  prop->num_vals = 0;

  while (va_arg (args, gchar*)) prop->num_vals++;

  prop->vals = (SmPropValue*) malloc (sizeof (SmPropValue) * prop->num_vals);

  for (i = 0; i < prop->num_vals; i++)
    {
      prop->vals[i].value = (void*)va_arg (args2, gchar*);
      prop->vals[i].length = strlen(prop->vals[i].value);
    }

  va_end (args2);

  return prop;
}

static SmProp*
gsm_args_to_prop (gchar* name, ...)
{
  SmProp* prop;
  va_list args;

  va_start (args, name);
  prop = gsm_args_to_propv (name, args);
  va_end (args);
  return prop;
}

static GSList*
gsm_prop_to_list (SmProp* prop)
{
  GSList *ret = NULL;
  gint i;

  for (i = 0; i < prop->num_vals; i++)
    ret = g_slist_append (ret, prop->vals[i].value);

  return ret;
}

static SmProp*
gsm_list_to_prop (gchar* name, GSList* list)
{
  SmProp *prop = (SmProp*) malloc (sizeof (SmProp));
  gint i;

  prop->name = strdup (name);
  prop->type = strdup (SmLISTofARRAY8);
  prop->num_vals = g_slist_length (list);

  prop->vals = (SmPropValue*) malloc (sizeof (SmPropValue) * prop->num_vals);

  for (i = 0; i < prop->num_vals; i++)
    {
      prop->vals[i].value = strdup (list->data);
      prop->vals[i].length = strlen (prop->vals[i].value);
      list = list->next;
    }

  return prop;
}

static SmProp*
gsm_int_to_prop (gchar* name, gint value)
{
  SmProp *prop = (SmProp*) malloc (sizeof (SmProp));

  prop->name = strdup (name);
  prop->type = strdup (SmCARD8);
  prop->num_vals = 1;

  prop->vals = (SmPropValue*) malloc (sizeof (SmPropValue) * prop->num_vals);

  prop->vals[0].value = strdup ("X");
  prop->vals[0].length = strlen (prop->vals[0].value);

  GSM_PROP_INT (prop) = (gchar)value;

  return prop;
}

/* Converts sh quoting into a gsm compatible prop (in execvp form).
 * Returns NULL when sh would require a continuation or ignore the line.
 * sh metacharcters are copied verbatim rather than generating errors. */ 
static SmProp*
gsm_sh_to_prop (gchar* name, gchar* sh)
{
  SmProp *prop = (SmProp*) malloc (sizeof (SmProp));
  gchar *dest, *src;
  gint i;
  
  prop->num_vals = 0;

  dest = src = sh = strdup (sh);

  while (*src)
    {
      gboolean inword = TRUE;

      while (*src && strchr (" \t\n", *src)) src++;

      if (!*src) break;

      while (inword)
	{
	  switch (*src)
	    {
	    case ' ':
	    case '\t':
	    case '\n':
	      src++;
	    case '\0':
	      inword = FALSE;
	      break;

	    case '\'':
	      while (*++src != '\'')
		{
		  if (!*src) goto error;
		  *dest++ = *src;
		}
	      src++;
	      break;

	    case '\"':
	      while (*++src != '\"')
		{
		  if (!*src) goto error;
		  if (*src == '\\' && strchr ("$`\\\"", *(src + 1)))
		    src++;
		  *dest++ = *src;
		}
	      src++;
	      break;

	    case '\\':
	      if (!*++src) goto error;
	    default:
	      *dest++ = *src++;  
	    }
	}

      *dest++ = '\0';
      prop->num_vals++;
    }
  if (!prop->num_vals) goto error;

  prop->name = strdup (name);
  prop->type = strdup (SmLISTofARRAY8);
  prop->vals = (SmPropValue*) malloc (sizeof (SmPropValue) * prop->num_vals);

  for (i = 0, src = sh; i < prop->num_vals; i++)
    {
      prop->vals[i].value = strdup (src);
      prop->vals[i].length = strlen (prop->vals[i].value);
      src += prop->vals[i].length + 1;
    }
  free (sh);

  return prop;

 error:
  free (sh);
  free (prop);

  return NULL;
}

/* Converts a gsm prop into a sh quoted string.
 * Returns NULL when the prop contains no values. */
static gchar*
gsm_prop_to_sh (SmProp* prop)
{
  gchar *sh, *dest, *src;
  gint i, len = 0;

  if (!prop->num_vals)
    return NULL;

  for (i = 0; i < prop->num_vals; i++)
    {
      src = (gchar*)prop->vals[i].value;

      len += strlen(src) + 1;

      if (!*src || strpbrk (src, " |&;()<>\t\n$`\'\"\\"))
	{
	  len += 2;
	  if (strchr (src, '\''))
	    while ((src = strpbrk (src, "$`\\\"")))
	      {
		src++;
		len++;
	      }
	}
    }

  sh = dest = g_new (gchar, len);

  for (i = 0; i < prop->num_vals; i++)
    {
      src = (gchar*)prop->vals[i].value;
      
      if (i)
	*dest++ = ' ';

      if (*src && !strpbrk (src, " |&;()<>\t\n$`\'\"\\"))
	{
	  while (*src) *dest++ = *src++;
	}
      else if (!strchr (src, '\''))
	{
	  *dest++ = '\'';
	  while (*src) *dest++ = *src++;
	  *dest++ = '\'';
	}
      else 
	{
	  *dest++ = '\"';
	  while (*src)
	    {
	      if (strchr ("$`\\\"", *src))
		  *dest++ = '\\';
	      *dest++ = *src++;
	    }
	  *dest++ = '\"';
	}
    }
  *dest = '\0';
  return sh;
}

