/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 * gnome-session-tl.c - Small utility program to manage gnome systemd session.

   Copyright (C) 1998 Tom Tromey
   Copyright (C) 2008,2019 Red Hat, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>

#include <locale.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <systemd/sd-daemon.h>

#include <glib.h>
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#define GSM_SERVICE_DBUS   "org.gnome.SessionManager"
#define GSM_PATH_DBUS      "/org/gnome/SessionManager"
#define GSM_INTERFACE_DBUS "org.gnome.SessionManager"

#define SYSTEMD_DBUS            "org.freedesktop.systemd1"
#define SYSTEMD_PATH_DBUS       "/org/freedesktop/systemd1"
#define SYSTEMD_INTERFACE_DBUS  "org.freedesktop.systemd1.Manager"

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
do_signal_init (void)
{
        g_autoptr(GDBusConnection) connection = NULL;
        g_autoptr(GVariant) reply = NULL;
        g_autoptr(GError) error = NULL;

        connection = get_session_bus ();
        if (connection == NULL)
                return;

        reply = g_dbus_connection_call_sync (connection,
                                             GSM_SERVICE_DBUS,
                                             GSM_PATH_DBUS,
                                             GSM_INTERFACE_DBUS,
                                             "Initialized",
                                             NULL,
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                             -1, NULL, &error);

        if (error != NULL)
                g_warning ("Failed to call signal initialization: %s",
                           error->message);
}

static void
do_start_unit (const gchar *unit, const char *mode)
{
        g_autoptr(GDBusConnection) connection = NULL;
        g_autoptr(GVariant) reply = NULL;
        g_autoptr(GError) error = NULL;

        connection = get_session_bus ();
        if (connection == NULL)
                return;

        reply = g_dbus_connection_call_sync (connection,
                                             SYSTEMD_DBUS,
                                             SYSTEMD_PATH_DBUS,
                                             SYSTEMD_INTERFACE_DBUS,
                                             "StartUnit",
                                             g_variant_new ("(ss)",
                                                            unit,
                                                            mode),
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                             -1, NULL, &error);

        if (error != NULL)
                g_warning ("Failed to start shutdown target: %s",
                           error->message);
}

static void
do_restart_dbus (void)
{
        g_autoptr(GDBusConnection) connection = NULL;
        g_autoptr(GVariant) reply = NULL;
        g_autoptr(GError) error = NULL;

        connection = get_session_bus ();
        if (connection == NULL)
                return;

        reply = g_dbus_connection_call_sync (connection,
                                             SYSTEMD_DBUS,
                                             SYSTEMD_PATH_DBUS,
                                             SYSTEMD_INTERFACE_DBUS,
                                             "StopUnit",
                                             g_variant_new ("(ss)",
                                                            "dbus.service",
                                                            "fail"),
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                             -1, NULL, &error);

        if (error != NULL)
                g_warning ("Failed to restart DBus service: %s",
                           error->message);
}

typedef struct {
        GMainLoop *loop;
        gint fifo_fd;
} MonitorLeader;

static gboolean
leader_term_or_int_signal_cb (gpointer user_data)
{
        MonitorLeader *data = (MonitorLeader*) user_data;

        g_main_loop_quit (data->loop);

        return G_SOURCE_REMOVE;
}

static gboolean
leader_fifo_io_cb (gint fd,
                   GIOCondition condition,
                   gpointer user_data)
{
        MonitorLeader *data = (MonitorLeader*) user_data;

        sd_notify (0, "STOPPING=1");

        if (condition & G_IO_IN) {
                char buf[1];
                read (data->fifo_fd, buf, 1);
                g_main_loop_quit (data->loop);
        }

        if (condition & G_IO_HUP) {
                g_main_loop_quit (data->loop);
        }

        return G_SOURCE_CONTINUE;
}

/**
 * do_monitor_leader:
 *
 * Function to monitor the leader to ensure clean session shutdown and
 * propagation of this information to/from loginctl/GDM.
 * See main.c systemd_leader_run() for more information.
 */
