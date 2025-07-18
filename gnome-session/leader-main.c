/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006 Novell, Inc.
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

#include <glib/gi18n.h>
#include <glib.h>
#include <glib-unix.h>
#include <gio/gio.h>

#include "gsm-util.h"

typedef struct {
        GDBusConnection *session_bus;
        GMainLoop *loop;
        int fifo_fd;
} Leader;

static void
leader_clear (Leader *ctx)
{
        g_clear_object (&ctx->session_bus);
        g_clear_object (&ctx->loop);
        g_close (ctx->fifo_fd, NULL);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (Leader, leader_clear);

static char *
shell_to_login_shell (const char *shell)
{
        gboolean in_etc_shells = FALSE;
        const char *candidate;
        g_autofree char *basename = NULL;

        if (IS_STRING_EMPTY (shell))
                return NULL;

        /* Check /etc/shells, to see if the shell is listed as a valid login
         * shell on this system */
        setusershell ();
        while ((candidate = getusershell ()) != NULL) {
                if (g_strcmp0 (shell, candidate) == 0) {
                        in_etc_shells = TRUE;
                        break;
                }
        }
        endusershell ();
        if (!in_etc_shells)
                return NULL;

        /* Sometimes false and nologin end up in /etc/shells, so let's double
         * check for them explicitly */
        basename = g_path_get_basename (shell);
        if (g_strcmp0 (basename, "false") == 0 ||
            g_strcmp0 (basename, "nologin") == 0)
                return NULL;

        /* It's a login shell! Let's do the UNIX magic of prepending '-' to
         * argv [0], and that will tell the shell to run as a login shell */
        return g_strdup_printf ("-%s", basename);
}

static void
maybe_reexec_with_login_shell (GStrv argv)
{
        const char *shell = NULL;
        g_autofree char *login_shell = NULL;
        GStrvBuilder *builder = NULL;
        g_auto (GStrv) inner_args = NULL;
        g_auto (GStrv) outer_args = NULL;

        /* No need to bother on the greeter (no user profile to execute) */
        if (g_strcmp0 (g_getenv ("XDG_SESSION_CLASS"), "greeter") == 0)
                return;

        /* Or with X (it has a mess of scripts that do this for us) */
        if (g_strcmp0 (g_getenv ("XDG_SESSION_TYPE"), "wayland") != 0)
                return;

        shell = g_getenv ("SHELL");
        login_shell = shell_to_login_shell (shell);

        /* Launching sessions with an invalid shell (i.e. nologin) is supported,
         * but these aren't real shells so we can't re-exec with them */
        if (login_shell == NULL)
                return;

        g_debug ("Relaunching with login shell %s (%s)", login_shell, shell);

        /* First, we construct the command executed by the login shell */
        builder = g_strv_builder_new ();
        g_strv_builder_add (builder, "exec");
        g_strv_builder_take (builder, g_shell_quote (argv [0]));
        g_strv_builder_add (builder, "--no-reexec");
        for (char **p = argv + 1; *p != NULL; p++)
                g_strv_builder_take (builder, g_shell_quote (*p));
        inner_args = g_strv_builder_unref_to_strv (builder);

        /* Next, we construct the invocation of the login shell itself */
        builder = g_strv_builder_new ();
        g_strv_builder_take (builder, g_steal_pointer (&login_shell));
        g_strv_builder_add (builder, "-c");
        g_strv_builder_take (builder, g_strjoinv (" ", inner_args));
        outer_args = g_strv_builder_unref_to_strv (builder);

        execv (shell, outer_args);
        g_warning ("Failed to re-execute with login shell (%s): %m", shell);
}

static void
initialize_gio (void)
{
        char *disable_fuse = NULL;
        char *use_vfs = NULL;

        disable_fuse = g_strdup (g_getenv ("GVFS_DISABLE_FUSE"));
        use_vfs = g_strdup (g_getenv ("GIO_USE_VFS"));

        g_setenv ("GVFS_DISABLE_FUSE", "1", TRUE);
        g_setenv ("GIO_USE_VFS", "local", TRUE);
        g_vfs_get_default ();

        if (use_vfs) {
                g_setenv ("GIO_USE_VFS", use_vfs, TRUE);
                g_free (use_vfs);
        } else {
                g_unsetenv ("GIO_USE_VFS");
        }

        if (disable_fuse) {
                g_setenv ("GVFS_DISABLE_FUSE", disable_fuse, TRUE);
                g_free (disable_fuse);
        } else {
                g_unsetenv ("GVFS_DISABLE_FUSE");
        }
}

static void
set_up_environment (void)
{
        g_autoptr (GSettings) locale_settings = NULL;
        g_autofree char *region = NULL;
        g_autofree char *ibus_path = NULL;
        GError *error = NULL;

        locale_settings = g_settings_new ("org.gnome.system.locale");
        region = g_settings_get_string (locale_settings, "region");
        if (!IS_STRING_EMPTY (region)) {
                if (g_strcmp0 (g_getenv ("LANG"), region) == 0) {
                        // LC_CTYPE
                        g_unsetenv ("LC_NUMERIC");
                        g_unsetenv ("LC_TIME");
                        // LC_COLLATE
                        g_unsetenv ("LC_MONETARY");
                        // LC_MESSAGES
                        g_unsetenv ("LC_PAPER");
                        // LC_NAME
                        g_unsetenv ("LC_ADDRESS");
                        g_unsetenv ("LC_TELEPHONE");
                        g_unsetenv ("LC_MEASUREMENT");
                        // LC_IDENTIFICATION
                } else {
                        // LC_CTYPE
                        g_setenv ("LC_NUMERIC", region, TRUE);
                        g_setenv ("LC_TIME", region, TRUE);
                        // LC_COLLATE
                        g_setenv ("LC_MONETARY", region, TRUE);
                        // LC_MESSAGES
                        g_setenv ("LC_PAPER", region, TRUE);
                        // LC_NAME
                        g_setenv ("LC_ADDRESS", region, TRUE);
                        g_setenv ("LC_TELEPHONE", region, TRUE);
                        g_setenv ("LC_MEASUREMENT", region, TRUE);
                        // LC_IDENTIFICATION
                }
        }

        ibus_path = g_find_program_in_path ("ibus-daemon");
        if (ibus_path) {
                if (IS_STRING_EMPTY (g_getenv ("QT_IM_MODULE")))
                        g_setenv ("QT_IM_MODULE", "ibus", TRUE);

                if (IS_STRING_EMPTY (g_getenv ("XMODIFIERS")))
                        g_setenv ("XMODIFIERS", "@im=ibus", TRUE);
        }

        /* We want to use the GNOME menus which has the designed categories. */
        g_setenv ("XDG_MENU_PREFIX", "gnome-", TRUE);

        gsm_util_export_activation_environment (&error);
        if (error)
                g_warning ("Failed to upload environment to DBus: %s", error->message);
        g_clear_error (&error);

        gsm_util_export_user_environment (&error);
        if (error)
                g_warning ("Failed to upload environment to systemd: %s", error->message);
        g_clear_error (&error);
}

static gboolean
systemd_unit_is_active (GDBusConnection  *connection,
                        const char       *unit,
                        GError          **error)
{
        g_autoptr(GVariant) result = NULL;
        const char *object_path = NULL;
        g_autoptr(GDBusProxy) unit_proxy = NULL;
        const char *active_state = NULL;

        result = g_dbus_connection_call_sync (connection,
                                              "org.freedesktop.systemd1",
                                              "/org/freedesktop/systemd1",
                                              "org.freedesktop.systemd1.Manager",
                                              "GetUnit",
                                              g_variant_new ("(s)", unit),
                                              G_VARIANT_TYPE("(o)"),
                                              G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                              -1, NULL, error);
        if (result == NULL) {
                g_autofree char *remote_error = g_dbus_error_get_remote_error (*error);
                if (g_strcmp0 (remote_error, "org.freedesktop.systemd1.NoSuchUnit") == 0) {
                        g_clear_error (error);
                }
                return FALSE;
        }
        g_variant_get (result, "(&o)", &object_path);

        unit_proxy = g_dbus_proxy_new_sync (connection,
                                            G_DBUS_PROXY_FLAGS_NONE,
                                            NULL,
                                            "org.freedesktop.systemd1",
                                            object_path,
                                            "org.freedesktop.systemd1.Unit",
                                            NULL,
                                            error);
        if (unit_proxy == NULL)
                return FALSE;

        g_clear_pointer (&result, g_variant_unref);
        result = g_dbus_proxy_get_cached_property (unit_proxy, "ActiveState");
        if (result == NULL) {
                g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "Error getting ActiveState property");
                return FALSE;
        }

        g_variant_get (result, "&s", &active_state);
        return g_str_equal (active_state, "active");
}

