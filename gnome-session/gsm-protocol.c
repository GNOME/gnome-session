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
#include <string.h>

#include <libgnome/libgnome.h>
#include <libgnomeui/libgnomeui.h>
#include <X11/SM/SMlib.h>

#include "session.h"
#include "gsm-protocol.h"
#include "gsm-typebuiltins.h"

#include "gsm-marshal.h"
#include "gsm-marshal.c"

#define GSM_IS_CLIENT_EVENT(event) (!strncmp (((SmProp*)(event))->name, GsmClientEvent, strlen (GsmClientEvent))) 
#define GSM_IS_GNOME_CLIENT(client) (!strcmp (SmcVendor ((SmcConn) client->smc_conn), GsmVendor))
#define GSM_CLIENT_TYPE(event) ((gchar*)((SmProp*)event)->vals[0].value)
#define GSM_CLIENT_HANDLE(event) ((gchar*)((SmProp*)event)->vals[1].value)
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

static void    commandv (GsmProtocol *protocol, va_list args);
static void    command (GsmProtocol *protocol, ...);
static gint    request_event (gpointer data);
static void    prop_free (SmProp* prop);
static gchar*  gsm_prop_to_sh (SmProp* prop);
static GSList* gsm_prop_to_list (SmProp* prop);
static SmProp* gsm_sh_to_prop (const gchar* name, const gchar* sh);
static SmProp* gsm_int_to_prop (gchar* name, gint value);
static SmProp* gsm_args_to_propv (gchar* name, va_list args);
static SmProp* gsm_args_to_prop (gchar* name, ...);

/* The object required to operate the protocol (assumed unique) */
static GsmProtocol* the_protocol = NULL;
/* The live session (assumed unique) */
static GsmSession*  live_session = NULL;

/* List of sessions which we are reading: */
static GSList* reads = NULL;

/* List of sessions on which we are getting/setting names: */
static GSList* names = NULL;

static GnomeClient *gnome_client_used = NULL;

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

enum {
  INITIALIZED,
  SESSION_NAME,
  NSIGNALS2
};

static guint gsm_session_signals[NSIGNALS2];

enum {
  REMOVE,
  REASONS,
  COMMAND,
  STATE,
  STYLE,
  ORDER,
  NSIGNALS
};

static guint gsm_client_signals[NSIGNALS];

static GObjectClass *gsm_session_parent_class = NULL;
static GObjectClass *gsm_protocol_parent_class = NULL;
static GObjectClass *gsm_client_parent_class = NULL;

/* These variables are session manager specific but this code only supports
 * a single session manager: */
/* Clients which we know to be in the session stored by protocol handle: */
static GHashTable* handle_table = NULL;

/* List of clients which we are attempting to add to the live session: */
static GSList* adds = NULL;

/* List of clients which we are attempting to remove from the live session: */
static GSList* removes = NULL;

enum {
  SAVED_SESSIONS,
  CURRENT_SESSION,
  NSIGNALS3
};

static guint gsm_protocol_signals[NSIGNALS3];

static void 
gsm_session_finalize (GObject *object)
{
	GsmSession* session = (GsmSession *) object;

	g_return_if_fail (GSM_IS_SESSION (session));

	if (session == live_session) {
		command (the_protocol, 
			 gsm_args_to_prop (GsmCommand, GsmDeselectClientEvents, NULL),
			 NULL);

		live_session = NULL;
	} else
		command (the_protocol, 
			 gsm_args_to_prop (GsmCommand, GsmFreeSession, session->handle, NULL),
			 NULL);

	g_free (session->name);

	gsm_session_parent_class->finalize (object);
}

static void
gsm_session_class_init (GsmSessionClass *klass)
{
	GObjectClass *gobject_class = (GObjectClass *) klass;

	gsm_session_signals [INITIALIZED] =
		g_signal_new (
			"initialized",
			G_TYPE_FROM_CLASS (gobject_class),
			G_SIGNAL_RUN_LAST,
			G_STRUCT_OFFSET (GsmSessionClass, initialized),
			NULL, NULL,
			g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE, 0); 

	gsm_session_signals [SESSION_NAME] =
		g_signal_new (
			"session_name",
			G_TYPE_FROM_CLASS (gobject_class),
			G_SIGNAL_RUN_LAST,
			G_STRUCT_OFFSET (GsmSessionClass, session_name),
			NULL, NULL,
			g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE, 0); 

	gobject_class->finalize = gsm_session_finalize;
  
	klass->initialized  = NULL;
	klass->session_name = NULL;
}

