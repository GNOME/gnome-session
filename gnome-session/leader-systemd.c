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

#include <glib.h>
#include <glib-unix.h>
#include <gio/gio.h>

#include "gsm-util.h"

typedef struct {
        GDBusConnection *session_bus;
        GMainLoop *loop;
        int fifo_fd;
        GDBusProxy *awaiting_shutdown;
} Leader;

static void
leader_clear (Leader *ctx)
{
        g_clear_object (&ctx->session_bus);
        g_clear_pointer (&ctx->loop, g_main_loop_unref);
        g_close (ctx->fifo_fd, NULL);
        g_clear_object (&ctx->awaiting_shutdown);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (Leader, leader_clear);

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
graphical_session_pre_state_changed_cb (GDBusProxy  *proxy,
                                        GVariant    *changed_properties,
                                        char       **invalidated_properties,
                                        gpointer     data)
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
        g_autoptr (GDBusProxy) proxy = NULL;
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

        ctx->awaiting_shutdown = g_steal_pointer (&proxy);

        return G_SOURCE_REMOVE;
}

/**
 * This is the session leader, i.e. it is the only process that's not managed
 * by the systemd user instance. This process is the one executed by GDM, and
 * it is part of the session scope in the system systemd instance. This process
 * works in conjunction with a service running within the user's service manager
 * (i.e. `gnome-session-monitor.service`) to implement the following:
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
int
main (int argc, char **argv)
{
        g_autoptr (GError) error = NULL;
        g_auto (Leader) ctx = { .fifo_fd = -1 };
        const char *session_name = NULL;
        const char *debug_string = NULL;
        g_autofree char *target = NULL;
        g_autofree char *fifo_path = NULL;
        struct stat statbuf;

        if (argc < 2)
            g_error ("No session name was specified");
        session_name = argv[1];

        debug_string = g_getenv ("GNOME_SESSION_DEBUG");
        if (debug_string != NULL)
            g_log_set_debug_enabled (atoi (debug_string) == 1);

        gsm_util_export_user_environment (&error);
        if (error)
                g_warning ("Failed to upload environment to systemd: %s", error->message);
        g_clear_error (&error);

        ctx.loop = g_main_loop_new (NULL, TRUE);

        ctx.session_bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
        if (ctx.session_bus == NULL)
                g_error ("Failed to obtain session bus: %s", error->message);

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
