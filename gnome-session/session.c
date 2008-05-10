/* session.c
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

#include <gtk/gtkmain.h>
#include <gtk/gtkdialog.h>

#include <string.h>

#include "app-autostart.h"
#include "app-resumed.h"
#include "logout-dialog.h"
#include "power-manager.h"
#include "gdm.h"
#include "gconf.h"
#include "gsm.h"
#include "session.h"
#include "xsmp.h"
#include "util.h"

static void append_default_apps       (GsmSession *session,
                                       char **autostart_dirs);

static void append_autostart_apps     (GsmSession *session,
				       const char *dir);

static void append_saved_session_apps (GsmSession *session);

static void append_required_apps      (GsmSession *session);

static void logout_dialog_response    (GsmLogoutDialog *logout_dialog,
		                       guint            response_id,
		                       gpointer         data);

static void initiate_shutdown         (GsmSession *session);

static void session_shutdown          (GsmSession *session);

static void client_saved_state         (GsmClient *client,
					gpointer   data);
static void client_request_phase2      (GsmClient *client,
					gpointer   data);
static void client_request_interaction (GsmClient *client,
					gpointer   data);
static void client_interaction_done    (GsmClient *client,
					gboolean   cancel_shutdown,
					gpointer   data);
static void client_save_yourself_done  (GsmClient *client,
					gpointer   data);
static void client_disconnected        (GsmClient *client,
					gpointer   data);

struct _GsmSession {
  /* Startup/resumed apps */
  GSList *apps;
  GHashTable *apps_by_name;

  /* Current status */
  GsmSessionPhase phase;
  guint timeout;
  GSList *pending_apps;

  /* SM clients */
  GSList *clients;

  /* When shutdown starts, all clients are put into shutdown_clients.
   * If they request phase2, they are moved from shutdown_clients to
   * phase2_clients. If they request interaction, they are appended
   * to interact_clients (the first client in interact_clients is
   * the one currently interacting). If they report that they're done,
   * they're removed from shutdown_clients/phase2_clients.
   *
   * Once shutdown_clients is empty, phase2 starts. Once phase2_clients
   * is empty, shutdown is complete.
   */
  GSList *shutdown_clients;
  GSList *interact_clients;
  GSList *phase2_clients;

  /* List of clients which were disconnected due to disabled condition
   * and shouldn't be automatically restarted */
  GSList *condition_clients;

  gint logout_response_id;
};

#define GSM_SESSION_PHASE_TIMEOUT 10 /* seconds */

/**
 * gsm_session_new:
 * @failsafe: whether or not to do a failsafe session
 *
 * Creates a new session. If @failsafe is %TRUE, it will only load the
 * default session. If @failsafe is %FALSE, it will also load the
 * contents of autostart directories and saved session info.
 *
 * Return value: a new %GsmSession
 **/
GsmSession *
gsm_session_new (gboolean failsafe)
{
  GsmSession *session = g_new0 (GsmSession, 1);
  char **autostart_dirs;
  int i;

  session->clients = NULL;
  session->condition_clients = NULL;

  session->logout_response_id = GTK_RESPONSE_NONE;

  session->apps_by_name = g_hash_table_new (g_str_hash, g_str_equal);

  autostart_dirs = gsm_util_get_autostart_dirs ();

  append_default_apps (session, autostart_dirs);

  if (failsafe)
    goto out;

  for (i = 0; autostart_dirs[i]; i++)
    {
      append_autostart_apps (session, autostart_dirs[i]);
    }

  append_saved_session_apps (session);

  /* We don't do this in the failsafe case, because the default
   * session should include all requirements anyway. */
  append_required_apps (session);

out:
  g_strfreev (autostart_dirs);

  return session;
}

static void
append_app (GsmSession *session, GsmApp *app)
{
  const char *basename = gsm_app_get_basename (app);
  GsmApp *dup = g_hash_table_lookup (session->apps_by_name, basename);

  if (dup)
    {
      /* FIXME */
      g_object_unref (app);
      return;
    }

  session->apps = g_slist_append (session->apps, app);
  g_hash_table_insert (session->apps_by_name, g_strdup (basename), app);
}

