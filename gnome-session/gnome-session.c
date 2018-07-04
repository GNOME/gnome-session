/* -*- mode:c; c-basic-offset: 8; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2018 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author:
 *   Iain Lane <iainl@gnome.org>
 */

/* This is the executable that is called to kick off a new session under
 * systemd. It does this:
 *  - Find all units that are failed or active and
 *    PartOf=graphical-session.target. Stop and reset them. These are things
 *    that failed or are lingering around from a previous login.
 *  - Upload the user's environment - except some login-session specific
 *    variables - into systemd. This means that variables set by GDM (etc.) are
 *    available to systemd-started processes.
 *  - Start the passed-in unit, which will usually be a target that starts all
 *    services required by the session.
 *  - Wait for it to finish, and then exit.
 */

#define G_LOG_USE_STRUCTURED

#include <config.h>
#include <errno.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <locale.h>

typedef struct {
        GMainLoop *main_loop;

        /* the systemd manager */
        GDBusProxy *manager_proxy;
        /* the unit we're starting */
        GDBusProxy *unit_proxy;

        /* prep work */
        gboolean env_upload_finished;
        guint n_reset_failed;

        /* params */
        gchar *part_of;
        /* from argv */
        const gchar *unit;

        int exit_code;
} Data;

static void
data_free (Data *data)
{
        g_clear_pointer (&data->main_loop, g_main_loop_unref);
        g_clear_object (&data->manager_proxy);
        g_clear_object (&data->unit_proxy);
        g_clear_pointer (&data->part_of, g_free);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (Data, data_free)

static void
quit (Data *data,
      int exit_code)
{
        data->exit_code = exit_code;
        g_main_loop_quit (data->main_loop);
}

static void
on_unit_properties_changed (GDBusProxy *proxy,
                            GVariant *changed_properties,
                            GStrv invalidated_properties G_GNUC_UNUSED,
                            gpointer user_data)
{
        Data *data;
        const gchar *active_state;

        data = (Data *) user_data;

        if (!g_variant_lookup (changed_properties, "ActiveState", "&s", &active_state))
                return;

        g_debug ("ActiveState changed to: %s", active_state);

        /* we wait for the unit to go to inactive or failed before quitting */
        if (g_strcmp0 (active_state, "inactive") == 0)
                quit (data, EXIT_SUCCESS);

        if (g_strcmp0 (active_state, "failed") == 0)
                quit (data, EXIT_FAILURE);
}

static void
on_start (GObject *source_object,
          GAsyncResult *res,
          gpointer user_data)
{
        g_autoptr(GError) error = NULL;
        Data *data;
        g_autoptr(GVariant) result = NULL;

        data = (Data *) user_data;

        result = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object), res, &error);

        if (result == NULL) {
                g_critical ("Start failed: %s", error->message);
                quit (data, EXIT_FAILURE);
                return;
        }

        g_signal_connect (data->unit_proxy,
                          "g-properties-changed",
                          G_CALLBACK (on_unit_properties_changed),
                          user_data);
}

static void
on_got_unit_proxy (GObject *source_object G_GNUC_UNUSED,
                   GAsyncResult *res,
                   gpointer user_data)
{
        g_autoptr(GError) error = NULL;
        Data *data;

        data = (Data *) user_data;

        data->unit_proxy = g_dbus_proxy_new_for_bus_finish (res, &error);

        if (data->unit_proxy == NULL) {
                g_critical ("Getting unit proxy failed: %s", error->message);
                quit (data, EXIT_FAILURE);
                return;
        }

        g_dbus_proxy_call (data->unit_proxy,
                           "Start",
                           g_variant_new ("(s)", "fail"),
                           G_DBUS_CALL_FLAGS_NO_AUTO_START,
                           -1, /* timeout_msec */
                           NULL, /* GCancellable */
                           on_start,
                           user_data);
}

