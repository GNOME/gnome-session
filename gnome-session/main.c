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

#include <libintl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#include <glib/gi18n.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <glib-unix.h>
#include <gio/gio.h>

#include "gdm-log.h"

#include "gsm-util.h"
#include "gsm-manager.h"
#include "gsm-session-fill.h"
#include "gsm-store.h"
#include "gsm-system.h"

#include <systemd/sd-journal.h>

#define GSM_DBUS_NAME "org.gnome.SessionManager"

static gboolean systemd_service = FALSE;
static gboolean show_version = FALSE;
static gboolean debug = FALSE;
static gboolean disable_acceleration_check = FALSE;
static const char *session_name = NULL;
static GsmManager *manager = NULL;

static GMainLoop *loop;

void
gsm_quit (void)
{
        g_main_loop_quit (loop);
}

static void
gsm_main (void)
{
        if (loop == NULL)
                loop = g_main_loop_new (NULL, TRUE);

        g_main_loop_run (loop);
}

static void
on_name_lost (GDBusConnection *connection,
              const char *name,
              gpointer    data)
{
        if (connection == NULL) {
                if (gsm_manager_get_dbus_disconnected (manager))
                        gsm_quit ();
                else {
                        g_warning ("Lost name on bus: %s", name);
                        gsm_fail_whale_dialog_we_failed (TRUE, TRUE, NULL);
                }
        } else {
                g_debug ("Calling name lost callback function");

                /*
                 * When the signal handler gets a shutdown signal, it calls
                 * this function to inform GsmManager to not restart
                 * applications in the off chance a handler is already queued
                 * to dispatch following the below call to gtk_main_quit.
                 */
                gsm_manager_set_phase (manager, GSM_MANAGER_PHASE_EXIT);

                gsm_quit ();
        }
}

static gboolean
term_or_int_signal_cb (gpointer data)
{
        g_autoptr(GError) error = NULL;
        GsmManager *manager = (GsmManager *)data;

        /* let the fatal signals interrupt us */
        g_debug ("Caught SIGINT/SIGTERM, shutting down normally.");

        if (!gsm_manager_logout (manager, GSM_MANAGER_LOGOUT_MODE_FORCE, &error)) {
                if (g_error_matches (error, GSM_MANAGER_ERROR, GSM_MANAGER_ERROR_NOT_IN_RUNNING)) {
                    gsm_quit ();
                    return FALSE;
                }

                g_critical ("Failed to log out: %s", error->message);
        }

        return FALSE;
}

static gboolean
sigusr2_cb (gpointer data)
{
        g_debug ("-------- MARK --------");
        return TRUE;
}

static gboolean
sigusr1_cb (gpointer data)
{
        gdm_log_toggle_debug ();
        return TRUE;
}

static void
on_name_acquired (GDBusConnection *connection,
                  const char *name,
                  gpointer data)
{
        gsm_manager_start (manager);
}

static void
create_manager (void)
{
        GsmStore *client_store;

        client_store = gsm_store_new ();
        manager = gsm_manager_new (client_store, FALSE, systemd_service);
        g_object_unref (client_store);

        g_unix_signal_add (SIGTERM, term_or_int_signal_cb, manager);
        g_unix_signal_add (SIGINT, term_or_int_signal_cb, manager);
        g_unix_signal_add (SIGUSR1, sigusr1_cb, manager);
        g_unix_signal_add (SIGUSR2, sigusr2_cb, manager);

        if (IS_STRING_EMPTY (session_name)) {
                session_name = _gsm_manager_get_default_session (manager);
        }

        if (!gsm_session_fill (manager, session_name)) {
                gsm_fail_whale_dialog_we_failed (FALSE, TRUE, NULL);
        }
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const char *name,
                 gpointer data)
{
        create_manager ();
}

