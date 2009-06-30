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

#ifdef HAVE_POLKIT_GNOME
#include <polkit-gnome/polkit-gnome.h>
#endif

#include "gsm-marshal.h"
#include "gsm-consolekit.h"

#define CK_NAME      "org.freedesktop.ConsoleKit"
#define CK_PATH      "/org/freedesktop/ConsoleKit"
#define CK_INTERFACE "org.freedesktop.ConsoleKit"

#define CK_MANAGER_PATH      "/org/freedesktop/ConsoleKit/Manager"
#define CK_MANAGER_INTERFACE "org.freedesktop.ConsoleKit.Manager"
#define CK_SEAT_INTERFACE    "org.freedesktop.ConsoleKit.Seat"
#define CK_SESSION_INTERFACE "org.freedesktop.ConsoleKit.Session"

#define GSM_CONSOLEKIT_GET_PRIVATE(o)                                   \
        (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSM_TYPE_CONSOLEKIT, GsmConsolekitPrivate))

struct _GsmConsolekitPrivate
{
        DBusGConnection *dbus_connection;
        DBusGProxy      *bus_proxy;
        DBusGProxy      *ck_proxy;
        guint32          is_connected : 1;
};

enum {
        PROP_0,
        PROP_IS_CONNECTED
};

enum {
        REQUEST_COMPLETED = 0,
        PRIVILEGES_COMPLETED,
        LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

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

G_DEFINE_TYPE (GsmConsolekit, gsm_consolekit, G_TYPE_OBJECT);

static void
gsm_consolekit_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
        GsmConsolekit *manager = GSM_CONSOLEKIT (object);

        switch (prop_id) {
        case PROP_IS_CONNECTED:
                g_value_set_boolean (value,
                                     manager->priv->is_connected);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object,
                                                   prop_id,
                                                   pspec);
        }
}

static void
gsm_consolekit_class_init (GsmConsolekitClass *manager_class)
{
        GObjectClass *object_class;
        GParamSpec   *param_spec;

        object_class = G_OBJECT_CLASS (manager_class);

        object_class->finalize = gsm_consolekit_finalize;
        object_class->get_property = gsm_consolekit_get_property;

        param_spec = g_param_spec_boolean ("is-connected",
                                           "Is connected",
                                           "Whether the session is connected to ConsoleKit",
                                           FALSE,
                                           G_PARAM_READABLE);

        g_object_class_install_property (object_class, PROP_IS_CONNECTED,
                                         param_spec);

        signals [REQUEST_COMPLETED] =
                g_signal_new ("request-completed",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmConsolekitClass, request_completed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__POINTER,
                              G_TYPE_NONE,
                              1, G_TYPE_POINTER);

        signals [PRIVILEGES_COMPLETED] =
                g_signal_new ("privileges-completed",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmConsolekitClass, privileges_completed),
                              NULL,
                              NULL,
                              gsm_marshal_VOID__BOOLEAN_BOOLEAN_POINTER,
                              G_TYPE_NONE,
                              3, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_POINTER);

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
                return DBUS_HANDLER_RESULT_HANDLED;
        }

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static gboolean
gsm_consolekit_ensure_ck_connection (GsmConsolekit  *manager,
                                     GError        **error)
{
        GError  *connection_error;
        gboolean is_connected;

        connection_error = NULL;

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
                                                         "org.freedesktop.ConsoleKit",
                                                         "/org/freedesktop/ConsoleKit/Manager",
                                                         "org.freedesktop.ConsoleKit.Manager",
                                                         &connection_error);

                if (manager->priv->ck_proxy == NULL) {
                        g_propagate_error (error, connection_error);
                        is_connected = FALSE;
                        goto out;
                }
        }

        is_connected = TRUE;

 out:
        if (manager->priv->is_connected != is_connected) {
                manager->priv->is_connected = is_connected;
                g_object_notify (G_OBJECT (manager), "is-connected");
        }

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
                } else if (manager->priv->bus_proxy == NULL) {
                        if (manager->priv->ck_proxy != NULL) {
                                g_object_unref (manager->priv->ck_proxy);
                                manager->priv->ck_proxy = NULL;
                        }
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

        if (manager->priv->ck_proxy != NULL) {
                g_object_unref (manager->priv->ck_proxy);
                manager->priv->ck_proxy = NULL;
        }

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
        if (manager->priv->bus_proxy != NULL) {
                g_object_unref (manager->priv->bus_proxy);
                manager->priv->bus_proxy = NULL;
        }

        if (manager->priv->ck_proxy != NULL) {
                g_object_unref (manager->priv->ck_proxy);
                manager->priv->ck_proxy = NULL;
        }

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

GQuark
gsm_consolekit_error_quark (void)
{
        static GQuark error_quark = 0;

        if (error_quark == 0) {
                error_quark = g_quark_from_static_string ("gsm-consolekit-error");
        }

        return error_quark;
}

GsmConsolekit *
gsm_consolekit_new (void)
{
        GsmConsolekit *manager;

        manager = g_object_new (GSM_TYPE_CONSOLEKIT, NULL);

        return manager;
}

static gboolean
try_system_stop (DBusGConnection *connection,
                 GError         **error)
{
        DBusGProxy *proxy;
        gboolean    res;

        proxy = dbus_g_proxy_new_for_name (connection,
                                           CK_NAME,
                                           CK_MANAGER_PATH,
                                           CK_MANAGER_INTERFACE);

        res = dbus_g_proxy_call_with_timeout (proxy,
                                              "Stop",
                                              INT_MAX,
                                              error,
                                              /* parameters: */
                                              G_TYPE_INVALID,
                                              /* return values: */
                                              G_TYPE_INVALID);
        return res;
}

static gboolean
try_system_restart (DBusGConnection *connection,
                    GError           **error)
{
        DBusGProxy *proxy;
        gboolean    res;

        proxy = dbus_g_proxy_new_for_name (connection,
                                           CK_NAME,
                                           CK_MANAGER_PATH,
                                           CK_MANAGER_INTERFACE);

        res = dbus_g_proxy_call_with_timeout (proxy,
                                              "Restart",
                                              INT_MAX,
                                              error,
                                              /* parameters: */
                                              G_TYPE_INVALID,
                                              /* return values: */
                                              G_TYPE_INVALID);
        return res;
}

static void
emit_restart_complete (GsmConsolekit *manager,
                       GError        *error)
{
        GError *call_error;

        call_error = NULL;

        if (error != NULL) {
                call_error = g_error_new_literal (GSM_CONSOLEKIT_ERROR,
                                                  GSM_CONSOLEKIT_ERROR_RESTARTING,
                                                  error->message);
        }

        g_signal_emit (G_OBJECT (manager),
                       signals [REQUEST_COMPLETED],
                       0, call_error);

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
                call_error = g_error_new_literal (GSM_CONSOLEKIT_ERROR,
                                                  GSM_CONSOLEKIT_ERROR_STOPPING,
                                                  error->message);
        }

        g_signal_emit (G_OBJECT (manager),
                       signals [REQUEST_COMPLETED],
                       0, call_error);

        if (call_error != NULL) {
                g_error_free (call_error);
        }
}