static void
append_default_apps (GsmSession *session, char **autostart_dirs)
{
  GSList *default_apps, *a;
  char **app_dirs;

  g_debug ("append_default_apps ()");

  app_dirs = gsm_util_get_app_dirs (); 

  default_apps = 
    gconf_client_get_list (gsm_gconf_get_client (),
			   GSM_GCONF_DEFAULT_SESSION_KEY, 
                           GCONF_VALUE_STRING,
			   NULL);

  for (a = default_apps; a; a = a->next)
    {
      GKeyFile *key_file;
      char *app_path = NULL;
      char *desktop_file;

      key_file = g_key_file_new ();

      if (!a->data)
        continue;

      desktop_file = g_strdup_printf ("%s.desktop", (char *) a->data); 

      g_debug ("Look for: %s", desktop_file);

      g_key_file_load_from_dirs (key_file, 
                                 desktop_file, 
                                 (const gchar**) app_dirs, 
                                 &app_path, 
                                 G_KEY_FILE_NONE, 
                                 NULL);

      if (!app_path)
        g_key_file_load_from_dirs (key_file, 
                                   desktop_file, 
                                   (const gchar**) autostart_dirs, 
                                   &app_path, 
                                   G_KEY_FILE_NONE, 
                                   NULL);

      if (app_path)
        {
          GsmApp *app;
	  char *client_id;

          g_debug ("Found in: %s", app_path);

	  client_id = gsm_xsmp_generate_client_id ();
	  app = gsm_app_autostart_new (app_path, client_id);
	  g_free (client_id);
	  g_free (app_path);

	  if (app)
            {
              g_debug ("read %s\n", desktop_file);
              append_app (session, app);
            }
          else
            g_warning ("could not read %s\n", desktop_file);
        }

      g_free (desktop_file);
    }

  g_slist_foreach (default_apps, (GFunc) g_free, NULL);
  g_slist_free (default_apps);
  g_strfreev (app_dirs);
}

static void
append_autostart_apps (GsmSession *session, const char *path)
{
  GDir *dir;
  const char *name;

  g_debug ("append_autostart_apps (%s)", path);

  dir = g_dir_open (path, 0, NULL);
  if (!dir)
    return;

  while ((name = g_dir_read_name (dir)))
    {
      GsmApp *app;
      char *desktop_file, *client_id;

      if (!g_str_has_suffix (name, ".desktop"))
	continue;

      desktop_file = g_build_filename (path, name, NULL);
      client_id = gsm_xsmp_generate_client_id ();
      app = gsm_app_autostart_new (desktop_file, client_id);
      if (app)
	{
	  g_debug ("read %s\n", desktop_file);
	  append_app (session, app);
	}
      else
	g_warning ("could not read %s\n", desktop_file);

      g_free (desktop_file);
      g_free (client_id);
    }

  g_dir_close (dir);
}

#if 0
static void
append_modern_session_apps (GsmSession *session,
			    const char *session_filename,
			    gboolean discard)
{
  GKeyFile *saved;
  char **clients;
  gsize num_clients, i;

  saved = g_key_file_new ();
  if (!g_key_file_load_from_file (saved, session_filename, 0, NULL))
    {
      /* FIXME: error handling? */
      g_key_file_free (saved);
      return;
    }

  clients = g_key_file_get_groups (saved, &num_clients);
  for (i = 0; i < num_clients; i++)
    {
      GsmApp *app = gsm_app_resumed_new_from_session (saved, clients[i],
						      discard);
      if (app)
	append_app (session, app);
    }

  g_key_file_free (saved);
}
#endif

/* FIXME: need to make sure this only happens once */
static void
append_legacy_session_apps (GsmSession *session, const char *session_filename)
{
  GKeyFile *saved;
  int num_clients, i;

  saved = g_key_file_new ();
  if (!g_key_file_load_from_file (saved, session_filename, 0, NULL))
    {
      /* FIXME: error handling? */
      g_key_file_free (saved);
      return;
    }

  num_clients = g_key_file_get_integer (saved, "Default", "num_clients", NULL);
  for (i = 0; i < num_clients; i++)
    {
      GsmApp *app = gsm_app_resumed_new_from_legacy_session (saved, i);
      if (app)
	append_app (session, app);
    }

  g_key_file_free (saved);
}

