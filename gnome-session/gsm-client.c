/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Novell, Inc.
 * Copyright (C) 2008 Red Hat, Inc.
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <gio/gio.h>

#include "gsm-client.h"
#include "org.gnome.SessionManager.ClientPrivate.h"

struct _GsmClient
{
        GObject                   parent_instance;

        char                     *id;
        char                     *app_id;
        char                     *bus_name;

        GPid                      caller_pid;

        GDBusConnection          *connection;
        GsmExportedClientPrivate *skeleton;
        guint                     watch_id;
};

typedef enum {
        PROP_0,
        PROP_APP_ID,
        PROP_BUS_NAME,
} GsmClientProperty;

enum {
        DISCONNECTED,
        END_SESSION_RESPONSE,
        LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (GsmClient, gsm_client, G_TYPE_OBJECT);

static guint32
get_next_client_serial (void)
{
        static guint32 next_serial = 1;
        guint32 serial;

        serial = next_serial++;

        if ((gint32)next_serial < 0)
                next_serial = 1;

        return serial;
}

static gboolean
handle_end_session_response (GsmExportedClientPrivate *skeleton,
                             GDBusMethodInvocation    *invocation,
                             gboolean                  is_ok,
                             const char               *reason,
                             GsmClient                *client)
{
        g_debug ("GsmClient(%s): got EndSessionResponse is-ok:%d reason=%s",
                 client->app_id, is_ok, reason);

        g_signal_emit (client, signals[END_SESSION_RESPONSE], 0, is_ok, reason);

        gsm_exported_client_private_complete_end_session_response (skeleton, invocation);
        return TRUE;
}

static gboolean
register_client (GsmClient *client)
{
        g_autoptr (GError) error = NULL;

        client->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
        if (error != NULL) {
                g_critical ("GsmClient: Couldn't connect to session bus: %s",
                            error->message);
                return FALSE;
        }

        client->skeleton = gsm_exported_client_private_skeleton_new ();
        g_debug ("exporting client to object path: %s", client->id);
        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (client->skeleton),
                                          client->connection, client->id, &error);
        if (error != NULL) {
                g_critical ("GsmClient: error exporting on session bus: %s",
                            error->message);
                return FALSE;
        }

        g_signal_connect (client->skeleton,
                          "handle-end-session-response",
                          G_CALLBACK (handle_end_session_response),
                          client);

        return TRUE;
}

static void
on_client_vanished (GDBusConnection *connection,
                    const char      *name,
                    gpointer         user_data)
{
        GsmClient *client = user_data;

        g_bus_unwatch_name (client->watch_id);
        client->watch_id = 0;

        client->caller_pid = 0;

        g_signal_emit (client, signals[DISCONNECTED], 0);
}

static GObject *
gsm_client_constructor (GType                  type,
                        guint                  n_construct_properties,
                        GObjectConstructParam *construct_properties)
{
        GsmClient *client;

        client = GSM_CLIENT (G_OBJECT_CLASS (gsm_client_parent_class)->constructor (type,
                                                                                    n_construct_properties,
                                                                                    construct_properties));

        g_free (client->id);
        client->id = g_strdup_printf ("/org/gnome/SessionManager/Client%u", get_next_client_serial ());

        if (!register_client (client)) {
                g_warning ("Unable to register client with session bus");
        }

        client->watch_id = g_bus_watch_name (G_BUS_TYPE_SESSION,
                                             client->bus_name,
                                             G_BUS_NAME_WATCHER_FLAGS_NONE,
                                             NULL,
                                             on_client_vanished,
                                             client,
                                             NULL);

        return G_OBJECT (client);
}

static void
gsm_client_init (GsmClient *client)
{
}

void
gsm_client_set_app_id (GsmClient  *client,
                       const char *app_id)
{
        g_return_if_fail (GSM_IS_CLIENT (client));

        if (g_strcmp0 (client->app_id, app_id) == 0)
                return;

        g_free (client->app_id);
        client->app_id = g_strdup (app_id ?: "");

        g_object_notify (G_OBJECT (client), "app-id");
}

static void
gsm_client_set_bus_name (GsmClient  *client,
                         const char *bus_name)
{
        g_return_if_fail (GSM_IS_CLIENT (client));
        g_return_if_fail (bus_name != NULL);

        if (g_strcmp0 (client->bus_name, bus_name) == 0)
                return;

        g_free (client->bus_name);
        client->bus_name = g_strdup (bus_name);

        g_object_notify (G_OBJECT (client), "bus-name");
}

