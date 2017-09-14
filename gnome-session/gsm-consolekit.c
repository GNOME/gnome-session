/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Jon McCann <jmccann@redhat.com>
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

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#ifdef HAVE_OLD_UPOWER
#define UPOWER_ENABLE_DEPRECATED 1
#include <upower.h>
#endif

#include "gsm-system.h"
#include "gsm-consolekit.h"

#define CK_NAME      "org.freedesktop.ConsoleKit"
#define CK_PATH      "/org/freedesktop/ConsoleKit"

#define CK_MANAGER_PATH      "/org/freedesktop/ConsoleKit/Manager"
#define CK_MANAGER_INTERFACE "org.freedesktop.ConsoleKit.Manager"
#define CK_SEAT_INTERFACE    "org.freedesktop.ConsoleKit.Seat"
#define CK_SESSION_INTERFACE "org.freedesktop.ConsoleKit.Session"

#define GSM_CONSOLEKIT_SESSION_TYPE_LOGIN_WINDOW "LoginWindow"

#define GSM_CONSOLEKIT_GET_PRIVATE(o)                                   \
        (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSM_TYPE_CONSOLEKIT, GsmConsolekitPrivate))

struct _GsmConsolekitPrivate
{
        DBusGConnection *dbus_connection;
        DBusGProxy      *bus_proxy;
        DBusGProxy      *ck_proxy;
        DBusGProxy      *session_proxy;
#ifdef HAVE_OLD_UPOWER
        UpClient        *up_client;
#endif

        gboolean         is_active;
        gboolean         restarting;

        gchar            *session_id;
};

enum {
        PROP_0,
        PROP_ACTIVE
};

static void     gsm_consolekit_class_init   (GsmConsolekitClass *klass);
static void     gsm_consolekit_init         (GsmConsolekit      *ck);
static void     gsm_consolekit_finalize     (GObject            *object);

static void     gsm_consolekit_free_dbus    (GsmConsolekit      *manager);

static DBusHandlerResult gsm_consolekit_dbus_filter (DBusConnection *connection,
                                                     DBusMessage    *message,
                                                     void           *user_data);

static void     gsm_consolekit_on_name_owner_changed (DBusGProxy        *bus_proxy,
                                                      const char        *name,
                                                      const char        *prev_owner,
                                                      const char        *new_owner,
                                                      GsmConsolekit   *manager);

static void gsm_consolekit_system_init (GsmSystemInterface *iface);

G_DEFINE_TYPE_WITH_CODE (GsmConsolekit, gsm_consolekit, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GSM_TYPE_SYSTEM,
                                                gsm_consolekit_system_init))

static void
gsm_consolekit_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
        GsmConsolekit *self = GSM_CONSOLEKIT (object);

        switch (prop_id) {
        case PROP_ACTIVE:
                self->priv->is_active = g_value_get_boolean (value);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
}

