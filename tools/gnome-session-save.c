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
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA.
*/

#include <config.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#define GSM_SERVICE_DBUS   "org.gnome.SessionManager"
#define GSM_PATH_DBUS      "/org/gnome/SessionManager"
#define GSM_INTERFACE_DBUS "org.gnome.SessionManager"

/* True if killing.  */
static gboolean do_logout = FALSE;

/* True if we should use dialog boxes */
static gboolean gui = FALSE;

/* True if we should do the requested action without confirmation */
static gboolean silent = FALSE;

static char *session_name = NULL;

static GOptionEntry options[] = {
        {"session-name", 's', 0, G_OPTION_ARG_STRING, &session_name, N_("Set the current session name"), N_("NAME")},
        {"kill", '\0', 0, G_OPTION_ARG_NONE, &do_logout, N_("Kill session"), NULL},
        {"gui",  '\0', 0, G_OPTION_ARG_NONE, &gui, N_("Use dialog boxes for errors"), NULL},
        {"silent", '\0', 0, G_OPTION_ARG_NONE, &silent, N_("Do not require confirmation"), NULL},
        {NULL}
};

static int exit_status = 0;

static void
display_error (const char *message)
{
        if (gui && !silent) {
                GtkWidget *dialog;

                dialog = gtk_message_dialog_new (NULL, 0, GTK_MESSAGE_ERROR,
                                                 GTK_BUTTONS_OK, message);

                /*gtk_window_set_default_icon_name (GTK_STOCK_SAVE);*/

                gtk_dialog_run (GTK_DIALOG (dialog));
                gtk_widget_destroy (dialog);
        } else {
                g_printerr ("%s\n", message);
        }
}

static DBusGProxy *
get_sm_proxy (DBusGConnection *connection)
{
        DBusGProxy *sm_proxy;

        sm_proxy = dbus_g_proxy_new_for_name (connection,
                                               GSM_SERVICE_DBUS,
                                               GSM_PATH_DBUS,
                                               GSM_INTERFACE_DBUS);

        return sm_proxy;
}

static DBusGConnection *
get_session_bus (void)
{
        DBusGConnection *bus;
        GError *error = NULL;

        bus = dbus_g_bus_get (DBUS_BUS_SESSION, &error);

        if (bus == NULL) {
                g_warning ("Couldn't connect to session bus: %s", error->message);
                g_error_free (error);
        }

        return bus;
}

static void
set_session_name (const char  *session_name)
{
        DBusGConnection *bus;
        DBusGProxy      *sm_proxy;
        GError          *error;
        gboolean         res;

        bus = get_session_bus ();

        if (bus == NULL) {
                display_error (_("Could not connect to the session manager"));
                goto out;
        }

        sm_proxy = get_sm_proxy (bus);
        if (sm_proxy == NULL) {
                display_error (_("Could not connect to the session manager"));
                goto out;
        }

        error = NULL;
        res = dbus_g_proxy_call (sm_proxy,
                                 "SetName",
                                 &error,
                                 G_TYPE_STRING, session_name,
                                 G_TYPE_INVALID, G_TYPE_INVALID);

        if (!res) {
                if (error != NULL) {
                        g_warning ("Failed to set session name '%s': %s",
                                   session_name, error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to set session name '%s'",
                                   session_name);
                }

                goto out;
        }

 out:
        if (sm_proxy != NULL) {
                g_object_unref (sm_proxy);
        }
}

static void
logout_session (gboolean show_confirmation)
{
        DBusGConnection *bus;
        DBusGProxy      *sm_proxy;
        GError          *error;
        gboolean         res;
        guint            flags;

        sm_proxy = NULL;

        bus = get_session_bus ();
        if (bus == NULL) {
                display_error (_("Could not connect to the session manager"));
                goto out;
        }

        sm_proxy = get_sm_proxy (bus);
        if (sm_proxy == NULL) {
                display_error (_("Could not connect to the session manager"));
                goto out;
        }

        flags = 0;
        if (! show_confirmation) {
                flags |= 1;
        }

        error = NULL;
        res = dbus_g_proxy_call (sm_proxy,
                                 "Logout",
                                 &error,
                                 G_TYPE_UINT, flags,
                                 G_TYPE_INVALID,
                                 G_TYPE_INVALID);

        if (!res) {
                if (error != NULL) {
                        g_warning ("Failed to call logout: %s",
                                   error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to call logout");
                }

                goto out;
        }

 out:
        if (sm_proxy != NULL) {
                g_object_unref (sm_proxy);
        }
}

int
main (int argc, char *argv[])
{
        GError *error;

        /* Initialize the i18n stuff */
        bindtextdomain (GETTEXT_PACKAGE, LOCALE_DIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        error = NULL;
        if (! gtk_init_with_args (&argc, &argv, NULL, options, NULL, &error)) {
                g_warning ("Unable to start: %s", error->message);
                g_error_free (error);
                exit (1);
        }

        if (do_logout) {
                logout_session (! silent);
        }

        return exit_status;
}
