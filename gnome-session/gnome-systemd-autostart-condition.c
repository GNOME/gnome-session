/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 * gnome-sstemd-autostart-condition.c - Evaluate and monitor autostart conditions
 *
 * Copyright (C) 2006, 2010 Novell, Inc.
 * Copyright (C) 2008,2020 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <systemd/sd-login.h>
#include <glib.h>
#include <glib-unix.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <fcntl.h>

#include "gsm-autostart-condition.h"

#define SYSTEMD_DBUS            "org.freedesktop.systemd1"
#define SYSTEMD_PATH_DBUS       "/org/freedesktop/systemd1"
#define SYSTEMD_INTERFACE_DBUS  "org.freedesktop.systemd1.Manager"

#define MONITOR_RUN_DIR "gnome-systemd-autostart-conditions"

typedef struct {
        GsmAutostartCondition kind;

        /* The directory (to enumerate services that need to be started/stopped) */
        GFile *directory;

        /* The GFile monitor if we are monitoring a file */
        GFileMonitor *monitor;

        /* settings object and key when monitoring the configuration */
        GSettings *settings;
        char *settings_key;
} ConditionMonitor;

static void
condition_monitor_free (ConditionMonitor *condition)
{
        g_return_if_fail (condition);

        g_clear_object (&condition->directory);

        /* No need to disconnect the signal handlers, we are
         * holding the only reference. */
        g_clear_object (&condition->monitor);
        g_clear_object (&condition->settings);
        g_clear_pointer (&condition->settings_key, g_free);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ConditionMonitor, condition_monitor_free)

static gboolean
evalute_condition (const char *condition)
{
        GsmAutostartCondition kind;
        g_autofree char *key = NULL;

        gsm_autostart_condition_parse (condition, &kind, &key);

        switch (kind) {
                case GSM_CONDITION_IF_EXISTS:
                case GSM_CONDITION_UNLESS_EXISTS: {
                        g_autofree char *file_path;
                        gboolean exists;

                        file_path = gsm_autostart_condition_resolve_file_path (key);
                        exists = g_file_test (file_path, G_FILE_TEST_EXISTS);

                        if (kind == GSM_CONDITION_IF_EXISTS)
                                return exists;
                        else
                                return !exists;
                }

                case GSM_CONDITION_GSETTINGS: {
                        g_autoptr(GSettings) settings = NULL;
                        g_autofree char *settings_key = NULL;

                        if (!gsm_autostart_condition_resolve_settings (key,
                                                                       &settings,
                                                                       &settings_key))
                                return FALSE;

                        return g_settings_get_boolean (settings, settings_key);
                }

                case GSM_CONDITION_IF_SESSION:
                case GSM_CONDITION_UNLESS_SESSION:
                        g_warning ("The session conditions cannot be implemented by the generator!");
                        return FALSE;

                case GSM_CONDITION_NONE:
                case GSM_CONDITION_UNKNOWN:
                        g_warning ("Invalid condition string \"%s\"", condition);
                        return FALSE;
        }

        return TRUE;
}

static GDBusConnection *
get_session_bus (void)
{
        g_autoptr(GError) error = NULL;
        GDBusConnection *bus;

        bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);

        if (bus == NULL)
                g_warning ("Couldn't connect to session bus: %s", error->message);

        return bus;
}

static void
do_start_stop_unit (const gchar *unit, gboolean start)
{
        g_autoptr(GDBusConnection) connection = NULL;
        g_autoptr(GVariant) reply = NULL;
        g_autoptr(GError) error = NULL;

        connection = get_session_bus ();
        if (connection == NULL)
                return;

        g_debug ("%s unit %s", start ? "Starting" : "Stopping", unit);
        reply = g_dbus_connection_call_sync (connection,
                                             SYSTEMD_DBUS,
                                             SYSTEMD_PATH_DBUS,
                                             SYSTEMD_INTERFACE_DBUS,
                                             start ? "StartUnit" : "StopUnit",
                                             g_variant_new ("(ss)",
                                                            unit,
                                                            "fail"),
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                             -1, NULL, &error);

        if (error != NULL)
                g_warning ("Failed to %s shutdown target: %s",
                           start ? "start" : "stop",
                           error->message);
}