static void
gsm_consolekit_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
        GsmConsolekit *self = GSM_CONSOLEKIT (object);

        switch (prop_id) {
        case PROP_ACTIVE:
                g_value_set_boolean (value, self->priv->is_active);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_consolekit_class_init (GsmConsolekitClass *manager_class)
{
        GObjectClass *object_class;

        object_class = G_OBJECT_CLASS (manager_class);

        object_class->get_property = gsm_consolekit_get_property;
        object_class->set_property = gsm_consolekit_set_property;
        object_class->finalize = gsm_consolekit_finalize;

        g_object_class_override_property (object_class, PROP_ACTIVE, "active");

        g_type_class_add_private (manager_class, sizeof (GsmConsolekitPrivate));
}

static DBusHandlerResult
gsm_consolekit_dbus_filter (DBusConnection *connection,
                            DBusMessage    *message,
                            void           *user_data)
{
        GsmConsolekit *manager;

        manager = GSM_CONSOLEKIT (user_data);

        if (dbus_message_is_signal (message,
                                    DBUS_INTERFACE_LOCAL, "Disconnected") &&
            strcmp (dbus_message_get_path (message), DBUS_PATH_LOCAL) == 0) {
                gsm_consolekit_free_dbus (manager);
                /* let other filters get this disconnected signal, so that they
                 * can handle it too */
        }

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void
is_active_cb (DBusGProxy     *proxy,
              DBusGProxyCall *call,
              gpointer        data)
{
        GsmConsolekit *self = data;
        GError *local_error = NULL;
        gboolean is_active;

        if (!dbus_g_proxy_end_call (proxy, call, &local_error,
                                    G_TYPE_BOOLEAN, &is_active,
                                    G_TYPE_INVALID)) {
                g_warning ("Failed IsActive call to ConsoleKit: %s",
                           local_error->message);
                g_clear_error (&local_error);
                return;
        }

        if (is_active != self->priv->is_active) {
                self->priv->is_active = is_active;
                g_object_notify ((GObject*) self, "active");
        }
}

static void
on_active_changed (DBusGProxy   *proxy,
                   gboolean      is_active,
                   GsmConsolekit *self)
{
        if (is_active != self->priv->is_active) {
                self->priv->is_active = is_active;
                g_object_notify ((GObject*) self, "active");
        }
}

static gboolean
gsm_consolekit_ensure_ck_connection (GsmConsolekit  *manager,
                                     GError        **error)
{
        GError  *connection_error;
        gboolean is_connected;
        gboolean ret;
        guint32 pid;

        connection_error = NULL;
        manager->priv->session_id = NULL;
        is_connected = FALSE;

        if (manager->priv->dbus_connection == NULL) {
                DBusConnection *connection;

                manager->priv->dbus_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM,
                                                                 &connection_error);

                if (manager->priv->dbus_connection == NULL) {
                        g_propagate_error (error, connection_error);
                        is_connected = FALSE;
                        goto out;
                }

                connection = dbus_g_connection_get_connection (manager->priv->dbus_connection);
                dbus_connection_set_exit_on_disconnect (connection, FALSE);
                dbus_connection_add_filter (connection,
                                            gsm_consolekit_dbus_filter,
                                            manager, NULL);
        }

        if (manager->priv->bus_proxy == NULL) {
                manager->priv->bus_proxy =
                        dbus_g_proxy_new_for_name_owner (manager->priv->dbus_connection,
                                                         DBUS_SERVICE_DBUS,
                                                         DBUS_PATH_DBUS,
                                                         DBUS_INTERFACE_DBUS,
                                                         &connection_error);

                if (manager->priv->bus_proxy == NULL) {
                        g_propagate_error (error, connection_error);
                        is_connected = FALSE;
                        goto out;
                }

                dbus_g_proxy_add_signal (manager->priv->bus_proxy,
                                         "NameOwnerChanged",
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_INVALID);

                dbus_g_proxy_connect_signal (manager->priv->bus_proxy,
                                             "NameOwnerChanged",
                                             G_CALLBACK (gsm_consolekit_on_name_owner_changed),
                                             manager, NULL);
        }

        if (manager->priv->ck_proxy == NULL) {
                manager->priv->ck_proxy =
                        dbus_g_proxy_new_for_name_owner (manager->priv->dbus_connection,
                                                         CK_NAME,
                                                         CK_MANAGER_PATH,
                                                         CK_MANAGER_INTERFACE,
                                                         &connection_error);

                if (manager->priv->ck_proxy == NULL) {
                        g_propagate_error (error, connection_error);
                        is_connected = FALSE;
                        goto out;
                }
        }

        pid = getpid ();
        ret = dbus_g_proxy_call (manager->priv->ck_proxy, "GetSessionForUnixProcess", &connection_error,
                                 G_TYPE_UINT, pid,
                                 G_TYPE_INVALID,
                                 DBUS_TYPE_G_OBJECT_PATH, &manager->priv->session_id,
                                 G_TYPE_INVALID);
        if (!ret) {
                g_propagate_error (error, connection_error);
                goto out;
        }

        if (manager->priv->session_proxy == NULL) {
                manager->priv->session_proxy =
                        dbus_g_proxy_new_for_name_owner (manager->priv->dbus_connection,
                                                         CK_NAME,
                                                         manager->priv->session_id,
                                                         CK_SESSION_INTERFACE,
                                                         &connection_error);

                if (manager->priv->session_proxy == NULL) {
                        g_propagate_error (error, connection_error);
                        is_connected = FALSE;
                        goto out;
                }

                dbus_g_proxy_begin_call (manager->priv->session_proxy,
                                         "IsActive",
                                         is_active_cb, g_object_ref (manager),
                                         (GDestroyNotify)g_object_unref,
                                         G_TYPE_INVALID);
                dbus_g_proxy_add_signal (manager->priv->session_proxy, "ActiveChanged", G_TYPE_BOOLEAN, G_TYPE_INVALID);
                dbus_g_proxy_connect_signal (manager->priv->session_proxy, "ActiveChanged",
                                             G_CALLBACK (on_active_changed), manager, NULL);
        }

#ifdef HAVE_OLD_UPOWER
        g_clear_object (&manager->priv->up_client);
        manager->priv->up_client = up_client_new ();
#endif

        is_connected = TRUE;

 out:
        if (!is_connected) {
                if (manager->priv->dbus_connection == NULL) {
                        if (manager->priv->bus_proxy != NULL) {
                                g_object_unref (manager->priv->bus_proxy);
                                manager->priv->bus_proxy = NULL;
                        }

                        if (manager->priv->ck_proxy != NULL) {
                                g_object_unref (manager->priv->ck_proxy);
                                manager->priv->ck_proxy = NULL;
                        }

                        g_clear_object (&manager->priv->session_proxy);
                        g_clear_object (&manager->priv->session_id);
                } else if (manager->priv->bus_proxy == NULL) {
                        if (manager->priv->ck_proxy != NULL) {
                                g_object_unref (manager->priv->ck_proxy);
                                manager->priv->ck_proxy = NULL;
                        }

                        g_clear_object (&manager->priv->session_proxy);
                        g_clear_object (&manager->priv->session_id);
                }
        }

        return is_connected;
}

static void
gsm_consolekit_on_name_owner_changed (DBusGProxy    *bus_proxy,
                                      const char    *name,
                                      const char    *prev_owner,
                                      const char    *new_owner,
                                      GsmConsolekit *manager)
{
        if (name != NULL && strcmp (name, "org.freedesktop.ConsoleKit") != 0) {
                return;
        }

        g_clear_object (&manager->priv->ck_proxy);
        g_clear_object (&manager->priv->session_proxy);

        gsm_consolekit_ensure_ck_connection (manager, NULL);

}

static void
gsm_consolekit_init (GsmConsolekit *manager)
{
        GError *error;

        manager->priv = GSM_CONSOLEKIT_GET_PRIVATE (manager);

        error = NULL;

        if (!gsm_consolekit_ensure_ck_connection (manager, &error)) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           error->message);
                g_error_free (error);
        }
}

