/* dbus.c
 * Copyright (C) 2007 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <glib.h>
#include <stdlib.h>

#include <gconf/gconf-client.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-bindings.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "dbus.h"
#include "gsm.h"
#include "dbus.h"

static DBusGConnection *connection = NULL;

/**
 * gsm_dbus_init:
 *
 * Starts dbus-daemon if it is not already running. This must be
 * called very soon after startup. It requires no initialization
 * beyond g_type_init() and gsm_xsmp_init().
 **/
void
gsm_dbus_init (void)
{
  char *argv[3];
  char *output, **vars;
  int status, i;
  GError *error;

  /* Check whether there is a dbus-daemon session instance currently
   * running (not spawned by us).
   */
  if (g_getenv ("DBUS_SESSION_BUS_ADDRESS"))
    {
      g_warning ("dbus-daemon is already running: processes launched by "
		 "D-Bus won't have access to $SESSION_MANAGER!");
      return;
    }

  argv[0] = DBUS_LAUNCH;
  argv[1] = "--exit-with-session";
  argv[2] = NULL;

  /* dbus-launch exits pretty quickly, but if necessary, we could
   * make this async. (It's a little annoying since the main loop isn't
   * running yet...)
   */
  error = NULL;
  g_spawn_sync (NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
		&output, NULL, &status, &error);
  if (error)
    {
      gsm_initialization_error (TRUE, "Could not start dbus-daemon: %s",
	  			error->message);
      /* not reached */
    }

  vars = g_strsplit (output, "\n", 0);
  for (i = 0; vars[i]; i++)
    putenv (vars[i]);

  /* Can't free the putenv'ed strings */
  g_free (vars);
  g_free (output);
}

void
gsm_dbus_check (void)
{
  GError *error = NULL;

  connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
  if (!connection)
    {
      gsm_initialization_error (TRUE, "Could not start D-Bus: %s",
				error->message);
      /* not reached */
    }

  dbus_connection_set_exit_on_disconnect (dbus_g_connection_get_connection (connection), FALSE);
}

DBusGConnection *
gsm_dbus_get_connection (void)
{
  return connection;
}


/* D-Bus server */

#define GSM_TYPE_DBUS_SERVER            (gsm_dbus_server_get_type ())
#define GSM_DBUS_SERVER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSM_TYPE_DBUS_SERVER, GsmDBusServer))
#define GSM_DBUS_SERVER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GSM_TYPE_DBUS_SERVER, GsmDBusServerClass))
#define GSM_IS_DBUS_SERVER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSM_TYPE_DBUS_SERVER))
#define GSM_IS_DBUS_SERVER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GSM_TYPE_DBUS_SERVER))
#define GSM_DBUS_SERVER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GSM_TYPE_DBUS_SERVER, GsmDBusServerClass))

typedef struct
{
  GObject parent;

} GsmDBusServer;

typedef struct
{
  GObjectClass parent_class;

} GsmDBusServerClass;

static GType gsm_dbus_server_get_type (void) G_GNUC_CONST;

static gboolean gsm_dbus_server_setenv (GsmDBusServer  *dbus,
					const char     *variable,
					const char     *value,
					GError        **error);

static gboolean gsm_dbus_server_initialization_error (GsmDBusServer  *dbus,
						      const char     *message,
						      gboolean        fatal,
						      GError        **error);

static gboolean gsm_dbus_server_logout (GsmDBusServer  *dbus,
                                        gint            mode,
					GError        **error);

static gboolean gsm_dbus_server_shutdown (GsmDBusServer  *dbus,
					  GError        **error);

#include "dbus-glue.h"

G_DEFINE_TYPE (GsmDBusServer, gsm_dbus_server, G_TYPE_OBJECT)

static void
gsm_dbus_server_init (GsmDBusServer *server)
{
  ;
}

static void
gsm_dbus_server_class_init (GsmDBusServerClass *klass)
{
  dbus_g_object_type_install_info (GSM_TYPE_DBUS_SERVER,
				   &dbus_glib_gsm_dbus_server_object_info);
}

static GQuark
gsm_dbus_error_quark (void)
{
	static GQuark error;
	if (!error)
		error = g_quark_from_static_string ("gsm_dbus_error_quark");
	return error;
}
#define GSM_DBUS_ERROR (gsm_dbus_error_quark ())