static void
start_stop_services (ConditionMonitor *condition, gboolean start)
{
        g_autoptr(GFileEnumerator) files = NULL;
        g_autoptr(GError) error = NULL;

        files = g_file_enumerate_children (condition->directory,
                                           G_FILE_ATTRIBUTE_STANDARD_NAME,
                                           G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                           NULL,
                                           &error);

        if (!files) {
                g_autofree char *path = NULL;

                path = g_file_get_path (condition->directory);
                g_warning ("Could not enumerate files in %s: %s", path, error->message);
                return;
        }

        while (TRUE) {
                GFileInfo *info;
                const char *service;

                if (!g_file_enumerator_iterate (files, &info, NULL, NULL, &error)) {
                        g_warning ("Error iterating enumerator: %s", error->message);
                        return;
                }
                if (!info)
                        break;

                service = g_file_info_get_name (info);
                do_start_stop_unit (service, start);
        }
}

static void
gsettings_condition_cb (GSettings  *settings,
                        const char *key,
                        gpointer    user_data)
{
        ConditionMonitor *condition = user_data;
        g_autofree char  *basename = NULL;
        gboolean          start;

        g_assert (g_str_equal (key, condition->settings_key));

        start = g_settings_get_boolean (settings, key);

        basename = g_file_get_basename (condition->directory);
        g_debug ("Autostart Condition \"%s\" condition changed: %d",
                 basename,
                 start);

        start_stop_services (condition, start);
}

static void
file_condition_cb (GFileMonitor     *monitor,
                   GFile            *file,
                   GFile            *other_file,
                   GFileMonitorEvent event,
                   gpointer          user_data)
{
        ConditionMonitor *condition = user_data;
        g_autofree char  *basename = NULL;
        gboolean          exists;
        gboolean          start;

        switch (event) {
        case G_FILE_MONITOR_EVENT_CREATED:
                exists = TRUE;
                break;
        case G_FILE_MONITOR_EVENT_DELETED:
                exists = FALSE;
                break;
        default:
                /* Ignore any other monitor event */
                return;
        }

        if (condition->kind == GSM_CONDITION_IF_EXISTS)
                start = exists;
        else
                start = !exists;

        basename = g_file_get_basename (condition->directory);
        g_debug ("Autostart Condition \"%s\" condition changed: %d",
                 basename,
                 start);

        start_stop_services (condition, start);
}