static void
do_monitor_leader (void)
{
        MonitorLeader data;
        g_autofree char *fifo_name = NULL;
        int res;

        data.loop = g_main_loop_new (NULL, TRUE);

        fifo_name = g_strdup_printf ("%s/gnome-session-leader-fifo",
                                     g_get_user_runtime_dir ());
        res = mkfifo (fifo_name, 0666);
        if (res < 0 && errno != EEXIST)
                g_warning ("Error creating FIFO: %m");

        data.fifo_fd = g_open (fifo_name, O_RDONLY | O_CLOEXEC, 0666);
        if (data.fifo_fd >= 0) {
                struct stat buf;

                res = fstat (data.fifo_fd, &buf);
                if (res < 0) {
                        g_warning ("Unable to monitor session leader: stat failed with error %m");
                        sd_notifyf (0, "STATUS=Unable to monitor session leader: FD is not a FIFO %m");
                        close (data.fifo_fd);
                        data.fifo_fd = -1;
                } else if (!(buf.st_mode & S_IFIFO)) {
                        g_warning ("Unable to monitor session leader: FD is not a FIFO");
                        sd_notify (0, "STATUS=Unable to monitor session leader: FD is not a FIFO");
                        close (data.fifo_fd);
                        data.fifo_fd = -1;
                } else {
                        sd_notify (0, "STATUS=Watching session leader");
                        g_unix_fd_add (data.fifo_fd, G_IO_HUP | G_IO_IN, leader_fifo_io_cb, &data);
                }
        } else {
                g_warning ("Unable to monitor session leader: Opening FIFO failed with %m");
                sd_notifyf (0, "STATUS=Unable to monitor session leader: Opening FIFO failed with %m");
        }

        g_unix_signal_add (SIGTERM, leader_term_or_int_signal_cb, &data);
        g_unix_signal_add (SIGINT, leader_term_or_int_signal_cb, &data);

        g_main_loop_run (data.loop);

        g_main_loop_unref (data.loop);
        /* FD is closed with the application. */
}

int
main (int argc, char *argv[])
{
        g_autoptr(GError) error = NULL;
        static gboolean   opt_shutdown;
        static gboolean   opt_monitor;
        static gboolean   opt_signal_init;
        static gboolean   opt_restart_dbus;
        static gboolean   opt_exec_stop_check;
        int     conflicting_options;
        GOptionContext *ctx;
        static const GOptionEntry options[] = {
                { "shutdown", '\0', 0, G_OPTION_ARG_NONE, &opt_shutdown, N_("Start gnome-session-shutdown.target"), NULL },
                { "monitor", '\0', 0, G_OPTION_ARG_NONE, &opt_monitor, N_("Start gnome-session-shutdown.target when receiving EOF or a single byte on stdin"), NULL },
                { "signal-init", '\0', 0, G_OPTION_ARG_NONE, &opt_signal_init, N_("Signal initialization done to gnome-session"), NULL },
                { "restart-dbus", '\0', 0, G_OPTION_ARG_NONE, &opt_restart_dbus, N_("Restart dbus.service if it is running"), NULL },
                { "exec-stop-check", '\0', 0, G_OPTION_ARG_NONE, &opt_exec_stop_check, N_("Run from ExecStopPost to start gnome-session-shutdown.target on service failure"), NULL },
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
        g_option_context_free (ctx);

        conflicting_options = 0;
        if (opt_shutdown)
                conflicting_options++;
        if (opt_monitor)
                conflicting_options++;
        if (opt_signal_init)
                conflicting_options++;
        if (opt_restart_dbus)
                conflicting_options++;
        if (opt_exec_stop_check)
                conflicting_options++;
        if (conflicting_options != 1) {
                g_printerr (_("Program needs exactly one parameter"));
                exit (1);
        }

        sd_notify (0, "READY=1");

        if (opt_signal_init) {
                do_signal_init ();
        } else if (opt_restart_dbus) {
                do_restart_dbus ();
        } else if (opt_shutdown) {
                do_start_unit ("gnome-session-shutdown.target", "replace-irreversibly");
        } else if (opt_monitor) {
                do_monitor_leader ();
                do_start_unit ("gnome-session-shutdown.target", "replace-irreversibly");
        } else if (opt_exec_stop_check) {
                /* Start failed target if the restart limit was hit */
                if (g_strcmp0 ("start-limit-hit", g_getenv ("SERVICE_RESULT")) == 0) {
                        do_start_unit ("gnome-session-shutdown.target", "fail");
                }
       } else {
                g_assert_not_reached ();
        }

        return 0;
}
