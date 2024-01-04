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

enum {
        GSM_CONDITION_NONE           = 0,
        GSM_CONDITION_IF_EXISTS      = 1,
        GSM_CONDITION_UNLESS_EXISTS  = 2,
        GSM_CONDITION_GSETTINGS      = 3,
        GSM_CONDITION_IF_SESSION     = 4,
        GSM_CONDITION_UNLESS_SESSION = 5,
        GSM_CONDITION_UNKNOWN        = 6
};

#define GSM_SESSION_CLIENT_DBUS_INTERFACE "org.gnome.SessionClient"

typedef struct
{
        gboolean              mask_systemd;
        char                 *desktop_filename;
        char                 *desktop_id;
        char                 *startup_id;

        GDesktopAppInfo      *app_info;
        /* provides defined in session definition */
        GSList               *session_provides;

        /* desktop file state */
        char                 *condition_string;
        gboolean              condition;
        gboolean              autorestart;

        GFileMonitor         *condition_monitor;
        guint                 condition_notify_id;
        GSettings            *condition_settings;

        int                   launch_type;
        GPid                  pid;
        guint                 child_watch_id;
} GsmAutostartAppPrivate;

enum {
        CONDITION_CHANGED,
        LAST_SIGNAL
};

typedef enum {
        PROP_DESKTOP_FILENAME = 1,
        PROP_MASK_SYSTEMD,
} GsmAutostartAppProperty;

static GParamSpec *props[PROP_MASK_SYSTEMD + 1] = { NULL, };

static guint signals[LAST_SIGNAL] = { 0 };

static void gsm_autostart_app_initable_iface_init (GInitableIface  *iface);

G_DEFINE_TYPE_WITH_CODE (GsmAutostartApp, gsm_autostart_app, GSM_TYPE_APP,
                         G_ADD_PRIVATE (GsmAutostartApp)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, gsm_autostart_app_initable_iface_init))

static void
gsm_autostart_app_init (GsmAutostartApp *app)
{
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (app);

        priv->pid = -1;
        priv->condition_monitor = NULL;
        priv->condition = FALSE;
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

        /* Check if app is systemd enabled and mask-systemd is set. */
        if (priv->mask_systemd &&
            g_desktop_app_info_has_key (priv->app_info,
                                        GSM_AUTOSTART_APP_SYSTEMD_KEY) &&
            g_desktop_app_info_get_boolean (priv->app_info,
                                            GSM_AUTOSTART_APP_SYSTEMD_KEY)) {
                g_debug ("app %s is disabled by " GSM_AUTOSTART_APP_SYSTEMD_KEY,
                         gsm_app_peek_id (app));
                return TRUE;
        }

        /* Do not check AutostartCondition - this method is only to determine
         if the app is unconditionally disabled */

        return FALSE;
}

static gboolean
parse_condition_string (const char *condition_string,
                        guint      *condition_kindp,
                        char      **keyp)
{
        const char *space;
        const char *key;
        int         len;
        guint       kind;

        space = condition_string + strcspn (condition_string, " ");
        len = space - condition_string;
        key = space;
        while (isspace ((unsigned char)*key)) {
                key++;
        }

        kind = GSM_CONDITION_UNKNOWN;

        if (!g_ascii_strncasecmp (condition_string, "if-exists", len) && key) {
                kind = GSM_CONDITION_IF_EXISTS;
        } else if (!g_ascii_strncasecmp (condition_string, "unless-exists", len) && key) {
                kind = GSM_CONDITION_UNLESS_EXISTS;
        } else if (!g_ascii_strncasecmp (condition_string, "GSettings", len)) {
                kind = GSM_CONDITION_GSETTINGS;
        } else if (!g_ascii_strncasecmp (condition_string, "GNOME3", len)) {
                condition_string = key;
                space = condition_string + strcspn (condition_string, " ");
                len = space - condition_string;
                key = space;
                while (isspace ((unsigned char)*key)) {
                        key++;
                }
                if (!g_ascii_strncasecmp (condition_string, "if-session", len) && key) {
                        kind = GSM_CONDITION_IF_SESSION;
                } else if (!g_ascii_strncasecmp (condition_string, "unless-session", len) && key) {
                        kind = GSM_CONDITION_UNLESS_SESSION;
                }
        }

        if (kind == GSM_CONDITION_UNKNOWN) {
                key = NULL;
        }

        if (keyp != NULL) {
                *keyp = g_strdup (key);
        }

        if (condition_kindp != NULL) {
                *condition_kindp = kind;
        }

        return (kind != GSM_CONDITION_UNKNOWN);
}