static void
on_load_unit (GObject *source_object,
              GAsyncResult *res,
              gpointer user_data)
{
        Data *data;
        g_autoptr(GError) error = NULL;
        const gchar *object_path;
        g_autoptr(GVariant) result = NULL;

        data = (Data *) user_data;

        result = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object), res, &error);

        if (result == NULL) {
                g_critical ("LoadUnit failed: %s", error->message);
                quit (data, EXIT_FAILURE);
                return;
        }

        g_variant_get (result, "(&o)", &object_path);

        g_debug ("Unit object path is: %s", object_path);

        g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                                  G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                  NULL, /* GDBusInterfaceInto */
                                  "org.freedesktop.systemd1",
                                  object_path,
                                  "org.freedesktop.systemd1.Unit",
                                  NULL, /* GCancellable */
                                  on_got_unit_proxy,
                                  user_data);

}

static void
maybe_start_unit (Data *data)
{
        if (data->n_reset_failed == 0 && data->env_upload_finished)
                g_dbus_proxy_call (data->manager_proxy,
                                   "LoadUnit",
                                   g_variant_new ("(s)", data->unit),
                                   G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                   -1, /* timeout_msec */
                                   NULL, /* GCancellable */
                                   on_load_unit,
                                   data);
}

static void
on_reset_failed_unit (GObject *source_object,
                      GAsyncResult *res,
                      gpointer user_data)
{
        g_autoptr(GError) error = NULL;
        Data *data;

        data = (Data *) user_data;

        /* no result, we're just checking if there was an error */
        g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object), res, &error);

        if (error != NULL) {
                g_critical ("ResetFailedUnit failed: %s", error->message);
                quit (data, EXIT_FAILURE);
                return;
        }

        data->n_reset_failed--;

        g_debug ("ResetFailedUnit finished: %d remaining", data->n_reset_failed);

        maybe_start_unit (data);
}

static void
on_stop_unit (GObject *source_object,
              GAsyncResult *res,
              gpointer user_data)
{
        g_autoptr(GError) error = NULL;
        g_autoptr(GVariant) result = NULL;
        g_autoptr(GVariant) resultv = NULL;
        Data *data;

        data = (Data *) user_data;

        g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object), res, &error);

        if (error != NULL) {
                g_critical ("StopUnit failed: %s", error->message);
                quit (data, EXIT_FAILURE);
                return;
        }

        maybe_start_unit (data);
}

typedef struct {
        gchar *active_state;
        gchar *unit_name;
        gchar *unit_object_path;
        Data *data;
} ResetFailedData;