static void
gsm_session_instance_init (GsmSession *session)
{
	session->name    = NULL;
	session->handle  = NULL;
	session->waiting = -1;
}

GType
gsm_session_get_type (void)
{
	static GType retval = 0;

	if (!retval) {
		static const GTypeInfo info = {
			sizeof (GsmSessionClass),
			(GBaseInitFunc)     NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc)    gsm_session_class_init,
			NULL,               /* class_finalize */
			NULL,               /* class_data */
			sizeof (GsmSession),
			0,                  /* n_preallocs */
			(GInstanceInitFunc) gsm_session_instance_init
		};

		retval = g_type_register_static (G_TYPE_OBJECT, "GsmSession", &info, 0);
		gsm_session_parent_class = g_type_class_ref (G_TYPE_OBJECT);
	}

	return retval;
}

GsmSession * 
gsm_session_new (char             *name,
		 GsmClientFactory  client_factory, 
		 gpointer          user_data)
{
	GsmSession *session;

	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (the_protocol != NULL, NULL);

	session = g_object_new (GSM_TYPE_SESSION, NULL);

	session->name           = g_strdup (name);
	session->client_factory = client_factory;
	session->data           = user_data;

	command (the_protocol, 
		 gsm_args_to_prop (GsmCommand, GsmReadSession, name, NULL), NULL);
	reads = g_slist_append (reads, session);
  
	return session;
}

GsmSession * 
gsm_session_live (GsmClientFactory client_factory,
		  gpointer         user_data)
{
	GsmSession *session;

	g_return_val_if_fail (the_protocol != NULL, NULL);

	if (live_session) {
		session = live_session;
		g_object_ref (live_session);
	} else {
		session = g_object_new (GSM_TYPE_SESSION, NULL);

		session->name           = NULL; /* FIXME */
		session->client_factory = client_factory;
		session->data           = user_data;

		live_session = session;

		command (the_protocol, 
			 gsm_args_to_prop (GsmCommand, 
					   GsmSelectClientEvents, NULL), NULL);
		command (the_protocol, 
			 gsm_args_to_prop (GsmCommand, 
					   GsmHandleWarnings, NULL), NULL); 
		command (the_protocol, 
			  gsm_args_to_prop (GsmCommand, 
					    GsmListClients, NULL), NULL);
	}

	return session;
}

void
gsm_session_start (GsmSession *session)
{
  g_return_if_fail(session != NULL);
  g_return_if_fail(session != live_session);

  if (session != live_session)
    {
      if (! live_session)
	{
	  live_session = session;
	  command (the_protocol, 
		   gsm_args_to_prop (GsmCommand, 
				     GsmSelectClientEvents, NULL), NULL);
	  gsm_session_set_name (session, session->name);
	}
      command (the_protocol, 
	       gsm_args_to_prop (GsmCommand, 
				 GsmStartSession, session->handle, NULL), 
	       NULL);
    }
}

void 
gsm_session_set_name (GsmSession* session, gchar* name)
{
  g_return_if_fail(session != NULL);
  g_return_if_fail(GSM_IS_SESSION (session));
  g_return_if_fail(session == live_session);
  g_return_if_fail(name != NULL);

  command (the_protocol, 
	   gsm_args_to_prop (GsmCommand, 
			     GsmSetSessionName, name, NULL), NULL);  
  names = g_slist_append (names, session);
}

/* GSM_CLIENT object */

static void 
gsm_client_finalize (GObject *object)
{
	GsmClient *client;

	client = GSM_CLIENT (object);

	if (client->handle)
		g_hash_table_remove (handle_table, client->handle);

	g_free (client->command);
	g_free (client->handle);

	gsm_client_parent_class->finalize (object);
}

static void client_reasons (GsmClient *client,
			    gboolean   confirm, 
			    GSList    *reasons);

