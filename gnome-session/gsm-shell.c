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
#include "gsm-shell.h"

#define SHELL_NAME      "org.gnome.Shell"
#define SHELL_PATH      "/org/gnome/Shell"
#define SHELL_INTERFACE "org.gnome.Shell"

#define SHELL_END_SESSION_DIALOG_PATH      "/org/gnome/SessionManager/EndSessionDialog"
#define SHELL_END_SESSION_DIALOG_INTERFACE "org.gnome.SessionManager.EndSessionDialog"

#define AUTOMATIC_ACTION_TIMEOUT 60

struct _GsmShellPrivate
{
        GDBusProxy      *end_session_dialog_proxy;
        GsmStore        *inhibitors;

        guint32          is_running : 1;

        gboolean         dialog_is_open;
        GsmShellEndSessionDialogType end_session_dialog_type;

        guint            update_idle_id;
        guint            watch_id;
};

enum {
        PROP_0,
        PROP_IS_RUNNING
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

static void     gsm_shell_class_init   (GsmShellClass *klass);
static void     gsm_shell_init         (GsmShell      *ck);
static void     gsm_shell_finalize     (GObject            *object);

static void     queue_end_session_dialog_update (GsmShell *shell);

G_DEFINE_TYPE_WITH_CODE (GsmShell, gsm_shell, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (GsmShell));

static void
gsm_shell_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
        GsmShell *shell = GSM_SHELL (object);

        switch (prop_id) {
        case PROP_IS_RUNNING:
                g_value_set_boolean (value,
                                     shell->priv->is_running);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object,
                                                   prop_id,
                                                   pspec);
        }
}

static void
gsm_shell_class_init (GsmShellClass *shell_class)
{
        GObjectClass *object_class;
        GParamSpec   *param_spec;

        object_class = G_OBJECT_CLASS (shell_class);

        object_class->finalize = gsm_shell_finalize;
        object_class->get_property = gsm_shell_get_property;

        param_spec = g_param_spec_boolean ("is-running",
                                           "Is running",
                                           "Whether GNOME Shell is running in the session",
                                           FALSE,
                                           G_PARAM_READABLE);

        g_object_class_install_property (object_class, PROP_IS_RUNNING,
                                         param_spec);

        signals [END_SESSION_DIALOG_OPENED] =
                g_signal_new ("end-session-dialog-opened",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmShellClass, end_session_dialog_opened),
                              NULL, NULL, NULL,
                              G_TYPE_NONE, 0);

        signals [END_SESSION_DIALOG_OPEN_FAILED] =
                g_signal_new ("end-session-dialog-open-failed",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmShellClass, end_session_dialog_open_failed),
                              NULL, NULL, NULL,
                              G_TYPE_NONE, 0);

        signals [END_SESSION_DIALOG_CLOSED] =
                g_signal_new ("end-session-dialog-closed",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmShellClass, end_session_dialog_closed),
                              NULL, NULL, NULL,
                              G_TYPE_NONE, 0);

        signals [END_SESSION_DIALOG_CANCELED] =
                g_signal_new ("end-session-dialog-canceled",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmShellClass, end_session_dialog_canceled),
                              NULL, NULL, NULL,
                              G_TYPE_NONE, 0);

        signals [END_SESSION_DIALOG_CONFIRMED_LOGOUT] =
                g_signal_new ("end-session-dialog-confirmed-logout",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmShellClass, end_session_dialog_confirmed_logout),
                              NULL, NULL, NULL,
                              G_TYPE_NONE, 0);

        signals [END_SESSION_DIALOG_CONFIRMED_SHUTDOWN] =
                g_signal_new ("end-session-dialog-confirmed-shutdown",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmShellClass, end_session_dialog_confirmed_shutdown),
                              NULL, NULL, NULL,
                              G_TYPE_NONE, 0);

        signals [END_SESSION_DIALOG_CONFIRMED_REBOOT] =
                g_signal_new ("end-session-dialog-confirmed-reboot",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmShellClass, end_session_dialog_confirmed_reboot),
                              NULL, NULL, NULL,
                              G_TYPE_NONE, 0);
}

