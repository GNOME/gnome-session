/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006 Novell, Inc.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libintl.h>
#include <signal.h>
#include <stdlib.h>

#include <glib/gi18n.h>
#include <glib/goption.h>
#include <gtk/gtkmain.h>
#include <gtk/gtkmessagedialog.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-bindings.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "gconf.h"
#include "util.h"
#include "gsm-manager.h"
#include "gsm-xsmp-server.h"
#include "gsm-client-store.h"

static DBusGConnection *connection = NULL;
static gboolean         failsafe;

static GOptionEntry entries[] = {
        { "failsafe", 'f', 0, G_OPTION_ARG_NONE, &failsafe,
          N_("Do not load user-specified applications"),
          NULL },
        { NULL, 0, 0, 0, NULL, NULL, NULL }
};

static void
gsm_dbus_init (void)
{
        char *argv[3];
        char *output, **vars;
        int status, i;
        GError *error;

        /* Check whether there is a dbus-daemon session instance currently
         * running (not spawned by us).
         */
        if (g_getenv ("DBUS_SESSION_BUS_ADDRESS")) {
                g_warning ("dbus-daemon is already running: processes launched by "
                           "D-Bus won't have access to $SESSION_MANAGER!");
                return;
        }

        argv[0] = DBUS_LAUNCH;
        argv[1] = "--exit-with-session";
        argv[2] = NULL;

        /* dbus-launch exits pretty quickly, but if necessary, we could
         * make this async. (It's a little annoying since the main loop isn't
         * running yet...)
         */
        error = NULL;
        g_spawn_sync (NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
                      &output, NULL, &status, &error);
        if (error) {
                gsm_util_init_error (TRUE, "Could not start dbus-daemon: %s",
                                     error->message);
                /* not reached */
        }

        vars = g_strsplit (output, "\n", 0);
        for (i = 0; vars[i]; i++) {
                putenv (vars[i]);
        }

        /* Can't free the putenv'ed strings */
        g_free (vars);
        g_free (output);
}

static void
gsm_dbus_check (void)
{
        GError *error = NULL;

        connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
        if (!connection) {
                gsm_util_init_error (TRUE, "Could not start D-Bus: %s",
                                     error->message);
                /* not reached */
        }

        dbus_connection_set_exit_on_disconnect (dbus_g_connection_get_connection (connection), FALSE);
}

int
main (int argc, char **argv)
{
        struct sigaction sa;
        GError          *error;
        char            *display_str;
        GsmManager      *manager;
        GsmClientStore  *store;
        GsmXsmpServer   *xsmp_server;

        bindtextdomain (GETTEXT_PACKAGE, LOCALE_DIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        sa.sa_handler = SIG_IGN;
        sa.sa_flags = 0;
        sigemptyset (&sa.sa_mask);
        sigaction (SIGPIPE, &sa, 0);

        error = NULL;
        gtk_init_with_args (&argc, &argv,
                            (char *) _(" - the GNOME session manager"),
                            entries, GETTEXT_PACKAGE,
                            &error);
        if (error != NULL) {
                gsm_util_init_error (TRUE, "%s", error->message);
        }

        /* Set DISPLAY explicitly for all our children, in case --display
         * was specified on the command line.
         */
        display_str = gdk_get_display ();
        g_setenv ("DISPLAY", display_str, TRUE);
        g_free (display_str);

        store = gsm_client_store_new ();

        /* Start up gconfd and dbus-daemon (in parallel) if they're not
         * already running. This requires us to initialize XSMP too, because
         * we want $SESSION_MANAGER to be set before launching dbus-daemon.
         */
        gsm_gconf_init ();

        xsmp_server = gsm_xsmp_server_new (store);

        gsm_dbus_init ();

        /* Now make sure they succeeded. (They'll call
         * gsm_util_init_error() if they failed.)
         */
        gsm_gconf_check ();
        gsm_dbus_check ();

        manager = gsm_manager_new (store, failsafe);

        gsm_xsmp_server_start (xsmp_server);
        gsm_manager_start (manager);

        gtk_main ();

        if (xsmp_server != NULL) {
                g_object_unref (xsmp_server);
        }

        gsm_gconf_shutdown ();

        if (manager != NULL) {
                g_object_unref (manager);
        }

        return 0;
}