static void
gsm_client_class_init (GsmClientClass *klass)
{
	GObjectClass *gobject_class = (GObjectClass*) klass;

	gsm_client_signals [REMOVE] =
		g_signal_new (
			"remove",
			G_TYPE_FROM_CLASS (gobject_class),
			G_SIGNAL_RUN_LAST,
			G_STRUCT_OFFSET (GsmClientClass, remove),
			NULL, NULL,
			g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE, 0); 

	gsm_client_signals [REASONS] =
		g_signal_new (
			"reasons",
			G_TYPE_FROM_CLASS (gobject_class),
			G_SIGNAL_RUN_FIRST,
			G_STRUCT_OFFSET (GsmClientClass, reasons),
			NULL, NULL,
			gsm_marshal_VOID__INT_POINTER,
			G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_POINTER);

	gsm_client_signals [COMMAND] =
		g_signal_new (
			"command",
			G_TYPE_FROM_CLASS (gobject_class),
			G_SIGNAL_RUN_LAST,
			G_STRUCT_OFFSET (GsmClientClass, command),
			NULL, NULL,
			g_cclosure_marshal_VOID__POINTER,
			G_TYPE_NONE, 1, G_TYPE_POINTER);

	gsm_client_signals [STATE] =
		g_signal_new (
			"state",
			G_TYPE_FROM_CLASS (gobject_class),
			G_SIGNAL_RUN_LAST,
			G_STRUCT_OFFSET (GsmClientClass, state),
			NULL, NULL,
			g_cclosure_marshal_VOID__ENUM,
			G_TYPE_NONE, 1, GSM_TYPE_GSM_STATE);

	gsm_client_signals [STYLE] =
		g_signal_new (
			"style",
			G_TYPE_FROM_CLASS (gobject_class),
			G_SIGNAL_RUN_LAST,
			G_STRUCT_OFFSET (GsmClientClass, style),
			NULL, NULL,
			g_cclosure_marshal_VOID__ENUM,
			G_TYPE_NONE, 1, GSM_TYPE_GSM_STYLE);

	gsm_client_signals [ORDER] =
		g_signal_new (
			"order",
			G_TYPE_FROM_CLASS (gobject_class),
			G_SIGNAL_RUN_LAST,
			G_STRUCT_OFFSET (GsmClientClass, order),
			NULL, NULL,
			g_cclosure_marshal_VOID__INT,
			G_TYPE_NONE, 1, G_TYPE_INT);
 
	gobject_class->finalize = gsm_client_finalize;
	
	klass->remove   = NULL;
	klass->reasons  = client_reasons;
	klass->command  = NULL;
	klass->state    = NULL;
	klass->style    = NULL;
	klass->order    = NULL;
}

static void
gsm_client_instance_init (GsmClient *client)
{
	client->handle  = NULL;
	client->session = (GsmSession *) live_session;
	client->order   = DEFAULT_PRIORITY;
	client->style   = GSM_NORMAL;
	client->state   = GSM_INACTIVE;
	client->command = NULL;
}

GType
gsm_client_get_type (void)
{
	static GType retval = 0;

	if (!retval) {
		static const GTypeInfo info = {
			sizeof (GsmClientClass),
			(GBaseInitFunc)     NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc)    gsm_client_class_init,
			NULL,               /* class_finalize */
			NULL,               /* class_data */
			sizeof (GsmClient),
			0,                  /* n_preallocs */
			(GInstanceInitFunc) gsm_client_instance_init
		};

		retval = g_type_register_static (G_TYPE_OBJECT, "GsmClient", &info, 0);
		gsm_client_parent_class = g_type_class_ref (G_TYPE_OBJECT);
	}

	return retval;
}

GsmClient * 
gsm_client_new (void)
{
	GsmClient *client;

	g_return_val_if_fail (the_protocol != NULL, NULL);

	client = g_object_new (GSM_TYPE_CLIENT, NULL);

	return client;
}

void
gsm_client_commit_remove (GsmClient *client)
{
  g_return_if_fail (client != NULL);
  g_return_if_fail (GSM_IS_CLIENT(client));
  g_return_if_fail (the_protocol != NULL);

  command (the_protocol,
	   gsm_args_to_prop (GsmCommand, 
			     GsmRemoveClient, client->handle, NULL), NULL);
  removes = g_slist_append (removes, client);
}

void
gsm_client_commit_add (GsmClient *client)
{
  g_return_if_fail (client != NULL);
  g_return_if_fail (GSM_IS_CLIENT(client));
  g_return_if_fail (the_protocol != NULL);

  command (the_protocol,
	   gsm_args_to_prop (GsmCommand, GsmAddClient, NULL),
	   gsm_sh_to_prop (SmRestartCommand, client->command),
	   gsm_int_to_prop (SmRestartStyleHint, styles[client->style]), 
	   gsm_int_to_prop (GsmPriority, client->order), NULL);
  g_object_ref (client);
  adds = g_slist_append (adds, client);
}