static gboolean
systemd_start_unit (GDBusConnection  *connection,
                    const char       *unit,
                    const char       *mode,
                    GError          **error)
{
        g_autoptr(GVariant) reply = NULL;
        reply = g_dbus_connection_call_sync (connection,
                                             "org.freedesktop.systemd1",
                                             "/org/freedesktop/systemd1",
                                             "org.freedesktop.systemd1.Manager",
                                             "StartUnit",
                                             g_variant_new ("(ss)", unit, mode),
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                             -1, NULL, error);
        return reply != NULL;
}

static gboolean
systemd_reset_failed (GDBusConnection  *connection,
                      GError          **error)
{
        g_autoptr(GVariant) reply = NULL;
        reply = g_dbus_connection_call_sync (connection,
                                             "org.freedesktop.systemd1",
                                             "/org/freedesktop/systemd1",
                                             "org.freedesktop.systemd1.Manager",
                                             "ResetFailed",
                                             NULL,
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                             -1, NULL, error);
        return reply != NULL;
}

static gboolean
leader_term_or_int_signal_cb (gpointer data)
{
        Leader *ctx = data;

        g_debug ("Session termination requested");

        /* Start a shutdown explicitly. */
        systemd_start_unit (ctx->session_bus, "gnome-session-shutdown.target",
                            "replace-irreversibly", NULL);

        if (write (ctx->fifo_fd, "S", 1) < 0) {
                g_warning ("Failed to signal shutdown to monitor: %m");
                g_main_loop_quit (ctx->loop);
        }

        return G_SOURCE_REMOVE;
}