static guint
acquire_name (void)
{
        return g_bus_own_name (G_BUS_TYPE_SESSION,
                               GSM_DBUS_NAME,
                               G_BUS_NAME_OWNER_FLAGS_NONE,
                               on_bus_acquired,
                               on_name_acquired,
                               on_name_lost,
                               NULL, NULL);
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

static gboolean
leader_term_or_int_signal_cb (gpointer data)
{
        gint fifo_fd = GPOINTER_TO_INT (data);

        g_debug ("Session termination requested");

        /* Start a shutdown explicitly. */
        gsm_util_start_systemd_unit ("gnome-session-shutdown.target", "replace-irreversibly", NULL);

        if (fifo_fd >= 0) {
                int res;

                /* If we have a fifo, try to signal the other side. */
                res = write (fifo_fd, "S", 1);
                if (res < 0) {
                        g_warning ("Error signaling shutdown to monitoring process: %m");
                        gsm_quit ();
                }
        } else {
                /* Otherwise quit immediately as we cannot wait on systemd */
                gsm_quit ();
        }

        return G_SOURCE_REMOVE;
}

static void
graphical_session_pre_state_changed_cb (GDBusProxy *proxy,
                                        GVariant   *changed_properties)
{
        const char *state;
        g_autoptr (GVariant) value = NULL;

        value = g_variant_lookup_value (changed_properties, "ActiveState", NULL);

        if (value == NULL)
                return;

        g_variant_get (value, "&s", &state);
        if (g_strcmp0 (state, "inactive") == 0) {
                g_debug ("Session services now inactive, quitting");
                gsm_quit ();
                return;
        }
}

static gboolean
monitor_hangup_cb (int          fd,
                   GIOCondition condition,
                   gpointer     user_data)
{
        g_autoptr (GDBusConnection) connection = NULL;
        g_autoptr (GVariant) unit = NULL;
        g_autoptr (GVariant) value = NULL;
        g_autoptr (GError) error = NULL;
        GDBusProxy *proxy = NULL;
        const char *unit_path = NULL;

        g_debug ("Services have begun stopping, waiting for them to finish stopping");

        connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);

        if (!connection) {
                g_debug ("Could not get bus connection: %s", error->message);
                gsm_quit ();
                return G_SOURCE_REMOVE;
        }

        unit = g_dbus_connection_call_sync (connection,
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
                g_debug ("Could not get unit for graphical-session-pre.target: %s", error->message);
                gsm_quit ();
                return G_SOURCE_REMOVE;
        }

        g_variant_get (unit, "(&o)", &unit_path);

        proxy = g_dbus_proxy_new_sync (connection,
                                       G_DBUS_PROXY_FLAGS_NONE,
                                       NULL,
                                       "org.freedesktop.systemd1",
                                       unit_path,
                                       "org.freedesktop.systemd1.Unit",
                                       NULL,
                                       &error);
        if (!proxy) {
                g_debug ("Could not get proxy for graphical-session-pre.target unit: %s", error->message);
                gsm_quit ();
                return G_SOURCE_REMOVE;
        }

        value = g_dbus_proxy_get_cached_property (proxy, "ActiveState");

        if (value) {
                const char *state;

                g_variant_get (value, "&s", &state);

                if (g_strcmp0 (state, "inactive") == 0) {
                        g_debug ("State of graphical-session-pre.target unit already inactive quitting");
                        gsm_quit ();
                        return G_SOURCE_REMOVE;
                }
                g_debug ("State of graphical-session-pre.target unit is '%s', waiting for it to go inactive", state);
        } else {
                g_debug ("State of graphical-session-pre.target unit is unknown, waiting for it to go inactive");
        }
        g_signal_connect (proxy,
                          "g-properties-changed",
                          G_CALLBACK (graphical_session_pre_state_changed_cb),
                          NULL);

        return G_SOURCE_REMOVE;
}

/**
 * systemd_leader_run:
 *
 * This is the session leader when running under systemd, i.e. it is the only
 * process that is *not* managed by the systemd user instance, this process
 * is the one executed by the GDM helpers and is part of the session scope in
 * the system systemd instance.
 *
 * This process works together with a service running in the user systemd
 * instance (currently gnome-session-ctl@monitor.service):
 *
 * - It needs to signal shutdown to the user session when receiving SIGTERM
 *
 * - It needs to quit just after the user session is done
 *
 * - The monitor instance needs to know if this process was killed
 *
 * All this is achieved by opening a named fifo in a well known location.
 * If this process receives SIGTERM or SIGINT then it will write a single byte
 * causing the monitor service to signal STOPPING=1 to systemd, triggering a
 * clean shutdown, solving the first item. The other two items are solved by
 * waiting for EOF/HUP on both sides and quitting immediately when receiving
 * that signal.
 *
 * As an example, a shutdown might look as follows:
 *
 * - session-X.scope for user is stopped
 * - Leader process receive SIGTERM
 * - Leader sends single byte
 * - Monitor process receives byte and signals STOPPING=1
 * - Systemd user instance starts session teardown
 * - Session is torn down, last job run is stopping monitor process (SIGTERM)
 * - Monitor process quits, closing FD in the process
 * - Leader process receives HUP and quits
 * - GDM shuts down its processes in the users scope
 *
 * The result is that the session is stopped cleanly.
 */