static void
monitor_dir_changed_cb (GFileMonitor     *monitor,
                        GFile            *file,
                        GFile            *other_file,
                        GFileMonitorEvent event_type,
                        GHashTable       *monitored_events)
{
        g_autoptr(GError) error = NULL;
        g_autofree char *dirname = NULL;
        g_autofree char *key = NULL;
        GsmAutostartCondition kind;

        if (event_type != G_FILE_MONITOR_EVENT_CREATED &&
            event_type != G_FILE_MONITOR_EVENT_MOVED_IN &&
            event_type != G_FILE_MONITOR_EVENT_DELETED &&
            event_type != G_FILE_MONITOR_EVENT_MOVED_OUT)
                return;

        dirname = g_file_get_basename (file);
        if (!gsm_autostart_condition_parse (dirname, &kind, &key)) {
                g_warning ("Directory \"%s\" is not a valid watch expression, ignoring.", dirname);
                return;
        }

        if (kind != GSM_CONDITION_IF_EXISTS &&
            kind != GSM_CONDITION_UNLESS_EXISTS &&
            kind != GSM_CONDITION_GSETTINGS) {
                g_warning ("Condition type used in directory \"%s\" is not supported", dirname);
                return;
        }

        if (event_type == G_FILE_MONITOR_EVENT_CREATED || event_type == G_FILE_MONITOR_EVENT_MOVED_IN) {
                g_autoptr(ConditionMonitor) condition = NULL;

                if (g_file_query_file_type (file, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL) != G_FILE_TYPE_DIRECTORY) {
                        g_warning ("Can only handle directorise for monitoring, \"%s\" is not!", dirname);
                        return;
                }

                condition = g_new0 (ConditionMonitor, 1);
                condition->kind = kind;
                condition->directory = g_object_ref (file);

                g_debug ("Registering new condition \"%s\"", dirname);
                if (g_hash_table_lookup (monitored_events, dirname)) {
                        g_warning ("Condition \"%s\" is already being monitored, this should not happen!", dirname);
                        return;
                }

                if (kind == GSM_CONDITION_GSETTINGS) {
                        g_autofree char *signal = NULL;

                        if (!gsm_autostart_condition_resolve_settings (key,
                                                                       &condition->settings,
                                                                       &condition->settings_key)) {
                                g_warning ("Could not setup GSetting condition monitor \"%s\"", key);
                                return;
                        }

                        g_debug ("Now monitoring settings for schema and key %s", key);
                        signal = g_strdup_printf ("changed::%s", condition->settings_key);
                        g_signal_connect (condition->settings, signal,
                                          G_CALLBACK (gsettings_condition_cb),
                                          condition);
                } else {
                        g_autoptr(GFile) file = NULL;
                        g_autofree char *file_path;

                        file_path = gsm_autostart_condition_resolve_file_path (key);
                        file = g_file_new_for_path (file_path);

                        condition->monitor = g_file_monitor (file, G_FILE_MONITOR_NONE, NULL, &error);
                        if (!condition->monitor) {
                                g_warning ("Failed to setup monitoring for condition %s: %s", key, error->message);
                                return;
                        }
                        g_debug ("Now monitoring file %s", file_path);
                        g_signal_connect (condition->monitor, "changed",
                                          G_CALLBACK (file_condition_cb),
                                          condition);
                }

                g_hash_table_insert (monitored_events, g_steal_pointer (&dirname), g_steal_pointer (&condition));

        } else if (event_type == G_FILE_MONITOR_EVENT_DELETED || event_type == G_FILE_MONITOR_EVENT_MOVED_OUT) {
                g_debug ("Removing condition \"%s\"", dirname);

                if (!g_hash_table_remove (monitored_events, dirname))
                        g_warning ("Tried to remove watch \"%s\" but it was not registered", dirname);
        }
}

static int
setup_monitoring (const char *monitor_dir_name)
{
        g_autoptr(GFileEnumerator) files = NULL;
        g_autoptr(GMainLoop) loop = NULL;
        g_autoptr(GFile) monitor_dir = NULL;
        g_autoptr(GFileMonitor) monitor = NULL;
        g_autoptr(GError) error = NULL;
        g_autoptr(GHashTable) monitored_events = NULL;

        monitor_dir = g_file_new_for_path (monitor_dir_name);
        /* Ensure monitoring directory exists. */
        if (!g_file_make_directory (monitor_dir, NULL, &error)) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
                        g_warning ("Could not create directory for monitoring %s: %s\n",
                                   monitor_dir_name, error->message);
                        return 1;
                }
        }

        monitor = g_file_monitor_directory (monitor_dir, G_FILE_MONITOR_NONE, NULL, &error);
        if (!monitor) {
                g_warning ("Could not start monitoring %s: %s\n", monitor_dir_name, error->message);
                return 1;
        }

        monitored_events = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) condition_monitor_free);
        g_signal_connect (monitor, "changed", G_CALLBACK (monitor_dir_changed_cb), monitored_events);


        /* Emulate create events for existing files/directories */
        files = g_file_enumerate_children (monitor_dir,
                                           G_FILE_ATTRIBUTE_STANDARD_NAME,
                                           G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                           NULL,
                                           &error);
        if (!files) {
                g_warning ("Could not enumerate files in %s: %s", monitor_dir_name, error->message);
                return 0;
        }

        while (TRUE) {
                GFile *file;

                if (!g_file_enumerator_iterate (files, NULL, &file, NULL, &error)) {
                        g_warning ("Error iterating enumerator: %s", error->message);
                        return 1;
                }
                if (!file)
                        break;

                monitor_dir_changed_cb (monitor, file, NULL, G_FILE_MONITOR_EVENT_CREATED, monitored_events);
        }


        loop = g_main_loop_new (NULL, FALSE);
        /* Terminate gracefully on SIGTERM. */
        g_unix_signal_add (SIGTERM,
                           G_SOURCE_FUNC (g_main_loop_quit),
                           loop);
        g_main_loop_run (loop);

        return 0;
}