static void
graphical_session_pre_state_changed_cb (GDBusProxy *proxy,
                                        GVariant   *changed_properties,
                                        gpointer    data)
{
        Leader *ctx = data;
        g_autoptr (GVariant) value = NULL;
        const char *state;

        value = g_variant_lookup_value (changed_properties, "ActiveState", NULL);
        if (value == NULL)
                return;
        g_variant_get (value, "&s", &state);

        if (g_strcmp0 (state, "inactive") == 0) {
                g_debug ("Session services now inactive, quitting");
                g_main_loop_quit (ctx->loop);
        }
}

static gboolean
monitor_hangup_cb (int          fd,
                   GIOCondition condition,
                   gpointer     user_data)
{
        Leader *ctx = user_data;
        g_autoptr (GVariant) unit = NULL;
        const char *unit_path = NULL;
        GDBusProxy *proxy = NULL;
        g_autoptr (GVariant) value = NULL;
        g_autoptr (GError) error = NULL;

        g_debug ("Services have begun stopping, waiting for them to finish stopping");

        unit = g_dbus_connection_call_sync (ctx->session_bus,
                                            "org.freedesktop.systemd1",
                                            "/org/freedesktop/systemd1",
                                            "org.freedesktop.systemd1.Manager",
                                            "GetUnit",
                                            g_variant_new ("(s)", "graphical-session-pre.target"),
                                            G_VARIANT_TYPE ("(o)"),
                                            G_DBUS_CALL_FLAGS_NONE,
                                            -1,
                                            NULL,
                                            &error);
        if (!unit) {
                g_warning ("Could not get unit for graphical-session-pre.target: %s", error->message);
                g_main_loop_quit (ctx->loop);
                return G_SOURCE_REMOVE;
        }

        g_variant_get (unit, "(&o)", &unit_path);

        proxy = g_dbus_proxy_new_sync (ctx->session_bus,
                                       G_DBUS_PROXY_FLAGS_NONE,
                                       NULL,
                                       "org.freedesktop.systemd1",
                                       unit_path,
                                       "org.freedesktop.systemd1.Unit",
                                       NULL,
                                       &error);
        if (!proxy) {
                g_warning ("Could not get proxy for graphical-session-pre.target unit: %s", error->message);
                g_main_loop_quit (ctx->loop);
                return G_SOURCE_REMOVE;
        }

        value = g_dbus_proxy_get_cached_property (proxy, "ActiveState");

        if (value) {
                const char *state;

                g_variant_get (value, "&s", &state);

                if (g_strcmp0 (state, "inactive") == 0) {
                        g_debug ("State of graphical-session-pre.target unit already inactive quitting");
                        g_main_loop_quit (ctx->loop);
                        return G_SOURCE_REMOVE;
                }
                g_debug ("State of graphical-session-pre.target unit is '%s', waiting for it to go inactive", state);
        } else {
                g_debug ("State of graphical-session-pre.target unit is unknown, waiting for it to go inactive");
        }

        g_signal_connect (proxy,
                          "g-properties-changed",
                          G_CALLBACK (graphical_session_pre_state_changed_cb),
                          ctx);

        return G_SOURCE_REMOVE;
}