static void
systemd_leader_run(void)
{
        g_autofree char *fifo_name = NULL;
        int res;
        int fifo_fd;

        fifo_name = g_strdup_printf ("%s/gnome-session-leader-fifo",
                                     g_get_user_runtime_dir ());
        res = mkfifo (fifo_name, 0666);
        if (res < 0 && errno != EEXIST)
                g_warning ("Error creating FIFO: %m");

        fifo_fd = g_open (fifo_name, O_WRONLY | O_CLOEXEC, 0666);
        if (fifo_fd >= 0) {
                struct stat buf;

                res = fstat (fifo_fd, &buf);
                if (res < 0) {
                        g_warning ("Unable to watch systemd session: fstat failed with %m");
                        close (fifo_fd);
                        fifo_fd = -1;
                } else if (!(buf.st_mode & S_IFIFO)) {
                        g_warning ("Unable to watch systemd session: FD is not a FIFO");
                        close (fifo_fd);
                        fifo_fd = -1;
                } else {
                        g_unix_fd_add (fifo_fd, G_IO_HUP, (GUnixFDSourceFunc) monitor_hangup_cb, NULL);
                }
        } else {
                g_warning ("Unable to watch systemd session: Opening FIFO failed with %m");
        }

        g_unix_signal_add (SIGHUP, leader_term_or_int_signal_cb, GINT_TO_POINTER (fifo_fd));
        g_unix_signal_add (SIGTERM, leader_term_or_int_signal_cb, GINT_TO_POINTER (fifo_fd));
        g_unix_signal_add (SIGINT, leader_term_or_int_signal_cb, GINT_TO_POINTER (fifo_fd));

        /* Sleep until we receive HUP or are killed. */
        gsm_main ();
        exit(0);
}