static void
on_shell_name_vanished (GDBusConnection *connection,
                        const gchar     *name,
                        gpointer         user_data)
{
        GsmShell *shell = user_data;
        shell->priv->is_running = FALSE;
}

static void
on_shell_name_appeared (GDBusConnection *connection,
                        const gchar     *name,
                        const gchar     *name_owner,
                        gpointer         user_data)
{
        GsmShell *shell = user_data;
        shell->priv->is_running = TRUE;
}

static void
gsm_shell_ensure_connection (GsmShell  *shell)
{
        if (shell->priv->watch_id != 0) {
                return;
        }

        shell->priv->watch_id = g_bus_watch_name (G_BUS_TYPE_SESSION,
                                                  SHELL_NAME,
                                                  G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                  on_shell_name_appeared,
                                                  on_shell_name_vanished,
                                                  shell, NULL);
}

static void
gsm_shell_init (GsmShell *shell)
{
        shell->priv = gsm_shell_get_instance_private (shell);

        gsm_shell_ensure_connection (shell);
}

static void
gsm_shell_finalize (GObject *object)
{
        GsmShell *shell;
        GObjectClass  *parent_class;

        shell = GSM_SHELL (object);

        parent_class = G_OBJECT_CLASS (gsm_shell_parent_class);

        g_object_unref (shell->priv->inhibitors);

        if (shell->priv->watch_id != 0) {
                g_bus_unwatch_name (shell->priv->watch_id);
                shell->priv->watch_id = 0;
        }

        if (parent_class->finalize != NULL) {
                parent_class->finalize (object);
        }
}

GsmShell *
gsm_shell_new (void)
{
        GsmShell *shell;

        shell = g_object_new (GSM_TYPE_SHELL, NULL);

        return shell;
}

GsmShell *
gsm_get_shell (void)
{
        static GsmShell *shell = NULL;

        if (shell == NULL) {
                shell = gsm_shell_new ();
        }

        return g_object_ref (shell);
}