/**
 * run_leader:
 *
 * This is the session leader, i.e. it is the only process that's not managed
 * by the user's service manager (i.e. the systemd user instance). This process
 * is the one executed by GDM, and it's part of the session scope in the system
 * systemd instance. This process works in conjunction with a service running
 * within the user's service manager (i.e. `gnome-session-monitor.service`) to
 * implement the following:
 *
 * - When asked to shut down cleanly (i.e. via SIGTERM), the leader needs to
 *   bring down the session
 * - The leader needs to quit right after the session shuts down
 * - If the leader shuts down uncleanly, the session needs to shut down as well.
 *
 * This is achieved by opening a named FIFO in a well known location. If this
 * process receives SIGTERM or SIGINT then it will write a single byte, causing
 * the monitor service to signal STOPPING=1 to systemd. This triggers a clean
 * shutdown, solving the first item. To handle unclean shutdowns, we also wait
 * for EOF/HUP on both sides and quit if that signal is received.
 *
 * As an example, a shutdown might look as follows:
 *
 * - session-X.scope for user is stopped
 * - Leader process receives SIGTERM
 * - Leader sends single byte
 * - Monitor process receives byte and signals STOPPING=1
 * - Systemd user instance starts session teardown
 * - The last job that runs during session teardown is a stop job for the
 *   monitor process.
 * - Monitor process quits, closing FD in the process
 * - Leader process receives HUP and quits
 * - GDM sees the leader quit and cleans up its state in response.
 *
 * The result is that the session is stopped cleanly.
 */
static int
run_leader (char *session_name)
{
        g_autoptr (GError) error = NULL;
        g_auto (Leader) ctx = { .fifo_fd = -1 };
        g_autofree char *target = NULL;
        g_autofree char *fifo_path = NULL;
        struct stat statbuf;

        ctx.loop = g_main_loop_new (NULL, TRUE);

        ctx.session_bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
        if (ctx.session_bus == NULL)
                g_error ("Failed to obtain session bus: %s", error->message);

        if (IS_STRING_EMPTY (session_name)) {
                g_autoptr (GSettings) settings = NULL;
                settings = g_settings_new ("org.gnome.desktop.session");
                session_name = g_settings_get_string (settings, "session-name");
        }

        /* We don't escape the name (i.e. we leave any '-' intact). */
        target = g_strdup_printf ("gnome-session-%s@%s.target",
                                  g_getenv ("XDG_SESSION_TYPE"), session_name);

        if (systemd_unit_is_active (ctx.session_bus, target, &error))
                g_error ("Session manager is already running!");
        if (error != NULL)
                g_warning ("Failed to check if unit %s is active: %s",
                           target, error->message);
        g_clear_error (&error);

        /* Reset all failed units; we are going to start a lot of things and
         * really do not want to run into errors because units have failed
         * in a previous session */
        if (!systemd_reset_failed (ctx.session_bus, &error))
                g_warning ("Failed to reset failed state of units: %s", error->message);
        g_clear_error (&error);

        g_message ("Starting GNOME session target: %s", target);

        if (!systemd_start_unit (ctx.session_bus, target, "fail", &error))
                g_error ("Failed to start unit %s: %s", target, error->message);

        fifo_path = g_build_filename (g_get_user_runtime_dir (),
                                      "gnome-session-leader-fifo",
                                      NULL);
        if (mkfifo (fifo_path, 0666) < 0 && errno != EEXIST)
                g_warning ("Failed to create leader FIFO: %m");

        ctx.fifo_fd = g_open (fifo_path, O_WRONLY | O_CLOEXEC, 0666);
        if (ctx.fifo_fd < 0)
                g_error ("Failed to watch systemd session: open failed: %m");
        if (fstat (ctx.fifo_fd, &statbuf) < 0)
                g_error ("Failed to watch systemd session: fstat failed: %m");
        else if (!(statbuf.st_mode & S_IFIFO))
                g_error ("Failed to watch systemd session: FD is not a FIFO");

        g_unix_fd_add (ctx.fifo_fd, G_IO_HUP, (GUnixFDSourceFunc) monitor_hangup_cb, &ctx);
        g_unix_signal_add (SIGHUP, leader_term_or_int_signal_cb, &ctx);
        g_unix_signal_add (SIGTERM, leader_term_or_int_signal_cb, &ctx);
        g_unix_signal_add (SIGINT, leader_term_or_int_signal_cb, &ctx);

        g_debug ("Waiting for session to shutdown");
        g_main_loop_run (ctx.loop);
        return 0;
}

