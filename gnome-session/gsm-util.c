/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 * gsm-util.c
 * Copyright (C) 2008 Lucas Rocha.
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
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "gsm-util.h"

static gchar **child_environment;

/* These are variables that will not be passed on to subprocesses
 * (either directly, via systemd or DBus).
 * Some of these are blacklisted as they might end up in the wrong session
 * (e.g. XDG_VTNR), others because they simply must never be passed on
 * (NOTIFY_SOCKET).
 */
static const char * const variable_blacklist[] = {
    "NOTIFY_SOCKET",
    "XDG_SEAT",
    "XDG_SESSION_ID",
    "XDG_VTNR",
    NULL
};

/* The following is copied from GDMs spawn_session function.
 *
 * Environment variables listed here will be copied into the user's service
 * environments if they are set in gnome-session's environment. If they are
 * not set in gnome-session's environment, they will be removed from the
 * service environments. This is to protect against environment variables
 * leaking from previous sessions (e.g. when switching from classic to
 * default GNOME $GNOME_SHELL_SESSION_MODE will become unset).
 */
static const char * const variable_unsetlist[] = {
    "DISPLAY",
    "XAUTHORITY",
    "WAYLAND_DISPLAY",
    "WAYLAND_SOCKET",
    "GNOME_SHELL_SESSION_MODE",
    "GNOME_SETUP_DISPLAY",

    /* None of the LC_* variables should survive a logout/login */
    "LC_CTYPE",
    "LC_NUMERIC",
    "LC_TIME",
    "LC_COLLATE",
    "LC_MONETARY",
    "LC_MESSAGES",
    "LC_PAPER",
    "LC_NAME",
    "LC_ADDRESS",
    "LC_TELEPHONE",
    "LC_MEASUREMENT",
    "LC_IDENTIFICATION",
    "LC_ALL",

    NULL
};

char *
gsm_util_find_desktop_file_for_app_name (const char *name,
                                         gboolean    autostart_first)
{
        char     *app_path;
        char    **app_dirs;
        GKeyFile *key_file;
        char     *desktop_file;
        int       i;

        app_path = NULL;

        app_dirs = gsm_util_get_desktop_dirs (autostart_first);

        key_file = g_key_file_new ();

        desktop_file = g_strdup_printf ("%s.desktop", name);

        g_debug ("GsmUtil: Looking for file '%s'", desktop_file);

        for (i = 0; app_dirs[i] != NULL; i++) {
                g_debug ("GsmUtil: Looking in '%s'", app_dirs[i]);
        }

        g_key_file_load_from_dirs (key_file,
                                   desktop_file,
                                   (const char **) app_dirs,
                                   &app_path,
                                   G_KEY_FILE_NONE,
                                   NULL);

        if (app_path != NULL) {
                g_debug ("GsmUtil: found in XDG dirs: '%s'", app_path);
        }

        /* look for gnome vendor prefix */
        if (app_path == NULL) {
                g_free (desktop_file);
                desktop_file = g_strdup_printf ("gnome-%s.desktop", name);

                g_key_file_load_from_dirs (key_file,
                                           desktop_file,
                                           (const char **) app_dirs,
                                           &app_path,
                                           G_KEY_FILE_NONE,
                                           NULL);
                if (app_path != NULL) {
                        g_debug ("GsmUtil: found in XDG dirs: '%s'", app_path);
                }
        }

        g_free (desktop_file);
        g_key_file_free (key_file);

        g_strfreev (app_dirs);

        return app_path;
}

static char ** autostart_dirs;

void
gsm_util_set_autostart_dirs (char ** dirs)
{
        autostart_dirs = g_strdupv (dirs);
}

static char **
gsm_util_get_standard_autostart_dirs (void)
{
        GPtrArray          *dirs;
        const char * const *system_config_dirs;
        const char * const *system_data_dirs;
        int                 i;

        dirs = g_ptr_array_new ();

        g_ptr_array_add (dirs,
                         g_build_filename (g_get_user_config_dir (),
                                           "autostart", NULL));

        system_data_dirs = g_get_system_data_dirs ();
        for (i = 0; system_data_dirs[i]; i++) {
                g_ptr_array_add (dirs,
                                 g_build_filename (system_data_dirs[i],
                                                   "gnome", "autostart", NULL));
        }

        system_config_dirs = g_get_system_config_dirs ();
        for (i = 0; system_config_dirs[i]; i++) {
                g_ptr_array_add (dirs,
                                 g_build_filename (system_config_dirs[i],
                                                   "autostart", NULL));
        }

        g_ptr_array_add (dirs, NULL);

        return (char **) g_ptr_array_free (dirs, FALSE);
}

