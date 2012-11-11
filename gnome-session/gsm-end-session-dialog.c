/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Red Hat, Inc.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Authors:
 *      Matthias Clasen
 */

#include <glib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include "gsm-inhibit-dialog.h"
#include "gsm-end-session-dialog-generated.h"

static GtkWidget *
gsm_end_session_dialog_new (guint               action,
                            guint               seconds,
                            const gchar *const *inhibitor_paths)
{
        GtkWidget *dialog;

        dialog = gsm_inhibit_dialog_new (action, seconds, inhibitor_paths);

        return dialog;
}

static void
inhibit_dialog_response (GsmInhibitDialog    *dialog,
                         guint                response_id,
                         GsmEndSessionDialog *object)
{
        int action;

        g_object_get (dialog, "action", &action, NULL);
        gtk_widget_destroy (GTK_WIDGET (dialog));

        switch (response_id) {
        case GTK_RESPONSE_CANCEL:
        case GTK_RESPONSE_NONE:
        case GTK_RESPONSE_DELETE_EVENT:
                if (action == GSM_LOGOUT_ACTION_LOGOUT
                    || action == GSM_LOGOUT_ACTION_SHUTDOWN
                    || action == GSM_LOGOUT_ACTION_REBOOT) {
                        g_print ("cancel action %d\n", action);
                        gsm_end_session_dialog_emit_canceled (object);
                        gsm_end_session_dialog_emit_closed (object);
                }
                break;
        case GTK_RESPONSE_ACCEPT:
                g_print ("confirm action %d\n", action);
                if (action == GSM_LOGOUT_ACTION_LOGOUT) {
                        gsm_end_session_dialog_emit_confirmed_logout (object);
                } else if (action == GSM_LOGOUT_ACTION_SHUTDOWN) {
                        gsm_end_session_dialog_emit_confirmed_shutdown (object);
                } else if (action == GSM_LOGOUT_ACTION_REBOOT) {
                        gsm_end_session_dialog_emit_confirmed_reboot (object);
                }
                break;
        default:
                g_assert_not_reached ();
                break;
        }
}

static gboolean
handle_open (GsmEndSessionDialog   *object,
             GDBusMethodInvocation *invocation,
             guint                  arg_type,
             guint                  arg_timestamp,
             guint                  arg_seconds_to_stay_open,
             const gchar *const    *arg_inhibitor_object_paths,
             gpointer               user_data)
{
        GtkWidget *dialog;

        g_print ("handle open\n");

        dialog = gsm_end_session_dialog_new (arg_type, arg_seconds_to_stay_open, arg_inhibitor_object_paths);

        g_signal_connect (dialog, "response",
                          G_CALLBACK (inhibit_dialog_response), object);

        gtk_window_present_with_time (GTK_WINDOW (dialog), arg_timestamp);

        gsm_end_session_dialog_complete_open (object, invocation);

        return TRUE;
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
        GDBusInterfaceSkeleton *iface;
        GError *error = NULL;

        g_print ("Acquired a message bus connection\n");

        iface = G_DBUS_INTERFACE_SKELETON (gsm_end_session_dialog_skeleton_new ());
        g_signal_connect (iface, "handle-open",
                          G_CALLBACK (handle_open), NULL);

        if (!g_dbus_interface_skeleton_export (iface,
                                               connection,
                                               "/org/gnome/SessionManager/EndSessionDialog",
                                               &error)) {
                g_warning ("Failed to export interface: %s", error->message);
                g_error_free (error);
                return;
        }
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
        g_print ("Acquired the name %s\n", name);
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
        g_print ("Lost the name %s\n", name);
}

gint
main (gint argc, gchar *argv[])
{
        guint id;

        gtk_init (&argc, &argv);

        id = g_bus_own_name (G_BUS_TYPE_SESSION,
                             "org.gnome.SessionManager.EndSessionDialog",
                             G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                             G_BUS_NAME_OWNER_FLAGS_REPLACE,
                             on_bus_acquired,
                             on_name_acquired,
                             on_name_lost,
                             NULL,
                             NULL);

        gtk_main ();

        g_bus_unown_name (id);

        return 0;
}