int
main (int argc, char **argv)
{
        g_autoptr (GError) error = NULL;
        g_autofree char *session_name = NULL;
        gboolean debug = FALSE;
        gboolean show_version = FALSE;
        gboolean disable_acceleration_check = FALSE;
        gboolean no_reexec = FALSE;
        g_autoptr (GOptionContext) options = NULL;
        GStrvBuilder *argv_dup_builder = NULL;
        g_auto (GStrv) argv_dup = NULL;

        GOptionEntry entries[] = {
                { "session", 0, 0, G_OPTION_ARG_STRING, &session_name, N_("Session to use"), N_("SESSION_NAME") },
                { "debug", 0, 0, G_OPTION_ARG_NONE, &debug, N_("Enable debugging code"), NULL },
                { "version", 0, 0, G_OPTION_ARG_NONE, &show_version, N_("Version of this application"), NULL },
                { "disable-acceleration-check", 0, 0, G_OPTION_ARG_NONE, &disable_acceleration_check, N_("This option is ignored"), NULL },
                { "no-reexec", 0, 0, G_OPTION_ARG_NONE, &no_reexec, N_("Don't re-exec into a login shell"), NULL },
                { NULL, 0, 0, 0, NULL, NULL, NULL }
        };

        /* Make sure that we have a session bus */
        if (!g_getenv ("DBUS_SESSION_BUS_ADDRESS"))
                g_error ("No session bus running! Cannot continue");

        /* Make sure we initialize gio in a way that does not autostart any daemon */
        initialize_gio ();

        setlocale (LC_ALL, "");
        bindtextdomain (GETTEXT_PACKAGE, LOCALE_DIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        argv_dup_builder = g_strv_builder_new ();
        for (size_t i = 0; i < argc; i++)
                g_strv_builder_add (argv_dup_builder, argv [i]);
        argv_dup = g_strv_builder_unref_to_strv (argv_dup_builder);

        options = g_option_context_new (_(" — the GNOME session manager"));
        g_option_context_add_main_entries (options, entries, GETTEXT_PACKAGE);
        if (!g_option_context_parse (options, &argc, &argv, &error))
                g_error ("%s", error->message);

        if (!debug) {
                const char *debug_string = g_getenv ("GNOME_SESSION_DEBUG");
                if (debug_string != NULL)
                        debug = atoi (debug_string) == 1;
        }
        g_log_set_debug_enabled (debug);

        if (show_version) {
                g_print ("%s %s\n", argv [0], VERSION);
                return 0;
        }

        /* Make sure that we were launched through a login shell. This ensures
         * that the user's shell profiles are sourced when logging into GNOME */
        if (!no_reexec)
                maybe_reexec_with_login_shell (argv_dup);

        if (disable_acceleration_check)
                g_warning ("Wayland assumes that acceleration works, so --disable-acceleration-check is deprecated!");

        set_up_environment ();

        return run_leader (session_name);
}
