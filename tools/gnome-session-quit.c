/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 * save-session.c - Small program to talk to session manager.

   Copyright (C) 1998 Tom Tromey
   Copyright (C) 2008 Red Hat, Inc.

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

#include <glib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#define GSM_SERVICE_DBUS   "org.gnome.SessionManager"
#define GSM_PATH_DBUS      "/org/gnome/SessionManager"
#define GSM_INTERFACE_DBUS "org.gnome.SessionManager"

enum {
        GSM_LOGOUT_MODE_NORMAL = 0,
        GSM_LOGOUT_MODE_NO_CONFIRMATION,
        GSM_LOGOUT_MODE_FORCE
};

static gboolean opt_logout = FALSE;
static gboolean opt_power_off = FALSE;
static gboolean opt_reboot = FALSE;
static gboolean opt_no_prompt = FALSE;
static gboolean opt_force = FALSE;

static GOptionEntry options[] = {
        {"logout", '\0', 0, G_OPTION_ARG_NONE, &opt_logout, N_("Log out"), NULL},
        {"power-off", '\0', 0, G_OPTION_ARG_NONE, &opt_power_off, N_("Power off"), NULL},
        {"reboot", '\0', 0, G_OPTION_ARG_NONE, &opt_reboot, N_("Reboot"), NULL},
        {"force", '\0', 0, G_OPTION_ARG_NONE, &opt_force, N_("Ignoring any existing inhibitors"), NULL},
        {"no-prompt", '\0', 0, G_OPTION_ARG_NONE, &opt_no_prompt, N_("Don’t prompt for user confirmation"), NULL},
        {NULL}
};

static void
display_error (const char *message)
{
        g_printerr ("%s\n", message);
}

static GDBusConnection *
get_session_bus (void)
{
        GDBusConnection *bus;
        GError *error = NULL;

        bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);

        if (bus == NULL) {
                g_warning ("Couldn't connect to session bus: %s", error->message);
                g_error_free (error);
        }

        return bus;
}

static GDBusProxy *
get_sm_proxy (void)
{
        GDBusConnection *connection;
        GDBusProxy      *sm_proxy;

        connection = get_session_bus ();
        if (connection == NULL) {
                display_error (_("Could not connect to the session manager"));
                return NULL;
        }

        sm_proxy = g_dbus_proxy_new_sync (connection,
                                          G_DBUS_PROXY_FLAGS_NONE,
                                          NULL,
                                          GSM_SERVICE_DBUS,
                                          GSM_PATH_DBUS,
                                          GSM_INTERFACE_DBUS,
                                          NULL, NULL);
        g_object_unref (connection);

        if (sm_proxy == NULL) {
                display_error (_("Could not connect to the session manager"));
                return NULL;
        }

        return sm_proxy;
}

static void
do_logout (unsigned int mode)
{
        GDBusProxy *sm_proxy;
        GVariant   *reply;
        GError     *error;

        sm_proxy = get_sm_proxy ();
        if (sm_proxy == NULL) {
                return;
        }

        error = NULL;
        reply = g_dbus_proxy_call_sync (sm_proxy,
                                        "Logout",
                                        g_variant_new ("(u)", mode),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1, NULL, &error);

        if (error != NULL) {
                g_warning ("Failed to call logout: %s",
                           error->message);
                g_error_free (error);
        } else {
                g_variant_unref (reply);
        }
        g_clear_object (&sm_proxy);
}

static void
do_power_off (const char *action)
{
        GDBusProxy *sm_proxy;
        GVariant   *reply;
        GError     *error;

        sm_proxy = get_sm_proxy ();
        if (sm_proxy == NULL) {
                return;
        }

        error = NULL;
        reply = g_dbus_proxy_call_sync (sm_proxy,
                                        action,
                                        NULL,
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1, NULL, &error);

        if (error != NULL) {
                g_warning ("Failed to call %s: %s",
                           action, error->message);
                g_error_free (error);
        } else {
                g_variant_unref (reply);
        }
        g_clear_object (&sm_proxy);
}

int
main (int argc, char *argv[])
{
        GError *error;
        int     conflicting_options;
        GOptionContext *ctx;

        /* Initialize the i18n stuff */
        setlocale (LC_ALL, "");
        bindtextdomain (GETTEXT_PACKAGE, LOCALE_DIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        error = NULL;
        ctx = g_option_context_new ("");
        g_option_context_add_main_entries (ctx, options, GETTEXT_PACKAGE);
        if (! g_option_context_parse (ctx, &argc, &argv, &error)) {
                g_warning ("Unable to start: %s", error->message);
                g_error_free (error);
                exit (1);
        }
        g_option_context_free (ctx);

        conflicting_options = 0;
        if (opt_logout)
                conflicting_options++;
        if (opt_power_off)
                conflicting_options++;
        if (opt_reboot)
                conflicting_options++;
        if (conflicting_options > 1)
                display_error (_("Program called with conflicting options"));

        if (opt_power_off) {
                do_power_off ("Shutdown");
        } else if (opt_reboot) {
                do_power_off ("Reboot");
        } else {
                /* default to logout */

                if (opt_force)
                        do_logout (GSM_LOGOUT_MODE_FORCE);
                else if (opt_no_prompt)
                        do_logout (GSM_LOGOUT_MODE_NO_CONFIRMATION);
                else
                        do_logout (GSM_LOGOUT_MODE_NORMAL);
        }

        return 0;
}
