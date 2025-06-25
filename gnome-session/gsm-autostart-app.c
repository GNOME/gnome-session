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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <ctype.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>

#include <glib.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-systemd.h>

#include <systemd/sd-journal.h>
#include <systemd/sd-daemon.h>

#include "gsm-autostart-app.h"
#include "gsm-util.h"

enum {
        AUTOSTART_LAUNCH_SPAWN = 0,
        AUTOSTART_LAUNCH_ACTIVATE
};

#define GSM_SESSION_CLIENT_DBUS_INTERFACE "org.gnome.SessionClient"

typedef struct
{
        char                 *desktop_filename;
        char                 *desktop_id;
        char                 *startup_id;

        GDesktopAppInfo      *app_info;

        int                   launch_type;
        GPid                  pid;
        guint                 child_watch_id;
} GsmAutostartAppPrivate;

typedef enum {
        PROP_DESKTOP_FILENAME = 1,
} GsmAutostartAppProperty;

static GParamSpec *props[PROP_DESKTOP_FILENAME + 1] = { NULL, };

static void gsm_autostart_app_initable_iface_init (GInitableIface  *iface);

G_DEFINE_TYPE_WITH_CODE (GsmAutostartApp, gsm_autostart_app, GSM_TYPE_APP,
                         G_ADD_PRIVATE (GsmAutostartApp)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, gsm_autostart_app_initable_iface_init))

static void
gsm_autostart_app_init (GsmAutostartApp *app)
{
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (app);

        priv->pid = -1;
}

static gboolean
is_disabled (GsmApp *app)
{
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (GSM_AUTOSTART_APP (app));

        /* GSM_AUTOSTART_APP_ENABLED_KEY key, used by old gnome-session */
        if (g_desktop_app_info_has_key (priv->app_info,
                                        GSM_AUTOSTART_APP_ENABLED_KEY) &&
            !g_desktop_app_info_get_boolean (priv->app_info,
                                             GSM_AUTOSTART_APP_ENABLED_KEY)) {
                g_debug ("app %s is disabled by " GSM_AUTOSTART_APP_ENABLED_KEY,
                         gsm_app_peek_id (app));
                return TRUE;
        }

        /* Hidden key, used by autostart spec */
        if (g_desktop_app_info_get_is_hidden (priv->app_info)) {
                g_debug ("app %s is disabled by Hidden",
                         gsm_app_peek_id (app));
                return TRUE;
        }

        /* Check OnlyShowIn/NotShowIn/TryExec */
        if (!g_desktop_app_info_get_show_in (priv->app_info, NULL)) {
                g_debug ("app %s is not for the current desktop",
                         gsm_app_peek_id (app));
                return TRUE;
        }

        /* Check if app is systemd enabled */
        if (g_desktop_app_info_has_key (priv->app_info,
                                        GSM_AUTOSTART_APP_SYSTEMD_KEY) &&
            g_desktop_app_info_get_boolean (priv->app_info,
                                            GSM_AUTOSTART_APP_SYSTEMD_KEY)) {
                g_debug ("app %s is disabled by " GSM_AUTOSTART_APP_SYSTEMD_KEY,
                         gsm_app_peek_id (app));
                return TRUE;
        }

        return FALSE;
}

static void
load_desktop_file (GsmAutostartApp  *app)
{
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (app);
        char    *dbus_name;
        char    *startup_id;

        g_assert (priv->app_info != NULL);

        dbus_name = g_desktop_app_info_get_string (priv->app_info,
                                                   GSM_AUTOSTART_APP_DBUS_NAME_KEY);
        if (dbus_name != NULL) {
                priv->launch_type = AUTOSTART_LAUNCH_ACTIVATE;
        } else {
                priv->launch_type = AUTOSTART_LAUNCH_SPAWN;
        }

        /* this must only be done on first load */
        switch (priv->launch_type) {
        case AUTOSTART_LAUNCH_SPAWN:
                startup_id =
                        g_desktop_app_info_get_string (priv->app_info,
                                                       GSM_AUTOSTART_APP_STARTUP_ID_KEY);

                if (startup_id == NULL) {
                        startup_id = gsm_util_generate_startup_id ();
                }
                break;
        case AUTOSTART_LAUNCH_ACTIVATE:
                startup_id = g_strdup (dbus_name);
                break;
        default:
                g_assert_not_reached ();
        }

        g_object_set (app,
                      "phase", GSM_MANAGER_PHASE_APPLICATION,
                      "startup-id", startup_id,
                      NULL);

        g_free (startup_id);
        g_free (dbus_name);
}