gboolean
gsm_shell_is_running (GsmShell *shell)
{
        gsm_shell_ensure_connection (shell);

        return shell->priv->is_running;
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
on_open_finished (GObject *source,
                  GAsyncResult *result,
                  gpointer user_data)
{
        GsmShell *shell = user_data;
        GError   *error;

        if (shell->priv->update_idle_id != 0) {
                g_source_remove (shell->priv->update_idle_id);
                shell->priv->update_idle_id = 0;
        }

        shell->priv->dialog_is_open = FALSE;

        error = NULL;
        g_dbus_proxy_call_finish (G_DBUS_PROXY (source), result, &error);

        if (error != NULL) {
                g_warning ("Unable to open shell end session dialog: %s", error->message);
                g_error_free (error);

                g_signal_emit (G_OBJECT (shell), signals[END_SESSION_DIALOG_OPEN_FAILED], 0);
                return;
        }

        g_signal_emit (G_OBJECT (shell), signals[END_SESSION_DIALOG_OPENED], 0);
}

static void
on_end_session_dialog_dbus_signal (GDBusProxy *proxy,
                                   gchar      *sender_name,
                                   gchar      *signal_name,
                                   GVariant   *parameters,
                                   GsmShell   *shell)
{
        struct {
                const char *name;
                int         index;
        } signal_map[] = {
                { "Closed", END_SESSION_DIALOG_CLOSED },
                { "Canceled", END_SESSION_DIALOG_CANCELED },
                { "ConfirmedLogout", END_SESSION_DIALOG_CONFIRMED_LOGOUT },
                { "ConfirmedReboot", END_SESSION_DIALOG_CONFIRMED_REBOOT },
                { "ConfirmedShutdown", END_SESSION_DIALOG_CONFIRMED_SHUTDOWN },
                { NULL, -1 }
        };
        int signal_index = -1;
        int i;

        for (i = 0; signal_map[i].name != NULL; i++) {
                if (g_strcmp0 (signal_map[i].name, signal_name) == 0) {
                        signal_index = signal_map[i].index;
                        break;
                }
        }

        if (signal_index == -1)
                return;

        shell->priv->dialog_is_open = FALSE;

        if (shell->priv->update_idle_id != 0) {
                g_source_remove (shell->priv->update_idle_id);
                shell->priv->update_idle_id = 0;
        }

        g_signal_handlers_disconnect_by_func (shell->priv->inhibitors,
                                              G_CALLBACK (queue_end_session_dialog_update),
                                              shell);

        g_signal_emit (G_OBJECT (shell), signals[signal_index], 0);
}

static void
on_end_session_dialog_name_owner_changed (GDBusProxy *proxy,
                                          GParamSpec *pspec,
                                          GsmShell   *shell)
{
        gchar *name_owner;

        name_owner = g_dbus_proxy_get_name_owner (proxy);
        if (name_owner == NULL) {
                g_clear_object (&shell->priv->end_session_dialog_proxy);
        }

        g_free (name_owner);
}

static gboolean
on_need_end_session_dialog_update (GsmShell *shell)
{
        /* No longer need an update */
        if (shell->priv->update_idle_id == 0)
                return FALSE;

        shell->priv->update_idle_id = 0;

        gsm_shell_open_end_session_dialog (shell,
                                           shell->priv->end_session_dialog_type,
                                           shell->priv->inhibitors);
        return FALSE;
}

static void
queue_end_session_dialog_update (GsmShell *shell)
{
        if (shell->priv->update_idle_id != 0)
                return;

        shell->priv->update_idle_id = g_idle_add ((GSourceFunc) on_need_end_session_dialog_update,
                                                  shell);
}

gboolean
gsm_shell_open_end_session_dialog (GsmShell *shell,
                                   GsmShellEndSessionDialogType type,
                                   GsmStore *inhibitors)
{
        GDBusProxy *proxy;
        GError *error;

        error = NULL;

        if (shell->priv->dialog_is_open) {
                g_return_val_if_fail (shell->priv->end_session_dialog_type == type,
                                      FALSE);

                return TRUE;
        }

        if (shell->priv->end_session_dialog_proxy == NULL) {
                proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                       G_DBUS_PROXY_FLAGS_NONE,
                                                       NULL,
                                                       SHELL_NAME,
                                                       SHELL_END_SESSION_DIALOG_PATH,
                                                       SHELL_END_SESSION_DIALOG_INTERFACE,
                                                       NULL, &error);

                if (error != NULL) {
                        g_critical ("Could not connect to the shell: %s",
                                    error->message);
                        g_error_free (error);
                        return FALSE;
                }

                shell->priv->end_session_dialog_proxy = proxy;

                g_signal_connect (proxy, "notify::g-name-owner",
                                  G_CALLBACK (on_end_session_dialog_name_owner_changed),
                                  shell);
                g_signal_connect (proxy, "g-signal",
                                  G_CALLBACK (on_end_session_dialog_dbus_signal),
                                  shell);
        }

        g_dbus_proxy_call (shell->priv->end_session_dialog_proxy,
                           "Open",
                           g_variant_new ("(uuu@ao)",
                                          type,
                                          0,
                                          AUTOMATIC_ACTION_TIMEOUT,
                                          get_array_from_store (inhibitors)),
                           G_DBUS_CALL_FLAGS_NONE,
                           G_MAXINT, NULL,
                           on_open_finished, shell);

        g_object_ref (inhibitors);

        if (shell->priv->inhibitors != NULL) {
                g_signal_handlers_disconnect_by_func (shell->priv->inhibitors,
                                                      G_CALLBACK (queue_end_session_dialog_update),
                                                      shell);
                g_object_unref (shell->priv->inhibitors);
        }

        shell->priv->inhibitors = inhibitors;

        g_signal_connect_swapped (inhibitors, "added",
                                  G_CALLBACK (queue_end_session_dialog_update),
                                  shell);

        g_signal_connect_swapped (inhibitors, "removed",
                                  G_CALLBACK (queue_end_session_dialog_update),
                                  shell);

        shell->priv->dialog_is_open = TRUE;
        shell->priv->end_session_dialog_type = type;

        return TRUE;
}

void
gsm_shell_close_end_session_dialog (GsmShell *shell)
{
        if (!shell->priv->end_session_dialog_proxy)
                return;

        shell->priv->dialog_is_open = FALSE;

        g_dbus_proxy_call (shell->priv->end_session_dialog_proxy,
                           "Close",
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           -1, NULL, NULL, NULL);
}