char **
gsm_util_get_autostart_dirs ()
{
        if (autostart_dirs) {
                return g_strdupv ((char **)autostart_dirs);
        }

        return gsm_util_get_standard_autostart_dirs ();
}

char **
gsm_util_get_app_dirs ()
{
        GPtrArray          *dirs;
        const char * const *system_data_dirs;
        int                 i;

        dirs = g_ptr_array_new ();

        g_ptr_array_add (dirs,
                         g_build_filename (g_get_user_data_dir (),
                                           "applications",
                                           NULL));

        system_data_dirs = g_get_system_data_dirs ();
        for (i = 0; system_data_dirs[i]; i++) {
                g_ptr_array_add (dirs,
                                 g_build_filename (system_data_dirs[i],
                                                   "applications",
                                                   NULL));
        }

        g_ptr_array_add (dirs, NULL);

        return (char **) g_ptr_array_free (dirs, FALSE);
}

char **
gsm_util_get_desktop_dirs (gboolean autostart_first)
{
        char **apps;
        char **autostart;
        char **standard_autostart;
        char **result;
        int    size;
        int    i;

        apps = gsm_util_get_app_dirs ();
        autostart = gsm_util_get_autostart_dirs ();

        /* Still, check the standard autostart dirs for things like fulfilling session reqs,
         * if using a non-standard autostart dir for autostarting */
        if (autostart_dirs != NULL)
                standard_autostart = gsm_util_get_standard_autostart_dirs ();
        else
                standard_autostart = NULL;

        size = 0;
        for (i = 0; apps[i] != NULL; i++) { size++; }
        for (i = 0; autostart[i] != NULL; i++) { size++; }
        if (standard_autostart != NULL)
                for (i = 0; standard_autostart[i] != NULL; i++) { size++; }

        result = g_new (char *, size + 1); /* including last NULL */

        size = 0;

        if (autostart_first) {
                for (i = 0; autostart[i] != NULL; i++, size++) {
                        result[size] = autostart[i];
                }
                if (standard_autostart != NULL) {
                        for (i = 0; standard_autostart[i] != NULL; i++, size++) {
                                result[size] = standard_autostart[i];
                        }
                }
                for (i = 0; apps[i] != NULL; i++, size++) {
                        result[size] = apps[i];
                }
        } else {
                for (i = 0; apps[i] != NULL; i++, size++) {
                        result[size] = apps[i];
                }
                if (standard_autostart != NULL) {
                        for (i = 0; standard_autostart[i] != NULL; i++, size++) {
                                result[size] = standard_autostart[i];
                        }
                }
                for (i = 0; autostart[i] != NULL; i++, size++) {
                        result[size] = autostart[i];
                }
        }

        g_free (apps);
        g_free (autostart);
        g_free (standard_autostart);

        result[size] = NULL;

        return result;
}

gboolean
gsm_util_text_is_blank (const char *str)
{
        if (str == NULL) {
                return TRUE;
        }

        while (*str) {
                if (!isspace(*str)) {
                        return FALSE;
                }

                str++;
        }

        return TRUE;
}

/**
 * gsm_util_init_error:
 * @fatal: whether or not the error is fatal to the login session
 * @format: printf-style error message format
 * @...: error message args
 *
 * Displays the error message to the user. If @fatal is %TRUE, gsm
 * will exit after displaying the message.
 *
 * This should be called for major errors that occur before the
 * session is up and running. (Notably, it positions the dialog box
 * itself, since no window manager will be running yet.)
 **/
void
gsm_util_init_error (gboolean    fatal,
                     const char *format, ...)
{
        char           *msg;
        va_list         args;
        gchar          *argv[13];

        va_start (args, format);
        msg = g_strdup_vprintf (format, args);
        va_end (args);

        argv[0] = "zenity";
        argv[1] = "--error";
        argv[2] = "--class";
        argv[3] = "mutter-dialog";
        argv[4] = "--title";
        argv[5] = "\"\"";
        argv[6] = "--text";
        argv[7] = msg;
        argv[8] = "--icon-name";
        argv[9] = "face-sad-symbolic";
        argv[10] = "--ok-label";
        argv[11] = _("_Log out");
        argv[12] = NULL;

        g_spawn_sync (NULL, argv, child_environment, 0, NULL, NULL, NULL, NULL, NULL, NULL);

        g_free (msg);

        if (fatal) {
                exit (1);
        }
}

/**
 * gsm_util_generate_startup_id:
 *
 * Generates a new SM client ID.
 *
 * Return value: an SM client ID.
 **/
