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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>

#include <dbus/dbus-glib.h>

#ifdef HAVE_POLKIT_GNOME
#include <polkit-gnome/polkit-gnome.h>
#endif

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
        LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void     gsm_consolekit_class_init   (GsmConsolekitClass *klass);
static void     gsm_consolekit_init         (GsmConsolekit      *ck);
static void     gsm_consolekit_finalize     (GObject              *object);

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

        g_type_class_add_private (manager_class, sizeof (GsmConsolekitPrivate));
}

static gboolean
gsm_consolekit_ensure_ck_connection (GsmConsolekit  *manager,
                                     GError        **error)
{
        GError  *connection_error;
        gboolean is_connected;

        connection_error = NULL;

        if (manager->priv->dbus_connection == NULL) {
                manager->priv->dbus_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM,
                                                                 &connection_error);

                if (manager->priv->dbus_connection == NULL) {
                        g_propagate_error (error, connection_error);
                        is_connected = FALSE;
                        goto out;
                }
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
                g_message ("Could not connect to ConsoleKit: %s",
                           error->message);
                g_error_free (error);
        }
}

static void
gsm_consolekit_finalize (GObject *object)
{
        GsmConsolekit *manager;
        GObjectClass  *parent_class;

        manager = GSM_CONSOLEKIT (object);

        parent_class = G_OBJECT_CLASS (gsm_consolekit_parent_class);

        if (manager->priv->bus_proxy != NULL) {
                g_object_unref (manager->priv->bus_proxy);
        }

        if (manager->priv->ck_proxy != NULL) {
                g_object_unref (manager->priv->ck_proxy);
        }

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
                       const char    *error_message)
{
        GError *call_error;

        call_error = NULL;

        if (error_message != NULL) {
                call_error = g_error_new_literal (GSM_CONSOLEKIT_ERROR,
                                                  GSM_CONSOLEKIT_ERROR_RESTARTING,
                                                  error_message);
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
                        emit_restart_complete (manager, error->message);
                }

                return;
        }

        local_error = NULL;

        res = try_system_restart (manager->priv->dbus_connection, &local_error);

        if (!res) {
                g_warning ("Unable to restart system: %s", local_error->message);
                emit_restart_complete (manager, local_error->message);
                g_error_free (local_error);

                return;
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

                return;
        }
}

static PolKitAction *
get_action_from_error (GError *error)
{
        PolKitAction *action;
        const char   *paction;

        action = polkit_action_new ();

        paction = NULL;

        if (g_str_has_prefix (error->message, "Not privileged for action: ")) {
                paction = error->message + strlen ("Not privileged for action: ");
        }

        polkit_action_set_action_id (action, paction);

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
        char         *error_message = NULL;
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

        if (local_error != NULL) {
                error_message = g_strdup (local_error->message);
                g_error_free (local_error);
        }

        if (!res) {
                emit_restart_complete (manager, error_message);
                g_free (error_message);
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
                g_error_free (error);
                return;
        }

        res = try_system_restart (manager->priv->dbus_connection, &error);

        if (!res) {
                if (dbus_g_error_has_name (error, "org.freedesktop.ConsoleKit.Manager.NotPrivileged")) {
                        request_restart_priv (manager, error);
                } else {
                        emit_restart_complete (manager, error->message);
                }

                g_error_free (error);
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
                g_error_free (error);
                return;
        }

        res = try_system_stop (manager->priv->dbus_connection, &error);

        if (!res) {
                g_warning ("Unable to stop system: %s", error->message);
                if (dbus_g_error_has_name (error, "org.freedesktop.ConsoleKit.Manager.NotPrivileged")) {
                        request_stop_priv (manager, error);
                } else {
                        emit_stop_complete (manager, error);
                }

                g_error_free (error);
        }
}

gboolean
gsm_consolekit_can_restart (GsmConsolekit *manager)
{
#ifdef HAVE_POLKIT_GNOME
        return gsm_consolekit_ensure_ck_connection (manager, NULL);
#else
        return FALSE;
#endif
}

gboolean
gsm_consolekit_can_stop (GsmConsolekit *manager)
{
#ifdef HAVE_POLKIT_GNOME
        return gsm_consolekit_ensure_ck_connection (manager, NULL);
#else
        return FALSE;
#endif
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