void      
gsm_client_commit_style (GsmClient *client)
{
  g_return_if_fail (client != NULL);
  g_return_if_fail (GSM_IS_CLIENT(client));
  g_return_if_fail (the_protocol != NULL);
 
  command (the_protocol,
	   gsm_args_to_prop (GsmCommand, 
			     GsmChangeProperties, client->handle, NULL),
	   gsm_int_to_prop (SmRestartStyleHint, styles[client->style]), NULL);
}

void      
gsm_client_commit_order (GsmClient *client)
{
  g_return_if_fail (client != NULL);
  g_return_if_fail (GSM_IS_CLIENT(client));
  g_return_if_fail (the_protocol != NULL);
 
  command (the_protocol,
	   gsm_args_to_prop (GsmCommand, 
			     GsmChangeProperties, client->handle, NULL),
	   gsm_int_to_prop (GsmPriority, client->order), 
	   NULL);
}

static void
client_reasons (GsmClient *client,
		gboolean   confirm,
		GSList    *reasons)
{
	GtkWidget *dialog;
	GString   *message;
	int        response;

	message = g_string_new (client->command);

	for (;reasons; reasons = reasons->next) {
		g_string_append_c (message, '\n');
		g_string_append (message, (char *) reasons->data);
	}

	dialog = gtk_message_dialog_new (NULL, 0, GTK_MESSAGE_WARNING,
					 confirm ? GTK_BUTTONS_OK_CANCEL : GTK_BUTTONS_OK,
					 message->str);
  
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

	gtk_widget_show_all (dialog);

	response = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);

	if (confirm && response == GTK_RESPONSE_OK)
		gsm_client_commit_remove (client);

	g_string_free (message, TRUE);
}

static void 
gsm_protocol_finalize (GObject *object)
{
	GsmProtocol *protocol;

	protocol = GSM_PROTOCOL (object);

	g_return_if_fail (protocol == the_protocol);

	if (gnome_client_used) {
		g_object_unref (gnome_client_used);
		gnome_client_used = NULL;
	}

	the_protocol = NULL;

	gsm_protocol_parent_class->finalize (object);
}

static void
gsm_protocol_class_init (GsmProtocolClass *klass)
{
	GObjectClass *gobject_class = (GObjectClass*) klass;

	gsm_protocol_signals [SAVED_SESSIONS] =
		g_signal_new (
			"saved_sessions",
			G_TYPE_FROM_CLASS (gobject_class),
			G_SIGNAL_RUN_LAST,
			G_STRUCT_OFFSET (GsmProtocolClass, saved_sessions),
			NULL, NULL,
			g_cclosure_marshal_VOID__POINTER,
			G_TYPE_NONE, 1, G_TYPE_POINTER); 

	gsm_protocol_signals [CURRENT_SESSION] =
		g_signal_new (
			"current_session",
			G_TYPE_FROM_CLASS (gobject_class),
			G_SIGNAL_RUN_LAST,
			G_STRUCT_OFFSET (GsmProtocolClass, current_session),
			NULL, NULL,
			g_cclosure_marshal_VOID__POINTER,
			G_TYPE_NONE, 1, G_TYPE_POINTER);

	gobject_class->finalize = gsm_protocol_finalize;
  
	klass->saved_sessions = NULL;
	klass->current_session = NULL;
}

GType
gsm_protocol_get_type (void)
{
	static GType retval = 0;

	if (!retval) {
		static const GTypeInfo info = {
			sizeof (GsmProtocolClass),
			(GBaseInitFunc)     NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc)    gsm_protocol_class_init,
			NULL,               /* class_finalize */
			NULL,               /* class_data */
			sizeof (GsmProtocol),
			0,                  /* n_preallocs */
			(GInstanceInitFunc) NULL
		};

		retval = g_type_register_static (G_TYPE_OBJECT, "GsmProtocol", &info, 0);
		gsm_protocol_parent_class = g_type_class_ref (G_TYPE_OBJECT);
	}

	return retval;
}