typedef enum {
  GSM_DBUS_ERROR_NOT_IN_INITIALIZATION,
  GSM_DBUS_ERROR_NOT_IN_RUNNING,
} GsmDBusError;

static gboolean
gsm_dbus_server_setenv (GsmDBusServer  *dbus,
			const char     *variable,
			const char     *value,
			GError        **error)
{
  if (gsm_session_get_phase (global_session) > GSM_SESSION_PHASE_INITIALIZATION)
    {
      g_set_error (error, GSM_DBUS_ERROR,
		   GSM_DBUS_ERROR_NOT_IN_INITIALIZATION,
		   "Setenv interface is only available during the Initialization phase");
      return FALSE;
    }

  g_setenv (variable, value, TRUE);
  return TRUE;
}

static gboolean
gsm_dbus_server_initialization_error (GsmDBusServer  *dbus,
				      const char     *message,
				      gboolean        fatal,
				      GError        **error)
{
  if (gsm_session_get_phase (global_session) > GSM_SESSION_PHASE_INITIALIZATION)
    {
      g_set_error (error, GSM_DBUS_ERROR,
		   GSM_DBUS_ERROR_NOT_IN_INITIALIZATION,
		   "InitializationError interface is only available during the Initialization phase");
      return FALSE;
    }

  gsm_initialization_error (fatal, "%s", message);
  return TRUE;
}

static gboolean
gsm_dbus_server_logout (GsmDBusServer  *dbus,
                        gint            logout_mode,
			GError        **error)
{
  if (gsm_session_get_phase (global_session) != GSM_SESSION_PHASE_RUNNING)
    {
      g_set_error (error, GSM_DBUS_ERROR,
		   GSM_DBUS_ERROR_NOT_IN_RUNNING,
		   "Shutdown interface is only available during the Running phase");
      return FALSE;
    }

  switch (logout_mode)
    {
    case GSM_SESSION_LOGOUT_MODE_NORMAL:
      gsm_session_initiate_shutdown (global_session, TRUE, GSM_SESSION_LOGOUT_TYPE_LOGOUT);
      break;

    case GSM_SESSION_LOGOUT_MODE_NO_CONFIRMATION:
      gsm_session_initiate_shutdown (global_session, FALSE, GSM_SESSION_LOGOUT_TYPE_LOGOUT);
      break;

    case GSM_SESSION_LOGOUT_MODE_FORCE:
      /* FIXME: Implement when session state saving is ready */
      break;

    default:
      g_assert_not_reached ();
    }

  return TRUE;
}

static gboolean
gsm_dbus_server_shutdown (GsmDBusServer  *dbus,
			  GError        **error)
{
  if (gsm_session_get_phase (global_session) != GSM_SESSION_PHASE_RUNNING)
    {
      g_set_error (error, GSM_DBUS_ERROR,
		   GSM_DBUS_ERROR_NOT_IN_RUNNING,
		   "Logout interface is only available during the Running phase");
      return FALSE;
    }

  gsm_session_initiate_shutdown (global_session, TRUE, GSM_SESSION_LOGOUT_TYPE_SHUTDOWN);

  return TRUE;
}

static GsmDBusServer *global_dbus_server;

void
gsm_dbus_run (void)
{
  DBusGConnection *connection = gsm_dbus_get_connection ();
  DBusGProxy *bus_proxy;
  GError *error = NULL;
  guint ret;

  g_assert (global_dbus_server == NULL);

  global_dbus_server = g_object_new (GSM_TYPE_DBUS_SERVER, NULL);
  dbus_g_connection_register_g_object (connection,
				       "/org/gnome/SessionManager",
				       G_OBJECT (global_dbus_server));

  bus_proxy = dbus_g_proxy_new_for_name (connection,
					 DBUS_SERVICE_DBUS,
					 DBUS_PATH_DBUS,
					 DBUS_INTERFACE_DBUS);
  if (!org_freedesktop_DBus_request_name (bus_proxy,
					  "org.gnome.SessionManager",
					  0, &ret, &error))
    {
      g_warning ("Unable to register name org.gnome.SessionManager: %s",
		 error->message);
      g_error_free (error);
    }

  g_object_unref (bus_proxy);
}

void
gsm_dbus_shutdown (void)
{
  g_object_unref (global_dbus_server);
  global_dbus_server = NULL;
}
