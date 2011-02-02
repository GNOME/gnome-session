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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "config.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "gsm-inhibitor.h"
#include "gsm-shell.h"

#define SHELL_NAME      "org.gnome.Shell"
#define SHELL_PATH      "/org/gnome/Shell"
#define SHELL_INTERFACE "org.gnome.Shell"

#define SHELL_END_SESSION_DIALOG_PATH      "/org/gnome/SessionManager/EndSessionDialog"
#define SHELL_END_SESSION_DIALOG_INTERFACE "org.gnome.SessionManager.EndSessionDialog"

#define GSM_SHELL_DBUS_TYPE_G_OBJECT_PATH_ARRAY (dbus_g_type_get_collection ("GPtrArray", DBUS_TYPE_G_OBJECT_PATH))

#define AUTOMATIC_ACTION_TIMEOUT 60

#define GSM_SHELL_GET_PRIVATE(o)                                   \
        (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSM_TYPE_SHELL, GsmShellPrivate))

struct _GsmShellPrivate
{
        DBusGConnection *bus_connection;
        DBusGProxy      *bus_proxy;
        DBusGProxy      *proxy;
        DBusGProxy      *end_session_dialog_proxy;

        guint32          is_running : 1;

        DBusGProxyCall  *end_session_open_call;
        GsmShellEndSessionDialogType end_session_dialog_type;
};

enum {
        PROP_0,
        PROP_IS_RUNNING
};

enum {
        END_SESSION_DIALOG_OPENED = 0,
        END_SESSION_DIALOG_OPEN_FAILED,
        END_SESSION_DIALOG_CANCELED,
        END_SESSION_DIALOG_CONFIRMED,
        NUMBER_OF_SIGNALS
};

static guint signals[NUMBER_OF_SIGNALS] = { 0 };

static void     gsm_shell_class_init   (GsmShellClass *klass);
static void     gsm_shell_init         (GsmShell      *ck);
static void     gsm_shell_finalize     (GObject            *object);

static void     gsm_shell_disconnect_from_bus    (GsmShell      *shell);

static DBusHandlerResult gsm_shell_bus_filter (DBusConnection *connection,
                                                DBusMessage    *message,
                                                void           *user_data);

static void     gsm_shell_on_name_owner_changed (DBusGProxy *bus_proxy,
                                                 const char *name,
                                                 const char *prev_owner,
                                                 const char *new_owner,
                                                 GsmShell   *shell);

G_DEFINE_TYPE (GsmShell, gsm_shell, G_TYPE_OBJECT);

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
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);

        signals [END_SESSION_DIALOG_OPEN_FAILED] =
                g_signal_new ("end-session-dialog-open-failed",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmShellClass, end_session_dialog_open_failed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);

        signals [END_SESSION_DIALOG_CANCELED] =
                g_signal_new ("end-session-dialog-canceled",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmShellClass, end_session_dialog_canceled),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);

        signals [END_SESSION_DIALOG_CONFIRMED] =
                g_signal_new ("end-session-dialog-confirmed",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmShellClass, end_session_dialog_confirmed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);

        g_type_class_add_private (shell_class, sizeof (GsmShellPrivate));
}

