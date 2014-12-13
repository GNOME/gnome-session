/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include "gsm-inhibitor.h"
#include "gsm-end-session-dialog.h"

#define END_SESSION_DIALOG_NAME      "org.gnome.SessionManager.EndSessionDialog"
#define END_SESSION_DIALOG_PATH      "/org/gnome/SessionManager/EndSessionDialog"
#define END_SESSION_DIALOG_INTERFACE "org.gnome.SessionManager.EndSessionDialog"

#define AUTOMATIC_ACTION_TIMEOUT 60

struct _GsmEndSessionDialogPrivate
{
        GDBusProxy              *end_session_dialog_proxy;
        GsmStore                *inhibitors;

        gboolean                 dialog_is_open;
        GsmEndSessionDialogType  end_session_dialog_type;

        guint                    update_idle_id;
        guint                    watch_id;
};

enum {
        END_SESSION_DIALOG_OPENED = 0,
        END_SESSION_DIALOG_OPEN_FAILED,
        END_SESSION_DIALOG_CLOSED,
        END_SESSION_DIALOG_CANCELED,
        END_SESSION_DIALOG_CONFIRMED_LOGOUT,
        END_SESSION_DIALOG_CONFIRMED_SHUTDOWN,
        END_SESSION_DIALOG_CONFIRMED_REBOOT,
        NUMBER_OF_SIGNALS
};

static guint signals[NUMBER_OF_SIGNALS] = { 0 };

static void gsm_end_session_dialog_finalize (GObject             *object);
static void queue_end_session_dialog_update (GsmEndSessionDialog *dialog);

G_DEFINE_TYPE_WITH_PRIVATE (GsmEndSessionDialog, gsm_end_session_dialog, G_TYPE_OBJECT);

static void
gsm_end_session_dialog_class_init (GsmEndSessionDialogClass *class)
{
        GObjectClass *object_class = G_OBJECT_CLASS (class);

        object_class->finalize = gsm_end_session_dialog_finalize;

        signals [END_SESSION_DIALOG_OPENED] =
                g_signal_new ("end-session-dialog-opened",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmEndSessionDialogClass, end_session_dialog_opened),
                              NULL, NULL, NULL,
                              G_TYPE_NONE, 0);

        signals [END_SESSION_DIALOG_OPEN_FAILED] =
                g_signal_new ("end-session-dialog-open-failed",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmEndSessionDialogClass, end_session_dialog_open_failed),
                              NULL, NULL, NULL,
                              G_TYPE_NONE, 0);

        signals [END_SESSION_DIALOG_CLOSED] =
                g_signal_new ("end-session-dialog-closed",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmEndSessionDialogClass, end_session_dialog_closed),
                              NULL, NULL, NULL,
                              G_TYPE_NONE, 0);

        signals [END_SESSION_DIALOG_CANCELED] =
                g_signal_new ("end-session-dialog-canceled",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmEndSessionDialogClass, end_session_dialog_canceled),
                              NULL, NULL, NULL,
                              G_TYPE_NONE, 0);

        signals [END_SESSION_DIALOG_CONFIRMED_LOGOUT] =
                g_signal_new ("end-session-dialog-confirmed-logout",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmEndSessionDialogClass, end_session_dialog_confirmed_logout),
                              NULL, NULL, NULL,
                              G_TYPE_NONE, 0);

        signals [END_SESSION_DIALOG_CONFIRMED_SHUTDOWN] =
                g_signal_new ("end-session-dialog-confirmed-shutdown",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmEndSessionDialogClass, end_session_dialog_confirmed_shutdown),
                              NULL, NULL, NULL,
                              G_TYPE_NONE, 0);

        signals [END_SESSION_DIALOG_CONFIRMED_REBOOT] =
                g_signal_new ("end-session-dialog-confirmed-reboot",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmEndSessionDialogClass, end_session_dialog_confirmed_reboot),
                              NULL, NULL, NULL,
                              G_TYPE_NONE, 0);
}

static void
gsm_end_session_dialog_init (GsmEndSessionDialog *dialog)
{
        dialog->priv = gsm_end_session_dialog_get_instance_private (dialog);

        dialog->priv->watch_id = g_bus_watch_name (G_BUS_TYPE_SESSION,
                                                   END_SESSION_DIALOG_NAME,
                                                   G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                   NULL,
                                                   NULL,
                                                   dialog,
                                                   NULL);
}

