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

#include <locale.h>

#include <glib/gi18n.h>
#include <glib.h>
#include <glib-unix.h>
#include <gio/gio.h>

#include "gsm-util.h"
#include "gsm-manager.h"
#include "gsm-session-fill.h"
#include "gsm-store.h"
#include "gsm-system.h"

static char *session_name = NULL;

static GsmManager *manager = NULL;
static GMainLoop *loop = NULL;

void
gsm_quit (void)
{
        g_main_loop_quit (loop);
}

static void
on_name_lost (GDBusConnection *connection,
              const char *name,
              gpointer    data)
{
        if (connection == NULL) {
                if (!gsm_manager_get_dbus_disconnected (manager))
                        g_log_structured (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
                                          "MESSAGE_ID", GSM_MANAGER_UNRECOVERABLE_FAILURE_MSGID,
                                          "MESSAGE", "Lost bus", name);
        } else {
                g_debug ("Calling name lost callback function");

                /*
                 * When the signal handler gets a shutdown signal, it calls
                 * this function to inform GsmManager to not restart
                 * applications in the off chance a handler is already queued
                 * to dispatch following the below call to gtk_main_quit.
                 */
                gsm_manager_set_phase (manager, GSM_MANAGER_PHASE_EXIT);
        }

        gsm_quit ();
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
        g_log_set_debug_enabled (!g_log_get_debug_enabled ());
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
        manager = gsm_manager_new ();

        g_unix_signal_add (SIGTERM, term_or_int_signal_cb, manager);
        g_unix_signal_add (SIGINT, term_or_int_signal_cb, manager);
        g_unix_signal_add (SIGUSR1, sigusr1_cb, manager);
        g_unix_signal_add (SIGUSR2, sigusr2_cb, manager);

        if (!gsm_session_fill (manager, session_name)) {
                g_log_structured (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
                                  "MESSAGE_ID", GSM_MANAGER_UNRECOVERABLE_FAILURE_MSGID,
                                  "MESSAGE", "Failed to fill session");
                gsm_quit ();
        }
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const char *name,
                 gpointer data)
{
        create_manager ();
}

int
main (int argc, char **argv)
{
        g_autoptr (GError) error = NULL;
        g_autoptr (GOptionContext) options = NULL;
        const char *debug = NULL;
        guint name_owner_id;
        static GOptionEntry entries[] = {
                { "session", 0, 0, G_OPTION_ARG_STRING, &session_name, N_("Session to use"), N_("SESSION_NAME") },
                { NULL, 0, 0, 0, NULL, NULL, NULL }
        };

        setlocale (LC_ALL, "");
        bindtextdomain (GETTEXT_PACKAGE, LOCALE_DIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        options = g_option_context_new (_(" — the GNOME session manager"));
        g_option_context_add_main_entries (options, entries, GETTEXT_PACKAGE);
        if (!g_option_context_parse (options, &argc, &argv, &error))
                g_error ("%s", error->message);

        debug = g_getenv ("GNOME_SESSION_DEBUG");
        if (debug != NULL)
                g_log_set_debug_enabled (atoi (debug) == 1);

        /* Talk to logind before acquiring a name, since it does synchronous
         * calls at initialization time that invoke a main loop and if we
         * already owned a name, then we would service too early during
         * that main loop. */
        g_object_unref (gsm_get_system ());

        name_owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                        "org.gnome.SessionManager",
                                        G_BUS_NAME_OWNER_FLAGS_NONE,
                                        on_bus_acquired,
                                        on_name_acquired,
                                        on_name_lost,
                                        NULL, NULL);

        loop = g_main_loop_new (NULL, TRUE);
        g_main_loop_run (loop);

        g_clear_object (&manager);
        g_free (session_name);
        g_bus_unown_name (name_owner_id);
        return 0;
}