#ifdef HAVE_POLKIT_GNOME
static void
system_restart_auth_cb (PolKitAction  *action,
                        gboolean       gained_privilege,
                        GError        *error,
                        GsmConsolekit *manager)
{
        GError  *local_error;
        gboolean res;

        if (!gained_privilege) {
                if (error != NULL) {
                        emit_restart_complete (manager, error);
                }

                return;
        }

        local_error = NULL;

        res = try_system_restart (manager->priv->dbus_connection, &local_error);

        if (!res) {
                g_warning ("Unable to restart system: %s", local_error->message);
                emit_restart_complete (manager, local_error);
                g_error_free (local_error);
        } else {
                emit_restart_complete (manager, NULL);
        }
}

static void
system_stop_auth_cb (PolKitAction  *action,
                     gboolean       gained_privilege,
                     GError        *error,
                     GsmConsolekit *manager)
{
        GError  *local_error;
        gboolean res;

        if (!gained_privilege) {
                if (error != NULL) {
                        emit_stop_complete (manager, error);
                }

                return;
        }

        local_error = NULL;

        res = try_system_stop (manager->priv->dbus_connection, &local_error);

        if (!res) {
                g_warning ("Unable to stop system: %s", local_error->message);
                emit_stop_complete (manager, local_error);
                g_error_free (local_error);
        } else {
                emit_stop_complete (manager, NULL);
        }
}

static PolKitAction *
get_action_from_error (GError *error)
{
        PolKitAction *action;
        char         *paction;

        action = polkit_action_new ();

        paction = NULL;
        if (g_str_has_prefix (error->message, "Not privileged for action: ")) {
                paction = g_strdup (error->message + strlen ("Not privileged for action: "));
                if (paction != NULL) {
                        char *p;

                        /* after 0.2.10 the error also includes the PK results */
                        p = strchr (paction, ' ');
                        if (p != NULL) {
                                *p = '\0';
                        }
                }
        }

        polkit_action_set_action_id (action, paction);

        g_free (paction);

        return action;
}
#endif /* HAVE_POLKIT_GNOME */