static void
gsm_consolekit_free_dbus (GsmConsolekit *manager)
{
        g_clear_object (&manager->priv->bus_proxy);
        g_clear_object (&manager->priv->ck_proxy);
        g_clear_object (&manager->priv->session_proxy);
        g_clear_object (&manager->priv->session_id);
#ifdef HAVE_OLD_UPOWER
        g_clear_object (&manager->priv->up_client);
#endif

        if (manager->priv->dbus_connection != NULL) {
                DBusConnection *connection;
                connection = dbus_g_connection_get_connection (manager->priv->dbus_connection);
                dbus_connection_remove_filter (connection,
                                               gsm_consolekit_dbus_filter,
                                               manager);

                dbus_g_connection_unref (manager->priv->dbus_connection);
                manager->priv->dbus_connection = NULL;
        }
}

static void
gsm_consolekit_finalize (GObject *object)
{
        GsmConsolekit *manager;
        GObjectClass  *parent_class;

        manager = GSM_CONSOLEKIT (object);

        parent_class = G_OBJECT_CLASS (gsm_consolekit_parent_class);

        gsm_consolekit_free_dbus (manager);

        if (parent_class->finalize != NULL) {
                parent_class->finalize (object);
        }
}

static void
emit_restart_complete (GsmConsolekit *manager,
                       GError        *error)
{
        GError *call_error;

        call_error = NULL;

        if (error != NULL) {
                call_error = g_error_new_literal (GSM_SYSTEM_ERROR,
                                                  GSM_SYSTEM_ERROR_RESTARTING,
                                                  error->message);
        }

        g_signal_emit_by_name (G_OBJECT (manager),
                               "request_completed", call_error);

        if (call_error != NULL) {
                g_error_free (call_error);
        }
}