static void
append_saved_session_apps (GsmSession *session)
{
  char *session_filename;

#if 0
  /* Try resuming last session first */
  session_filename = g_build_filename (g_get_home_dir (), ".gnome2",
				       "session-state", "last", NULL);
  if (g_file_test (session_filename, G_FILE_TEST_EXISTS))
    {
      append_modern_session_apps (session, session_filename, TRUE);
      g_free (session_filename);
      return;
    }
  g_free (session_filename);

  /* Else, try resuming default session */
  session_filename = g_build_filename (g_get_home_dir (), ".gnome2",
				       "session-state", "default", NULL);
  if (g_file_test (session_filename, G_FILE_TEST_EXISTS))
    {
      append_modern_session_apps (session, session_filename, FALSE);
      g_free (session_filename);
      return;
    }
  g_free (session_filename);
#endif

  /* Finally, try resuming from the old gnome-session's files */
  session_filename = g_build_filename (g_get_home_dir (), ".gnome2",
				       "session", NULL);
  if (g_file_test (session_filename, G_FILE_TEST_EXISTS))
    {
      append_legacy_session_apps (session, session_filename);
      g_free (session_filename);
      return;
    }
  g_free (session_filename);
}

static void
append_required_apps (GsmSession *session)
{
  GSList *required_components, *r, *a;
  GConfEntry *entry;
  const char *service, *default_provider;
  GsmApp *app;
  gboolean found;

  required_components = gconf_client_all_entries (gsm_gconf_get_client (),
						  GSM_GCONF_REQUIRED_COMPONENTS_DIRECTORY, NULL);

  for (r = required_components; r; r = r->next)
    {
      entry = (GConfEntry *) r->data;

      service = strrchr (entry->key, '/');
      if (!service)
	continue;
      service++;
      default_provider = gconf_value_get_string (entry->value);
      if (!default_provider)
	continue;

      for (a = session->apps, found = FALSE; a; a = a->next)
	{
	  app = a->data;

	  if (gsm_app_provides (app, service))
	    {
	      found = TRUE;
	      break;
	    }
	}

      if (!found)
	{
	  char *client_id;

	  client_id = gsm_xsmp_generate_client_id ();
	  app = gsm_app_autostart_new (default_provider, client_id);
	  g_free (client_id);
	  if (app)
	    append_app (session, app);
	  /* FIXME: else error */
	}

      gconf_entry_free (entry);
    }

  g_slist_free (required_components);
}

static void start_phase (GsmSession *session);

static void
end_phase (GsmSession *session)
{
  g_slist_free (session->pending_apps);
  session->pending_apps = NULL;

  g_debug ("ending phase %d\n", session->phase);

  session->phase++;

  if (session->phase < GSM_SESSION_PHASE_RUNNING)
    start_phase (session);
}

static void
app_condition_changed (GsmApp *app, gboolean condition, gpointer data)
{
  GsmSession *session;
  GsmClient *client = NULL;
  GSList *cl = NULL;

  g_return_if_fail (data != NULL);

  session = (GsmSession *) data;

  /* Check for an existing session client for this app */
  for (cl = session->clients; cl; cl = cl->next)
    {
      GsmClient *c = GSM_CLIENT (cl->data);

      if (!strcmp (app->client_id, gsm_client_get_client_id (c)))
        client = c;
    }

  if (condition)
    {
      GError *error = NULL;

      if (app->pid <= 0 && client == NULL)
        gsm_app_launch (app, &error);

      if (error != NULL)
        {
          g_warning ("Not able to launch autostart app from its condition: %s",
                     error->message);

          g_error_free (error);
        }
    }
  else
    {
      /* Kill client in case condition if false and make sure it won't
       * be automatically restarted by adding the client to 
       * condition_clients */
      session->condition_clients =
            g_slist_prepend (session->condition_clients, client);
      gsm_client_die (client);
      app->pid = -1; 
    }
}

static void
app_registered (GsmApp *app, gpointer data)
{
  GsmSession *session = data;

  session->pending_apps = g_slist_remove (session->pending_apps, app);
  g_signal_handlers_disconnect_by_func (app, app_registered, session);

  if (!session->pending_apps)
    {
      if (session->timeout > 0)
        {
          g_source_remove (session->timeout);
          session->timeout = 0;
        }

      end_phase (session);
    }
}

