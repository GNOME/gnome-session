/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Novell, Inc.
 * Copyright (C) 2008 Red Hat, Inc.
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
#include <sys/wait.h>
#include <glib.h>
#include <gio/gio.h>

#include <gconf/gconf-client.h>

#include "gsm-autostart-app.h"
#include "util.h"

enum {
        AUTOSTART_LAUNCH_SPAWN = 0,
        AUTOSTART_LAUNCH_ACTIVATE,
};

#define GSM_SESSION_CLIENT_DBUS_INTERFACE "org.gnome.SessionClient"

struct _GsmAutostartAppPrivate {
        char                 *desktop_filename;
        char                 *desktop_id;
        char                 *startup_id;

        GFileMonitor         *monitor;
        gboolean              condition;
        EggDesktopFile       *desktop_file;

        int                   launch_type;
        GPid                  pid;
        guint                 child_watch_id;
        DBusGProxy           *proxy;
        DBusGProxyCall       *proxy_call;
};

enum {
        CONDITION_CHANGED,
        LAST_SIGNAL
};

enum {
        PROP_0,
        PROP_DESKTOP_FILENAME,
};

static guint signals[LAST_SIGNAL] = { 0 };

#define GSM_AUTOSTART_APP_GET_PRIVATE(object) (G_TYPE_INSTANCE_GET_PRIVATE ((object), GSM_TYPE_AUTOSTART_APP, GsmAutostartAppPrivate))

G_DEFINE_TYPE (GsmAutostartApp, gsm_autostart_app, GSM_TYPE_APP)

static void
gsm_autostart_app_init (GsmAutostartApp *app)
{
        app->priv = GSM_AUTOSTART_APP_GET_PRIVATE (app);

        app->priv->pid = -1;
        app->priv->monitor = NULL;
        app->priv->condition = FALSE;
}

static gboolean
load_desktop_file (GsmAutostartApp *app)
{
        char   *dbus_name;
        char   *startup_id;
        char   *phase_str;
        int     phase;

        if (app->priv->desktop_file == NULL) {
                return FALSE;
        }

        phase_str = egg_desktop_file_get_string (app->priv->desktop_file,
                                                 "X-GNOME-Autostart-Phase",
                                                 NULL);
        if (phase_str != NULL) {
                if (strcmp (phase_str, "Initialization") == 0) {
                        phase = GSM_MANAGER_PHASE_INITIALIZATION;
                } else if (strcmp (phase_str, "WindowManager") == 0) {
                        phase = GSM_MANAGER_PHASE_WINDOW_MANAGER;
                } else if (strcmp (phase_str, "Panel") == 0) {
                        phase = GSM_MANAGER_PHASE_PANEL;
                } else if (strcmp (phase_str, "Desktop") == 0) {
                        phase = GSM_MANAGER_PHASE_DESKTOP;
                } else {
                        phase = GSM_MANAGER_PHASE_APPLICATION;
                }

                g_free (phase_str);
        } else {
                phase = GSM_MANAGER_PHASE_APPLICATION;
        }

        dbus_name = egg_desktop_file_get_string (app->priv->desktop_file,
                                                 "X-GNOME-DBus-Name",
                                                 NULL);
        if (dbus_name != NULL) {
                app->priv->launch_type = AUTOSTART_LAUNCH_ACTIVATE;
        } else {
                app->priv->launch_type = AUTOSTART_LAUNCH_SPAWN;
        }

        /* this must only be done on first load */
        switch (app->priv->launch_type) {
        case AUTOSTART_LAUNCH_SPAWN:
                startup_id = gsm_util_generate_startup_id ();
                break;
        case AUTOSTART_LAUNCH_ACTIVATE:
                startup_id = g_strdup (dbus_name);
                break;
        default:
                g_assert_not_reached ();
        }

        g_object_set (app,
                      "phase", phase,
                      "startup-id", startup_id,
                      NULL);

        g_free (startup_id);
        g_free (dbus_name);

        return TRUE;
}