static void
emit_stop_complete (GsmConsolekit *manager,
                    GError        *error)
{
        GError *call_error;

        call_error = NULL;

        if (error != NULL) {
                call_error = g_error_new_literal (GSM_SYSTEM_ERROR,
                                                  GSM_SYSTEM_ERROR_STOPPING,
                                                  error->message);
        }

        g_signal_emit_by_name (G_OBJECT (manager),
                               "request_completed", call_error);

        if (call_error != NULL) {
                g_error_free (call_error);
        }
}

static void
gsm_consolekit_attempt_restart (GsmSystem *system)
{
        GsmConsolekit *manager = GSM_CONSOLEKIT (system);
        gboolean res;
        GError  *error;

        error = NULL;

        if (!gsm_consolekit_ensure_ck_connection (manager, &error)) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           error->message);
                emit_restart_complete (manager, error);
                g_error_free (error);
                return;
        }

        res = dbus_g_proxy_call_with_timeout (manager->priv->ck_proxy,
                                              "Restart",
                                              INT_MAX,
                                              &error,
                                              G_TYPE_INVALID,
                                              G_TYPE_INVALID);

        if (!res) {
                g_warning ("Unable to restart system: %s", error->message);
                emit_restart_complete (manager, error);
                g_error_free (error);
        } else {
                emit_restart_complete (manager, NULL);
        }
}

static void
gsm_consolekit_attempt_stop (GsmSystem *system)
{
        GsmConsolekit *manager = GSM_CONSOLEKIT (system);
        gboolean res;
        GError  *error;

        error = NULL;

        if (!gsm_consolekit_ensure_ck_connection (manager, &error)) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           error->message);
                emit_stop_complete (manager, error);
                g_error_free (error);
                return;
        }

        res = dbus_g_proxy_call_with_timeout (manager->priv->ck_proxy,
                                              "Stop",
                                              INT_MAX,
                                              &error,
                                              G_TYPE_INVALID,
                                              G_TYPE_INVALID);

        if (!res) {
                g_warning ("Unable to stop system: %s", error->message);
                emit_stop_complete (manager, error);
                g_error_free (error);
        } else {
                emit_stop_complete (manager, NULL);
        }
}

static gboolean
get_current_session_id (DBusConnection *connection,
                        char          **session_id)
{
        DBusError       local_error;
        DBusMessage    *message;
        DBusMessage    *reply;
        gboolean        ret;
        DBusMessageIter iter;
        const char     *value;

        ret = FALSE;
        reply = NULL;

        dbus_error_init (&local_error);
        message = dbus_message_new_method_call (CK_NAME,
                                                CK_MANAGER_PATH,
                                                CK_MANAGER_INTERFACE,
                                                "GetCurrentSession");
        if (message == NULL) {
                goto out;
        }

        dbus_error_init (&local_error);
        reply = dbus_connection_send_with_reply_and_block (connection,
                                                           message,
                                                           -1,
                                                           &local_error);
        if (reply == NULL) {
                if (dbus_error_is_set (&local_error)) {
                        g_warning ("Unable to determine session: %s", local_error.message);
                        dbus_error_free (&local_error);
                        goto out;
                }
        }

        dbus_message_iter_init (reply, &iter);
        dbus_message_iter_get_basic (&iter, &value);
        if (session_id != NULL) {
                *session_id = g_strdup (value);
        }

        ret = TRUE;
 out:
        if (message != NULL) {
                dbus_message_unref (message);
        }
        if (reply != NULL) {
                dbus_message_unref (reply);
        }

        return ret;
}

static gboolean
get_seat_id_for_session (DBusConnection *connection,
                         const char     *session_id,
                         char          **seat_id)
{
        DBusError       local_error;
        DBusMessage    *message;
        DBusMessage    *reply;
        gboolean        ret;
        DBusMessageIter iter;
        const char     *value;

        ret = FALSE;
        reply = NULL;

        dbus_error_init (&local_error);
        message = dbus_message_new_method_call (CK_NAME,
                                                session_id,
                                                CK_SESSION_INTERFACE,
                                                "GetSeatId");
        if (message == NULL) {
                goto out;
        }

        dbus_error_init (&local_error);
        reply = dbus_connection_send_with_reply_and_block (connection,
                                                           message,
                                                           -1,
                                                           &local_error);
        if (reply == NULL) {
                if (dbus_error_is_set (&local_error)) {
                        g_warning ("Unable to determine seat: %s", local_error.message);
                        dbus_error_free (&local_error);
                        goto out;
                }
        }

        dbus_message_iter_init (reply, &iter);
        dbus_message_iter_get_basic (&iter, &value);
        if (seat_id != NULL) {
                *seat_id = g_strdup (value);
        }

        ret = TRUE;
 out:
        if (message != NULL) {
                dbus_message_unref (message);
        }
        if (reply != NULL) {
                dbus_message_unref (reply);
        }

        return ret;
}

