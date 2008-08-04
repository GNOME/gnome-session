/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * gnome-settings-daemon-helper: Does things g-s-d should do, but doesn't:
 *
 *   1) Sets screen resolution
 *   2) Sets GTK_RC_FILES
 *
 * (This should go away when g-s-d does these itself.)
 *
 * Most of this code comes from the old gnome-session/gsm-xrandr.c.
 *
 * Copyright (C) 2003 Red Hat, Inc.
 * Copyright (C) 2007 Novell, Inc.
 */

#include <config.h>

#include <stdio.h>
#include <unistd.h>

#include <dbus/dbus-glib.h>
#include <gtk/gtk.h>

static void
set_gtk1_theme_rcfile (void)
{
        DBusGConnection *connection;
        DBusGProxy      *gsm;
        char            *value;
        GError          *error;

        error = NULL;
        connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);

        if (connection == NULL) {
                g_error ("couldn't get D-Bus connection: %s", error->message);
        }

        gsm = dbus_g_proxy_new_for_name (connection,
                                         "org.gnome.SessionManager",
                                         "/org/gnome/SessionManager",
                                         "org.gnome.SessionManager");

        value = g_strdup_printf (SYSCONFDIR "/gtk/gtkrc:%s/.gtkrc-1.2-gnome2", g_get_home_dir ());
        if (!dbus_g_proxy_call (gsm, "Setenv", &error,
                                G_TYPE_STRING, "GTK_RC_FILES",
                                G_TYPE_STRING, value,
                                G_TYPE_INVALID,
                                G_TYPE_INVALID)) {
                g_warning ("Could not set GTK_RC_FILES: %s", error->message);
                g_error_free (error);
        }

        g_free (value);
}

int
main (int argc, char **argv)
{
        gtk_init (&argc, &argv);

        /* Point GTK_RC_FILES (for gtk 1.2) at a file that we change in in
         * gnome-settings-daemon.
         */
        set_gtk1_theme_rcfile ();

        return 0;
}