GsmProtocol * 
gsm_protocol_new (GnomeClient *gnome_client)
{
	GsmProtocol *protocol;

	g_return_val_if_fail (!the_protocol, NULL);
	g_return_val_if_fail (GNOME_IS_CLIENT (gnome_client), NULL);
	g_return_val_if_fail (GNOME_CLIENT_CONNECTED (gnome_client), NULL);
	g_return_val_if_fail (GSM_IS_GNOME_CLIENT (gnome_client), NULL);

	gnome_client_used = g_object_ref (gnome_client);

	protocol = g_object_new (GSM_TYPE_PROTOCOL, NULL);
	protocol->smc_conn = gnome_client->smc_conn;
	the_protocol = protocol;

	request_event (protocol);

	return protocol;
}

void 
gsm_protocol_get_saved_sessions (GsmProtocol* protocol)
{
  g_return_if_fail(protocol != NULL);
  g_return_if_fail(GSM_IS_PROTOCOL (protocol));

  command (protocol, 
	   gsm_args_to_prop (GsmCommand, 
			     GsmListSessions, NULL), NULL);  
}

void
gsm_protocol_get_current_session (GsmProtocol* protocol)
{
  g_return_if_fail(protocol != NULL);
  g_return_if_fail(GSM_IS_PROTOCOL (protocol));

  command (protocol, 
	   gsm_args_to_prop (GsmCommand, 
			     GsmGetCurrentSession, NULL), NULL);  
}

void 
gsm_protocol_set_autosave_mode (GsmProtocol *protocol,
			     gboolean     auto_save)
{
  SmProp prop;
  SmPropValue vals[2];

  g_return_if_fail(protocol != NULL);
  g_return_if_fail(GSM_IS_PROTOCOL (protocol));

  vals[0].length = strlen (GsmAutoSaveMode);
  vals[0].value = GsmAutoSaveMode;

  vals[1].length = 1;
  vals[1].value = auto_save ? "1" : "0";

  prop.name = GsmCommand;
  prop.type = SmLISTofARRAY8;
  prop.num_vals = 2;
  prop.vals = vals;
  
  command (protocol, &prop, NULL);
}

void 
gsm_protocol_set_sessionsave_mode (GsmProtocol *protocol,
				gboolean sessionsave)
{
  SmProp prop;
  SmPropValue vals[2];

  g_return_if_fail(protocol != NULL);
  g_return_if_fail(GSM_IS_PROTOCOL (protocol));

  vals[0].length = strlen (GsmSessionSaveMode);
  vals[0].value = GsmSessionSaveMode;

  vals[1].length = 1;
  vals[1].value = sessionsave ? "1" : "0";

  prop.name = GsmCommand;
  prop.type = SmLISTofARRAY8;
  prop.num_vals = 2;
  prop.vals = vals;

  command (protocol, &prop, NULL);
}

void
gsm_protocol_set_session_name (GsmProtocol *protocol,
			       const char  *name)
{
  if(name) {
    SmProp prop;
    SmPropValue vals[2];

    g_return_if_fail(protocol != NULL);
    g_return_if_fail(GSM_IS_PROTOCOL (protocol));

    vals[0].length = strlen (GsmSetSessionName);
    vals[0].value = GsmSetSessionName;

    vals[1].length = strlen (name);
    vals[1].value = g_strdup (name);

    prop.name = GsmCommand;
    prop.type = SmLISTofARRAY8;
    prop.num_vals = 2;
    prop.vals = vals;

    command (protocol, &prop, NULL); 
  }
}  

/* Public utilities */
gboolean   gsm_sh_quotes_balance (const char *sh)
{
  SmProp* prop = gsm_sh_to_prop (SmRestartCommand, sh);
  gboolean complete = prop != NULL;
  prop_free (prop);
  return complete;
}

/* Interal utilities */
static void
commandv (GsmProtocol* protocol, va_list args)
{
  SmcConn smc_conn = (SmcConn) protocol->smc_conn;
  SmProp** props;
  gint i, nprops = 0;
  va_list args2;

  G_VA_COPY (args2, args);

  while (va_arg (args, gchar*)) nprops++;

  props = (SmProp**) malloc (sizeof (SmProp*) * nprops);

  for (i = 0; i < nprops; i++)
    props[i] = (SmProp*)va_arg (args2, SmProp*);

  va_end (args2);

  SmcSetProperties (smc_conn, nprops, props);
  free (props);
}

static void
command (GsmProtocol* protocol, ...)
{
  va_list args;

  va_start (args, protocol);
  commandv (protocol, args);
  va_end (args);
}