static void
if_exists_condition_cb (GFileMonitor     *monitor,
                        GFile            *file,
                        GFile            *other_file,
                        GFileMonitorEvent event,
                        GsmApp           *app)
{
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (GSM_AUTOSTART_APP (app));
        gboolean                condition = FALSE;

        switch (event) {
        case G_FILE_MONITOR_EVENT_CREATED:
                condition = TRUE;
                break;
        case G_FILE_MONITOR_EVENT_DELETED:
                condition = FALSE;
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
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (GSM_AUTOSTART_APP (app));
        gboolean                condition = FALSE;

        switch (event) {
        case G_FILE_MONITOR_EVENT_CREATED:
                condition = FALSE;
                break;
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
gsettings_condition_cb (GSettings  *settings,
                        const char *key,
                        gpointer    user_data)
{
        GsmApp *app = GSM_APP (user_data);
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (GSM_AUTOSTART_APP (app));
        gboolean                condition;

        g_return_if_fail (GSM_IS_APP (user_data));

        condition = g_settings_get_boolean (settings, key);

        g_debug ("GsmAutostartApp: app:%s condition changed condition:%d",
                 gsm_app_peek_id (app),
                 condition);

        /* Emit only if the condition actually changed */
        if (condition != priv->condition) {
                priv->condition = condition;
                g_signal_emit (app, signals[CONDITION_CHANGED], 0, condition);
        }
}

static gboolean
setup_gsettings_condition_monitor (GsmAutostartApp *app,
                                   const char      *key)
{
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (app);
        GSettingsSchemaSource *source;
        GSettingsSchema *schema;
        GSettings *settings;
        GSettingsSchemaKey *schema_key;
        const GVariantType *key_type;
        char **elems;
        gboolean retval = FALSE;
        char *signal;

        retval = FALSE;

        schema = NULL;

        elems = g_strsplit (key, " ", 2);

        if (elems == NULL)
                goto out;

        if (elems[0] == NULL || elems[1] == NULL)
                goto out;

        source = g_settings_schema_source_get_default ();

        schema = g_settings_schema_source_lookup (source, elems[0], TRUE);

        if (schema == NULL)
                goto out;

        if (!g_settings_schema_has_key (schema, elems[1]))
                goto out;

        schema_key = g_settings_schema_get_key (schema, elems[1]);

        g_assert (schema_key != NULL);

        key_type = g_settings_schema_key_get_value_type (schema_key);

        g_settings_schema_key_unref (schema_key);

        g_assert (key_type != NULL);

        if (!g_variant_type_equal (key_type, G_VARIANT_TYPE_BOOLEAN))
                goto out;

        settings = g_settings_new_full (schema, NULL, NULL);
        retval = g_settings_get_boolean (settings, elems[1]);

        signal = g_strdup_printf ("changed::%s", elems[1]);
        g_signal_connect (G_OBJECT (settings), signal,
                          G_CALLBACK (gsettings_condition_cb), app);
        g_free (signal);

        priv->condition_settings = settings;

out:
        if (schema)
                g_settings_schema_unref (schema);
        g_strfreev (elems);

        return retval;
}

static void
if_session_condition_cb (GObject    *object,
                         GParamSpec *pspec,
                         gpointer    user_data)
{
        GsmApp *app = GSM_APP (user_data);
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (GSM_AUTOSTART_APP (app));
        char                   *session_name;
        char                   *key;
        gboolean                condition;

        g_return_if_fail (GSM_IS_APP (user_data));

        parse_condition_string (priv->condition_string, NULL, &key);

        g_object_get (object, "session-name", &session_name, NULL);
        condition = strcmp (session_name, key) == 0;
        g_free (session_name);

        g_free (key);

        g_debug ("GsmAutostartApp: app:%s condition changed condition:%d",
                 gsm_app_peek_id (app),
                 condition);

        /* Emit only if the condition actually changed */
        if (condition != priv->condition) {
                priv->condition = condition;
                g_signal_emit (app, signals[CONDITION_CHANGED], 0, condition);
        }
}

static void
unless_session_condition_cb (GObject    *object,
                             GParamSpec *pspec,
                             gpointer    user_data)
{
        GsmApp *app = GSM_APP (user_data);
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (GSM_AUTOSTART_APP (app));
        char                   *session_name;
        char                   *key;
        gboolean                condition;

        g_return_if_fail (GSM_IS_APP (user_data));

        parse_condition_string (priv->condition_string, NULL, &key);

        g_object_get (object, "session-name", &session_name, NULL);
        condition = strcmp (session_name, key) != 0;
        g_free (session_name);

        g_free (key);

        g_debug ("GsmAutostartApp: app:%s condition changed condition:%d",
                 gsm_app_peek_id (app),
                 condition);

        /* Emit only if the condition actually changed */
        if (condition != priv->condition) {
                priv->condition = condition;
                g_signal_emit (app, signals[CONDITION_CHANGED], 0, condition);
        }
}

static char *
resolve_conditional_file_path (const char *file)
{
        if (g_path_is_absolute (file))
                return g_strdup (file);
        return g_build_filename (g_get_user_config_dir (), file, NULL);
}

static void
setup_condition_monitor (GsmAutostartApp *app)
{
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (app);
        guint    kind;
        char    *key;
        gboolean res;
        gboolean disabled;

        if (priv->condition_monitor != NULL) {
                g_file_monitor_cancel (priv->condition_monitor);
        }

        if (priv->condition_string == NULL) {
                return;
        }

        /* if it is disabled outright there is no point in monitoring */
        if (is_disabled (GSM_APP (app))) {
                return;
        }

        key = NULL;
        res = parse_condition_string (priv->condition_string, &kind, &key);
        if (! res) {
                g_free (key);
                return;
        }

        if (key == NULL) {
                return;
        }

        if (kind == GSM_CONDITION_IF_EXISTS) {
                char  *file_path;
                GFile *file;

                file_path = resolve_conditional_file_path (key);
                disabled = !g_file_test (file_path, G_FILE_TEST_EXISTS);

                file = g_file_new_for_path (file_path);
                priv->condition_monitor = g_file_monitor_file (file, 0, NULL, NULL);

                g_signal_connect (priv->condition_monitor, "changed",
                                  G_CALLBACK (if_exists_condition_cb),
                                  app);

                g_object_unref (file);
                g_free (file_path);
        } else if (kind == GSM_CONDITION_UNLESS_EXISTS) {
                char  *file_path;
                GFile *file;

                file_path = resolve_conditional_file_path (key);
                disabled = g_file_test (file_path, G_FILE_TEST_EXISTS);

                file = g_file_new_for_path (file_path);
                priv->condition_monitor = g_file_monitor_file (file, 0, NULL, NULL);

                g_signal_connect (priv->condition_monitor, "changed",
                                  G_CALLBACK (unless_exists_condition_cb),
                                  app);

                g_object_unref (file);
                g_free (file_path);
        } else if (kind == GSM_CONDITION_GSETTINGS) {
                disabled = !setup_gsettings_condition_monitor (app, key);
        } else if (kind == GSM_CONDITION_IF_SESSION) {
                GsmManager *manager;
                char *session_name;

                /* get the singleton */
                manager = gsm_manager_get ();

                g_object_get (manager, "session-name", &session_name, NULL);
                disabled = strcmp (session_name, key) != 0;

                g_signal_connect (manager, "notify::session-name",
                                  G_CALLBACK (if_session_condition_cb), app);
                g_free (session_name);
        } else if (kind == GSM_CONDITION_UNLESS_SESSION) {
                GsmManager *manager;
                char *session_name;

                /* get the singleton */
                manager = gsm_manager_get ();

                g_object_get (manager, "session-name", &session_name, NULL);
                disabled = strcmp (session_name, key) == 0;

                g_signal_connect (manager, "notify::session-name",
                                  G_CALLBACK (unless_session_condition_cb), app);
                g_free (session_name);
        } else {
                disabled = TRUE;
        }

        g_free (key);

        if (disabled) {
                /* FIXME: cache the disabled value? */
        }
}

static void
load_desktop_file (GsmAutostartApp  *app)
{
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (app);
        char    *dbus_name;
        char    *startup_id;
        char    *phase_str;
        int      phase;
        gboolean res;

        g_assert (priv->app_info != NULL);

        phase_str = g_desktop_app_info_get_string (priv->app_info,
                                                   GSM_AUTOSTART_APP_PHASE_KEY);
        if (phase_str != NULL) {
                if (strcmp (phase_str, "EarlyInitialization") == 0) {
                        phase = GSM_MANAGER_PHASE_EARLY_INITIALIZATION;
                } else if (strcmp (phase_str, "PreDisplayServer") == 0) {
                        phase = GSM_MANAGER_PHASE_PRE_DISPLAY_SERVER;
                } else if (strcmp (phase_str, "DisplayServer") == 0) {
                        phase = GSM_MANAGER_PHASE_DISPLAY_SERVER;
                } else if (strcmp (phase_str, "Initialization") == 0) {
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

        res = g_desktop_app_info_has_key (priv->app_info,
                                          GSM_AUTOSTART_APP_AUTORESTART_KEY);
        if (res) {
                priv->autorestart = g_desktop_app_info_get_boolean (priv->app_info,
                                                                         GSM_AUTOSTART_APP_AUTORESTART_KEY);
        } else {
                priv->autorestart = FALSE;
        }

        g_free (priv->condition_string);
        priv->condition_string = g_desktop_app_info_get_string (priv->app_info,
                                                                   "AutostartCondition");
        setup_condition_monitor (app);

        g_object_set (app,
                      "phase", phase,
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
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (self);

        switch ((GsmAutostartAppProperty) prop_id) {
        case PROP_DESKTOP_FILENAME:
                gsm_autostart_app_set_desktop_filename (self, g_value_get_string (value));
                break;
        case PROP_MASK_SYSTEMD:
                if (priv->mask_systemd != g_value_get_boolean (value)) {
                        priv->mask_systemd = g_value_get_boolean (value);
                        g_object_notify_by_pspec (object, props[PROP_MASK_SYSTEMD]);
                }
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
        case PROP_MASK_SYSTEMD:
                g_value_set_boolean (value, priv->mask_systemd);
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

        if (priv->session_provides) {
                g_slist_free_full (priv->session_provides, g_free);
                priv->session_provides = NULL;
        }

        g_clear_pointer (&priv->condition_string, g_free);
        g_clear_object (&priv->condition_settings);
        g_clear_object (&priv->app_info);
        g_clear_pointer (&priv->desktop_filename, g_free);
        g_clear_pointer (&priv->desktop_id, g_free);

        if (priv->child_watch_id > 0) {
                g_source_remove (priv->child_watch_id);
                priv->child_watch_id = 0;
        }

        if (priv->condition_monitor) {
                g_file_monitor_cancel (priv->condition_monitor);
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

static gboolean
is_conditionally_disabled (GsmApp *app)
{
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (GSM_AUTOSTART_APP (app));
        gboolean                res;
        gboolean                disabled;
        char                   *key;
        guint                   kind;

        /* Check AutostartCondition */
        if (priv->condition_string == NULL) {
                return FALSE;
        }

        key = NULL;
        res = parse_condition_string (priv->condition_string, &kind, &key);
        if (! res) {
                g_free (key);
                return TRUE;
        }

        if (key == NULL) {
                return TRUE;
        }

        if (kind == GSM_CONDITION_IF_EXISTS) {
                char *file_path;

                file_path = resolve_conditional_file_path (key);
                disabled = !g_file_test (file_path, G_FILE_TEST_EXISTS);
                g_free (file_path);
        } else if (kind == GSM_CONDITION_UNLESS_EXISTS) {
                char *file_path;

                file_path = resolve_conditional_file_path (key);
                disabled = g_file_test (file_path, G_FILE_TEST_EXISTS);
                g_free (file_path);
        } else if (kind == GSM_CONDITION_GSETTINGS &&
                   priv->condition_settings != NULL) {
                char **elems;
                elems = g_strsplit (key, " ", 2);
                disabled = !g_settings_get_boolean (priv->condition_settings, elems[1]);
                g_strfreev (elems);
        } else if (kind == GSM_CONDITION_IF_SESSION) {
                GsmManager *manager;
                char *session_name;

                /* get the singleton */
                manager = gsm_manager_get ();

                g_object_get (manager, "session-name", &session_name, NULL);
                disabled = strcmp (session_name, key) != 0;
                g_free (session_name);
        } else if (kind == GSM_CONDITION_UNLESS_SESSION) {
                GsmManager *manager;
                char *session_name;

                /* get the singleton */
                manager = gsm_manager_get ();

                g_object_get (manager, "session-name", &session_name, NULL);
                disabled = strcmp (session_name, key) == 0;
                g_free (session_name);
        } else {
                disabled = TRUE;
        }

        /* Set initial condition */
        priv->condition = !disabled;

        g_free (key);

        return disabled;
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

static gboolean
gsm_autostart_app_restart (GsmApp  *app,
                           GError **error)
{
        GError  *local_error;
        gboolean res;

        /* ignore stop errors - it is fine if it is already stopped */
        local_error = NULL;
        res = gsm_app_stop (app, &local_error);
        if (! res) {
                g_debug ("GsmAutostartApp: Couldn't stop app: %s", local_error->message);
                g_error_free (local_error);
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
        gchar           *provides_str;
        char           **provides;
        gsize            i;
        GSList          *l;
        GsmAutostartApp *self = GSM_AUTOSTART_APP (app);
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (self);

        g_return_val_if_fail (GSM_IS_APP (app), FALSE);

        if (priv->app_info == NULL) {
                return FALSE;
        }

        for (l = priv->session_provides; l != NULL; l = l->next) {
                if (!strcmp (l->data, service))
                        return TRUE;
        }

        provides_str = g_desktop_app_info_get_string (priv->app_info,
                                                      GSM_AUTOSTART_APP_PROVIDES_KEY);
        if (!provides_str) {
                return FALSE;
        }
        provides = g_strsplit (provides_str, ";", -1);
        g_free (provides_str);

        for (i = 0; provides[i]; i++) {
                if (!strcmp (provides[i], service)) {
                        g_strfreev (provides);
                        return TRUE;
                }
        }

        g_strfreev (provides);

        return FALSE;
}

static char **
gsm_autostart_app_get_provides (GsmApp *app)
{
        GsmAutostartApp  *self = GSM_AUTOSTART_APP (app);
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (self);
        gchar            *provides_str;
        char            **provides;
        gsize             provides_len;
        char            **result;
        gsize             result_len;
        int               i;
        GSList           *l;

        g_return_val_if_fail (GSM_IS_APP (app), NULL);

        if (priv->app_info == NULL) {
                return NULL;
        }

        provides_str = g_desktop_app_info_get_string (priv->app_info,
                                                      GSM_AUTOSTART_APP_PROVIDES_KEY);

        if (provides_str == NULL) {
                return NULL;
        }

        provides = g_strsplit (provides_str, ";", -1);
        provides_len = g_strv_length (provides);
        g_free (provides_str);

        if (!priv->session_provides) {
                return provides;
        }

        result_len = provides_len + g_slist_length (priv->session_provides);
        result = g_new (char *, result_len + 1); /* including last NULL */

        for (i = 0; provides[i] != NULL; i++)
                result[i] = provides[i];
        g_free (provides);

        for (l = priv->session_provides; l != NULL; l = l->next, i++)
                result[i] = g_strdup (l->data);

        result[i] = NULL;

        g_assert (i == result_len);

        return result;
}

void
gsm_autostart_app_add_provides (GsmAutostartApp *aapp,
                                const char      *provides)
{
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (aapp);

        g_return_if_fail (GSM_IS_AUTOSTART_APP (aapp));

        priv->session_provides = g_slist_prepend (priv->session_provides,
                                                  g_strdup (provides));
}

static gboolean
gsm_autostart_app_has_autostart_condition (GsmApp     *app,
                                           const char *condition)
{
        GsmAutostartApp *self = GSM_AUTOSTART_APP (app);
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (self);

        g_return_val_if_fail (GSM_IS_APP (app), FALSE);
        g_return_val_if_fail (condition != NULL, FALSE);

        if (priv->condition_string == NULL) {
                return FALSE;
        }

        if (strcmp (priv->condition_string, condition) == 0) {
                return TRUE;
        }

        return FALSE;
}

static gboolean
gsm_autostart_app_get_autorestart (GsmApp *app)
{
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (GSM_AUTOSTART_APP (app));
        gboolean res;
        gboolean autorestart;

        if (priv->app_info == NULL) {
                return FALSE;
        }

        autorestart = FALSE;

        res = g_desktop_app_info_has_key (priv->app_info,
                                          GSM_AUTOSTART_APP_AUTORESTART_KEY);
        if (res) {
                autorestart = g_desktop_app_info_get_boolean (priv->app_info,
                                                              GSM_AUTOSTART_APP_AUTORESTART_KEY);
        }

        return autorestart;
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

static gboolean
gsm_autostart_app_save_to_keyfile (GsmApp    *base_app,
                                   GKeyFile  *keyfile,
                                   GError   **error)
{
        GsmAutostartApp *app = GSM_AUTOSTART_APP (base_app);
        GsmAutostartAppPrivate *priv = gsm_autostart_app_get_instance_private (app);
        char   **provides = NULL;
        char    *dbus_name;
        char    *phase;
        gboolean res;

        provides = gsm_app_get_provides (base_app);
        if (provides != NULL) {
                g_key_file_set_string_list (keyfile,
                                            G_KEY_FILE_DESKTOP_GROUP,
                                            GSM_AUTOSTART_APP_PROVIDES_KEY,
                                            (const char * const *)
                                            provides,
                                            g_strv_length (provides));
                g_strfreev (provides);
        }

        phase = g_desktop_app_info_get_string (priv->app_info,
                                                   GSM_AUTOSTART_APP_PHASE_KEY);
        if (phase != NULL) {
                g_key_file_set_string (keyfile,
                                       G_KEY_FILE_DESKTOP_GROUP,
                                       GSM_AUTOSTART_APP_PHASE_KEY,
                                       phase);
                g_free (phase);
        }

        dbus_name = g_desktop_app_info_get_string (priv->app_info,
                                                   GSM_AUTOSTART_APP_DBUS_NAME_KEY);
        if (dbus_name != NULL) {
                g_key_file_set_string (keyfile,
                                       G_KEY_FILE_DESKTOP_GROUP,
                                       GSM_AUTOSTART_APP_DBUS_NAME_KEY,
                                       dbus_name);
                g_free (dbus_name);
        }

        res = g_desktop_app_info_has_key (priv->app_info,
                                          GSM_AUTOSTART_APP_AUTORESTART_KEY);
        if (res) {
                g_key_file_set_boolean (keyfile,
                                        G_KEY_FILE_DESKTOP_GROUP,
                                        GSM_AUTOSTART_APP_AUTORESTART_KEY,
                                        g_desktop_app_info_get_boolean (priv->app_info,
                                                                        GSM_AUTOSTART_APP_AUTORESTART_KEY));
        }

        res = g_desktop_app_info_has_key (priv->app_info,
                                          GSM_AUTOSTART_APP_AUTORESTART_KEY);
        if (res) {
                char *autostart_condition;

                autostart_condition = g_desktop_app_info_get_string (priv->app_info, "AutostartCondition");

                g_key_file_set_string (keyfile,
                                       G_KEY_FILE_DESKTOP_GROUP,
                                       "AutostartCondition",
                                       autostart_condition);
                g_free (autostart_condition);
        }

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
        app_class->impl_is_conditionally_disabled = is_conditionally_disabled;
        app_class->impl_is_running = is_running;
        app_class->impl_start = gsm_autostart_app_start;
        app_class->impl_restart = gsm_autostart_app_restart;
        app_class->impl_stop = gsm_autostart_app_stop;
        app_class->impl_provides = gsm_autostart_app_provides;
        app_class->impl_get_provides = gsm_autostart_app_get_provides;
        app_class->impl_has_autostart_condition = gsm_autostart_app_has_autostart_condition;
        app_class->impl_get_app_id = gsm_autostart_app_get_app_id;
        app_class->impl_get_autorestart = gsm_autostart_app_get_autorestart;
        app_class->impl_save_to_keyfile = gsm_autostart_app_save_to_keyfile;

        props[PROP_DESKTOP_FILENAME] =
                g_param_spec_string ("desktop-filename",
                                     "Desktop filename",
                                     "Freedesktop .desktop file",
                                     NULL,
                                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

        props[PROP_MASK_SYSTEMD] =
                g_param_spec_boolean ("mask-systemd",
                                      "Mask if systemd started",
                                      "Mask if GNOME systemd flag is set in desktop file",
                                      FALSE,
                                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

        g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);

        signals[CONDITION_CHANGED] =
                g_signal_new ("condition-changed",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmAutostartAppClass, condition_changed),
                              NULL, NULL, NULL,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_BOOLEAN);
}

GsmApp *
gsm_autostart_app_new (const char *desktop_file,
                       gboolean    mask_systemd,
                       GError    **error)
{
        return (GsmApp*) g_initable_new (GSM_TYPE_AUTOSTART_APP, NULL, error,
                                         "desktop-filename", desktop_file,
                                         "mask-systemd", mask_systemd,
                                         NULL);
}
