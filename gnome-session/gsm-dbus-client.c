/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <gio/gio.h>

#include "org.gnome.SessionManager.ClientPrivate.h"
#include "gsm-dbus-client.h"

#include "gsm-manager.h"
#include "gsm-util.h"

#define SM_DBUS_NAME                     "org.gnome.SessionManager"
#define SM_DBUS_CLIENT_PRIVATE_INTERFACE "org.gnome.SessionManager.ClientPrivate"

struct _GsmDBusClient
{
        GObject               parent_instance;

        char                 *bus_name;
        GPid                  caller_pid;
        GsmClientRestartStyle restart_style_hint;

        GDBusConnection      *connection;
        GsmExportedClientPrivate *skeleton;
        guint                 watch_id;
};

typedef enum {
        PROP_BUS_NAME = 1,
} GsmDBusClientProperty;

static GParamSpec *props[PROP_BUS_NAME + 1] = { NULL, };

G_DEFINE_TYPE (GsmDBusClient, gsm_dbus_client, GSM_TYPE_CLIENT)

static gboolean
setup_connection (GsmDBusClient *client)
{
        GError *error = NULL;

        if (client->connection == NULL) {
                client->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
                if (error != NULL) {
                        g_debug ("GsmDbusClient: Couldn't connect to session bus: %s",
                                 error->message);
                        g_error_free (error);
                        return FALSE;
                }
        }

        return TRUE;
}

static gboolean
handle_end_session_response (GsmExportedClientPrivate *skeleton,
                             GDBusMethodInvocation    *invocation,
                             gboolean                  is_ok,
                             const char               *reason,
                             GsmDBusClient            *client)
{
        g_debug ("GsmDBusClient: got EndSessionResponse is-ok:%d reason=%s", is_ok, reason);
        gsm_client_end_session_response (GSM_CLIENT (client),
                                         is_ok, FALSE, FALSE, reason);

        gsm_exported_client_private_complete_end_session_response (skeleton, invocation);
        return TRUE;
}

static GObject *
gsm_dbus_client_constructor (GType                  type,
                             guint                  n_construct_properties,
                             GObjectConstructParam *construct_properties)
{
        GsmDBusClient *client;
        GError *error = NULL;
        GsmExportedClientPrivate *skeleton;

        client = GSM_DBUS_CLIENT (G_OBJECT_CLASS (gsm_dbus_client_parent_class)->constructor (type,
                                                                                              n_construct_properties,
                                                                                              construct_properties));

        if (! setup_connection (client)) {
                g_object_unref (client);
                return NULL;
        }

        skeleton = gsm_exported_client_private_skeleton_new ();
        client->skeleton = skeleton;
        g_debug ("exporting dbus client to object path: %s", gsm_client_peek_id (GSM_CLIENT (client)));
        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (skeleton),
                                          client->connection,
                                          gsm_client_peek_id (GSM_CLIENT (client)),
                                          &error);

        if (error != NULL) {
                g_critical ("error exporting client private on session bus: %s", error->message);
                g_error_free (error);
                g_object_unref (client);
                return NULL;
        }

        g_signal_connect (skeleton, "handle-end-session-response",
                          G_CALLBACK (handle_end_session_response), client);

        return G_OBJECT (client);
}

static void
gsm_dbus_client_init (GsmDBusClient *client)
{
}