static char *
get_current_seat_id (DBusConnection *connection)
{
        gboolean res;
        char    *session_id;
        char    *seat_id;

        session_id = NULL;
        seat_id = NULL;

        res = get_current_session_id (connection, &session_id);
        if (res) {
                res = get_seat_id_for_session (connection, session_id, &seat_id);
        }
        g_free (session_id);

        return seat_id;
}

static void
gsm_consolekit_set_session_idle (GsmSystem *system,
                                 gboolean   is_idle)
{
        GsmConsolekit *manager = GSM_CONSOLEKIT (system);
        gboolean        res;
        GError         *error;
        char           *session_id;
        DBusMessage    *message;
        DBusMessage    *reply;
        DBusError       dbus_error;
        DBusMessageIter iter;

        error = NULL;

        if (!gsm_consolekit_ensure_ck_connection (manager, &error)) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           error->message);
                g_error_free (error);
                return;
        }

        session_id = NULL;
        res = get_current_session_id (dbus_g_connection_get_connection (manager->priv->dbus_connection),
                                      &session_id);
        if (!res) {
                goto out;
        }


        g_debug ("Updating ConsoleKit idle status: %d", is_idle);
        message = dbus_message_new_method_call (CK_NAME,
                                                session_id,
                                                CK_SESSION_INTERFACE,
                                                "SetIdleHint");
        if (message == NULL) {
                g_debug ("Couldn't allocate the D-Bus message");
                return;
        }

        dbus_message_iter_init_append (message, &iter);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_BOOLEAN, &is_idle);

        /* FIXME: use async? */
        dbus_error_init (&dbus_error);
        reply = dbus_connection_send_with_reply_and_block (dbus_g_connection_get_connection (manager->priv->dbus_connection),
                                                           message,
                                                           -1,
                                                           &dbus_error);
        dbus_message_unref (message);

        if (reply != NULL) {
                dbus_message_unref (reply);
        }

        if (dbus_error_is_set (&dbus_error)) {
                g_debug ("%s raised:\n %s\n\n", dbus_error.name, dbus_error.message);
                dbus_error_free (&dbus_error);
        }

out:
        g_free (session_id);
}

static gboolean
seat_can_activate_sessions (DBusConnection *connection,
                            const char     *seat_id)
{
        DBusError       local_error;
        DBusMessage    *message;
        DBusMessage    *reply;
        DBusMessageIter iter;
        gboolean        can_activate;

        can_activate = FALSE;
        reply = NULL;

        dbus_error_init (&local_error);
        message = dbus_message_new_method_call (CK_NAME,
                                                seat_id,
                                                CK_SEAT_INTERFACE,
                                                "CanActivateSessions");
        if (message == NULL) {
                goto out;
        }

        dbus_error_init (&local_error);
        reply = dbus_connection_send_with_reply_and_block (connection,
                                                           message,
                                                           -1,
                                                           &local_error);
        if (reply == NULL) {
                if (dbus_error_is_set (&local_error)) {
                        g_warning ("Unable to activate session: %s", local_error.message);
                        dbus_error_free (&local_error);
                        goto out;
                }
        }

        dbus_message_iter_init (reply, &iter);
        dbus_message_iter_get_basic (&iter, &can_activate);

 out:
        if (message != NULL) {
                dbus_message_unref (message);
        }
        if (reply != NULL) {
                dbus_message_unref (reply);
        }

        return can_activate;
}