static gboolean
phase_timeout (gpointer data)
{
  GsmSession *session = data;
  GSList *a;

  session->timeout = 0;

  for (a = session->pending_apps; a; a = a->next)
    {
      g_warning ("Application '%s' failed to register before timeout",
		 gsm_app_get_basename (a->data));
      g_signal_handlers_disconnect_by_func (a->data, app_registered, session);

      /* FIXME: what if the app was filling in a required slot? */
    }

  end_phase (session);
  return FALSE;
}

static void
start_phase (GsmSession *session)
{
  GsmApp *app;
  GSList *a;
  GError *err = NULL;

  g_debug ("starting phase %d\n", session->phase);

  g_slist_free (session->pending_apps);
  session->pending_apps = NULL;

  for (a = session->apps; a; a = a->next)
    {
      app = a->data;

      if (gsm_app_get_phase (app) != session->phase)
	continue;

      /* Keep track of app autostart condition in order to react
       * accordingly in the future. */
      g_signal_connect (app, "condition-changed",
        		G_CALLBACK (app_condition_changed), session);

      if (gsm_app_is_disabled (app))
	continue;

      if (gsm_app_launch (app, &err) > 0)
	{
	  if (session->phase == GSM_SESSION_PHASE_INITIALIZATION)
	    {
              /* Applications from Initialization phase are considered 
               * registered when they exit normally. This is because
               * they are expected to just do "something" and exit */
	      g_signal_connect (app, "exited",
				G_CALLBACK (app_registered), session);
	    }

	  if (session->phase < GSM_SESSION_PHASE_APPLICATION)
	    {
	      g_signal_connect (app, "registered",
				G_CALLBACK (app_registered), session);

	      session->pending_apps =
	   	    g_slist_prepend (session->pending_apps, app);
	    }
	}
      else if (err != NULL)
	{
	  g_warning ("Could not launch application '%s': %s",
		     gsm_app_get_basename (app), err->message);
	  g_error_free (err);
	  err = NULL;
	}
    }

  if (session->pending_apps)
    {
      if (session->phase < GSM_SESSION_PHASE_APPLICATION)
	{
	  session->timeout = g_timeout_add (GSM_SESSION_PHASE_TIMEOUT * 1000,
					    phase_timeout, session);
	}
    }
  else
    end_phase (session);
}

void
gsm_session_start (GsmSession *session)
{
  session->phase = GSM_SESSION_PHASE_INITIALIZATION;

  start_phase (session);
}

GsmSessionPhase
gsm_session_get_phase (GsmSession *session)
{
  return session->phase;
}

char *
gsm_session_register_client (GsmSession *session,
			     GsmClient  *client,
			     const char *id)
{
  GSList *a;
  char *client_id = NULL; 

  /* If we're shutting down, we don't accept any new session
     clients. */
  if (session->phase == GSM_SESSION_PHASE_SHUTDOWN)
    return FALSE;

  if (id == NULL)
    client_id = gsm_xsmp_generate_client_id ();
  else
    {
      for (a = session->clients; a; a = a->next)
        {
          GsmClient *client = GSM_CLIENT (a->data);

          /* We can't have two clients with the same id. */
          if (!strcmp (id, gsm_client_get_client_id (client)))
            {
              return NULL;
            }
        }
      
      client_id = g_strdup (id);
    }

  g_debug ("Adding new client %s to session", id);

  g_signal_connect (client, "saved_state",
		    G_CALLBACK (client_saved_state), session);
  g_signal_connect (client, "request_phase2",
		    G_CALLBACK (client_request_phase2), session);
  g_signal_connect (client, "request_interaction",
		    G_CALLBACK (client_request_interaction), session);
  g_signal_connect (client, "interaction_done",
		    G_CALLBACK (client_interaction_done), session);
  g_signal_connect (client, "save_yourself_done",
		    G_CALLBACK (client_save_yourself_done), session);
  g_signal_connect (client, "disconnected",
		    G_CALLBACK (client_disconnected), session);

  session->clients = g_slist_prepend (session->clients, client);

  /* If it's a brand new client id, we just accept the client*/
  if (id == NULL)
    return client_id;

  /* If we're starting up the session, try to match the new client
   * with one pending apps for the current phase. If not, try to match
   * with any of the autostarted apps. */
  if (session->phase < GSM_SESSION_PHASE_APPLICATION)
    a = session->pending_apps;
  else
    a = session->apps;

  for (; a; a = a->next)
    {
      GsmApp *app = GSM_APP (a->data);

      if (!strcmp (client_id, app->client_id))
        {
          gsm_app_registered (app);
          return client_id;
        }
    }

  g_free (client_id);

  return NULL;
}