static void
gsm_end_session_dialog_finalize (GObject *object)
{
        GsmEndSessionDialog *dialog = GSM_END_SESSION_DIALOG (object);

        g_object_unref (dialog->priv->inhibitors);

        if (dialog->priv->watch_id != 0) {
                g_bus_unwatch_name (dialog->priv->watch_id);
                dialog->priv->watch_id = 0;
        }

        G_OBJECT_CLASS (gsm_end_session_dialog_parent_class)->finalize (object);
}

GsmEndSessionDialog *
gsm_end_session_dialog_new (void)
{
        GsmEndSessionDialog *dialog;

        dialog = g_object_new (GSM_TYPE_END_SESSION_DIALOG, NULL);

        return dialog;
}

static gboolean
add_inhibitor_to_array (const char      *id,
                        GsmInhibitor    *inhibitor,
                        GVariantBuilder *builder)
{
        g_variant_builder_add (builder, "o", gsm_inhibitor_peek_id (inhibitor));
        return FALSE;
}

static GVariant *
get_array_from_store (GsmStore *inhibitors)
{
        GVariantBuilder builder;

        g_variant_builder_init (&builder, G_VARIANT_TYPE ("ao"));
        gsm_store_foreach (inhibitors,
                           (GsmStoreFunc) add_inhibitor_to_array,
                           &builder);

        return g_variant_builder_end (&builder);
}

static void
remove_update_idle_source (GsmEndSessionDialog *dialog)
{
        if (dialog->priv->update_idle_id != 0) {
                g_source_remove (dialog->priv->update_idle_id);
                dialog->priv->update_idle_id = 0;
        }
}

static void
on_open_finished (GObject      *source,
                  GAsyncResult *result,
                  gpointer      user_data)
{
        GsmEndSessionDialog *dialog;
        GError *error;

        dialog = GSM_END_SESSION_DIALOG (user_data);

        remove_update_idle_source (dialog);

        dialog->priv->dialog_is_open = FALSE;

        error = NULL;
        g_dbus_proxy_call_finish (G_DBUS_PROXY (source), result, &error);

        if (error != NULL) {
                g_warning ("Unable to open end session dialog: %s", error->message);
                g_error_free (error);

                g_signal_emit (dialog, signals[END_SESSION_DIALOG_OPEN_FAILED], 0);
                return;
        }

        g_signal_emit (dialog, signals[END_SESSION_DIALOG_OPENED], 0);
}

static void
on_end_session_dialog_closed (GsmEndSessionDialog *dialog)
{
        remove_update_idle_source (dialog);

        g_signal_handlers_disconnect_by_func (dialog->priv->inhibitors,
                                              G_CALLBACK (queue_end_session_dialog_update),
                                              dialog);

        g_signal_emit (G_OBJECT (dialog), signals[END_SESSION_DIALOG_CLOSED], 0);
}

static void
on_end_session_dialog_canceled (GsmEndSessionDialog *dialog)
{
        remove_update_idle_source (dialog);

        g_signal_handlers_disconnect_by_func (dialog->priv->inhibitors,
                                              G_CALLBACK (queue_end_session_dialog_update),
                                              dialog);

        g_signal_emit (G_OBJECT (dialog), signals[END_SESSION_DIALOG_CANCELED], 0);
}

static void
on_end_session_dialog_confirmed_logout (GsmEndSessionDialog *dialog)
{
        remove_update_idle_source (dialog);

        g_signal_emit (G_OBJECT (dialog), signals[END_SESSION_DIALOG_CONFIRMED_LOGOUT], 0);
}

static void
on_end_session_dialog_confirmed_shutdown (GsmEndSessionDialog *dialog)
{
        remove_update_idle_source (dialog);

        g_signal_emit (G_OBJECT (dialog), signals[END_SESSION_DIALOG_CONFIRMED_SHUTDOWN], 0);
}

static void
on_end_session_dialog_confirmed_reboot (GsmEndSessionDialog *dialog)
{
        remove_update_idle_source (dialog);

        g_signal_emit (G_OBJECT (dialog), signals[END_SESSION_DIALOG_CONFIRMED_REBOOT], 0);
}

static void
on_end_session_dialog_dbus_signal (GDBusProxy          *proxy,
                                   gchar               *sender_name,
                                   gchar               *signal_name,
                                   GVariant            *parameters,
                                   GsmEndSessionDialog *dialog)
{
        if (g_strcmp0 (signal_name, "Closed") == 0) {
                on_end_session_dialog_closed (dialog);
        } else if (g_strcmp0 (signal_name, "Canceled") == 0) {
                on_end_session_dialog_canceled (dialog);
        } else if (g_strcmp0 (signal_name ,"ConfirmedLogout") == 0) {
                on_end_session_dialog_confirmed_logout (dialog);
        } else if (g_strcmp0 (signal_name ,"ConfirmedReboot") == 0) {
                on_end_session_dialog_confirmed_reboot (dialog);
        } else if (g_strcmp0 (signal_name ,"ConfirmedShutdown") == 0) {
                on_end_session_dialog_confirmed_shutdown (dialog);
        }
}