static void
reset_failed_data_free (ResetFailedData *reset_failed_data)
{
        /* don't free data, it's not our ref */
        g_free (reset_failed_data->unit_name);
        g_free (reset_failed_data->active_state);
        g_free (reset_failed_data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ResetFailedData, reset_failed_data_free)

typedef struct {
        guint subscription_id;
        Data *data;
} PropertiesChangedData;

static void
on_unit_properties_changed_stop (GDBusConnection *connection,
                                 const gchar *sender_name,
                                 const gchar *object_path,
                                 const gchar *interface_name,
                                 const gchar *signal_name,
                                 GVariant *parameters,
                                 gpointer user_data)
{
        PropertiesChangedData *properties_changed_data;
        g_autoptr(GVariant) changed_properties_v = NULL;
        g_auto(GVariantDict) changed_properties;
        const gchar *active_state;

        properties_changed_data = (PropertiesChangedData *) user_data;

        g_debug ("Got PropertiesChanged(%s)", object_path);

        g_variant_get (parameters, "(s@a{sv}as)", NULL, &changed_properties_v, NULL);

        if (changed_properties_v == NULL) {
                g_critical ("Failed to unpack PropertiesChanged data for %s",
                            object_path);
                quit (properties_changed_data->data, EXIT_FAILURE);
        }

        g_variant_dict_init (&changed_properties, changed_properties_v);

        if (!g_variant_dict_lookup (&changed_properties, "ActiveState", "s", &active_state))
                return;

        g_debug ("ActiveState changed to: %s", active_state);

        /* we wait for the unit to go to inactive or failed */
        if (g_strcmp0 (active_state, "inactive") == 0 ||
            g_strcmp0 (active_state, "failed") == 0) {
                properties_changed_data->data->n_reset_failed--;
                g_debug ("Stopping watching %s, %d left",
                         object_path,
                         properties_changed_data->data->n_reset_failed);
                g_dbus_connection_signal_unsubscribe (connection,
                                                      properties_changed_data->subscription_id);
        }
}

static void
on_got_part_of (GObject *source_object,
                GAsyncResult *res,
                gpointer user_data)
{
        GDBusConnection *connection;
        Data *data;
        g_autoptr(GError) error = NULL;
        g_autofree gchar *name_owner = NULL;
        const gchar * const *partof;
        PropertiesChangedData *properties_changed_data;
        g_autoptr(ResetFailedData) reset_failed_data = (ResetFailedData *) user_data;
        g_autoptr(GVariant) result = NULL;
        g_autoptr(GVariant) result_unpacked = NULL;

        connection = G_DBUS_CONNECTION (source_object);

        data = reset_failed_data->data;

        result = g_dbus_connection_call_finish (connection, res, &error);

        if (result == NULL) {
                g_critical ("Getting PartOf %s failed: %s",
                            reset_failed_data->unit_name,
                            error->message);
                quit (reset_failed_data->data, EXIT_FAILURE);
                return;
        }

        g_variant_get (result, "(v)", &result_unpacked);
        partof = g_variant_get_strv (result_unpacked, NULL);

        if (g_strv_contains (partof, reset_failed_data->data->part_of)) {
                /* failed, let's reset it */
                if (g_strcmp0 (reset_failed_data->active_state, "failed") == 0) {
                        g_dbus_proxy_call (reset_failed_data->data->manager_proxy,
                                           "ResetFailedUnit",
                                           g_variant_new ("(s)", reset_failed_data->unit_name),
                                           G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                           -1, /* timeout_msec */
                                           NULL, /* GCancellable */
                                           on_reset_failed_unit,
                                           reset_failed_data->data);
                        return;
                /* it's running (ish) - stop it */
                } else if (g_strcmp0 (reset_failed_data->active_state, "inactive") != 0) {
                        name_owner = g_dbus_proxy_get_name_owner (data->manager_proxy);

                        g_debug ("Calling StopUnit(%s)", reset_failed_data->unit_name);
                        /* wait for it to stop */
                        properties_changed_data = g_new0 (PropertiesChangedData, 1);
                        properties_changed_data->data = data;
                        properties_changed_data->subscription_id = g_dbus_connection_signal_subscribe (
                                connection,
                                name_owner,
                                "org.freedesktop.DBus.Properties",
                                "PropertiesChanged",
                                reset_failed_data->unit_object_path,
                                NULL, /* arg0 */
                                G_DBUS_SIGNAL_FLAGS_NONE,
                                on_unit_properties_changed_stop,
                                properties_changed_data,
                                g_free);
                        g_dbus_proxy_call (reset_failed_data->data->manager_proxy,
                                           "StopUnit",
                                           g_variant_new ("(ss)", reset_failed_data->unit_name, "fail"),
                                           G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                           -1, /* timeout_msec */
                                           NULL, /* GCancellable */
                                           on_stop_unit,
                                           reset_failed_data->data);
                        return;
                }
        }

        reset_failed_data->data->n_reset_failed--;
        maybe_start_unit (reset_failed_data->data);
}

static void
on_list_units (GObject *source_object,
               GAsyncResult *res,
               gpointer user_data)
{
        const gchar *active_state;
        GDBusConnection *connection;
        g_autoptr(GError) error = NULL;
        GVariantIter iter;
        const gchar *unit_name;
        const gchar *unit_object_path;
        Data *data;
        ResetFailedData *reset_failed_data;
        g_autoptr(GVariant) result = NULL;
        g_autoptr(GVariant) result_unpacked = NULL;

        data = (Data *) user_data;

        result = g_dbus_proxy_call_finish (data->manager_proxy, res, &error);
        connection = g_dbus_proxy_get_connection (data->manager_proxy);

        if (result == NULL) {
                g_critical ("ListUnits failed: %s", error->message);
                quit (data, EXIT_FAILURE);
                return;
        }

        g_variant_get (result, "(@a(ssssssouso))", &result_unpacked);

        g_variant_iter_init (&iter, result_unpacked);

        while (g_variant_iter_loop (&iter, "(&s&s&s&s&s&s&ou&s&o)",
                                    &unit_name,
                                    NULL, /* description */
                                    NULL, /* load state */
                                    &active_state,
                                    NULL, /* sub state */
                                    NULL, /* follower */
                                    &unit_object_path,
                                    NULL, /* job queued */
                                    NULL, /* job type */
                                    NULL /* job object path */)) {
                reset_failed_data = g_new0 (ResetFailedData, 1);
                reset_failed_data->active_state = g_strdup (active_state);
                reset_failed_data->unit_name = g_strdup (unit_name);
                reset_failed_data->unit_object_path = g_strdup (unit_object_path);
                reset_failed_data->data = data;
                data->n_reset_failed++;

                /* Is it PartOf graphical-session.target? */
                g_dbus_connection_call (connection,
                                        "org.freedesktop.systemd1",
                                        unit_object_path,
                                        "org.freedesktop.DBus.Properties",
                                        "Get",
                                        g_variant_new ("(ss)",
                                                       "org.freedesktop.systemd1.Unit",
                                                       "PartOf"),
                                        G_VARIANT_TYPE("(v)"),
                                        G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                        -1, /* timeout_msec */
                                        NULL, /* GCancellable */
                                        on_got_part_of,
                                        reset_failed_data);
        }

        maybe_start_unit (data);
}

static void
on_set_environment (GObject *source_object G_GNUC_UNUSED,
                    GAsyncResult *res,
                    gpointer user_data)
{
        g_autoptr(GError) error = NULL;
        Data *data;

        data = (Data *) user_data;

        /* no result, we're just checking if there was an error */
        g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object), res, &error);

        if (error != NULL) {
                g_critical ("SetEnvironment failed: %s", error->message);
                quit (data, EXIT_FAILURE);
                return;
        }

        g_debug ("Environment upload complete");
        data->env_upload_finished = TRUE;

        maybe_start_unit (data);
}