static void
client_saved_state (GsmClient *client, gpointer data)
{
  /* FIXME */
}

void
gsm_session_initiate_shutdown (GsmSession           *session,
			       gboolean              show_confirmation,
                               GsmSessionLogoutType  logout_type)
{
  gboolean logout_prompt;

  if (session->phase == GSM_SESSION_PHASE_SHUTDOWN)
    {
      /* Already shutting down, nothing more to do */
      return;
    }

  logout_prompt = gconf_client_get_bool (gsm_gconf_get_client (),
                                         "/apps/gnome-session/options/logout_prompt", 
                                         NULL);

  /* Global settings overides input parameter in order to disable confirmation 
   * dialog accordingly. If we're shutting down, we always show the confirmation 
   * dialog */
  logout_prompt = (logout_prompt && show_confirmation) || 
                  (logout_type == GSM_SESSION_LOGOUT_TYPE_SHUTDOWN);
  
  if (logout_prompt)
    {
      GtkWidget *logout_dialog;

      logout_dialog = gsm_get_logout_dialog (logout_type,
                                             gdk_screen_get_default (),
                                             gtk_get_current_event_time ());

      g_signal_connect (G_OBJECT (logout_dialog), 
                        "response",
      		        G_CALLBACK (logout_dialog_response), 
                        session);

      gtk_widget_show (logout_dialog);

      return;
    }

  initiate_shutdown (session);
}

static void
session_shutdown_phase2 (GsmSession *session)
{
  GSList *cl;

  for (cl = session->phase2_clients; cl; cl = cl->next)
    gsm_client_save_yourself_phase2 (cl->data);
}

static void
session_cancel_shutdown (GsmSession *session)
{
  GSList *cl;

  session->phase = GSM_SESSION_PHASE_RUNNING;

  g_slist_free (session->shutdown_clients);
  session->shutdown_clients = NULL;
  g_slist_free (session->interact_clients);
  session->interact_clients = NULL;
  g_slist_free (session->phase2_clients);
  session->phase2_clients = NULL;

  for (cl = session->clients; cl; cl = cl->next)
    gsm_client_shutdown_cancelled (cl->data);

  /* Restore initial logout response id */
  session->logout_response_id = GTK_RESPONSE_NONE;
}

static void
logout_dialog_response (GsmLogoutDialog *logout_dialog,
		        guint            response_id,
		        gpointer         data)
{
  GsmSession *session = (GsmSession *) data;
  GsmPowerManager *power_manager;

  gtk_widget_destroy (GTK_WIDGET (logout_dialog));

  /* In case of dialog cancel, switch user, hibernate and suspend, we just
   * perform the respective action and return, without shutting down the 
   * session. */
  switch (response_id) 
    {
    case GTK_RESPONSE_CANCEL:
    case GTK_RESPONSE_NONE:
    case GTK_RESPONSE_DELETE_EVENT:
      return;

    case GSM_LOGOUT_RESPONSE_SWITCH_USER:
      gdm_new_login ();
      return;

    case GSM_LOGOUT_RESPONSE_STD:
      power_manager = gsm_get_power_manager ();

      if (gsm_power_manager_can_hibernate (power_manager))
        gsm_power_manager_attempt_hibernate (power_manager);

      g_object_unref (power_manager);

      return;

    case GSM_LOGOUT_RESPONSE_STR:
      power_manager = gsm_get_power_manager ();

      if (gsm_power_manager_can_suspend (power_manager))
        gsm_power_manager_attempt_suspend (power_manager);

      g_object_unref (power_manager);

      return;
 
    default:
      break;
    }

  session->logout_response_id = response_id; 

  initiate_shutdown (session);
}