int
main (int argc, char **argv)
{
        GError           *error = NULL;
        static char     **override_autostart_dirs = NULL;
        static char      *opt_session_name = NULL;
        const char       *debug_string = NULL;
        const char       *env_override_autostart_dirs = NULL;
        g_auto(GStrv)     env_override_autostart_dirs_v = NULL;
        guint             name_owner_id;
        GOptionContext   *options;
        static GOptionEntry entries[] = {
                { "systemd-service", 0, 0, G_OPTION_ARG_NONE, &systemd_service, N_("Running as systemd service"), NULL },
                { "autostart", 'a', 0, G_OPTION_ARG_STRING_ARRAY, &override_autostart_dirs, N_("Override standard autostart directories"), N_("AUTOSTART_DIR") },
                { "session", 0, 0, G_OPTION_ARG_STRING, &opt_session_name, N_("Session to use"), N_("SESSION_NAME") },
                { "debug", 0, 0, G_OPTION_ARG_NONE, &debug, N_("Enable debugging code"), NULL },
                { "version", 0, 0, G_OPTION_ARG_NONE, &show_version, N_("Version of this application"), NULL },
                { "disable-acceleration-check", 0, 0, G_OPTION_ARG_NONE, &disable_acceleration_check, N_("This option is ignored"), NULL },
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

        debug_string = g_getenv ("GNOME_SESSION_DEBUG");
        if (debug_string != NULL) {
                debug = atoi (debug_string) == 1;
        }

        error = NULL;
        options = g_option_context_new (_(" — the GNOME session manager"));
        g_option_context_add_main_entries (options, entries, GETTEXT_PACKAGE);
        g_option_context_parse (options, &argc, &argv, &error);
        if (error != NULL) {
                g_warning ("%s", error->message);
                exit (1);
        }

        g_option_context_free (options);

        if (show_version) {
                g_print ("%s %s\n", argv [0], VERSION);
                exit (0);
        }

        if (disable_acceleration_check)
                g_warning ("Wayland assumes that acceleration works, so --disable-acceleration-check is deprecated!");

        /* Rebind stdout/stderr to the journal explicitly, so that
         * journald picks ups the nicer "gnome-session" as the program
         * name instead of whatever shell script GDM happened to use.
         */
        if (!debug) {
                int journalfd;

                journalfd = sd_journal_stream_fd (PACKAGE, LOG_INFO, 0);
                if (journalfd >= 0) {
                        dup2(journalfd, 1);
                        dup2(journalfd, 2);
                }
        }

        gdm_log_init ();
        gdm_log_set_debug (debug);

        env_override_autostart_dirs = g_getenv ("GNOME_SESSION_AUTOSTART_DIR");

        if (env_override_autostart_dirs != NULL && env_override_autostart_dirs[0] != '\0') {
                env_override_autostart_dirs_v = g_strsplit (env_override_autostart_dirs, ":", 0);
                gsm_util_set_autostart_dirs (env_override_autostart_dirs_v);
        } else {
                gsm_util_set_autostart_dirs (override_autostart_dirs);

                /* Export the override autostart dirs parameter to the environment
                 * in case we are running on systemd. */
                if (override_autostart_dirs) {
                        g_autofree char *autostart_dirs = NULL;
                        autostart_dirs = g_strjoinv (":", override_autostart_dirs);
                        g_setenv ("GNOME_SESSION_AUTOSTART_DIR", autostart_dirs, TRUE);
                }
        }

        gsm_util_export_activation_environment (&error);
        if (error) {
                g_warning ("Failed to upload environment to DBus: %s", error->message);
                g_clear_error (&error);
        }

        session_name = opt_session_name;

        gsm_util_export_user_environment (&error);
        if (error && !g_getenv ("RUNNING_UNDER_GDM"))
                g_warning ("Failed to upload environment to systemd: %s", error->message);
        g_clear_error (&error);

        if (!systemd_service) {
                const gchar *session_type = NULL;
                g_autofree gchar *gnome_session_target = NULL;

                session_type = g_getenv ("XDG_SESSION_TYPE");

                /* We really need to resolve the session name at this point,
                 * which requires talking to GSettings internally. */
                if (IS_STRING_EMPTY (session_name)) {
                        session_name = _gsm_manager_get_default_session (NULL);
                }

                /* We don't escape the name (i.e. we leave any '-' intact). */
                gnome_session_target = g_strdup_printf ("gnome-session-%s@%s.target", session_type, session_name);

                if (!gsm_util_systemd_unit_is_active (gnome_session_target, &error)) {
                        if (error != NULL) {
                                g_warning ("Could not check if unit %s is active: %s",
                                           gnome_session_target, error->message);
                                g_clear_error (&error);
                        }
                        /* Reset all failed units; we are going to start a lof ot things and
                         * really do not want to run into errors because units have failed
                         * in a previous session
                         */
                        gsm_util_systemd_reset_failed (&error);
                        if (error && !g_getenv ("RUNNING_UNDER_GDM"))
                                g_warning ("Failed to reset failed state of units: %s", error->message);
                        g_clear_error (&error);

                        if (gsm_util_start_systemd_unit (gnome_session_target, "fail", &error)) {
                                /* We started the unit, open fifo and sleep forever. */
                                systemd_leader_run ();
                                exit (0);
                        }

                        /* We could not start the unit, fall back. */
                        if (g_getenv ("RUNNING_UNDER_GDM"))
                                g_message ("Falling back to non-systemd startup procedure. This is expected to happen for GDM sessions.");
                        else
                                g_warning ("Falling back to non-systemd startup procedure due to error: %s", error->message);
                        g_clear_error (&error);
                } else {
                        g_warning ("Session manager already running!");
                        exit (1);
                }
        }

        {
                gchar *ibus_path;

                ibus_path = g_find_program_in_path("ibus-daemon");

                if (ibus_path) {
                        const gchar *p;
                        p = g_getenv ("QT_IM_MODULE");
                        if (!p || !*p)
                                p = "ibus";
                        gsm_util_setenv ("QT_IM_MODULE", p);
                        p = g_getenv ("XMODIFIERS");
                        if (!p || !*p)
                                p = "@im=ibus";
                        gsm_util_setenv ("XMODIFIERS", p);
                }

                g_free (ibus_path);
        }

        /* We want to use the GNOME menus which has the designed categories.
         */
        gsm_util_setenv ("XDG_MENU_PREFIX", "gnome-");

        /* Talk to logind before acquiring a name, since it does synchronous
         * calls at initialization time that invoke a main loop and if we
         * already owned a name, then we would service too early during
         * that main loop.
         */
        g_object_unref (gsm_get_system ());

        name_owner_id  = acquire_name ();

        gsm_main ();

        gsm_util_export_user_environment (NULL);

        g_clear_object (&manager);
        g_free (gl_renderer);

        g_bus_unown_name (name_owner_id);
        gdm_log_shutdown ();

        return 0;
}
