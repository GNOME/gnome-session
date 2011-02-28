/* -*- mode:c; c-basic-offset: 8; indent-tabs-mode: nil; -*- */
/* Tool to set the property _GNOME_SESSION_ACCELERATED on the root window */
/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author:
 *   Colin Walters <walters@verbum.org>
 */

#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xatom.h>

static void
exit_1_message (const char *msg) G_GNUC_NORETURN;

static void
exit_1_message (const char *msg)
{
  g_printerr ("%s", msg);
  exit (1);
}

int
main (int argc, char **argv)
{
        GdkDisplay *display = NULL;
        int estatus;
        Atom is_accelerated_atom;
        char *child_argv[] = {LIBEXECDIR "/gnome-session-check-accelerated-helper"};
        Window rootwin;
        glong is_accelerated;
        GError *error = NULL;

        gtk_init (NULL, NULL);

        display = gdk_display_get_default ();
        rootwin = gdk_x11_get_default_root_xwindow ();

        is_accelerated_atom = gdk_x11_get_xatom_by_name_for_display (display, "_GNOME_SESSION_ACCELERATED");

        {
                Atom type;
                gint format;
                gulong nitems;
                gulong bytes_after;
                guchar *data;

                gdk_x11_display_error_trap_push (display);
                XGetWindowProperty (GDK_DISPLAY_XDISPLAY (display), rootwin,
                                    is_accelerated_atom,
                                    0, G_MAXLONG, False, XA_CARDINAL, &type, &format, &nitems,
                                    &bytes_after, &data);
                gdk_x11_display_error_trap_pop_ignored (display);

                if (type == XA_CARDINAL) {
                        glong *is_accelerated_ptr = (glong*) data;
                        /* Exit 1 if the property is 0 (false) */
                        return (*is_accelerated_ptr == 0) ? 1 : 0;
                }
        }

        /* We don't have the property or it's the wrong type.  Try to compute it now. */

        estatus = 1;
        if (!g_spawn_sync (NULL, (char**)child_argv, NULL, 0,
                           NULL, NULL, NULL, NULL, &estatus, &error)) {
                is_accelerated = FALSE;
                g_printerr ("gnome-session-check-accelerated: Failed to run helper: %s\n", error->message);
                g_clear_error (&error);
        } else {
                is_accelerated = (estatus == 0);
                if (!is_accelerated)
                        g_printerr ("gnome-session-check-accelerated: Helper exited with code %d\n", estatus);
        }

        XChangeProperty (GDK_DISPLAY_XDISPLAY (display),
                         rootwin,
                         is_accelerated_atom,
                         XA_CARDINAL, 32, PropModeReplace, (guchar *) &is_accelerated, 1);

        gdk_display_sync (display);

        return is_accelerated;
}