static void
request_restart_priv (GsmConsolekit *manager,
                      GError        *error)
{
#ifdef HAVE_POLKIT_GNOME
        PolKitAction *action;
        pid_t         pid;
        gboolean      res = FALSE;
        guint         xid;
        GError       *local_error;

        action = get_action_from_error (error);

        xid = 0;
        pid = getpid ();

        local_error = NULL;
        res = polkit_gnome_auth_obtain (action,
                                        xid,
                                        pid,
                                        (PolKitGnomeAuthCB) system_restart_auth_cb,
                                        manager,
                                        &local_error);

        polkit_action_unref (action);

        if (!res) {
                if (local_error != NULL) {
                        g_warning ("Unable to obtain auth to restart system: %s",
                                   local_error->message);

                        emit_restart_complete (manager, local_error);
                        g_error_free (local_error);
                }
        }
#else
        g_assert_not_reached ();
#endif /* HAVE POLKIT */
}

static void
request_stop_priv (GsmConsolekit *manager,
                   GError        *error)
{
#ifdef HAVE_POLKIT_GNOME
        PolKitAction *action;
        pid_t         pid;
        gboolean      res = FALSE;
        guint         xid;
        GError       *local_error;

        action = get_action_from_error (error);

        xid = 0;
        pid = getpid ();

        local_error = NULL;
        res = polkit_gnome_auth_obtain (action,
                                        xid,
                                        pid,
                                        (PolKitGnomeAuthCB) system_stop_auth_cb,
                                        manager,
                                        &local_error);

        polkit_action_unref (action);

        if (!res) {
                if (local_error != NULL) {
                        g_warning ("Unable to obtain auth to stop system: %s",
                                   local_error->message);

                        emit_stop_complete (manager, local_error);
                        g_error_free (local_error);
                }
        }
#else
        g_assert_not_reached ();
#endif /* HAVE POLKIT */
}

void
gsm_consolekit_attempt_restart (GsmConsolekit *manager)
{
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

        res = try_system_restart (manager->priv->dbus_connection, &error);

        if (!res) {
                if (dbus_g_error_has_name (error, "org.freedesktop.ConsoleKit.Manager.NotPrivileged")) {
                        request_restart_priv (manager, error);
                } else {
                        g_warning ("Unable to restart system: %s", error->message);
                        emit_restart_complete (manager, error);
                }

                g_error_free (error);
        } else {
                emit_restart_complete (manager, NULL);
        }
}