static gboolean
gsm_consolekit_can_switch_user (GsmSystem *system)
{
        GsmConsolekit *manager = GSM_CONSOLEKIT (system);
        GError  *error;
        char    *seat_id;
        gboolean ret;

        error = NULL;

        if (!gsm_consolekit_ensure_ck_connection (manager, &error)) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           error->message);
                g_error_free (error);
                return FALSE;
        }

        seat_id = get_current_seat_id (dbus_g_connection_get_connection (manager->priv->dbus_connection));
        if (seat_id == NULL || seat_id[0] == '\0') {
                g_debug ("seat id is not set; can't switch sessions");
                return FALSE;
        }

        ret = seat_can_activate_sessions (dbus_g_connection_get_connection (manager->priv->dbus_connection),
                                          seat_id);
        g_free (seat_id);

        return ret;
}

static gboolean
gsm_consolekit_can_restart (GsmSystem *system)
{
        GsmConsolekit *manager = GSM_CONSOLEKIT (system);
        gboolean res;
        gboolean can_restart;
        GError  *error;

        error = NULL;

        if (!gsm_consolekit_ensure_ck_connection (manager, &error)) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           error->message);
                g_error_free (error);
                return FALSE;
        }

        res = dbus_g_proxy_call_with_timeout (manager->priv->ck_proxy,
                                              "CanRestart",
                                              INT_MAX,
                                              &error,
                                              G_TYPE_INVALID,
                                              G_TYPE_BOOLEAN, &can_restart,
                                              G_TYPE_INVALID);

        if (!res) {
                g_warning ("Could not query CanRestart from ConsoleKit: %s",
                           error->message);
                g_error_free (error);
                return FALSE;
        }

        return can_restart;
}

static gboolean
gsm_consolekit_can_stop (GsmSystem *system)
{
        GsmConsolekit *manager = GSM_CONSOLEKIT (system);
        gboolean res;
        gboolean can_stop;
        GError  *error;

        error = NULL;

        if (!gsm_consolekit_ensure_ck_connection (manager, &error)) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           error->message);
                g_error_free (error);
                return FALSE;
        }

        res = dbus_g_proxy_call_with_timeout (manager->priv->ck_proxy,
                                              "CanStop",
                                              INT_MAX,
                                              &error,
                                              G_TYPE_INVALID,
                                              G_TYPE_BOOLEAN, &can_stop,
                                              G_TYPE_INVALID);

        if (!res) {
                g_warning ("Could not query CanStop from ConsoleKit: %s",
                           error->message);
                g_error_free (error);
                return FALSE;
        }
        return can_stop;
}

static gchar *
gsm_consolekit_get_current_session_type (GsmConsolekit *manager)
{
        GError *gerror;
        DBusConnection *connection;
        DBusError error;
        DBusMessage *message = NULL;
        DBusMessage *reply = NULL;
        gchar *session_id;
        gchar *ret;
        DBusMessageIter iter;
        const char *value;

        session_id = NULL;
        ret = NULL;
        gerror = NULL;

        if (!gsm_consolekit_ensure_ck_connection (manager, &gerror)) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           gerror->message);
                g_error_free (gerror);
                goto out;
        }

        connection = dbus_g_connection_get_connection (manager->priv->dbus_connection);
        if (!get_current_session_id (connection, &session_id)) {
                goto out;
        }

        dbus_error_init (&error);
        message = dbus_message_new_method_call (CK_NAME,
                                                session_id,
                                                CK_SESSION_INTERFACE,
                                                "GetSessionType");
        if (message == NULL) {
                goto out;
        }

        reply = dbus_connection_send_with_reply_and_block (connection,
                                                           message,
                                                           -1,
                                                           &error);

        if (reply == NULL) {
                if (dbus_error_is_set (&error)) {
                        g_warning ("Unable to determine session type: %s", error.message);
                        dbus_error_free (&error);
                }
                goto out;
        }

        dbus_message_iter_init (reply, &iter);
        dbus_message_iter_get_basic (&iter, &value);
        ret = g_strdup (value);

out:
        if (message != NULL) {
                dbus_message_unref (message);
        }
        if (reply != NULL) {
                dbus_message_unref (reply);
        }
        g_free (session_id);

        return ret;
}

static gboolean
gsm_consolekit_is_login_session (GsmSystem *system)
{
        GsmConsolekit *consolekit = GSM_CONSOLEKIT (system);
        char *session_type;
        gboolean ret;

        session_type = gsm_consolekit_get_current_session_type (consolekit);

        ret = (g_strcmp0 (session_type, GSM_CONSOLEKIT_SESSION_TYPE_LOGIN_WINDOW) == 0);

        g_free (session_type);

        return ret;
}

