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

#include <glib/gi18n.h>
#include <glib.h>
#include <glib-unix.h>
#include <gio/gio.h>

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
static gboolean disable_acceleration_check = FALSE;
static const char *session_name = NULL;
static GsmManager *manager = NULL;
static char *gl_renderer = NULL;

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
        GsmManager *manager = (GsmManager *)data;

        if (connection == NULL) {
                g_warning ("Lost name on bus: %s", name);
                gsm_fail_whale_dialog_we_failed (TRUE, TRUE, NULL);
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
        manager = gsm_manager_new (client_store, failsafe);
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

        _gsm_manager_set_renderer (manager, gl_renderer);
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
        new_argv = g_malloc ((argc + 3) * sizeof (*argv));

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

        if (getenv ("DISPLAY") == NULL) {
                /* Not connected to X11, someone else will take care of checking GL */
                return TRUE;
        }

        if (!g_spawn_sync (NULL, (char **) argv, NULL, 0, NULL, NULL, &gl_renderer, NULL,
                           &status, error)) {
                return FALSE;
        }

        return g_spawn_check_exit_status (status, error);
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
                g_setenv ("GVFS_DISABLE_FUSE", use_vfs, TRUE);
                g_free (disable_fuse);
        } else {
                g_unsetenv ("GVFS_DISABLE_FUSE");
        }
}

int
main (int argc, char **argv)
{
        GError           *error = NULL;
        static char     **override_autostart_dirs = NULL;
        static char      *opt_session_name = NULL;
        const char       *debug_string = NULL;
        gboolean          gl_failed = FALSE;
        guint             name_owner_id;
        GOptionContext   *options;
        static GOptionEntry entries[] = {
                { "autostart", 'a', 0, G_OPTION_ARG_STRING_ARRAY, &override_autostart_dirs, N_("Override standard autostart directories"), N_("AUTOSTART_DIR") },
                { "session", 0, 0, G_OPTION_ARG_STRING, &opt_session_name, N_("Session to use"), N_("SESSION_NAME") },
                { "debug", 0, 0, G_OPTION_ARG_NONE, &debug, N_("Enable debugging code"), NULL },
                { "failsafe", 'f', 0, G_OPTION_ARG_NONE, &failsafe, N_("Do not load user-specified applications"), NULL },
                { "version", 0, 0, G_OPTION_ARG_NONE, &show_version, N_("Version of this application"), NULL },
                /* Translators: the 'fail whale' is the black dialog we show when something goes seriously wrong */
                { "whale", 0, 0, G_OPTION_ARG_NONE, &please_fail, N_("Show the fail whale dialog for testing"), NULL },
                { "disable-acceleration-check", 0, 0, G_OPTION_ARG_NONE, &disable_acceleration_check, N_("Disable hardware acceleration check"), NULL },
                { NULL, 0, 0, 0, NULL, NULL, NULL }
        };

        /* Make sure that we have a session bus */
        if (!require_dbus_session (argc, argv, &error)) {
                gsm_util_init_error (TRUE, "%s", error->message);
        }

        /* From 3.14 GDM sets XDG_CURRENT_DESKTOP. For compatibility with
         * older versions of GDM,  other display managers, and startx,
         * set a fallback value if we don't find it set.
         */
        if (g_getenv ("XDG_CURRENT_DESKTOP") == NULL) {
            g_setenv("XDG_CURRENT_DESKTOP", "GNOME", TRUE);
            gsm_util_setenv ("XDG_CURRENT_DESKTOP", "GNOME");
        }

        /* Make sure we initialize gio in a way that does not autostart any daemon */
        initialize_gio ();

        setlocale (LC_ALL, "");
        bindtextdomain (GETTEXT_PACKAGE, LOCALE_DIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        debug_string = g_getenv ("GNOME_SESSION_DEBUG");
        if (debug_string != NULL) {
                debug = rpmatch (debug_string) == TRUE || atoi (debug_string) == 1;
        }

        error = NULL;
        options = g_option_context_new (_(" - the GNOME session manager"));
        g_option_context_add_main_entries (options, entries, GETTEXT_PACKAGE);
        g_option_context_parse (options, &argc, &argv, &error);
        if (error != NULL) {
                g_warning ("%s", error->message);
                exit (1);
        }

        g_option_context_free (options);

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

        gdm_log_init ();
        gdm_log_set_debug (debug);

        if (disable_acceleration_check) {
                g_debug ("hardware acceleration check is disabled");
        } else {
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
        }

        if (show_version) {
                g_print ("%s %s\n", argv [0], VERSION);
                exit (0);
        }

        if (gl_failed) {
                gsm_fail_whale_dialog_we_failed (FALSE, TRUE, NULL);
                gsm_main ();
                exit (1);
        }

        if (please_fail) {
                gsm_fail_whale_dialog_we_failed (TRUE, TRUE, NULL);
                gsm_main ();
                exit (1);
        }

        gsm_util_export_activation_environment (NULL);

#ifdef HAVE_SYSTEMD
        gsm_util_export_user_environment (NULL);
#endif

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

        /* Some third-party programs rely on GNOME_DESKTOP_SESSION_ID to
         * detect if GNOME is running. We keep this for compatibility reasons.
         */
        gsm_util_setenv ("GNOME_DESKTOP_SESSION_ID", "this-is-deprecated");

        /* We want to use the GNOME menus which has the designed categories.
         */
        gsm_util_setenv ("XDG_MENU_PREFIX", "gnome-");

        /* hack to fix keyring until we can reorder things in 3.20
         * https://bugzilla.gnome.org/show_bug.cgi?id=738205
         */
        if (g_strcmp0 (g_getenv ("XDG_SESSION_TYPE"), "wayland") == 0 &&
            g_getenv ("GSM_SKIP_SSH_AGENT_WORKAROUND") == NULL) {
                char *ssh_socket;

                ssh_socket = g_build_filename (g_get_user_runtime_dir (), "keyring", "ssh", NULL);
                gsm_util_setenv ("SSH_AUTH_SOCK", ssh_socket);
                g_free (ssh_socket);
        }

        gsm_util_set_autostart_dirs (override_autostart_dirs);
        session_name = opt_session_name;

        /* Talk to logind before acquiring a name, since it does synchronous
         * calls at initialization time that invoke a main loop and if we
         * already owned a name, then we would service too early during
         * that main loop.
         */
        g_object_unref (gsm_get_system ());

        name_owner_id  = acquire_name ();

        gsm_main ();

        g_clear_object (&manager);
        g_free (gl_renderer);

        g_bus_unown_name (name_owner_id);
        gdm_log_shutdown ();

        return 0;
}
