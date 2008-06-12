/* app-autostart.c
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <string.h>
#include <gio/gio.h>

#include "app-autostart.h"
#include "gconf.h"

enum {
  CONDITION_CHANGED,
  LAST_SIGNAL
};

struct _GsmAppAutostartPrivate 
{
  GFileMonitor         *monitor;
  gboolean              condition;
};

static guint signals[LAST_SIGNAL] = { 0 };

static void gsm_app_autostart_dispose (GObject *object);

static gboolean is_disabled (GsmApp *app);

#define GSM_APP_AUTOSTART_GET_PRIVATE(object) \
        (G_TYPE_INSTANCE_GET_PRIVATE ((object), GSM_TYPE_APP_AUTOSTART, GsmAppAutostartPrivate))

G_DEFINE_TYPE (GsmAppAutostart, gsm_app_autostart, GSM_TYPE_APP)

static void
gsm_app_autostart_init (GsmAppAutostart *app)
{
  app->priv = GSM_APP_AUTOSTART_GET_PRIVATE (app);

  app->priv->monitor = NULL;
  app->priv->condition = FALSE;
}

static void
gsm_app_autostart_class_init (GsmAppAutostartClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GsmAppClass *app_class = GSM_APP_CLASS (klass);

  object_class->dispose = gsm_app_autostart_dispose;

  app_class->is_disabled = is_disabled;

  signals[CONDITION_CHANGED] =
    g_signal_new ("condition-changed",
                  G_OBJECT_CLASS_TYPE (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GsmAppAutostartClass, condition_changed),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOOLEAN,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_BOOLEAN);

  g_type_class_add_private (object_class, sizeof (GsmAppAutostartPrivate));
}

static void
gsm_app_autostart_dispose (GObject *object)
{
  GsmAppAutostartPrivate *priv;

  priv = GSM_APP_AUTOSTART (object)->priv;

  if (priv->monitor)
    g_file_monitor_cancel (priv->monitor);
}

GsmApp *
gsm_app_autostart_new (const char *desktop_file,
		       const char *client_id)
{
  GsmApp *app;

  app = g_object_new (GSM_TYPE_APP_AUTOSTART,
		      "desktop-file", desktop_file,
		      "client-id", client_id,
		      NULL);
  if (!app->desktop_file)
    {
      g_object_unref (app);
      app = NULL;
    }

  return app;
}

static void
unless_exists_condition_cb (GFileMonitor *monitor,
                            GFile *file,
                            GFile *other_file,
                            GFileMonitorEvent event,
                            GsmApp *app)
{
  GsmAppAutostartPrivate *priv;
  gboolean condition = FALSE;

  priv = GSM_APP_AUTOSTART (app)->priv;

  switch (event) {
  case G_FILE_MONITOR_EVENT_DELETED:
    condition = TRUE;
    break;

  default:
    /* Ignore any other monitor event */
    return;
  }

  /* Emit only if the condition actually changed */
  if (condition != priv->condition)
    { 
      priv->condition = condition;
      g_signal_emit (app, signals[CONDITION_CHANGED], 0, condition);
    }
}

static void
if_exists_condition_cb (GFileMonitor *monitor,
                        GFile *file,
                        GFile *other_file,
                        GFileMonitorEvent event,
                        GsmApp *app)
{
  GsmAppAutostartPrivate *priv;
  gboolean condition = FALSE;

  priv = GSM_APP_AUTOSTART (app)->priv;

  switch (event) {
  case G_FILE_MONITOR_EVENT_CREATED:
    condition = TRUE;
    break;

  default:
    /* Ignore any other monitor event */
    return;
  }

  /* Emit only if the condition actually changed */
  if (condition != priv->condition)
    { 
      priv->condition = condition;
      g_signal_emit (app, signals[CONDITION_CHANGED], 0, condition);
    }
}

static void
gconf_condition_cb (GConfClient *client,
                    guint       cnxn_id,
                    GConfEntry  *entry,
                    gpointer    user_data)
{
  GsmApp *app;
  GsmAppAutostartPrivate *priv;
  gboolean condition = FALSE;

  g_return_if_fail (GSM_IS_APP (user_data));

  app = GSM_APP (user_data);

  priv = GSM_APP_AUTOSTART (app)->priv;

  if (entry->value != NULL && entry->value->type == GCONF_VALUE_BOOL) 
    condition = gconf_value_get_bool (entry->value);

  /* Emit only if the condition actually changed */
  if (condition != priv->condition)
    { 
      priv->condition = condition;
      g_signal_emit (app, signals[CONDITION_CHANGED], 0, condition);
    }
}