static void
gsm_client_set_property (GObject       *object,
                         guint          prop_id,
                         const GValue  *value,
                         GParamSpec    *pspec)
{
        GsmClient *self = GSM_CLIENT (object);

        switch ((GsmClientProperty) prop_id) {
        case PROP_APP_ID:
                gsm_client_set_app_id (self, g_value_get_string (value));
                break;
        case PROP_BUS_NAME:
                gsm_client_set_bus_name (self, g_value_get_string (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_client_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
        GsmClient *self = GSM_CLIENT (object);

        switch ((GsmClientProperty) prop_id) {
        case PROP_APP_ID:
                g_value_set_string (value, self->app_id);
                break;
        case PROP_BUS_NAME:
                g_value_set_string (value, self->bus_name);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_client_dispose (GObject *object)
{
        GsmClient *client;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSM_IS_CLIENT (object));
        client = GSM_CLIENT (object);

        g_free (client->id);
        g_free (client->app_id);
        g_free (client->bus_name);

        if (client->skeleton != NULL) {
                g_dbus_interface_skeleton_unexport_from_connection (G_DBUS_INTERFACE_SKELETON (client->skeleton),
                                                                    client->connection);
                g_clear_object (&client->skeleton);
        }

        g_clear_object (&client->connection);

        if (client->watch_id != 0)
                g_bus_unwatch_name (client->watch_id);

        G_OBJECT_CLASS (gsm_client_parent_class)->dispose (object);
}

static void
gsm_client_class_init (GsmClientClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsm_client_get_property;
        object_class->set_property = gsm_client_set_property;
        object_class->constructor = gsm_client_constructor;
        object_class->dispose = gsm_client_dispose;

        signals[DISCONNECTED] =
                g_signal_new ("disconnected",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              0, NULL, NULL, NULL,
                              G_TYPE_NONE,
                              0);
        signals[END_SESSION_RESPONSE] =
                g_signal_new ("end-session-response",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              0, NULL, NULL, NULL,
                              G_TYPE_NONE,
                              2, G_TYPE_BOOLEAN, G_TYPE_STRING);

        g_object_class_install_property (object_class,
                                         PROP_APP_ID,
                                         g_param_spec_string ("app-id",
                                                              "app-id",
                                                              "app-id",
                                                              "",
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY));
        g_object_class_install_property (object_class,
                                         PROP_BUS_NAME,
                                         g_param_spec_string ("bus-name",
                                                              "bus-name",
                                                              "bus-name",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY));
}

const char *
gsm_client_peek_id (GsmClient *client)
{
        g_return_val_if_fail (GSM_IS_CLIENT (client), NULL);
        return client->id;
}

const char *
gsm_client_peek_bus_name (GsmClient *client)
{
        g_return_val_if_fail (GSM_IS_CLIENT (client), NULL);
        return client->bus_name;
}

/**
 * gsm_client_peek_app_id:
 * @client: a #GsmClient.
 *
 * Note that the application ID might not be known; this happens when for XSMP
 * clients that we did not start ourselves, for instance.
 *
 * Returns: the application ID of the client, or %NULL if no such ID is
 * known. The string is owned by @client.
 **/
const char *
gsm_client_peek_app_id (GsmClient *client)
{
        g_return_val_if_fail (GSM_IS_CLIENT (client), NULL);
        return client->app_id;
}

gboolean
gsm_client_cancel_end_session (GsmClient *client,
                               GError   **error)
{
        g_return_val_if_fail (GSM_IS_CLIENT (client), FALSE);

        g_debug ("GsmClient: sending CancelEndSession signal to %s", client->bus_name);

        gsm_exported_client_private_emit_cancel_end_session (client->skeleton);
        return TRUE;
}


gboolean
gsm_client_query_end_session (GsmClient                *client,
                              GsmClientEndSessionFlag   flags,
                              GError                  **error)
{
        g_return_val_if_fail (GSM_IS_CLIENT (client), FALSE);

        g_debug ("GsmClient: sending QueryEndSession signal to %s", client->bus_name);

        gsm_exported_client_private_emit_query_end_session (client->skeleton, flags);
        return TRUE;
}

gboolean
gsm_client_end_session (GsmClient                *client,
                        GsmClientEndSessionFlag   flags,
                        GError                  **error)
{
        g_return_val_if_fail (GSM_IS_CLIENT (client), FALSE);

        g_debug ("GsmClient: sending EndSession signal to %s", client->bus_name);

        gsm_exported_client_private_emit_end_session (client->skeleton, flags);
        return TRUE;
}

gboolean
gsm_client_stop (GsmClient *client,
                 GError   **error)
{
        g_return_val_if_fail (GSM_IS_CLIENT (client), FALSE);

        g_debug ("GsmClient: sending Stop signal to %s", client->bus_name);

        gsm_exported_client_private_emit_stop (client->skeleton);
        return TRUE;
}

GsmClient *
gsm_client_new (const char *app_id,
                const char *bus_name)
{
        return g_object_new (GSM_TYPE_CLIENT,
                             "app-id", app_id,
                             "bus-name", bus_name,
                             NULL);
}