static void
upload_environment (Data *data)
{
        g_auto(GStrv) environ = NULL;
        const gchar *xdg_config_dirs;
        g_autofree gchar *xdg_config_dirs_new = NULL;
        /* these are login-session specific and we don't want them to leak
         * through from the caller */
        static const gchar * const variable_blacklist[] = {
                "XDG_SEAT",
                "XDG_SESSION_ID",
                "XDG_VTNR",
                NULL
        };

        environ = g_get_environ ();

        for (int i = 0; variable_blacklist[i] != NULL; i++)
                environ = g_environ_unsetenv (environ, variable_blacklist[i]);

        /*
         * If we're starting a systemd unit, let's put a directory into
         * XDG_CONFIG_DIRS. The idea here is that things can drop (e.g.) XDG
         * autostart desktop files in there, to disable them if there's a
         * corresponding unit.
         */
        xdg_config_dirs = g_environ_getenv (environ, "XDG_CONFIG_DIRS");
        if (xdg_config_dirs == NULL) {
                /* default value */
                environ = g_environ_setenv (environ,
                                            "XDG_CONFIG_DIRS", GNOME_SESSION_SYSTEMD_DIR ":" DEFAULT_XDG_CONFIG_DIRS,
                                            FALSE);
        } else {
                xdg_config_dirs_new = g_strdup_printf ("%s:%s",
                                                       GNOME_SESSION_SYSTEMD_DIR,
                                                       xdg_config_dirs);
                environ = g_environ_setenv (environ,
                                            "XDG_CONFIG_DIRS", xdg_config_dirs_new,
                                            FALSE);
        }

        g_dbus_proxy_call (data->manager_proxy,
                           "SetEnvironment",
                           g_variant_new ("(^as)", (const gchar * const *) environ),
                           G_DBUS_CALL_FLAGS_NO_AUTO_START,
                           -1,
                           NULL, /* GCancellable */
                           on_set_environment,
                           data);
}