static void
initiate_shutdown (GsmSession *session)
{
  GSList *cl;

  session->phase = GSM_SESSION_PHASE_SHUTDOWN;

  if (session->clients == NULL)
    session_shutdown (session);

  for (cl = session->clients; cl; cl = cl->next)
    {
      GsmClient *client = GSM_CLIENT (cl->data);

      session->shutdown_clients =
	g_slist_prepend (session->shutdown_clients, client);

      gsm_client_save_yourself (client, FALSE);
    }
}

static void
session_shutdown (GsmSession *session)
{
  GSList *cl;

  /* FIXME: do this in reverse phase order */
  for (cl = session->clients; cl; cl = cl->next)
    gsm_client_die (cl->data);

  switch (session->logout_response_id) 
    {
    case GSM_LOGOUT_RESPONSE_SHUTDOWN:
      gdm_set_logout_action (GDM_LOGOUT_ACTION_SHUTDOWN);
      break;

    case GSM_LOGOUT_RESPONSE_REBOOT:
      gdm_set_logout_action (GDM_LOGOUT_ACTION_REBOOT);
      break;

    default:
      gtk_main_quit ();
      break;
    }
}

static void
client_request_phase2 (GsmClient *client, gpointer data)
{
  GsmSession *session = data;

  /* Move the client from shutdown_clients to phase2_clients */

  session->shutdown_clients =
    g_slist_remove (session->shutdown_clients, client);
  session->phase2_clients =
    g_slist_prepend (session->phase2_clients, client);
}

static void
client_request_interaction (GsmClient *client, gpointer data)
{
  GsmSession *session = data;

  session->interact_clients =
    g_slist_append (session->interact_clients, client);

  if (!session->interact_clients->next)
    gsm_client_interact (client);
}

static void
client_interaction_done (GsmClient *client, gboolean cancel_shutdown,
			 gpointer data)
{
  GsmSession *session = data;

  g_return_if_fail (session->interact_clients &&
		    (GsmClient *)session->interact_clients->data == client);

  if (cancel_shutdown)
    {
      session_cancel_shutdown (session);
      return;
    }

  /* Remove this client from interact_clients, and if there's another
   * client waiting to interact, let it interact now.
   */
  session->interact_clients =
    g_slist_remove (session->interact_clients, client);
  if (session->interact_clients)
    gsm_client_interact (session->interact_clients->data);
}

static void
client_save_yourself_done (GsmClient *client, gpointer data)
{
  GsmSession *session = data;

  session->shutdown_clients =
    g_slist_remove (session->shutdown_clients, client);
  session->interact_clients =
    g_slist_remove (session->interact_clients, client);
  session->phase2_clients =
    g_slist_remove (session->phase2_clients, client);

  if (session->phase == GSM_SESSION_PHASE_SHUTDOWN && 
      !session->shutdown_clients)
    {
      if (session->phase2_clients)
	session_shutdown_phase2 (session);
      else
	session_shutdown (session);
    }
}

static void
client_disconnected (GsmClient *client, gpointer data)
{
  GsmSession *session = data;
  gboolean is_condition_client = FALSE;

  session->clients =
    g_slist_remove (session->clients, client);
  session->shutdown_clients =
    g_slist_remove (session->shutdown_clients, client);
  session->interact_clients =
    g_slist_remove (session->interact_clients, client);
  session->phase2_clients =
    g_slist_remove (session->phase2_clients, client);

  if (g_slist_find (session->condition_clients, client))
    {
      session->condition_clients =
        g_slist_remove (session->condition_clients, client);

      is_condition_client = TRUE;
    }

  if (session->phase != GSM_SESSION_PHASE_SHUTDOWN && 
      gsm_client_get_autorestart (client) &&
      !is_condition_client)
    {
      GError *error = NULL;

      gsm_client_restart (client, &error);

      if (error)
      {
        g_warning ("Error on restarting session client: %s", error->message);
        g_clear_error (&error);
      }
    }

  g_object_unref (client);

  if (session->phase == GSM_SESSION_PHASE_SHUTDOWN && !session->clients)
    gtk_main_quit ();
}