static GsmClient* 
find_client (SmProp* prop, GsmSession *session)
{
  GsmClient* client = NULL;

  if (!handle_table)
    handle_table = g_hash_table_new (g_str_hash, g_str_equal);

  client = (GsmClient*)g_hash_table_lookup (handle_table, 
					    GSM_CLIENT_HANDLE(prop));
  if (!client)
    {
      client = GSM_CLIENT (session->client_factory (session->data));
      client->handle  = g_strdup (GSM_CLIENT_HANDLE (prop));
      g_hash_table_insert (handle_table, client->handle, client);
      command ((GsmProtocol*)the_protocol, 
	       gsm_args_to_prop (GsmCommand, 
				 GsmListProperties, client->handle, NULL), 
	       NULL);
    }
  return client;
}     

/* Event loop implementation: */
static void
dispatch_event (SmcConn smc_conn, SmPointer data, 
		   int nprops, SmProp **props)
{
  GsmProtocol *protocol = (GsmProtocol*)data;

  if(nprops > 0)
    {
      if (GSM_IS_CLIENT_EVENT (props[0]))
	{
	  if (!strcmp (GSM_CLIENT_TYPE (props[0]), GsmProperty))
	    {
	      gint i;
	      gchar *restart_command = NULL, *tmp_command = NULL;
	      GsmClient* client = find_client (props[0], live_session);
	      GsmSession* session = client->session;
	      prop_free (props[0]);

	      if (!g_slist_find(removes, client))
		{
		  for (i = 1; i < nprops; i++)
		    {
		      gchar* name = GSM_PROP_NAME (props[i]);
		      
		      if (!strcmp (name, GsmPriority))
			{
			  guint order = GSM_PROP_INT (props[i]);
			  g_signal_emit (client, gsm_client_signals [ORDER], 0, order); 
			  client->order = order;
			}
		      else if (!strcmp (name, SmRestartStyleHint))
			{
			  guint style = r_styles[(int)GSM_PROP_INT (props[i])];
			  g_signal_emit (client, gsm_client_signals [STYLE], 0, style); 
			  client->style = style;
			}
		      else if (!strcmp (name, SmCloneCommand))
			{
			  tmp_command = gsm_prop_to_sh (props[i]);
			}
		      else if (!strcmp (name, SmRestartCommand))
			{
			  if (! command && ! client->command)
			    {
			      restart_command = gsm_prop_to_sh (props[i]);
			      tmp_command = restart_command;
			    }
			}
		      prop_free (props[i]);
		    }

		  if (tmp_command)
		    {		      
		      g_signal_emit (client, gsm_client_signals [COMMAND], 0, tmp_command);
		      if (! client->command && session->waiting)
			{
			  session->waiting--;
			  if (!session->waiting)
			    g_signal_emit (session, gsm_session_signals[INITIALIZED], 0);
			}
		      g_free (client->command);
		      client->command = tmp_command;
		      
		      if (restart_command != tmp_command)
			g_free (restart_command);
		    }
		}
	    }
	  else
	    {
	      gint i;
	      for (i = 0; i < nprops; i++)
		{
		  GsmClient* client = find_client (props[i], live_session);
		  gchar* type = GSM_CLIENT_TYPE (props[i]);

		  if (!strcmp (type, GsmReasons))
		    {
		      GSList *list = gsm_prop_to_list (props[i]);
		      gboolean confirm; 
		      
		      list = g_slist_remove (list, list->data);
		      list = g_slist_remove (list, list->data);
		      confirm = strcmp (list->data, "0");
		      list = g_slist_remove (list, list->data);
		      g_signal_emit (client, gsm_client_signals [REASONS], 0, confirm, list);
		      command (protocol, 
			       gsm_args_to_prop (GsmCommand, 
						 GsmClearClientWarning, 
						 client->handle, NULL), 
			       NULL);  
		      g_slist_free (list);
		    }
		  else if (!strcmp (type, GsmRemove))
		    {
		      g_signal_emit (client, gsm_client_signals [REMOVE], 0);
		      removes = g_slist_remove(removes, client);
		      g_object_unref (client);
		    }
		  else
		    {
		      GsmState state = GSM_INACTIVE;
		      GsmSession* session = client->session;

		      if (! strcmp (type, GsmStart))
			state = GSM_START;
		      else if (! strcmp (type, GsmConnected))
			state = GSM_CONNECTED;
		      else if (! strcmp (type, GsmSave))
			state = GSM_SAVE;
		      else if (! strcmp (type, GsmUnknown))
			state = GSM_UNKNOWN;
		      
		      g_signal_emit (client, gsm_client_signals [STATE], 0, state);
		      client->state = state;

		      if (session->waiting == -1)
			session->waiting = 1;
		      else if (session->waiting)
			session->waiting++;
		      
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
	      client->handle = g_strdup(GSM_COMMAND_ARG1 (props[0]));
	      g_hash_table_insert (handle_table, client->handle, client);
	      adds = g_slist_remove (adds, client);
	      prop_free (props[0]);
	    }
	  else if (!strcmp (GSM_COMMAND_ARG0 (props[0]), GsmSetSessionName) && 
	      names)
	    {
	      GsmSession* session = (GsmSession*) names->data;
	      gchar* name = g_strdup (GSM_COMMAND_ARG1 (props[0]));
	      g_signal_emit (session, gsm_session_signals [SESSION_NAME], 0, name);
	      g_free (session->name);
	      session->name = name;
	      names = g_slist_remove (names, session);
	      prop_free (props[0]);
	    }
	  else if (!strcmp (GSM_COMMAND_ARG0 (props[0]), GsmReadSession))
	    {
	      GSList *list = NULL;
	      gint i;
	      
	      for (i = 0; i < nprops; i++)
		list = g_slist_append (list, GSM_COMMAND_ARG1 (props[i]));
	      
	      g_signal_emit (protocol, gsm_protocol_signals [SAVED_SESSIONS], 0, list);
	      
	      for (i = 0; i < nprops; i++)
		prop_free (props[i]);
	      g_slist_free (list);
	    }
	  else if (!strcmp (GSM_COMMAND_ARG0 (props[0]), GsmGetCurrentSession))
	    {
	      g_signal_emit (protocol,
			     gsm_protocol_signals [CURRENT_SESSION], 0, 
			     GSM_COMMAND_ARG1 (props[0]));
	    }
	  else if (!strcmp (GSM_COMMAND_ARG0 (props[0]), GsmStartSession) && 
		   reads)
	    {
	      GsmSession* session = (GsmSession*)reads->data;
	      gint i;

	      session->handle = g_strdup (GSM_COMMAND_ARG1 (props[0]));
	      prop_free (props[0]);
	      prop_free (props[1]);
	      for (i = 2; i < nprops; i++)
		{
		  GsmClient* client = find_client (props[i], session);
		  gchar* type = GSM_CLIENT_TYPE (props[i]);
		  GsmState state = GSM_INACTIVE;

		  client->session = session;
		  if (! strcmp (type, GsmStart))
		    state = GSM_START;
		  else if (! strcmp (type, GsmConnected))
		    state = GSM_CONNECTED;
		  else if (! strcmp (type, GsmSave))
		    state = GSM_SAVE;
		  else if (! strcmp (type, GsmUnknown))
		    state = GSM_UNKNOWN;

		  g_signal_emit (client, gsm_client_signals [STATE], 0, state);
		  client->state = state;
		  if (session->waiting == -1)
		    session->waiting = 1;
		  else if (session->waiting)
		    session->waiting++;
		  prop_free (props[i]);
		}  
	      reads = g_slist_remove (reads, session);
	    }
	  /* Dispatch other things ... */
	}
      free (props);
    }

  g_idle_add (request_event, data);
}

/* libSM prevents us form calling SmcGetProperties within its own callback
* (dispatch_event) so we have to use an idle routine to make the call*/
static gint
request_event (gpointer data)
{
  GsmProtocol *protocol = (GsmProtocol *)data;
  SmcConn smc_conn = (SmcConn) protocol->smc_conn;
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
gsm_sh_to_prop (const char *name,
		const char *sh)
{
  SmProp *prop = (SmProp*) malloc (sizeof (SmProp));
  gchar *dest, *src, *tmp;
  gint i;
  
  prop->num_vals = 0;

  dest = src = tmp = strdup (sh);

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

  for (i = 0, src = tmp; i < prop->num_vals; i++)
    {
      prop->vals[i].value = strdup (src);
      prop->vals[i].length = strlen (prop->vals[i].value);
      src += prop->vals[i].length + 1;
    }

  free (tmp);

  return prop;

 error:
  free (tmp);
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