static void
on_got_manager_proxy (GObject *source_object G_GNUC_UNUSED,
                      GAsyncResult *res,
                      gpointer user_data)
{
        Data *data;
        g_autoptr(GError) error = NULL;
        g_autofree gchar *name_owner = NULL;

        data = (Data *) user_data;

        data->manager_proxy = g_dbus_proxy_new_for_bus_finish (res, &error);

        if (data->manager_proxy == NULL) {
                g_critical ("Getting manager proxy failed: %s", error->message);
                quit (data, EXIT_FAILURE);
                return;
         }

        name_owner = g_dbus_proxy_get_name_owner (data->manager_proxy);

        /* If there's no systemd --user, just become the old gnome-session */
        if (name_owner == NULL) {
                g_debug ("No systemd user manager running, will run gnome-session as fallback.");

                if (execlp ("gnome-session", "gnome-session", NULL) < 0) {
                        g_critical ("Failed to execve() gnome-session: %s", g_strerror (errno));
                        quit (data, EXIT_FAILURE);
                }
        }

        /* we need to get PropertiesChanged */
        g_dbus_proxy_call_sync (data->manager_proxy,
                                "Subscribe",
                                NULL, /* parameters */
                                G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                -1, /* timeoput_msec */
                                NULL, /* GCancellable */
                                &error);

        if (error != NULL) {
                g_critical ("Calling Subscribe() failed: %s", error->message);
                quit (data, EXIT_FAILURE);
        }

        /* get failed units */
        g_dbus_proxy_call (data->manager_proxy,
                           "ListUnits",
                           NULL, /* parameters */
                           G_DBUS_CALL_FLAGS_NO_AUTO_START,
                           -1, /* timeout_msec */
                           NULL, /* GCancellable */
                           on_list_units,
                           user_data);

        /* upload the env at the same time */
        upload_environment (data);
}

int
main (int argc, char *argv[])
{
        gboolean opt_debug = FALSE;
        g_auto(GStrv) opt_units = NULL;
        g_autofree gchar *opt_part_of = NULL;
        g_autoptr(GOptionContext) context = NULL;
        g_autoptr(GError) error = NULL;
        g_auto(Data) data = { NULL };

        GOptionEntry options[] = {
                { "debug", 'd', 0, G_OPTION_ARG_NONE, &opt_debug, _("Enable debugging output"), NULL },
                { "partof", 'p', 0, G_OPTION_ARG_STRING, &opt_part_of, _("Reset failed state of units which are PartOf this (default: graphical-session.target)"), "UNIT" },
                { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &opt_units, _("Unit to start"), "UNIT"},
                { NULL }
        };


        setlocale (LC_ALL, "");
        bindtextdomain (GETTEXT_PACKAGE, LOCALE_DIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        context = g_option_context_new (_("- start a graphical session unit"));
        g_option_context_add_main_entries (context, options, GETTEXT_PACKAGE);

        if (!g_option_context_parse (context, &argc, &argv, &error)) {
                g_printerr ("Error: %s\n", error->message);
                return EXIT_FAILURE;
        }

        if (!opt_units || !opt_units[0]) {
                g_printerr ("Error: No unit specified\n");
                return EXIT_FAILURE;
        }

        data.unit = opt_units[0];

        if (opt_debug)
                g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

        if (opt_part_of)
                data.part_of = opt_part_of;
        else
                data.part_of = g_strdup ("graphical-session.target");

        data.main_loop = g_main_loop_new (NULL, FALSE);

        /* this starts the whole thing off */
        g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                                  G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                  NULL, /* GDBusInterfaceInto */
                                  "org.freedesktop.systemd1",
                                  "/org/freedesktop/systemd1",
                                  "org.freedesktop.systemd1.Manager",
                                  NULL, /* GCancellable */
                                  on_got_manager_proxy,
                                  &data);

        g_main_loop_run (data.main_loop);

        return data.exit_code;
}