static void
gsm_autostart_app_set_desktop_filename (GsmAutostartApp *app,
                                        const char      *desktop_filename)
{
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (app);

        if (g_strcmp0 (priv->desktop_filename, desktop_filename) == 0)
                return;

        if (priv->app_info != NULL) {
                g_clear_object (&priv->app_info);
                g_clear_pointer (&priv->desktop_filename, g_free);
                g_clear_pointer (&priv->desktop_id, g_free);
        }

        if (desktop_filename != NULL) {
                priv->desktop_filename = g_strdup (desktop_filename);
                priv->desktop_id = g_path_get_basename (desktop_filename);
        }

        g_object_notify_by_pspec (G_OBJECT (app), props[PROP_DESKTOP_FILENAME]);
}

static void
gsm_autostart_app_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
        GsmAutostartApp *self = GSM_AUTOSTART_APP (object);

        switch ((GsmAutostartAppProperty) prop_id) {
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
        GsmAutostartApp *self = GSM_AUTOSTART_APP (object);
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (self);

        switch ((GsmAutostartAppProperty) prop_id) {
        case PROP_DESKTOP_FILENAME:
                if (priv->app_info != NULL) {
                        g_value_set_string (value, g_desktop_app_info_get_filename (priv->app_info));
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
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (GSM_AUTOSTART_APP (object));

        g_clear_pointer (&priv->startup_id, g_free);

        g_clear_object (&priv->app_info);
        g_clear_pointer (&priv->desktop_filename, g_free);
        g_clear_pointer (&priv->desktop_id, g_free);

        if (priv->child_watch_id > 0) {
                g_source_remove (priv->child_watch_id);
                priv->child_watch_id = 0;
        }

        G_OBJECT_CLASS (gsm_autostart_app_parent_class)->dispose (object);
}

static gboolean
is_running (GsmApp *app)
{
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (GSM_AUTOSTART_APP (app));
        gboolean                is;

        /* is running if pid is still valid or
         * or a client is connected
         */
        /* FIXME: check client */
        is = (priv->pid != -1);

        return is;
}

static void
app_exited (GPid             pid,
            int              status,
            GsmAutostartApp *app)
{
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (app);

        g_debug ("GsmAutostartApp: (pid:%d) done (%s:%d)",
                 (int) pid,
                 WIFEXITED (status) ? "status"
                 : WIFSIGNALED (status) ? "signal"
                 : "unknown",
                 WIFEXITED (status) ? WEXITSTATUS (status)
                 : WIFSIGNALED (status) ? WTERMSIG (status)
                 : -1);

        g_spawn_close_pid (priv->pid);
        priv->pid = -1;
        priv->child_watch_id = 0;

        if (WIFEXITED (status)) {
                gsm_app_exited (GSM_APP (app), WEXITSTATUS (status));
        } else if (WIFSIGNALED (status)) {
                gsm_app_died (GSM_APP (app), WTERMSIG (status));
        }
}

static int
_signal_pid (int pid,
             int signal)
{
        int status;

        /* perhaps block sigchld */
        g_debug ("GsmAutostartApp: sending signal %d to process %d", signal, pid);
        errno = 0;
        status = kill (pid, signal);

        if (status < 0) {
                if (errno == ESRCH) {
                        g_warning ("Child process %d was already dead.",
                                   (int)pid);
                } else {
                        g_warning ("Couldn't kill child process %d: %s",
                                   pid,
                                   g_strerror (errno));
                }
        }

        /* perhaps unblock sigchld */

        return status;
}

static gboolean
autostart_app_stop_spawn (GsmAutostartApp *app,
                          GError         **error)
{
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (app);
        int res;

        if (priv->pid < 1) {
                g_set_error (error,
                             GSM_APP_ERROR,
                             GSM_APP_ERROR_STOP,
                             "Not running");
                return FALSE;
        }

        res = _signal_pid (priv->pid, SIGTERM);
        if (res != 0) {
                g_set_error (error,
                             GSM_APP_ERROR,
                             GSM_APP_ERROR_STOP,
                             "Unable to stop: %s",
                             g_strerror (errno));
                return FALSE;
        }

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
        GsmAutostartApp *self = GSM_AUTOSTART_APP (app);
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (self);
        gboolean         ret;

        g_return_val_if_fail (priv->app_info != NULL, FALSE);

        switch (priv->launch_type) {
        case AUTOSTART_LAUNCH_SPAWN:
                ret = autostart_app_stop_spawn (self, error);
                break;
        case AUTOSTART_LAUNCH_ACTIVATE:
                ret = autostart_app_stop_activate (self, error);
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        return ret;
}

static void
app_launched (GAppLaunchContext *ctx,
              GAppInfo    *appinfo,
              GVariant    *platform_data,
              gpointer     data)
{
        GsmAutostartApp *app = data;
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (app);
        gint pid;
        gchar *sn_id;

        pid = 0;
        sn_id = NULL;

        g_variant_lookup (platform_data, "pid", "i", &pid);
        g_variant_lookup (platform_data, "startup-notification-id", "s", &sn_id);
        priv->pid = pid;
        priv->startup_id = sn_id;

        /* We are not interested in the result. */
        gnome_start_systemd_scope (priv->desktop_id,
                                   pid,
                                   NULL,
                                   NULL,
                                   NULL, NULL, NULL);
}

static void
on_child_setup (GsmAutostartApp *app)
{
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (app);
        int standard_output, standard_error;

        /* The FALSE means programs aren't expected to prefix each
         * line with <n> prefix to specify priority.
         */
        standard_output = sd_journal_stream_fd (priv->desktop_id,
                                                LOG_INFO,
                                                FALSE);
        standard_error = sd_journal_stream_fd (priv->desktop_id,
                                               LOG_WARNING,
                                               FALSE);

        if (standard_output >= 0) {
                dup2 (standard_output, STDOUT_FILENO);
                close (standard_output);
        }

        if (standard_error >= 0) {
                dup2 (standard_error, STDERR_FILENO);
                close (standard_error);
        }
}

static gboolean
autostart_app_start_spawn (GsmAutostartApp *app,
                           GError         **error)
{
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (app);
        gboolean         success;
        GError          *local_error;
        const char      *startup_id;
        const char * const *variable_blacklist;
        const char * const *child_environment;
        int i;
        GAppLaunchContext *ctx;
        GSpawnChildSetupFunc child_setup_func = NULL;
        gpointer             child_setup_data = NULL;
        guint handler;

        startup_id = gsm_app_peek_startup_id (GSM_APP (app));
        g_assert (startup_id != NULL);

        g_debug ("GsmAutostartApp: starting %s: command=%s startup-id=%s", priv->desktop_id, g_app_info_get_commandline (G_APP_INFO (priv->app_info)), startup_id);

        g_free (priv->startup_id);
        local_error = NULL;
        ctx = g_app_launch_context_new ();

        variable_blacklist = gsm_util_get_variable_blacklist ();
        for (i = 0; variable_blacklist[i] != NULL; i++)
                g_app_launch_context_unsetenv (ctx, variable_blacklist[i]);

        child_environment = gsm_util_listenv ();
        for (i = 0; child_environment[i] != NULL; i++) {
                char **environment_tuple;
                const char *key;
                const char *value;

                environment_tuple = g_strsplit (child_environment[i], "=", 2);
                key = environment_tuple[0];
                value = environment_tuple[1];

                if (value != NULL)
                        g_app_launch_context_setenv (ctx, key, value);

                g_strfreev (environment_tuple);
        }

        if (startup_id != NULL) {
                g_app_launch_context_setenv (ctx, "DESKTOP_AUTOSTART_ID", startup_id);
        }

        if (sd_booted () > 0) {
                child_setup_func = (GSpawnChildSetupFunc) on_child_setup;
                child_setup_data = app;
        }

        handler = g_signal_connect (ctx, "launched", G_CALLBACK (app_launched), app);
        success = g_desktop_app_info_launch_uris_as_manager (priv->app_info,
                                                             NULL,
                                                             ctx,
                                                             G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
                                                             child_setup_func, child_setup_data,
                                                             NULL, NULL,
                                                             &local_error);
        g_signal_handler_disconnect (ctx, handler);

        if (success) {
                if (priv->pid > 0) {
                        g_debug ("GsmAutostartApp: started pid:%d", priv->pid);
                        priv->child_watch_id = g_child_watch_add (priv->pid,
                                                                       (GChildWatchFunc)app_exited,
                                                                       app);
                }
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
start_notify (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
        GError          *error;
        GsmAutostartApp *app = GSM_AUTOSTART_APP (user_data);
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (app);

        error = NULL;

        g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);

        if (error != NULL) {
                g_warning ("GsmAutostartApp: Error starting application: %s", error->message);
                g_error_free (error);
        } else {
                g_debug ("GsmAutostartApp: Started application %s", priv->desktop_id);
        }
}

static gboolean
autostart_app_start_activate (GsmAutostartApp  *app,
                              GError          **error)
{
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (app);
        const char      *name;
        char            *path;
        char            *arguments;
        GDBusConnection *bus;
        GError          *local_error;

        local_error = NULL;
        bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &local_error);
        if (local_error != NULL) {
                g_warning ("error getting session bus: %s", local_error->message);
                g_propagate_error (error, local_error);
                return FALSE;
        }

        name = gsm_app_peek_startup_id (GSM_APP (app));
        g_assert (name != NULL);

        path = g_desktop_app_info_get_string (priv->app_info,
                                              GSM_AUTOSTART_APP_DBUS_PATH_KEY);
        if (path == NULL) {
                /* just pick one? */
                path = g_strdup ("/");
        }

        arguments = g_desktop_app_info_get_string (priv->app_info,
                                                   GSM_AUTOSTART_APP_DBUS_ARGS_KEY);

        g_dbus_connection_call (bus,
                                name,
                                path,
                                GSM_SESSION_CLIENT_DBUS_INTERFACE,
                                "Start",
                                g_variant_new ("(s)", arguments),
                                NULL,
                                G_DBUS_CALL_FLAGS_NONE,
                                -1, NULL,
                                start_notify, app);
        g_object_unref (bus);

        return TRUE;
}

static gboolean
gsm_autostart_app_start (GsmApp  *app,
                         GError **error)
{
        GsmAutostartApp *self = GSM_AUTOSTART_APP (app);
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (self);
        gboolean         ret;

        g_return_val_if_fail (priv->app_info != NULL, FALSE);

        switch (priv->launch_type) {
        case AUTOSTART_LAUNCH_SPAWN:
                ret = autostart_app_start_spawn (self, error);
                break;
        case AUTOSTART_LAUNCH_ACTIVATE:
                ret = autostart_app_start_activate (self, error);
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        return ret;
}

static const char *
gsm_autostart_app_get_app_id (GsmApp *app)
{
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (GSM_AUTOSTART_APP (app));

        if (priv->app_info == NULL) {
                return NULL;
        }

        return g_app_info_get_id (G_APP_INFO (priv->app_info));
}

static gboolean
gsm_autostart_app_initable_init (GInitable *initable,
                                 GCancellable *cancellable,
                                 GError  **error)
{
        GsmAutostartApp *app = GSM_AUTOSTART_APP (initable);
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (app);

        g_assert (priv->desktop_filename != NULL);
        priv->app_info = g_desktop_app_info_new_from_filename (priv->desktop_filename);
        if (priv->app_info == NULL) {
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Desktop file %s for application %s could not be parsed or references a missing TryExec binary",
                             priv->desktop_filename,
                             priv->desktop_id);
                return FALSE;
        }

        load_desktop_file (app);

        return TRUE;
}

static void
gsm_autostart_app_initable_iface_init (GInitableIface  *iface)
{
        iface->init = gsm_autostart_app_initable_init;
}

static void
gsm_autostart_app_class_init (GsmAutostartAppClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        GsmAppClass  *app_class = GSM_APP_CLASS (klass);

        object_class->set_property = gsm_autostart_app_set_property;
        object_class->get_property = gsm_autostart_app_get_property;
        object_class->dispose = gsm_autostart_app_dispose;

        app_class->impl_is_disabled = is_disabled;
        app_class->impl_is_running = is_running;
        app_class->impl_start = gsm_autostart_app_start;
        app_class->impl_stop = gsm_autostart_app_stop;
        app_class->impl_get_app_id = gsm_autostart_app_get_app_id;

        props[PROP_DESKTOP_FILENAME] =
                g_param_spec_string ("desktop-filename",
                                     "Desktop filename",
                                     "Freedesktop .desktop file",
                                     NULL,
                                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

        g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);
}

GsmApp *
gsm_autostart_app_new (const char *desktop_file,
                       GError    **error)
{
        return (GsmApp*) g_initable_new (GSM_TYPE_AUTOSTART_APP, NULL, error,
                                         "desktop-filename", desktop_file,
                                         NULL);
}