void
gsm_consolekit_attempt_stop (GsmConsolekit *manager)
{
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

        res = try_system_stop (manager->priv->dbus_connection, &error);

        if (!res) {
                if (dbus_g_error_has_name (error, "org.freedesktop.ConsoleKit.Manager.NotPrivileged")) {
                        request_stop_priv (manager, error);
                } else {
                        g_warning ("Unable to stop system: %s", error->message);
                        emit_stop_complete (manager, error);
                }

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

void
gsm_consolekit_set_session_idle (GsmConsolekit *manager,
                                 gboolean       is_idle)
{
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

gboolean
gsm_consolekit_can_switch_user (GsmConsolekit *manager)
{
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

#ifdef HAVE_POLKIT_GNOME
static PolKitResult
gsm_consolekit_get_result_for_action (GsmConsolekit *manager,
                                      const char    *action_id)
{
        PolKitGnomeContext *gnome_context;
        PolKitAction *action;
        PolKitCaller *caller;
        DBusError dbus_error;
        PolKitError *error;
        PolKitResult result;

        gnome_context = polkit_gnome_context_get (NULL);

        if (gnome_context == NULL) {
                return POLKIT_RESULT_UNKNOWN;
        }

        if (gnome_context->pk_tracker == NULL) {
                return POLKIT_RESULT_UNKNOWN;
        }

        dbus_error_init (&dbus_error);
        caller = polkit_tracker_get_caller_from_pid (gnome_context->pk_tracker,
                                                     getpid (),
                                                     &dbus_error);
        dbus_error_free (&dbus_error);

        if (caller == NULL) {
                return POLKIT_RESULT_UNKNOWN;
        }

        action = polkit_action_new ();
        if (!polkit_action_set_action_id (action, action_id)) {
                polkit_action_unref (action);
                polkit_caller_unref (caller);
                return POLKIT_RESULT_UNKNOWN;
        }

        error = NULL;
        result = polkit_context_is_caller_authorized (gnome_context->pk_context,
                                                      action, caller, FALSE,
                                                      &error);
        if (polkit_error_is_set (error)) {
                polkit_error_free (error);
        }
        polkit_action_unref (action);
        polkit_caller_unref (caller);

        return result;
}

static gboolean
gsm_consolekit_can_do_action (GsmConsolekit *manager,
                              const char    *action_id)
{
        PolKitResult  result;
        gboolean      res;
        GError       *error;

        error = NULL;
        res = gsm_consolekit_ensure_ck_connection (manager, &error);
        if (!res) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           error->message);
                g_error_free (error);
                return FALSE;
        }

        result = gsm_consolekit_get_result_for_action (manager, action_id);

        return result != POLKIT_RESULT_NO && result != POLKIT_RESULT_UNKNOWN;
}

static gboolean
gsm_consolekit_is_session_for_other_user (GsmConsolekit *manager,
                                          const char    *object_path,
                                          unsigned int   current_uid)
{
        DBusGProxy    *proxy;
        gboolean       res;
        char          *type;
        unsigned int   uid;

        proxy = dbus_g_proxy_new_for_name (manager->priv->dbus_connection,
                                           CK_NAME,
                                           object_path,
                                           CK_SESSION_INTERFACE);

        res = dbus_g_proxy_call_with_timeout (proxy,
                                              "GetUnixUser",
                                              INT_MAX,
                                              NULL,
                                              /* parameters: */
                                              G_TYPE_INVALID,
                                              /* return values: */
                                              G_TYPE_UINT, &uid,
                                              G_TYPE_INVALID);

        /* error is bad: we consider there's another user */
        if (!res)
                return TRUE;

        if (uid == current_uid)
                return FALSE;

        /* filter out login sessions */
        res = dbus_g_proxy_call_with_timeout (proxy,
                                              "GetSessionType",
                                              INT_MAX,
                                              NULL,
                                              /* parameters: */
                                              G_TYPE_INVALID,
                                              /* return values: */
                                              G_TYPE_STRING, &type,
                                              G_TYPE_INVALID);

        /* error is bad: we consider there's another user */
        if (!res)
                return TRUE;

        if (g_strcmp0 (type, GSM_CONSOLEKIT_SESSION_TYPE_LOGIN_WINDOW) == 0) {
                g_free (type);
                return FALSE;
        }

        g_free (type);

        return TRUE;
}

static gboolean
gsm_consolekit_is_single_user (GsmConsolekit *manager)
{
        DBusGProxy   *proxy;
        GError       *error;
        gboolean      res;
        gboolean      single;
        GPtrArray    *array;
        unsigned int  current_uid;
        int           i;

        /* We use the same logic than the one used by ConsoleKit here -- it'd
         * be nice to have a ConsoleKit API to help us, but well...
         * If there's any error, we just assume it's multiple users. */

        proxy = dbus_g_proxy_new_for_name (manager->priv->dbus_connection,
                                           CK_NAME,
                                           CK_MANAGER_PATH,
                                           CK_MANAGER_INTERFACE);

        error = NULL;
        res = dbus_g_proxy_call_with_timeout (proxy,
                                              "GetSessions",
                                              INT_MAX,
                                              &error,
                                              /* parameters: */
                                              G_TYPE_INVALID,
                                              /* return values: */
                                              dbus_g_type_get_collection ("GPtrArray", DBUS_TYPE_G_OBJECT_PATH), &array,
                                              G_TYPE_INVALID);

        if (!res) {
                g_warning ("Unable to list sessions: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        single = TRUE;
        current_uid = getuid ();

        for (i = 0; i < array->len; i++) {
                char *object_path;

                object_path = g_ptr_array_index (array, i);

                if (gsm_consolekit_is_session_for_other_user (manager,
                                                              object_path,
                                                              current_uid)) {
                        single = FALSE;
                        break;
                }
        }

        g_ptr_array_foreach (array, (GFunc) g_free, NULL);
        g_ptr_array_free (array, TRUE);

        return single;
}

static void
obtain_privileges_cb (PolKitAction  *action,
                      gboolean       gained_privilege,
                      GError        *error,
                      GsmConsolekit *manager)
{
        g_signal_emit (G_OBJECT (manager),
                       signals [PRIVILEGES_COMPLETED],
                       0, gained_privilege, FALSE, error);
}

static gboolean
gsm_consolekit_obtain_privileges_for_action (GsmConsolekit *manager,
                                             const char    *action_id)
{
        PolKitAction *action;
        pid_t         pid;
        guint         xid;
        gboolean      res;

        action = polkit_action_new ();
        polkit_action_set_action_id (action, action_id);

        xid = 0;
        pid = getpid ();

        res = polkit_gnome_auth_obtain (action,
                                        xid,
                                        pid,
                                        (PolKitGnomeAuthCB) obtain_privileges_cb,
                                        manager,
                                        NULL);

        polkit_action_unref (action);

        return res;
}

static gboolean
gsm_consolekit_get_privileges_for_actions (GsmConsolekit *manager,
                                           const char    *single_action_id,
                                           const char    *multiple_action_id)
{
        PolKitResult  result;
        gboolean      res;
        GError       *error;
        const char   *action_id;

        error = NULL;
        res = gsm_consolekit_ensure_ck_connection (manager, &error);
        if (!res) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           error->message);
                g_error_free (error);
                return FALSE;
        }

        if (gsm_consolekit_is_single_user (manager)) {
                action_id = single_action_id;
        } else {
                action_id = multiple_action_id;
        }

        result = gsm_consolekit_get_result_for_action (manager, action_id);

        switch (result) {
        case POLKIT_RESULT_UNKNOWN:
        case POLKIT_RESULT_NO:
                return FALSE;
        case POLKIT_RESULT_ONLY_VIA_ADMIN_AUTH:
        case POLKIT_RESULT_ONLY_VIA_ADMIN_AUTH_KEEP_SESSION:
        case POLKIT_RESULT_ONLY_VIA_ADMIN_AUTH_KEEP_ALWAYS:
        case POLKIT_RESULT_ONLY_VIA_SELF_AUTH:
        case POLKIT_RESULT_ONLY_VIA_SELF_AUTH_KEEP_SESSION:
        case POLKIT_RESULT_ONLY_VIA_SELF_AUTH_KEEP_ALWAYS:
                if (!gsm_consolekit_obtain_privileges_for_action (manager,
                                                                  action_id)) {
                        /* if the call doesn't work, then we were not even able
                         * to do the call requesting the privileges: the setup
                         * is likely broken */
                        return FALSE;
                }
                break;
        case POLKIT_RESULT_YES:
                g_signal_emit (G_OBJECT (manager),
                               signals [PRIVILEGES_COMPLETED],
                               0, TRUE, FALSE, NULL);
                break;
        case POLKIT_RESULT_ONLY_VIA_ADMIN_AUTH_ONE_SHOT:
        case POLKIT_RESULT_ONLY_VIA_SELF_AUTH_ONE_SHOT:
                g_signal_emit (G_OBJECT (manager),
                               signals [PRIVILEGES_COMPLETED],
                               0, TRUE, TRUE, NULL);
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        return TRUE;
}
#endif

gboolean
gsm_consolekit_get_restart_privileges (GsmConsolekit *manager)
{
#ifdef HAVE_POLKIT_GNOME
        return gsm_consolekit_get_privileges_for_actions (manager,
                                                          "org.freedesktop.consolekit.system.restart",
                                                          "org.freedesktop.consolekit.system.restart-multiple-users");
#else
        g_debug ("GsmConsolekit: built without PolicyKit-gnome support");
        return FALSE;
#endif
}

gboolean
gsm_consolekit_get_stop_privileges (GsmConsolekit *manager)
{
#ifdef HAVE_POLKIT_GNOME
        return gsm_consolekit_get_privileges_for_actions (manager,
                                                          "org.freedesktop.consolekit.system.stop",
                                                          "org.freedesktop.consolekit.system.stop-multiple-users");
#else
        g_debug ("GsmConsolekit: built without PolicyKit-gnome support");
        return FALSE;
#endif
}

gboolean
gsm_consolekit_can_restart (GsmConsolekit *manager)
{
#ifdef HAVE_POLKIT_GNOME
        return gsm_consolekit_can_do_action (manager, "org.freedesktop.consolekit.system.restart") ||
               gsm_consolekit_can_do_action (manager, "org.freedesktop.consolekit.system.restart-multiple-users");
#else
        g_debug ("GsmConsolekit: built without PolicyKit-gnome support - cannot restart system");
        return FALSE;
#endif
}

gboolean
gsm_consolekit_can_stop (GsmConsolekit *manager)
{
#ifdef HAVE_POLKIT_GNOME
        return gsm_consolekit_can_do_action (manager, "org.freedesktop.consolekit.system.stop") ||
               gsm_consolekit_can_do_action (manager, "org.freedesktop.consolekit.system.stop-multiple-users");
#else
        g_debug ("GsmConsolekit: built without PolicyKit-gnome support - cannot stop system");
        return FALSE;
#endif
}

gchar *
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


GsmConsolekit *
gsm_get_consolekit (void)
{
        static GsmConsolekit *manager = NULL;

        if (manager == NULL) {
                manager = gsm_consolekit_new ();
        }

        return g_object_ref (manager);
}