/* adapted from PolicyKit */
static gboolean
get_caller_info (GsmDBusClient *client,
                 const char    *sender,
                 uid_t         *calling_uid_out,
                 pid_t         *calling_pid_out)
{
        g_autoptr(GDBusConnection) connection = NULL;
        GError          *error;
        g_autoptr(GVariant) uid_variant = NULL;
        g_autoptr(GVariant) pid_variant = NULL;
        uid_t            uid;
        pid_t            pid;

        if (sender == NULL) {
                return FALSE;
        }

        error = NULL;
        connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);

        if (error != NULL) {
                g_warning ("error getting session bus: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        uid_variant = g_dbus_connection_call_sync (connection,
                                                   "org.freedesktop.DBus",
                                                   "/org/freedesktop/DBus",
                                                   "org.freedesktop.DBus",
                                                   "GetConnectionUnixUser",
                                                   g_variant_new ("(s)", sender),
                                                   G_VARIANT_TYPE ("(u)"),
                                                   G_DBUS_CALL_FLAGS_NONE,
                                                   -1, NULL, &error);

        if (error != NULL) {
                g_debug ("GetConnectionUnixUser() failed: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        pid_variant = g_dbus_connection_call_sync (connection,
                                                   "org.freedesktop.DBus",
                                                   "/org/freedesktop/DBus",
                                                   "org.freedesktop.DBus",
                                                   "GetConnectionUnixProcessID",
                                                   g_variant_new ("(s)", sender),
                                                   G_VARIANT_TYPE ("(u)"),
                                                   G_DBUS_CALL_FLAGS_NONE,
                                                   -1, NULL, &error);

        if (error != NULL) {
                g_debug ("GetConnectionUnixProcessID() failed: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        g_variant_get (uid_variant, "(u)", &uid);
        g_variant_get (pid_variant, "(u)", &pid);

        if (calling_uid_out != NULL) {
                *calling_uid_out = uid;
        }
        if (calling_pid_out != NULL) {
                *calling_pid_out = pid;
        }

        g_debug ("uid = %d", uid);
        g_debug ("pid = %d", pid);

        return TRUE;
}

static void
on_client_vanished (GDBusConnection *connection,
                    const char      *name,
                    gpointer         user_data)
{
        GsmDBusClient  *client = user_data;

        g_bus_unwatch_name (client->watch_id);
        client->watch_id = 0;

        gsm_client_disconnected (GSM_CLIENT (client));
}

static void
gsm_dbus_client_set_bus_name (GsmDBusClient  *client,
                              const char     *bus_name)
{
        g_return_if_fail (GSM_IS_DBUS_CLIENT (client));

        g_free (client->bus_name);

        client->bus_name = g_strdup (bus_name);
        g_object_notify_by_pspec (G_OBJECT (client), props[PROP_BUS_NAME]);

        if (!get_caller_info (client, bus_name, NULL, &client->caller_pid)) {
                client->caller_pid = 0;
        }

        client->watch_id = g_bus_watch_name (G_BUS_TYPE_SESSION,
                                                   bus_name,
                                                   G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                   NULL,
                                                   on_client_vanished,
                                                   client,
                                                   NULL);
}

const char *
gsm_dbus_client_get_bus_name (GsmDBusClient  *client)
{
        g_return_val_if_fail (GSM_IS_DBUS_CLIENT (client), NULL);

        return client->bus_name;
}

static void
gsm_dbus_client_set_property (GObject       *object,
                              guint          prop_id,
                              const GValue  *value,
                              GParamSpec    *pspec)
{
        GsmDBusClient *self = GSM_DBUS_CLIENT (object);

        switch ((GsmDBusClientProperty) prop_id) {
        case PROP_BUS_NAME:
                gsm_dbus_client_set_bus_name (self, g_value_get_string (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_dbus_client_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
        GsmDBusClient *self = GSM_DBUS_CLIENT (object);

        switch ((GsmDBusClientProperty) prop_id) {
        case PROP_BUS_NAME:
                g_value_set_string (value, self->bus_name);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_dbus_client_finalize (GObject *object)
{
        GsmDBusClient *client = (GsmDBusClient *) object;

        g_free (client->bus_name);

        if (client->skeleton != NULL) {
                g_dbus_interface_skeleton_unexport_from_connection (G_DBUS_INTERFACE_SKELETON (client->skeleton),
                                                                    client->connection);
                g_clear_object (&client->skeleton);
        }

        g_clear_object (&client->connection);

        if (client->watch_id != 0)
                g_bus_unwatch_name (client->watch_id);

        G_OBJECT_CLASS (gsm_dbus_client_parent_class)->finalize (object);
}

static GKeyFile *
dbus_client_save (GsmClient *client,
                  GsmApp    *app,
                  GError   **error)
{
        g_debug ("GsmDBusClient: saving client with id %s",
                 gsm_client_peek_id (client));

        /* FIXME: We still don't support client saving for D-Bus
         * session clients */

        return NULL;
}

static gboolean
dbus_client_stop (GsmClient *client,
                  GError   **error)
{
        GsmDBusClient  *dbus_client = (GsmDBusClient *) client;
        gsm_exported_client_private_emit_stop (dbus_client->skeleton);
        return TRUE;
}

static char *
dbus_client_get_app_name (GsmClient *client)
{
        /* Always use app-id instead */
        return NULL;
}

static GsmClientRestartStyle
dbus_client_get_restart_style_hint (GsmClient *client)
{
        return (GSM_DBUS_CLIENT (client)->restart_style_hint);
}

static guint
dbus_client_get_unix_process_id (GsmClient *client)
{
        return (GSM_DBUS_CLIENT (client)->caller_pid);
}

static gboolean
dbus_client_query_end_session (GsmClient                *client,
                               GsmClientEndSessionFlag   flags,
                               GError                  **error)
{
        GsmDBusClient  *dbus_client = (GsmDBusClient *) client;

        if (dbus_client->bus_name == NULL) {
                g_set_error (error,
                             GSM_CLIENT_ERROR,
                             GSM_CLIENT_ERROR_NOT_REGISTERED,
                             "Client is not registered");
                return FALSE;
        }

        g_debug ("GsmDBusClient: sending QueryEndSession signal to %s", dbus_client->bus_name);

        gsm_exported_client_private_emit_query_end_session (dbus_client->skeleton, flags);
        return TRUE;
}

static gboolean
dbus_client_end_session (GsmClient                *client,
                         GsmClientEndSessionFlag   flags,
                         GError                  **error)
{
        GsmDBusClient  *dbus_client = (GsmDBusClient *) client;

        gsm_exported_client_private_emit_end_session (dbus_client->skeleton, flags);
        return TRUE;
}

static gboolean
dbus_client_cancel_end_session (GsmClient *client,
                                GError   **error)
{
        GsmDBusClient  *dbus_client = (GsmDBusClient *) client;
        gsm_exported_client_private_emit_cancel_end_session (dbus_client->skeleton);
        return TRUE;
}

static void
gsm_dbus_client_class_init (GsmDBusClientClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        GsmClientClass *client_class = GSM_CLIENT_CLASS (klass);

        object_class->finalize             = gsm_dbus_client_finalize;
        object_class->constructor          = gsm_dbus_client_constructor;
        object_class->get_property         = gsm_dbus_client_get_property;
        object_class->set_property         = gsm_dbus_client_set_property;

        client_class->impl_save                   = dbus_client_save;
        client_class->impl_stop                   = dbus_client_stop;
        client_class->impl_query_end_session      = dbus_client_query_end_session;
        client_class->impl_end_session            = dbus_client_end_session;
        client_class->impl_cancel_end_session     = dbus_client_cancel_end_session;
        client_class->impl_get_app_name           = dbus_client_get_app_name;
        client_class->impl_get_restart_style_hint = dbus_client_get_restart_style_hint;
        client_class->impl_get_unix_process_id    = dbus_client_get_unix_process_id;

        props[PROP_BUS_NAME] =
                g_param_spec_string ("bus-name",
                                     "bus-name",
                                     "bus-name",
                                     NULL,
                                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

        g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);
}

GsmClient *
gsm_dbus_client_new (const char *startup_id,
                     const char *bus_name)
{
        GsmDBusClient *client;

        client = g_object_new (GSM_TYPE_DBUS_CLIENT,
                               "startup-id", startup_id,
                               "bus-name", bus_name,
                               NULL);

        return GSM_CLIENT (client);
}