static gboolean
gsm_consolekit_can_suspend (GsmSystem *system)
{
#ifdef HAVE_OLD_UPOWER
        GsmConsolekit *consolekit = GSM_CONSOLEKIT (system);
        return up_client_get_can_suspend (consolekit->priv->up_client);
#else
        return FALSE;
#endif
}

static gboolean
gsm_consolekit_can_hibernate (GsmSystem *system)
{
#ifdef HAVE_OLD_UPOWER
        GsmConsolekit *consolekit = GSM_CONSOLEKIT (system);
        return up_client_get_can_hibernate (consolekit->priv->up_client);
#else
        return FALSE;
#endif
}

static void
gsm_consolekit_suspend (GsmSystem *system)
{
#ifdef HAVE_OLD_UPOWER
        GsmConsolekit *consolekit = GSM_CONSOLEKIT (system);
        GError *error = NULL;
        gboolean ret;

        ret = up_client_suspend_sync (consolekit->priv->up_client, NULL, &error);
        if (!ret) {
                g_warning ("Unexpected suspend failure: %s", error->message);
                g_error_free (error);
        }
#endif
}

static void
gsm_consolekit_hibernate (GsmSystem *system)
{
#ifdef HAVE_OLD_UPOWER
        GsmConsolekit *consolekit = GSM_CONSOLEKIT (system);
        GError *error = NULL;
        gboolean ret;

        ret = up_client_hibernate_sync (consolekit->priv->up_client, NULL, &error);
        if (!ret) {
                g_warning ("Unexpected hibernate failure: %s", error->message);
                g_error_free (error);
        }
#endif
}

static void
gsm_consolekit_add_inhibitor (GsmSystem        *system,
                              const gchar      *id,
                              GsmInhibitorFlag  flag)
{
}

static void
gsm_consolekit_remove_inhibitor (GsmSystem   *system,
                                 const gchar *id)
{
}

static void
gsm_consolekit_prepare_shutdown (GsmSystem *system,
                                 gboolean   restart)
{
        GsmConsolekit *consolekit = GSM_CONSOLEKIT (system);

        consolekit->priv->restarting = restart;
        g_signal_emit_by_name (system, "shutdown-prepared", TRUE);
}

static void
gsm_consolekit_complete_shutdown (GsmSystem *system)
{
        GsmConsolekit *consolekit = GSM_CONSOLEKIT (system);

        if (consolekit->priv->restarting)
                gsm_consolekit_attempt_restart (system);
        else
                gsm_consolekit_attempt_stop (system);
}

static gboolean
gsm_consolekit_is_last_session_for_user (GsmSystem *system)
{
        return FALSE;
}

static void
gsm_consolekit_system_init (GsmSystemInterface *iface)
{
        iface->can_switch_user = gsm_consolekit_can_switch_user;
        iface->can_stop = gsm_consolekit_can_stop;
        iface->can_restart = gsm_consolekit_can_restart;
        iface->can_suspend = gsm_consolekit_can_suspend;
        iface->can_hibernate = gsm_consolekit_can_hibernate;
        iface->attempt_stop = gsm_consolekit_attempt_stop;
        iface->attempt_restart = gsm_consolekit_attempt_restart;
        iface->suspend = gsm_consolekit_suspend;
        iface->hibernate = gsm_consolekit_hibernate;
        iface->set_session_idle = gsm_consolekit_set_session_idle;
        iface->is_login_session = gsm_consolekit_is_login_session;
        iface->add_inhibitor = gsm_consolekit_add_inhibitor;
        iface->remove_inhibitor = gsm_consolekit_remove_inhibitor;
        iface->prepare_shutdown = gsm_consolekit_prepare_shutdown;
        iface->complete_shutdown = gsm_consolekit_complete_shutdown;
        iface->is_last_session_for_user = gsm_consolekit_is_last_session_for_user;
}

GsmConsolekit *
gsm_consolekit_new (void)
{
        GsmConsolekit *manager;

        manager = g_object_new (GSM_TYPE_CONSOLEKIT, NULL);

        return manager;
}