int
main (int argc, char *argv[])
{
        static gboolean opt_monitor;
        static char* opt_condition = NULL;
        g_autofree char* monitor_dir = NULL;
        g_autofree char *fifo_path = NULL;
        int conflicting_options;
        gboolean gnome_session = TRUE;
        g_autoptr(GOptionContext) ctx = NULL;
        g_autoptr(GError) error = NULL;
        static const GOptionEntry options[] = {
                { "monitor", '\0', 0, G_OPTION_ARG_NONE, &opt_monitor, N_("Monitor conditions of autostart targets."), NULL },
                { "condition", '\0', 0, G_OPTION_ARG_STRING, &opt_condition, N_("Check condition and register it with monitoring service"), NULL },
                { NULL },
        };

        /* Initialize the i18n stuff */
        setlocale (LC_ALL, "");
        bindtextdomain (GETTEXT_PACKAGE, LOCALE_DIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        ctx = g_option_context_new ("");
        g_option_context_add_main_entries (ctx, options, GETTEXT_PACKAGE);
        if (! g_option_context_parse (ctx, &argc, &argv, &error)) {
                g_warning ("Unable to start: %s", error->message);
                exit (1);
        }

        conflicting_options = 0;
        if (opt_monitor)
                conflicting_options++;
        if (opt_condition)
                conflicting_options++;
        if (conflicting_options != 1) {
                g_printerr (_("Program needs exactly one parameter"));
                exit (1);
        }

        fifo_path = g_build_filename (g_get_user_runtime_dir (), "gnome-session-leader-fifo", NULL);
        if (!g_file_test (fifo_path, G_FILE_TEST_EXISTS)) {
                g_debug ("Detected we are not in a GNOME session");
                gnome_session = FALSE;
        }

        monitor_dir = g_build_filename (g_get_user_runtime_dir (), MONITOR_RUN_DIR, NULL);

        if (opt_monitor) {
                if (!gnome_session) {
                        g_warning ("Not running inside a GNOME session, refusing to start!");
                        return 1;
                }

                return setup_monitoring (monitor_dir);

        } if (opt_condition) {
                g_autofree gchar *condition_dir = NULL;
                gchar *service_unit = NULL;
                g_autofree gchar *service_file = NULL;
                int fd;

                if (!gnome_session) {
                        g_debug ("Not running inside a GNOME session, deferring decision!");
                        return 0;
                }

                condition_dir = g_build_filename (monitor_dir, opt_condition, NULL);

                /* Register the condition with the monitoring service */
                if (g_mkdir_with_parents (condition_dir, 0755) < 0) {
                        g_warning ("Could not create condition monitoring directory!");
                        return 1;
                }

                if (sd_pid_get_user_unit (0, &service_unit) < 0) {
                        g_warning ("Could not resolve user unit!");
                        return 1;
                }

                service_file = g_build_filename (condition_dir, service_unit, NULL);
                free (service_unit);
                fd = g_open (service_file, O_CREAT | O_WRONLY, 0644);
                if (fd < 0) {
                        g_warning ("Could not create monitoring file");
                        return 1;
                }
                g_close (fd, NULL);

                /* And finally, actually evaluate the condition.
                 *
                 * Note that this is currently **not** race free. If the monitoring
                 * service is slow, then we might miss events. To fix this, we could
                 * add some sort of hand-shake to ensure the monitoring has been set
                 * up (e.g. created a "monitored" file and wait for its creation
                 * here).
                 */
                if (evalute_condition (opt_condition))
                        return 0;
                else
                        return 1;
        } else {
                g_assert_not_reached ();
                return 0;
        }
}