static void
on_end_session_dialog_name_owner_changed (GDBusProxy          *proxy,
                                          GParamSpec          *pspec,
                                          GsmEndSessionDialog *dialog)
{
        gchar *name_owner;

        name_owner = g_dbus_proxy_get_name_owner (proxy);
        if (name_owner == NULL) {
                g_clear_object (&dialog->priv->end_session_dialog_proxy);
        }

        g_free (name_owner);
}

static gboolean
on_need_end_session_dialog_update (GsmEndSessionDialog *dialog)
{
        /* No longer need an update */
        if (dialog->priv->update_idle_id == 0)
                return FALSE;

        dialog->priv->update_idle_id = 0;

        gsm_end_session_dialog_open (dialog,
                                     dialog->priv->end_session_dialog_type,
                                     dialog->priv->inhibitors);
        return FALSE;
}

static void
queue_end_session_dialog_update (GsmEndSessionDialog *dialog)
{
        if (dialog->priv->update_idle_id != 0)
                return;

        dialog->priv->update_idle_id = g_idle_add ((GSourceFunc) on_need_end_session_dialog_update,
                                                   dialog);
}

gboolean
gsm_end_session_dialog_open (GsmEndSessionDialog     *dialog,
                             GsmEndSessionDialogType  type,
                             GsmStore                *inhibitors)
{
        GDBusProxy *proxy;
        GError *error;

        error = NULL;

        if (dialog->priv->dialog_is_open) {
                g_return_val_if_fail (dialog->priv->end_session_dialog_type == type,
                                      FALSE);
                return TRUE;
        }

        if (dialog->priv->end_session_dialog_proxy == NULL) {
                proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                       G_DBUS_PROXY_FLAGS_NONE,
                                                       NULL,
                                                       END_SESSION_DIALOG_NAME,
                                                       END_SESSION_DIALOG_PATH,
                                                       END_SESSION_DIALOG_INTERFACE,
                                                       NULL,
                                                       &error);

                if (error != NULL) {
                        g_critical ("Could not connect to the end session dialog: %s",
                                    error->message);
                        g_error_free (error);
                        return FALSE;
                }

                dialog->priv->end_session_dialog_proxy = proxy;

                g_signal_connect (proxy, "notify::g-name-owner",
                                  G_CALLBACK (on_end_session_dialog_name_owner_changed),
                                  dialog);
                g_signal_connect (proxy, "g-signal",
                                  G_CALLBACK (on_end_session_dialog_dbus_signal),
                                  dialog);
        }

        g_dbus_proxy_call (dialog->priv->end_session_dialog_proxy,
                           "Open",
                           g_variant_new ("(uuu@ao)",
                                          type,
                                          0,
                                          AUTOMATIC_ACTION_TIMEOUT,
                                          get_array_from_store (inhibitors)),
                           G_DBUS_CALL_FLAGS_NONE,
                           G_MAXINT,
                           NULL,
                           on_open_finished,
                           dialog);

        g_object_ref (inhibitors);

        if (dialog->priv->inhibitors != NULL) {
                g_signal_handlers_disconnect_by_func (dialog->priv->inhibitors,
                                                      G_CALLBACK (queue_end_session_dialog_update),
                                                      dialog);
                g_object_unref (dialog->priv->inhibitors);
        }

        dialog->priv->inhibitors = inhibitors;

        g_signal_connect_swapped (inhibitors, "added",
                                  G_CALLBACK (queue_end_session_dialog_update),
                                  dialog);

        g_signal_connect_swapped (inhibitors, "removed",
                                  G_CALLBACK (queue_end_session_dialog_update),
                                  dialog);

        dialog->priv->dialog_is_open = TRUE;
        dialog->priv->end_session_dialog_type = type;

        return TRUE;
}

void
gsm_end_session_dialog_close (GsmEndSessionDialog *dialog)
{
        if (!dialog->priv->end_session_dialog_proxy)
                return;

        g_dbus_proxy_call (dialog->priv->end_session_dialog_proxy,
                           "Close",
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           -1, NULL, NULL, NULL);
}