static gboolean
is_disabled (GsmApp *app)
{
  GsmAppAutostartPrivate *priv;
  char *condition;
  gboolean autorestart = FALSE;

  priv = GSM_APP_AUTOSTART (app)->priv;

  if (egg_desktop_file_has_key (app->desktop_file,
				"X-GNOME-AutoRestart", NULL))
    {
      autorestart = 
         egg_desktop_file_get_boolean (app->desktop_file,
                                       "X-GNOME-AutoRestart", NULL); 
    }

  /* X-GNOME-Autostart-enabled key, used by old gnome-session */
  if (egg_desktop_file_has_key (app->desktop_file,
				"X-GNOME-Autostart-enabled", NULL) &&
      !egg_desktop_file_get_boolean (app->desktop_file,
				     "X-GNOME-Autostart-enabled", NULL))
    {
      g_debug ("app %s is disabled by X-GNOME-Autostart-enabled",
	       gsm_app_get_basename (app));
      return TRUE;
    }

  /* Hidden key, used by autostart spec */
  if (egg_desktop_file_get_boolean (app->desktop_file,
				    EGG_DESKTOP_FILE_KEY_HIDDEN, NULL))
    {
      g_debug ("app %s is disabled by Hidden",
	       gsm_app_get_basename (app));
      return TRUE;
    }

  /* Check OnlyShowIn/NotShowIn/TryExec */
  if (!egg_desktop_file_can_launch (app->desktop_file, "GNOME"))
    {
      g_debug ("app %s not installed or not for GNOME",
	       gsm_app_get_basename (app));
      return TRUE;
    }

  /* Check AutostartCondition */
  condition = egg_desktop_file_get_string (app->desktop_file,
					   "AutostartCondition",
					   NULL);

  if (condition)
    {
      gboolean disabled;
      char *space, *key;
      int len;

      space = condition + strcspn (condition, " ");
      len = space - condition;
      key = space;
      while (isspace ((unsigned char)*key))
	key++;

      if (!g_ascii_strncasecmp (condition, "if-exists", len) && key)
	{
	  char *file_path = g_build_filename (g_get_user_config_dir (), key, NULL);

	  disabled = !g_file_test (file_path, G_FILE_TEST_EXISTS);

          if (autorestart)
            {
              GFile *file = g_file_new_for_path (file_path);

              priv->monitor = g_file_monitor_file (file, 0, NULL, NULL);

              g_signal_connect (priv->monitor, "changed",
                                G_CALLBACK (if_exists_condition_cb), 
                                app);

              g_object_unref (file);
	    }

          g_free (file_path);
	}
      else if (!g_ascii_strncasecmp (condition, "unless-exists", len) && key)
	{
	  char *file_path = g_build_filename (g_get_user_config_dir (), key, NULL);

	  disabled = g_file_test (file_path, G_FILE_TEST_EXISTS);

          if (autorestart)
            {
              GFile *file = g_file_new_for_path (file_path);

              priv->monitor = g_file_monitor_file (file, 0, NULL, NULL);

              g_signal_connect (priv->monitor, "changed",
                                G_CALLBACK (unless_exists_condition_cb), 
                                app);

              g_object_unref (file);
	    }

          g_free (file_path);
	}
      else if (!g_ascii_strncasecmp (condition, "GNOME", len))
	{
	  if (key)
	    {
              GConfClient *client;

              client = gsm_gconf_get_client ();

              g_assert (GCONF_IS_CLIENT (client));

	      disabled = !gconf_client_get_bool (client, key, NULL);

              if (autorestart)
                {
                  gchar *dir;

                  dir = g_path_get_dirname (key);

                  /* Add key dir in order to be able to keep track
                   * of changed in the key later */
                  gconf_client_add_dir (client, dir,
                                        GCONF_CLIENT_PRELOAD_RECURSIVE, NULL);

                  g_free (dir);

                  gconf_client_notify_add (client,
                                           key,
                                           gconf_condition_cb,
                                           app, NULL, NULL);
                }
	    }
	  else
	    disabled = FALSE;
	}
      else
	disabled = TRUE;

      g_free (condition);

      /* Set initial condition */
      priv->condition = !disabled;

      if (disabled)
	{
	  g_debug ("app %s is disabled by AutostartCondition",
		   gsm_app_get_basename (app));
	  return TRUE;
	}
    }

  priv->condition = TRUE;

  return FALSE;
}