char *
gsm_util_generate_startup_id (void)
{
        static int     sequence = -1;
        static guint   rand1 = 0;
        static guint   rand2 = 0;
        static pid_t   pid = 0;
        struct timeval tv;

        /* The XSMP spec defines the ID as:
         *
         * Version: "1"
         * Address type and address:
         *   "1" + an IPv4 address as 8 hex digits
         *   "2" + a DECNET address as 12 hex digits
         *   "6" + an IPv6 address as 32 hex digits
         * Time stamp: milliseconds since UNIX epoch as 13 decimal digits
         * Process-ID type and process-ID:
         *   "1" + POSIX PID as 10 decimal digits
         * Sequence number as 4 decimal digits
         *
         * XSMP client IDs are supposed to be globally unique: if
         * SmsGenerateClientID() is unable to determine a network
         * address for the machine, it gives up and returns %NULL.
         * GNOME and KDE have traditionally used a fourth address
         * format in this case:
         *   "0" + 16 random hex digits
         *
         * We don't even bother trying SmsGenerateClientID(), since the
         * user's IP address is probably "192.168.1.*" anyway, so a random
         * number is actually more likely to be globally unique.
         */

        if (!rand1) {
                rand1 = g_random_int ();
                rand2 = g_random_int ();
                pid = getpid ();
        }

        sequence = (sequence + 1) % 10000;
        gettimeofday (&tv, NULL);
        return g_strdup_printf ("10%.04x%.04x%.10lu%.3u%.10lu%.4d",
                                rand1,
                                rand2,
                                (unsigned long) tv.tv_sec,
                                (unsigned) tv.tv_usec,
                                (unsigned long) pid,
                                sequence);
}

static gboolean
gsm_util_update_activation_environment (const char  *variable,
                                        const char  *value,
                                        GError     **error)
{
        GDBusConnection *connection;
        gboolean         environment_updated;
        GVariantBuilder  builder;
        GVariant        *reply;
        GError          *bus_error = NULL;

        environment_updated = FALSE;
        connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, error);

        if (connection == NULL) {
                return FALSE;
        }

        g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ss}"));
        g_variant_builder_add (&builder, "{ss}", variable, value);

        reply = g_dbus_connection_call_sync (connection,
                                             "org.freedesktop.DBus",
                                             "/org/freedesktop/DBus",
                                             "org.freedesktop.DBus",
                                             "UpdateActivationEnvironment",
                                             g_variant_new ("(@a{ss})",
                                                            g_variant_builder_end (&builder)),
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1, NULL, &bus_error);

        if (bus_error != NULL) {
                g_propagate_error (error, bus_error);
        } else {
                environment_updated = TRUE;
                g_variant_unref (reply);
        }

        g_clear_object (&connection);

        return environment_updated;
}

#define ENV_NAME_PATTERN "[a-zA-Z_][a-zA-Z0-9_]*"
#ifdef SYSTEMD_STRICT_ENV
#define ENV_VALUE_PATTERN "(?:[ \t\n]|[^[:cntrl:]])*"
#else
#define ENV_VALUE_PATTERN ".*"
#endif

gboolean
gsm_util_export_activation_environment (GError     **error)
{

        GDBusConnection *connection;
        gboolean         environment_updated = FALSE;
        char           **entry_names;
        int              i = 0;
        GVariantBuilder  builder;
        GRegex          *name_regex, *value_regex;
        GVariant        *reply;
        GError          *bus_error = NULL;

        connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, error);

        if (connection == NULL) {
                return FALSE;
        }

        name_regex = g_regex_new ("^" ENV_NAME_PATTERN "$", G_REGEX_OPTIMIZE, 0, error);

        if (name_regex == NULL) {
                return FALSE;
        }


        value_regex = g_regex_new ("^" ENV_VALUE_PATTERN "$", G_REGEX_OPTIMIZE, 0, error);

        if (value_regex == NULL) {
                return FALSE;
        }

        if (child_environment == NULL) {
                child_environment = g_listenv ();
        }

        g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ss}"));
        for (entry_names = g_listenv (); entry_names[i] != NULL; i++) {
                const char *entry_name = entry_names[i];
                const char *entry_value = g_getenv (entry_name);

                if (g_strv_contains (variable_blacklist, entry_name))
                    continue;

                if (!g_utf8_validate (entry_name, -1, NULL) ||
                    !g_regex_match (name_regex, entry_name, 0, NULL) ||
                    !g_utf8_validate (entry_value, -1, NULL) ||
                    !g_regex_match (value_regex, entry_value, 0, NULL)) {

                    g_message ("Environment variable is unsafe to export to dbus: %s", entry_name);
                    continue;
                }

                child_environment = g_environ_setenv (child_environment,
                                                      entry_name, entry_value,
                                                      TRUE);
                g_variant_builder_add (&builder, "{ss}", entry_name, entry_value);
        }
        g_regex_unref (name_regex);
        g_regex_unref (value_regex);

        g_strfreev (entry_names);

        reply = g_dbus_connection_call_sync (connection,
                                             "org.freedesktop.DBus",
                                             "/org/freedesktop/DBus",
                                             "org.freedesktop.DBus",
                                             "UpdateActivationEnvironment",
                                             g_variant_new ("(@a{ss})",
                                                            g_variant_builder_end (&builder)),
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1, NULL, &bus_error);

        if (bus_error != NULL) {
                g_propagate_error (error, bus_error);
        } else {
                environment_updated = TRUE;
                g_variant_unref (reply);
        }

        g_clear_object (&connection);

        return environment_updated;
}

