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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <config.h>

#include <libintl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <glib/gi18n.h>
#include <glib.h>
#include <gtk/gtk.h>

#include <glib-unix.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-bindings.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "gdm-log.h"

#include "gsm-util.h"
#include "gsm-manager.h"
#include "gsm-session-fill.h"
#include "gsm-store.h"
#include "gsm-system.h"
#include "gsm-fail-whale.h"

#ifdef HAVE_SYSTEMD
#include <systemd/sd-journal.h>
#endif

#define GSM_DBUS_NAME "org.gnome.SessionManager"

static gboolean failsafe = FALSE;
static gboolean show_version = FALSE;
static gboolean debug = FALSE;
static gboolean please_fail = FALSE;

static DBusGProxy *bus_proxy = NULL;

static void shutdown_cb (gpointer data);

static void
on_bus_name_lost (DBusGProxy *bus_proxy,
                  const char *name,
                  gpointer    data)
{
        g_warning ("Lost name on bus: %s, exiting", name);
        exit (1);
}

static gboolean
acquire_name_on_proxy (DBusGProxy *bus_proxy,
                       const char *name)
{
        GError     *error;
        guint       result;
        gboolean    res;
        gboolean    ret;

        ret = FALSE;

        if (bus_proxy == NULL) {
                goto out;
        }

        error = NULL;
        res = dbus_g_proxy_call (bus_proxy,
                                 "RequestName",
                                 &error,
                                 G_TYPE_STRING, name,
                                 G_TYPE_UINT, 0,
                                 G_TYPE_INVALID,
                                 G_TYPE_UINT, &result,
                                 G_TYPE_INVALID);
        if (! res) {
                if (error != NULL) {
                        g_warning ("Failed to acquire %s: %s", name, error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to acquire %s", name);
                }
                goto out;
        }

        if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
                if (error != NULL) {
                        g_warning ("Failed to acquire %s: %s", name, error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to acquire %s", name);
                }
                goto out;
        }

        /* register for name lost */
        dbus_g_proxy_add_signal (bus_proxy,
                                 "NameLost",
                                 G_TYPE_STRING,
                                 G_TYPE_INVALID);
        dbus_g_proxy_connect_signal (bus_proxy,
                                     "NameLost",
                                     G_CALLBACK (on_bus_name_lost),
                                     NULL,
                                     NULL);


        ret = TRUE;

 out:
        return ret;
}

static gboolean
acquire_name (void)
{
        GError          *error;
        DBusGConnection *connection;

        error = NULL;
        connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
        if (connection == NULL) {
                gsm_util_init_error (TRUE,
                                     "Could not connect to session bus: %s",
                                     error->message);
                return FALSE;
        }

        bus_proxy = dbus_g_proxy_new_for_name_owner (connection,
                                                     DBUS_SERVICE_DBUS,
                                                     DBUS_PATH_DBUS,
                                                     DBUS_INTERFACE_DBUS,
                                                     &error);
        if (error != NULL) {
                gsm_util_init_error (TRUE,
                                     "Could not connect to session bus: %s",
                                     error->message);
                return FALSE;
        }

        if (! acquire_name_on_proxy (bus_proxy, GSM_DBUS_NAME) ) {
                gsm_util_init_error (TRUE,
                                     "%s",
                                     "Could not acquire name on session bus");
                return FALSE;
        }

        return TRUE;
}

static gboolean
term_or_int_signal_cb (gpointer data)
{
        GsmManager *manager = (GsmManager *)data;

        /* let the fatal signals interrupt us */
        g_debug ("Caught SIGINT/SIGTERM, shutting down normally.");

        gsm_manager_logout (manager, GSM_MANAGER_LOGOUT_MODE_FORCE, NULL);

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
shutdown_cb (gpointer data)
{
        GsmManager *manager = (GsmManager *)data;
        g_debug ("Calling shutdown callback function");

        /*
         * When the signal handler gets a shutdown signal, it calls
         * this function to inform GsmManager to not restart
         * applications in the off chance a handler is already queued
         * to dispatch following the below call to gtk_main_quit.
         */
        gsm_manager_set_phase (manager, GSM_MANAGER_PHASE_EXIT);

        gtk_main_quit ();
}

static gboolean
require_dbus_session (int      argc,
                      char   **argv,
                      GError **error)
{
        char **new_argv;
        int    i;

        if (g_getenv ("DBUS_SESSION_BUS_ADDRESS"))
                return TRUE;

        /* Just a sanity check to prevent infinite recursion if
         * dbus-launch fails to set DBUS_SESSION_BUS_ADDRESS 
         */
        g_return_val_if_fail (!g_str_has_prefix (argv[0], "dbus-launch"),
                              TRUE);

        /* +2 for our new arguments, +1 for NULL */
        new_argv = g_malloc (argc + 3 * sizeof (*argv));

        new_argv[0] = "dbus-launch";
        new_argv[1] = "--exit-with-session";
        for (i = 0; i < argc; i++) {
                new_argv[i + 2] = argv[i];
	}
        new_argv[i + 2] = NULL;
        
        if (!execvp ("dbus-launch", new_argv)) {
                g_set_error (error, 
                             G_SPAWN_ERROR,
                             G_SPAWN_ERROR_FAILED,
                             "No session bus and could not exec dbus-launch: %s",
                             g_strerror (errno));
                return FALSE;
        }

        /* Should not be reached */
        return TRUE;
}

static gboolean
check_gl (GError **error)
{
        int status;
        char *argv[] = { LIBEXECDIR "/gnome-session-check-accelerated", NULL };

        if (!g_spawn_sync (NULL, (char **) argv, NULL, 0, NULL, NULL, NULL, NULL,
                           &status, error)) {
                return FALSE;
        }

        return g_spawn_check_exit_status (status, error);
}

int
main (int argc, char **argv)
{
        GError           *error = NULL;
        char             *display_str;
        GsmManager       *manager;
        GsmStore         *client_store;
        static char     **override_autostart_dirs = NULL;
        static char      *opt_session_name = NULL;
        const char       *session_name;
        gboolean          gl_failed = FALSE;
        static GOptionEntry entries[] = {
                { "autostart", 'a', 0, G_OPTION_ARG_STRING_ARRAY, &override_autostart_dirs, N_("Override standard autostart directories"), N_("AUTOSTART_DIR") },
                { "session", 0, 0, G_OPTION_ARG_STRING, &opt_session_name, N_("Session to use"), N_("SESSION_NAME") },
                { "debug", 0, 0, G_OPTION_ARG_NONE, &debug, N_("Enable debugging code"), NULL },
                { "failsafe", 'f', 0, G_OPTION_ARG_NONE, &failsafe, N_("Do not load user-specified applications"), NULL },
                { "version", 0, 0, G_OPTION_ARG_NONE, &show_version, N_("Version of this application"), NULL },
                /* Translators: the 'fail whale' is the black dialog we show when something goes seriously wrong */
                { "whale", 0, 0, G_OPTION_ARG_NONE, &please_fail, N_("Show the fail whale dialog for testing"), NULL },
                { NULL, 0, 0, 0, NULL, NULL, NULL }
        };

        /* Make sure that we have a session bus */
        if (!require_dbus_session (argc, argv, &error)) {
                gsm_util_init_error (TRUE, "%s", error->message);
        }

        /* Check GL, if it doesn't work out then force software fallback */
        if (!check_gl (&error)) {
                gl_failed = TRUE;

                g_debug ("hardware acceleration check failed: %s",
                         error? error->message : "");
                g_clear_error (&error);
                if (g_getenv ("LIBGL_ALWAYS_SOFTWARE") == NULL) {
                        g_setenv ("LIBGL_ALWAYS_SOFTWARE", "1", TRUE);
                        if (!check_gl (&error)) {
                                g_warning ("software acceleration check failed: %s",
                                           error? error->message : "");
                                g_clear_error (&error);
                        } else {
                                gl_failed = FALSE;
                        }
                }
        }

        bindtextdomain (GETTEXT_PACKAGE, LOCALE_DIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        error = NULL;
        gtk_init_with_args (&argc, &argv,
                            (char *) _(" - the GNOME session manager"),
                            entries, GETTEXT_PACKAGE,
                            &error);
        if (error != NULL) {
                g_warning ("%s", error->message);
                exit (1);
        }

        if (show_version) {
                g_print ("%s %s\n", argv [0], VERSION);
                exit (0);
        }

        /* Rebind stdout/stderr to the journal explicitly, so that
         * journald picks ups the nicer "gnome-session" as the program
         * name instead of whatever shell script GDM happened to use.
         */
#ifdef HAVE_SYSTEMD
        if (!debug) {
                int journalfd;

                journalfd = sd_journal_stream_fd (PACKAGE, LOG_INFO, 0);
                if (journalfd >= 0) {
                        dup2(journalfd, 1);
                        dup2(journalfd, 2);
                }
        }
#endif

        if (gl_failed) {
                gsm_fail_whale_dialog_we_failed (FALSE, TRUE, NULL);
                gtk_main ();
                exit (1);
        }

        if (please_fail) {
                gsm_fail_whale_dialog_we_failed (TRUE, TRUE, NULL);
                gtk_main ();
                exit (1);
        }

        gdm_log_init ();
        gdm_log_set_debug (debug);

        /* Set DISPLAY explicitly for all our children, in case --display
         * was specified on the command line.
         */
        display_str = gdk_get_display ();
        gsm_util_setenv ("DISPLAY", display_str);
        g_free (display_str);

        /* Some third-party programs rely on GNOME_DESKTOP_SESSION_ID to
         * detect if GNOME is running. We keep this for compatibility reasons.
         */
        gsm_util_setenv ("GNOME_DESKTOP_SESSION_ID", "this-is-deprecated");

        /* We want to use the GNOME menus which has the designed categories.
         */
        gsm_util_setenv ("XDG_MENU_PREFIX", "gnome-");

        client_store = gsm_store_new ();

        /* Talk to logind before acquiring a name, since it does synchronous
         * calls at initialization time that invoke a main loop and if we
         * already owned a name, then we would service too early during
         * that main loop.
         */
        g_object_unref (gsm_get_system ());

        if (!acquire_name ()) {
                gsm_fail_whale_dialog_we_failed (TRUE, TRUE, NULL);
                gtk_main ();
                exit (1);
        }

        manager = gsm_manager_new (client_store, failsafe);

        g_signal_connect_object (bus_proxy,
                                 "destroy",
                                 G_CALLBACK (shutdown_cb),
                                 manager,
                                 G_CONNECT_SWAPPED);

        g_unix_signal_add (SIGTERM, term_or_int_signal_cb, manager);
        g_unix_signal_add (SIGINT, term_or_int_signal_cb, manager);
        g_unix_signal_add (SIGUSR1, sigusr1_cb, manager);
        g_unix_signal_add (SIGUSR2, sigusr2_cb, manager);

        if (IS_STRING_EMPTY (opt_session_name))
                session_name = _gsm_manager_get_default_session (manager);
        else
                session_name = opt_session_name;

        gsm_util_set_autostart_dirs (override_autostart_dirs);

        if (!gsm_session_fill (manager, session_name)) {
                gsm_fail_whale_dialog_we_failed (FALSE, TRUE, NULL);
        }

        gsm_manager_start (manager);

        gtk_main ();

        g_clear_object (&manager);
        g_clear_object (&client_store);
        g_clear_object (&bus_proxy);

        gdm_log_shutdown ();

        return 0;
}