static DBusHandlerResult
gsm_shell_bus_filter (DBusConnection *connection,
                       DBusMessage    *message,
                       void           *user_data)
{
        GsmShell *shell;

        shell = GSM_SHELL (user_data);

        if (dbus_message_is_signal (message,
                                    DBUS_INTERFACE_LOCAL, "Disconnected") &&
            strcmp (dbus_message_get_path (message), DBUS_PATH_LOCAL) == 0) {
                gsm_shell_disconnect_from_bus (shell);
                return DBUS_HANDLER_RESULT_HANDLED;
        }

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static gboolean
gsm_shell_ensure_connection (GsmShell  *shell,
                             GError   **error)
{
        GError  *connection_error;
        gboolean is_running;

        connection_error = NULL;

        if (shell->priv->bus_connection == NULL) {
                DBusConnection *connection;

                shell->priv->bus_connection = dbus_g_bus_get (DBUS_BUS_SESSION,
                                                               &connection_error);

                if (shell->priv->bus_connection == NULL) {
                        g_propagate_error (error, connection_error);
                        is_running = FALSE;
                        goto out;
                }

                connection = dbus_g_connection_get_connection (shell->priv->bus_connection);
                dbus_connection_set_exit_on_disconnect (connection, FALSE);
                dbus_connection_add_filter (connection,
                                            gsm_shell_bus_filter,
                                            shell, NULL);
        }

        if (shell->priv->bus_proxy == NULL) {
                shell->priv->bus_proxy =
                        dbus_g_proxy_new_for_name_owner (shell->priv->bus_connection,
                                                         DBUS_SERVICE_DBUS,
                                                         DBUS_PATH_DBUS,
                                                         DBUS_INTERFACE_DBUS,
                                                         &connection_error);

                if (shell->priv->bus_proxy == NULL) {
                        g_propagate_error (error, connection_error);
                        is_running = FALSE;
                        goto out;
                }

                dbus_g_proxy_add_signal (shell->priv->bus_proxy,
                                         "NameOwnerChanged",
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_INVALID);

                dbus_g_proxy_connect_signal (shell->priv->bus_proxy,
                                             "NameOwnerChanged",
                                             G_CALLBACK (gsm_shell_on_name_owner_changed),
                                             shell, NULL);
        }

        if (shell->priv->proxy == NULL) {
                shell->priv->proxy =
                        dbus_g_proxy_new_for_name_owner (shell->priv->bus_connection,
                                                         SHELL_NAME,
                                                         SHELL_PATH,
                                                         SHELL_INTERFACE,
                                                         &connection_error);

                if (shell->priv->proxy == NULL) {
                        g_propagate_error (error, connection_error);
                        is_running = FALSE;
                        goto out;
                }
        }

        g_debug ("GsmShell: Connected to the shell");

        is_running = TRUE;

 out:
        if (shell->priv->is_running != is_running) {
                shell->priv->is_running = is_running;
                g_object_notify (G_OBJECT (shell), "is-running");
        }

        if (!is_running) {
                g_debug ("GsmShell: Not connected to the shell");

                if (shell->priv->bus_connection == NULL) {
                        if (shell->priv->bus_proxy != NULL) {
                                g_object_unref (shell->priv->bus_proxy);
                                shell->priv->bus_proxy = NULL;
                        }

                        if (shell->priv->proxy != NULL) {
                                g_object_unref (shell->priv->proxy);
                                shell->priv->proxy = NULL;
                        }
                } else if (shell->priv->bus_proxy == NULL) {
                        if (shell->priv->proxy != NULL) {
                                g_object_unref (shell->priv->proxy);
                                shell->priv->proxy = NULL;
                        }
                }
        }

        return is_running;
}

static void
gsm_shell_on_name_owner_changed (DBusGProxy    *bus_proxy,
                                 const char    *name,
                                 const char    *prev_owner,
                                 const char    *new_owner,
                                 GsmShell      *shell)
{
        if (name != NULL && strcmp (name, SHELL_NAME) != 0) {
                return;
        }

        if (shell->priv->proxy != NULL) {
                g_object_unref (shell->priv->proxy);
                shell->priv->proxy = NULL;
        }

        gsm_shell_ensure_connection (shell, NULL);
}

static void
gsm_shell_init (GsmShell *shell)
{
        GError *error;

        shell->priv = GSM_SHELL_GET_PRIVATE (shell);

        error = NULL;

        if (!gsm_shell_ensure_connection (shell, &error)) {
                g_debug ("GsmShell: Could not connect to the shell: %s",
                         error->message);
                g_error_free (error);
        }
}

static void
gsm_shell_disconnect_from_bus (GsmShell *shell)
{
        if (shell->priv->bus_proxy != NULL) {
                g_object_unref (shell->priv->bus_proxy);
                shell->priv->bus_proxy = NULL;
        }

        if (shell->priv->proxy != NULL) {
                g_object_unref (shell->priv->proxy);
                shell->priv->proxy = NULL;
        }

        if (shell->priv->bus_connection != NULL) {
                DBusConnection *connection;
                connection = dbus_g_connection_get_connection (shell->priv->bus_connection);
                dbus_connection_remove_filter (connection,
                                               gsm_shell_bus_filter,
                                               shell);

                dbus_g_connection_unref (shell->priv->bus_connection);
                shell->priv->bus_connection = NULL;
        }
}

static void
gsm_shell_finalize (GObject *object)
{
        GsmShell *shell;
        GObjectClass  *parent_class;

        shell = GSM_SHELL (object);

        parent_class = G_OBJECT_CLASS (gsm_shell_parent_class);

        gsm_shell_disconnect_from_bus (shell);

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
        gsm_shell_ensure_connection (shell, NULL);

        return shell->priv->is_running;
}

static gboolean
add_inhibitor_to_array (const char   *id,
                        GsmInhibitor *inhibitor,
                        GPtrArray    *array)
{

        g_ptr_array_add (array,
                         g_strdup (gsm_inhibitor_peek_id (inhibitor)));
        return FALSE;
}

static GPtrArray *
get_array_from_store (GsmStore *inhibitors)
{
        GPtrArray *array;

        array = g_ptr_array_new ();

        gsm_store_foreach (inhibitors,
                           (GsmStoreFunc) add_inhibitor_to_array,
                           array);

        return array;
}

static void
on_open_finished (DBusGProxy     *proxy,
                  DBusGProxyCall *call,
                  GsmShell       *shell)
{
        GError         *error;
        gboolean        res;

        g_assert (shell->priv->end_session_open_call == call);

        error = NULL;
        res = dbus_g_proxy_end_call (proxy,
                                     call,
                                     &error,
                                     G_TYPE_INVALID);
        shell->priv->end_session_open_call = NULL;

        if (!res) {
                g_warning ("Unable to open shell end session dialog");
                g_signal_emit (G_OBJECT (shell), signals[END_SESSION_DIALOG_OPEN_FAILED], 0);
                return;
        }

        g_signal_emit (G_OBJECT (shell), signals[END_SESSION_DIALOG_OPENED], 0);
}

static void
on_end_session_dialog_canceled (DBusGProxy *proxy,
                                GsmShell   *shell)
{
        g_signal_emit (G_OBJECT (shell), signals[END_SESSION_DIALOG_CANCELED], 0);
}

static void
on_end_session_dialog_confirmed (DBusGProxy *proxy,
                                 GsmShell   *shell)
{
        g_signal_emit (G_OBJECT (shell), signals[END_SESSION_DIALOG_CONFIRMED], 0);
}

static void
on_end_session_dialog_proxy_destroyed (DBusGProxy *proxy,
                                       GsmShell   *shell)
{
        if (shell->priv->end_session_dialog_proxy != NULL) {
                g_object_unref (shell->priv->proxy);
                shell->priv->end_session_dialog_proxy = NULL;
        }
}

gboolean
gsm_shell_open_end_session_dialog (GsmShell *shell,
                                   GsmShellEndSessionDialogType type,
                                   GsmStore *inhibitors,
                                   GsmStore *clients)
{
        DBusGProxyCall  *call;
        GPtrArray *inhibitor_array;
        GError *error;

        error = NULL;
        if (!gsm_shell_ensure_connection (shell, &error)) {
                g_warning ("Could not connect to the shell: %s",
                           error->message);
                g_error_free (error);
                return FALSE;
        }

        if (shell->priv->end_session_open_call != NULL) {
                g_return_val_if_fail (shell->priv->end_session_dialog_type == type,
                                      FALSE);

                return TRUE;
        }

        if (shell->priv->end_session_dialog_proxy == NULL) {
                DBusGProxy *proxy;

                proxy = dbus_g_proxy_new_for_name (shell->priv->bus_connection,
                                                   SHELL_NAME,
                                                   SHELL_END_SESSION_DIALOG_PATH,
                                                   SHELL_END_SESSION_DIALOG_INTERFACE);

                g_assert (proxy != NULL);

                shell->priv->end_session_dialog_proxy = proxy;

                g_signal_connect (proxy, "destroy",
                                  G_CALLBACK (on_end_session_dialog_proxy_destroyed),
                                  shell);

                dbus_g_proxy_add_signal (shell->priv->end_session_dialog_proxy,
                                         "Canceled", G_TYPE_INVALID);
                dbus_g_proxy_connect_signal (shell->priv->end_session_dialog_proxy,
                                             "Canceled",
                                             G_CALLBACK (on_end_session_dialog_canceled),
                                             shell, NULL);

                dbus_g_proxy_add_signal (shell->priv->end_session_dialog_proxy,
                                         "Confirmed", G_TYPE_INVALID);
                dbus_g_proxy_connect_signal (shell->priv->end_session_dialog_proxy,
                                             "Confirmed",
                                             G_CALLBACK (on_end_session_dialog_confirmed),
                                             shell, NULL);

        }

        inhibitor_array = get_array_from_store (inhibitors);

        call = dbus_g_proxy_begin_call_with_timeout (shell->priv->end_session_dialog_proxy,
                                                     "Open",
                                                     (DBusGProxyCallNotify)
                                                     on_open_finished,
                                                     shell,
                                                     NULL,
                                                     INT_MAX,
                                                     G_TYPE_UINT,
                                                     (guint) type,
                                                     G_TYPE_UINT,
                                                     (guint) 0,
                                                     G_TYPE_UINT,
                                                     AUTOMATIC_ACTION_TIMEOUT,
                                                     GSM_SHELL_DBUS_TYPE_G_OBJECT_PATH_ARRAY,
                                                     inhibitor_array,
                                                     G_TYPE_INVALID);
        g_ptr_array_foreach (inhibitor_array, (GFunc) g_free, NULL);
        g_ptr_array_free (inhibitor_array, TRUE);

        if (call == NULL) {
                g_warning ("Unable to make Open call");
                return FALSE;
        }

        shell->priv->end_session_open_call = call;
        shell->priv->end_session_dialog_type = type;

        return TRUE;
}