gboolean
gsm_util_export_user_environment (GError     **error)
{

        GDBusConnection *connection;
        gboolean         environment_updated = FALSE;
        char           **entries;
        int              i = 0;
        GVariantBuilder  builder;
        GRegex          *regex;
        GVariant        *reply;
        GError          *bus_error = NULL;

        connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, error);

        if (connection == NULL) {
                return FALSE;
        }

        regex = g_regex_new ("^" ENV_NAME_PATTERN "=" ENV_VALUE_PATTERN "$", G_REGEX_OPTIMIZE, 0, error);

        if (regex == NULL) {
                return FALSE;
        }

        entries = g_get_environ ();

        for (i = 0; variable_blacklist[i] != NULL; i++)
                entries = g_environ_unsetenv (entries, variable_blacklist[i]);

        g_variant_builder_init (&builder, G_VARIANT_TYPE ("(asas)"));

        g_variant_builder_open (&builder, G_VARIANT_TYPE ("as"));
        for (i = 0; variable_unsetlist[i] != NULL; i++)
                g_variant_builder_add (&builder, "s", variable_unsetlist[i]);
        for (i = 0; variable_blacklist[i] != NULL; i++)
                g_variant_builder_add (&builder, "s", variable_blacklist[i]);
        g_variant_builder_close (&builder);

        g_variant_builder_open (&builder, G_VARIANT_TYPE ("as"));
        for (i = 0; entries[i] != NULL; i++) {
                const char *entry = entries[i];

                if (!g_utf8_validate (entry, -1, NULL) ||
                    !g_regex_match (regex, entry, 0, NULL)) {

                    g_message ("Environment entry is unsafe to upload into user environment: %s", entry);
                    continue;
                }

                g_variant_builder_add (&builder, "s", entry);
        }
        g_variant_builder_close (&builder);
        g_regex_unref (regex);

        g_strfreev (entries);

        reply = g_dbus_connection_call_sync (connection,
                                             "org.freedesktop.systemd1",
                                             "/org/freedesktop/systemd1",
                                             "org.freedesktop.systemd1.Manager",
                                             "UnsetAndSetEnvironment",
                                             g_variant_builder_end (&builder),
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                             -1, NULL, &bus_error);

        if (bus_error != NULL) {
                g_propagate_error (error, bus_error);
        } else {
                environment_updated = TRUE;
                g_variant_unref (reply);
        }

        g_clear_object (&connection);

        return environment_updated;
}

static gboolean
gsm_util_update_user_environment (const char  *variable,
                                  const char  *value,
                                  GError     **error)
{
        GDBusConnection *connection;
        gboolean         environment_updated;
        char            *entry;
        GVariantBuilder  builder;
        GVariant        *reply;
        GError          *bus_error = NULL;

        environment_updated = FALSE;
        connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, error);

        if (connection == NULL) {
                return FALSE;
        }

        g_variant_builder_init (&builder, G_VARIANT_TYPE ("as"));
        entry = g_strdup_printf ("%s=%s", variable, value);
        g_variant_builder_add (&builder, "s", entry);
        g_free (entry);

        reply = g_dbus_connection_call_sync (connection,
                                             "org.freedesktop.systemd1",
                                             "/org/freedesktop/systemd1",
                                             "org.freedesktop.systemd1.Manager",
                                             "SetEnvironment",
                                             g_variant_new ("(@as)",
                                                            g_variant_builder_end (&builder)),
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                             -1, NULL, &bus_error);

        if (bus_error != NULL) {
                g_propagate_error (error, bus_error);
        } else {
                environment_updated = TRUE;
                g_variant_unref (reply);
        }

        g_clear_object (&connection);

        return environment_updated;
}