static void
gsm_autostart_app_set_desktop_filename (GsmAutostartApp *app,
                                        const char      *desktop_filename)
{
        GError *error;

        g_debug ("GsmAutostartApp: setting desktop filename to %s", desktop_filename);

        if (app->priv->desktop_file != NULL) {
                egg_desktop_file_free (app->priv->desktop_file);
                app->priv->desktop_file = NULL;
                g_free (app->priv->desktop_id);
        }

        if (desktop_filename == NULL) {
                return;
        }

        app->priv->desktop_id = g_path_get_basename (desktop_filename);

        error = NULL;
        app->priv->desktop_file = egg_desktop_file_new (desktop_filename, &error);
        if (app->priv->desktop_file == NULL) {
                g_warning ("Could not parse desktop file %s: %s",
                           desktop_filename,
                           error->message);
                g_error_free (error);
                return;
        }
}

static void
gsm_autostart_app_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
        GsmAutostartApp *self;

        self = GSM_AUTOSTART_APP (object);

        switch (prop_id) {
        case PROP_DESKTOP_FILENAME:
                gsm_autostart_app_set_desktop_filename (self, g_value_get_string (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_autostart_app_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
        GsmAutostartApp *self;

        self = GSM_AUTOSTART_APP (object);

        switch (prop_id) {
        case PROP_DESKTOP_FILENAME:
                if (self->priv->desktop_file != NULL) {
                        g_value_set_string (value, egg_desktop_file_get_source (self->priv->desktop_file));
                } else {
                        g_value_set_string (value, NULL);
                }
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_autostart_app_dispose (GObject *object)
{
        GsmAutostartAppPrivate *priv;

        priv = GSM_AUTOSTART_APP (object)->priv;

        if (priv->startup_id) {
                g_free (priv->startup_id);
                priv->startup_id = NULL;
        }

        if (priv->desktop_file) {
                egg_desktop_file_free (priv->desktop_file);
                priv->desktop_file = NULL;
        }

        if (priv->desktop_id) {
                g_free (priv->desktop_id);
                priv->desktop_id = NULL;
        }

        if (priv->child_watch_id > 0) {
                g_source_remove (priv->child_watch_id);
                priv->child_watch_id = 0;
        }

        if (priv->proxy_call != NULL) {
                dbus_g_proxy_cancel_call (priv->proxy, priv->proxy_call);
                priv->proxy_call = NULL;
        }

        if (priv->proxy != NULL) {
                g_object_unref (priv->proxy);
                priv->proxy = NULL;
        }

        if (priv->monitor) {
                g_file_monitor_cancel (priv->monitor);
        }
}


static void
if_exists_condition_cb (GFileMonitor     *monitor,
                        GFile            *file,
                        GFile            *other_file,
                        GFileMonitorEvent event,
                        GsmApp           *app)
{
        GsmAutostartAppPrivate *priv;
        gboolean                condition = FALSE;

        priv = GSM_AUTOSTART_APP (app)->priv;

        switch (event) {
        case G_FILE_MONITOR_EVENT_CREATED:
                condition = TRUE;
                break;

        default:
                /* Ignore any other monitor event */
                return;
        }

        /* Emit only if the condition actually changed */
        if (condition != priv->condition) {
                priv->condition = condition;
                g_signal_emit (app, signals[CONDITION_CHANGED], 0, condition);
        }
}

static void
unless_exists_condition_cb (GFileMonitor     *monitor,
                            GFile            *file,
                            GFile            *other_file,
                            GFileMonitorEvent event,
                            GsmApp           *app)
{
        GsmAutostartAppPrivate *priv;
        gboolean                condition = FALSE;

        priv = GSM_AUTOSTART_APP (app)->priv;

        switch (event) {
        case G_FILE_MONITOR_EVENT_DELETED:
                condition = TRUE;
                break;

        default:
                /* Ignore any other monitor event */
                return;
        }

        /* Emit only if the condition actually changed */
        if (condition != priv->condition) {
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
        GsmApp                 *app;
        GsmAutostartAppPrivate *priv;
        gboolean                condition = FALSE;

        g_return_if_fail (GSM_IS_APP (user_data));

        app = GSM_APP (user_data);

        priv = GSM_AUTOSTART_APP (app)->priv;

        if (entry->value != NULL && entry->value->type == GCONF_VALUE_BOOL) {
                condition = gconf_value_get_bool (entry->value);
        }

        /* Emit only if the condition actually changed */
        if (condition != priv->condition) {
                priv->condition = condition;
                g_signal_emit (app, signals[CONDITION_CHANGED], 0, condition);
        }
}

static gboolean
is_running (GsmApp *app)
{
        GsmAutostartAppPrivate *priv;
        gboolean                is;

        priv = GSM_AUTOSTART_APP (app)->priv;

        /* is running if pid is still valid or
         * or a client is connected
         */
        /* FIXME: check client */
        is = (priv->pid != -1);

        return is;
}

static gboolean
is_disabled (GsmApp *app)
{
        GsmAutostartAppPrivate *priv;
        char                   *condition;
        gboolean                autorestart = FALSE;

        priv = GSM_AUTOSTART_APP (app)->priv;

        if (egg_desktop_file_has_key (priv->desktop_file,
                                      "X-GNOME-AutoRestart", NULL)) {
                autorestart = egg_desktop_file_get_boolean (priv->desktop_file,
                                                            "X-GNOME-AutoRestart",
                                                            NULL);
        }

        /* X-GNOME-Autostart-enabled key, used by old gnome-session */
        if (egg_desktop_file_has_key (priv->desktop_file,
                                      "X-GNOME-Autostart-enabled", NULL) &&
            !egg_desktop_file_get_boolean (priv->desktop_file,
                                           "X-GNOME-Autostart-enabled", NULL)) {
                g_debug ("app %s is disabled by X-GNOME-Autostart-enabled",
                         gsm_app_get_id (app));
                return TRUE;
        }

        /* Hidden key, used by autostart spec */
        if (egg_desktop_file_get_boolean (priv->desktop_file,
                                          EGG_DESKTOP_FILE_KEY_HIDDEN, NULL)) {
                g_debug ("app %s is disabled by Hidden",
                         gsm_app_get_id (app));
                return TRUE;
        }

        /* Check OnlyShowIn/NotShowIn/TryExec */
        if (!egg_desktop_file_can_launch (priv->desktop_file, "GNOME")) {
                g_debug ("app %s not installed or not for GNOME",
                         gsm_app_get_id (app));
                return TRUE;
        }

        /* Check AutostartCondition */
        condition = egg_desktop_file_get_string (priv->desktop_file,
                                                 "AutostartCondition",
                                                 NULL);

        if (condition) {
                gboolean disabled;
                char *space, *key;
                int len;

                space = condition + strcspn (condition, " ");
                len = space - condition;
                key = space;
                while (isspace ((unsigned char)*key)) {
                        key++;
                }

                if (!g_ascii_strncasecmp (condition, "if-exists", len) && key) {
                        char *file_path = g_build_filename (g_get_user_config_dir (), key, NULL);

                        disabled = !g_file_test (file_path, G_FILE_TEST_EXISTS);

                        if (autorestart) {
                                GFile *file = g_file_new_for_path (file_path);

                                priv->monitor = g_file_monitor_file (file, 0, NULL, NULL);

                                g_signal_connect (priv->monitor, "changed",
                                                  G_CALLBACK (if_exists_condition_cb),
                                                  app);

                                g_object_unref (file);
                        }

                        g_free (file_path);
                } else if (!g_ascii_strncasecmp (condition, "unless-exists", len) && key) {
                        char *file_path = g_build_filename (g_get_user_config_dir (), key, NULL);

                        disabled = g_file_test (file_path, G_FILE_TEST_EXISTS);

                        if (autorestart) {
                                GFile *file = g_file_new_for_path (file_path);

                                priv->monitor = g_file_monitor_file (file, 0, NULL, NULL);

                                g_signal_connect (priv->monitor, "changed",
                                                  G_CALLBACK (unless_exists_condition_cb),
                                                  app);

                                g_object_unref (file);
                        }

                        g_free (file_path);
                } else if (!g_ascii_strncasecmp (condition, "GNOME", len)) {
                        if (key) {
                                GConfClient *client;

                                client = gconf_client_get_default ();

                                g_assert (GCONF_IS_CLIENT (client));

                                disabled = !gconf_client_get_bool (client, key, NULL);

                                if (autorestart) {
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
                                g_object_unref (client);
                        } else {
                                disabled = FALSE;
                        }
                } else {
                        disabled = TRUE;
                }

                g_free (condition);

                /* Set initial condition */
                priv->condition = !disabled;

                if (disabled) {
                        g_debug ("app %s is disabled by AutostartCondition",
                                 gsm_app_get_id (app));
                        return TRUE;
                }
        }

        priv->condition = TRUE;

        return FALSE;
}

static void
app_exited (GPid             pid,
            int              status,
            GsmAutostartApp *app)
{
        g_debug ("GsmAutostartApp: (pid:%d) done (%s:%d)",
                 (int) pid,
                 WIFEXITED (status) ? "status"
                 : WIFSIGNALED (status) ? "signal"
                 : "unknown",
                 WIFEXITED (status) ? WEXITSTATUS (status)
                 : WIFSIGNALED (status) ? WTERMSIG (status)
                 : -1);

        g_spawn_close_pid (app->priv->pid);
        app->priv->pid = -1;
        app->priv->child_watch_id = 0;

        if (WIFEXITED (status)) {
                gsm_app_exited (GSM_APP (app));
        } else if (WIFSIGNALED (status)) {
                gsm_app_died (GSM_APP (app));
        }
}

static gboolean
autostart_app_stop_spawn (GsmAutostartApp *app,
                          GError         **error)
{
        return TRUE;
}

static gboolean
autostart_app_stop_activate (GsmAutostartApp *app,
                             GError         **error)
{
        return TRUE;
}

static gboolean
gsm_autostart_app_stop (GsmApp  *app,
                        GError **error)
{
        GsmAutostartApp *aapp;
        gboolean         ret;

        aapp = GSM_AUTOSTART_APP (app);

        g_return_val_if_fail (aapp->priv->desktop_file != NULL, FALSE);

        switch (aapp->priv->launch_type) {
        case AUTOSTART_LAUNCH_SPAWN:
                ret = autostart_app_stop_spawn (aapp, error);
                break;
        case AUTOSTART_LAUNCH_ACTIVATE:
                ret = autostart_app_stop_activate (aapp, error);
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        return ret;
}

static gboolean
autostart_app_start_spawn (GsmAutostartApp *app,
                           GError         **error)
{
        char            *env[2] = { NULL, NULL };
        gboolean         success;
        GError          *local_error;
        const char      *startup_id;

        startup_id = gsm_app_get_startup_id (GSM_APP (app));
        g_assert (startup_id != NULL);

        env[0] = g_strdup_printf ("DESKTOP_AUTOSTART_ID=%s", startup_id);
        g_debug ("GsmAutostartApp: starting %s: %s", app->priv->desktop_id, startup_id);

        local_error = NULL;
        success = egg_desktop_file_launch (app->priv->desktop_file,
                                           NULL,
                                           &local_error,
                                           EGG_DESKTOP_FILE_LAUNCH_PUTENV, env,
                                           EGG_DESKTOP_FILE_LAUNCH_FLAGS, G_SPAWN_DO_NOT_REAP_CHILD,
                                           EGG_DESKTOP_FILE_LAUNCH_RETURN_PID, &app->priv->pid,
                                           EGG_DESKTOP_FILE_LAUNCH_RETURN_STARTUP_ID, &app->priv->startup_id,
                                           NULL);
        g_free (env[0]);

        if (success) {
                g_debug ("GsmAutostartApp: started pid:%d", app->priv->pid);
                app->priv->child_watch_id = g_child_watch_add (app->priv->pid,
                                                               (GChildWatchFunc)app_exited,
                                                               app);
        } else {
                g_set_error (error,
                             GSM_APP_ERROR,
                             GSM_APP_ERROR_START,
                             "Unable to start application: %s", local_error->message);
                g_error_free (local_error);
        }

        return success;
}

static void
start_notify (DBusGProxy      *proxy,
              DBusGProxyCall  *call,
              GsmAutostartApp *app)
{
        gboolean res;
        GError  *error;

        error = NULL;
        res = dbus_g_proxy_end_call (proxy,
                                     call,
                                     &error,
                                     G_TYPE_INVALID);
        app->priv->proxy_call = NULL;

        if (! res) {
                g_warning ("GsmAutostartApp: Error starting application: %s", error->message);
                g_error_free (error);
        } else {
                g_debug ("GsmAutostartApp: Started application %s", app->priv->desktop_id);
        }
}

static gboolean
autostart_app_start_activate (GsmAutostartApp  *app,
                              GError          **error)
{
        const char      *name;
        char            *path;
        char            *arguments;
        DBusGConnection *bus;
        GError          *local_error;

        local_error = NULL;
        bus = dbus_g_bus_get (DBUS_BUS_SESSION, &local_error);
        if (bus == NULL) {
                if (local_error != NULL) {
                        g_warning ("error getting session bus: %s", local_error->message);
                }
                g_propagate_error (error, local_error);
                return FALSE;
        }

        name = gsm_app_get_startup_id (GSM_APP (app));
        g_assert (name != NULL);

        path = egg_desktop_file_get_string (app->priv->desktop_file,
                                            "X-GNOME-DBus-Path",
                                            NULL);
        if (path == NULL) {
                /* just pick one? */
                path = g_strdup ("/");
        }

        arguments = egg_desktop_file_get_string (app->priv->desktop_file,
                                                 "X-GNOME-DBus-Start-Arguments",
                                                 NULL);

        app->priv->proxy = dbus_g_proxy_new_for_name (bus,
                                                      name,
                                                      path,
                                                      GSM_SESSION_CLIENT_DBUS_INTERFACE);
        if (app->priv->proxy == NULL) {
                g_set_error (error,
                             GSM_APP_ERROR,
                             GSM_APP_ERROR_START,
                             "Unable to start application: unable to create proxy for client");
                return FALSE;
        }

        app->priv->proxy_call = dbus_g_proxy_begin_call (app->priv->proxy,
                                                         "Start",
                                                         (DBusGProxyCallNotify)start_notify,
                                                         app,
                                                         NULL,
                                                         G_TYPE_STRING, arguments,
                                                         G_TYPE_INVALID);
        if (app->priv->proxy_call == NULL) {
                g_object_unref (app->priv->proxy);
                app->priv->proxy = NULL;
                g_set_error (error,
                             GSM_APP_ERROR,
                             GSM_APP_ERROR_START,
                             "Unable to start application: unable to call Start on client");
                return FALSE;
        }

        return TRUE;
}

static gboolean
gsm_autostart_app_start (GsmApp  *app,
                         GError **error)
{
        GsmAutostartApp *aapp;
        gboolean         ret;

        aapp = GSM_AUTOSTART_APP (app);

        g_return_val_if_fail (aapp->priv->desktop_file != NULL, FALSE);

        switch (aapp->priv->launch_type) {
        case AUTOSTART_LAUNCH_SPAWN:
                ret = autostart_app_start_spawn (aapp, error);
                break;
        case AUTOSTART_LAUNCH_ACTIVATE:
                ret = autostart_app_start_activate (aapp, error);
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        return ret;
}

static gboolean
gsm_autostart_app_restart (GsmApp  *app,
                           GError **error)
{
        GError  *local_error;
        gboolean res;

        local_error = NULL;
        res = gsm_app_stop (app, &local_error);
        if (! res) {
                g_propagate_error (error, local_error);
                return FALSE;
        }

        res = gsm_app_start (app, &local_error);
        if (! res) {
                g_propagate_error (error, local_error);
                return FALSE;
        }

        return TRUE;
}

static gboolean
gsm_autostart_app_provides (GsmApp     *app,
                            const char *service)
{
        char           **provides;
        gsize            len;
        gsize            i;
        GsmAutostartApp *aapp;

        g_return_val_if_fail (GSM_IS_APP (app), FALSE);

        aapp = GSM_AUTOSTART_APP (app);

        if (aapp->priv->desktop_file == NULL) {
                return FALSE;
        }

        provides = egg_desktop_file_get_string_list (aapp->priv->desktop_file,
                                                     "X-GNOME-Provides",
                                                     &len, NULL);
        if (!provides) {
                return FALSE;
        }

        for (i = 0; i < len; i++) {
                if (!strcmp (provides[i], service)) {
                        g_strfreev (provides);
                        return TRUE;
                }
        }

        g_strfreev (provides);
        return FALSE;
}

static gboolean
gsm_autostart_app_get_autorestart (GsmApp *app)
{
        gboolean res;
        gboolean autorestart;

        if (GSM_AUTOSTART_APP (app)->priv->desktop_file == NULL) {
                return FALSE;
        }

        autorestart = FALSE;

        res = egg_desktop_file_has_key (GSM_AUTOSTART_APP (app)->priv->desktop_file,
                                        "X-GNOME-AutoRestart",
                                        NULL);
        if (res) {
                autorestart = egg_desktop_file_get_boolean (GSM_AUTOSTART_APP (app)->priv->desktop_file,
                                                            "X-GNOME-AutoRestart",
                                                            NULL);
        }

        return autorestart;
}

static const char *
gsm_autostart_app_get_id (GsmApp *app)
{
        const char *location;
        const char *slash;

        if (GSM_AUTOSTART_APP (app)->priv->desktop_file == NULL) {
                return NULL;
        }

        location = egg_desktop_file_get_source (GSM_AUTOSTART_APP (app)->priv->desktop_file);

        slash = strrchr (location, '/');
        if (slash != NULL) {
                return slash + 1;
        } else {
                return location;
        }
}

static GObject *
gsm_autostart_app_constructor (GType                  type,
                               guint                  n_construct_properties,
                               GObjectConstructParam *construct_properties)
{
        GsmAutostartApp *app;
        const char      *id;

        app = GSM_AUTOSTART_APP (G_OBJECT_CLASS (gsm_autostart_app_parent_class)->constructor (type,
                                                                                               n_construct_properties,
                                                                                               construct_properties));

        id = gsm_autostart_app_get_id (GSM_APP (app));

        g_object_set (app, "id", id, NULL);

        if (! load_desktop_file (app)) {
                g_object_unref (app);
                app = NULL;
        }

        return G_OBJECT (app);
}

static void
gsm_autostart_app_class_init (GsmAutostartAppClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        GsmAppClass  *app_class = GSM_APP_CLASS (klass);

        object_class->set_property = gsm_autostart_app_set_property;
        object_class->get_property = gsm_autostart_app_get_property;
        object_class->dispose = gsm_autostart_app_dispose;
        object_class->constructor = gsm_autostart_app_constructor;

        app_class->impl_is_disabled = is_disabled;
        app_class->impl_is_running = is_running;
        app_class->impl_start = gsm_autostart_app_start;
        app_class->impl_restart = gsm_autostart_app_restart;
        app_class->impl_stop = gsm_autostart_app_stop;
        app_class->impl_provides = gsm_autostart_app_provides;
        app_class->impl_get_id = gsm_autostart_app_get_id;
        app_class->impl_get_autorestart = gsm_autostart_app_get_autorestart;

        g_object_class_install_property (object_class,
                                         PROP_DESKTOP_FILENAME,
                                         g_param_spec_string ("desktop-filename",
                                                              "Desktop filename",
                                                              "Freedesktop .desktop file",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        signals[CONDITION_CHANGED] =
                g_signal_new ("condition-changed",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmAutostartAppClass, condition_changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__BOOLEAN,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_BOOLEAN);

        g_type_class_add_private (object_class, sizeof (GsmAutostartAppPrivate));
}

GsmApp *
gsm_autostart_app_new (const char *desktop_file)
{
        GsmAutostartApp *app;

        app = g_object_new (GSM_TYPE_AUTOSTART_APP,
                            "desktop-filename", desktop_file,
                            NULL);

        return GSM_APP (app);
}