gboolean
gsm_util_systemd_unit_is_active (const char  *unit,
                                 GError     **error)
{
        g_autoptr(GDBusProxy) proxy = NULL;
        g_autoptr(GVariant) result = NULL;
        g_autofree gchar *object_path = NULL;
        g_autofree gchar *active_state = NULL;
        g_autoptr(GDBusProxy) unit_proxy = NULL;

        proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                               G_DBUS_PROXY_FLAGS_NONE,
                                               NULL,
                                               "org.freedesktop.systemd1",
                                               "/org/freedesktop/systemd1",
                                               "org.freedesktop.systemd1.Manager",
                                               NULL,
                                               error);

        if (proxy == NULL) {
                return FALSE;
        }

        result = g_dbus_proxy_call_sync (proxy,
                                         "GetUnit",
                                         g_variant_new ("(s)", unit),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         -1,
                                         NULL,
                                         error);

        if (result == NULL) {
                if (error && *error) {
                        g_autofree char *remote_error = g_dbus_error_get_remote_error (*error);

                        if (g_strcmp0 (remote_error, "org.freedesktop.systemd1.NoSuchUnit") == 0) {
                                g_clear_error (error);
                        }
                }
                return FALSE;
        }

        g_variant_get (result, "(o)", &object_path);
        g_clear_pointer (&result, g_variant_unref);

        unit_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                    G_DBUS_PROXY_FLAGS_NONE,
                                                    NULL,
                                                    "org.freedesktop.systemd1",
                                                    object_path,
                                                    "org.freedesktop.systemd1.Unit",
                                                    NULL,
                                                    error);

        if (unit_proxy == NULL) {
            return FALSE;
        }

        result = g_dbus_proxy_get_cached_property (unit_proxy, "ActiveState");

        if (result == NULL) {
                g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "Error getting ActiveState property");
                return FALSE;
        }

        g_variant_get (result, "s", &active_state);

        return g_str_equal (active_state, "active");
}

gboolean
gsm_util_start_systemd_unit (const char  *unit,
                             const char  *mode,
                             GError     **error)
{
        g_autoptr(GDBusConnection) connection = NULL;
        g_autoptr(GVariant)        reply = NULL;
        GError          *bus_error = NULL;

        connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, error);

        if (connection == NULL)
                return FALSE;

        reply = g_dbus_connection_call_sync (connection,
                                             "org.freedesktop.systemd1",
                                             "/org/freedesktop/systemd1",
                                             "org.freedesktop.systemd1.Manager",
                                             "StartUnit",
                                             g_variant_new ("(ss)",
                                                            unit, mode),
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                             -1, NULL, &bus_error);

        if (bus_error != NULL) {
                g_propagate_error (error, bus_error);
                return FALSE;
        }

        return TRUE;
}

gboolean
gsm_util_systemd_reset_failed (GError **error)
{
        g_autoptr(GDBusConnection) connection = NULL;
        g_autoptr(GVariant)        reply = NULL;
        GError *bus_error = NULL;

        connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, error);

        if (connection == NULL)
                return FALSE;

        reply = g_dbus_connection_call_sync (connection,
                                             "org.freedesktop.systemd1",
                                             "/org/freedesktop/systemd1",
                                             "org.freedesktop.systemd1.Manager",
                                             "ResetFailed",
                                             NULL,
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                             -1, NULL, &bus_error);

        if (bus_error != NULL) {
                g_propagate_error (error, bus_error);
                return FALSE;
        }

        return TRUE;
}

void
gsm_util_setenv (const char *variable,
                 const char *value)
{
        GError *error = NULL;

        if (child_environment == NULL)
                child_environment = g_listenv ();

        if (!value)
                child_environment = g_environ_unsetenv (child_environment, variable);
        else
                child_environment = g_environ_setenv (child_environment, variable, value, TRUE);

        /* If this fails it isn't fatal, it means some things like session
         * management and keyring won't work in activated clients.
         */
        if (!gsm_util_update_activation_environment (variable, value, &error)) {
                g_warning ("Could not make bus activated clients aware of %s=%s environment variable: %s", variable, value, error->message);
                g_clear_error (&error);
        }

        /* If this fails, the system user session won't get the updated environment
         */
        if (!gsm_util_update_user_environment (variable, value, &error)) {
                g_debug ("Could not make systemd aware of %s=%s environment variable: %s", variable, value, error->message);
                g_clear_error (&error);
        }
}

const char * const *
gsm_util_listenv (void)
{
        return (const char * const *) child_environment;

}

const char * const *
gsm_util_get_variable_blacklist (void)
{
        return variable_blacklist;
}
