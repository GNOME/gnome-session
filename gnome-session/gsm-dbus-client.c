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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "gsm-dbus-client.h"
#include "gsm-dbus-client-glue.h"
#include "gsm-marshal.h"

#include "gsm-manager.h"

#define GSM_DBUS_CLIENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSM_TYPE_DBUS_CLIENT, GsmDBusClientPrivate))

#define CLIENT_INTERFACE "org.gnome.SessionManager.DBusClient"

struct GsmDBusClientPrivate
{
        char *bus_name;
};

enum {
        PROP_0,
        PROP_BUS_NAME,
};
enum {
        STOP,
        QUERY_END_SESSION,
        END_SESSION,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GsmDBusClient, gsm_dbus_client, GSM_TYPE_CLIENT)

GQuark
gsm_dbus_client_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("gsm_dbus_client_error");
        }

        return ret;
}

#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
gsm_dbus_client_error_get_type (void)
{
        static GType etype = 0;

        if (etype == 0) {
                static const GEnumValue values[] = {
                        ENUM_ENTRY (GSM_DBUS_CLIENT_ERROR_GENERAL, "GeneralError"),
                        ENUM_ENTRY (GSM_DBUS_CLIENT_ERROR_NOT_CLIENT, "NotClient"),
                        { 0, 0, 0 }
                };

                g_assert (GSM_DBUS_CLIENT_NUM_ERRORS == G_N_ELEMENTS (values) - 1);

                etype = g_enum_register_static ("GsmDbusClientError", values);
        }

        return etype;
}

static GObject *
gsm_dbus_client_constructor (GType                  type,
                             guint                  n_construct_properties,
                             GObjectConstructParam *construct_properties)
{
        GsmDBusClient *client;

        client = GSM_DBUS_CLIENT (G_OBJECT_CLASS (gsm_dbus_client_parent_class)->constructor (type,
                                                                                              n_construct_properties,
                                                                                              construct_properties));



        return G_OBJECT (client);
}

static void
gsm_dbus_client_init (GsmDBusClient *client)
{
        client->priv = GSM_DBUS_CLIENT_GET_PRIVATE (client);
}

static void
gsm_dbus_client_set_bus_name (GsmDBusClient  *client,
                              const char     *bus_name)
{
        g_return_if_fail (GSM_IS_DBUS_CLIENT (client));

        g_free (client->priv->bus_name);

        client->priv->bus_name = g_strdup (bus_name);
        g_object_notify (G_OBJECT (client), "bus-name");
}

const char *
gsm_dbus_client_get_bus_name (GsmDBusClient  *client)
{
        g_return_val_if_fail (GSM_IS_DBUS_CLIENT (client), NULL);

        return client->priv->bus_name;
}

static void
gsm_dbus_client_set_property (GObject       *object,
                              guint          prop_id,
                              const GValue  *value,
                              GParamSpec    *pspec)
{
        GsmDBusClient *self;

        self = GSM_DBUS_CLIENT (object);

        switch (prop_id) {
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
        GsmDBusClient *self;

        self = GSM_DBUS_CLIENT (object);

        switch (prop_id) {
        case PROP_BUS_NAME:
                g_value_set_string (value, self->priv->bus_name);
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

        g_free (client->priv->bus_name);

        G_OBJECT_CLASS (gsm_dbus_client_parent_class)->finalize (object);
}

static gboolean
dbus_client_stop (GsmClient *client,
                  GError   **error)
{
        GsmDBusClient  *dbus_client = (GsmDBusClient *) client;
        DBusMessage    *message;
        gboolean        ret;
        DBusConnection *connection;
        DBusError       local_error;

        ret = FALSE;

        /* unicast the signal to only the registered bus name */
        message = dbus_message_new_signal (gsm_client_get_id (client),
                                           CLIENT_INTERFACE,
                                           "Stop");
        if (message == NULL) {
                goto out;
        }
        if (!dbus_message_set_destination (message, dbus_client->priv->bus_name)) {
                goto out;
        }

        dbus_error_init (&local_error);
        connection = dbus_bus_get (DBUS_BUS_SESSION, &local_error);
        if (dbus_error_is_set (&local_error)) {
                g_warning ("%s", local_error.message);
                dbus_error_free (&local_error);
                goto out;
        }

        if (!dbus_connection_send (connection, message, NULL)) {
                goto out;
        }

        ret = TRUE;

 out:
        if (message != NULL) {
                dbus_message_unref (message);
        }

        return ret;
}

static void
dbus_client_query_end_session (GsmClient *client,
                               guint      flags)
{
        GsmDBusClient  *dbus_client = (GsmDBusClient *) client;
        g_debug ("GsmDBusClient: sending QueryEndSession signal to %s", dbus_client->priv->bus_name);
        /* FIXME: unicast signal */
        g_signal_emit (dbus_client, signals[QUERY_END_SESSION], 0, flags);
}

static void
dbus_client_end_session (GsmClient *client,
                         guint      flags)
{
        GsmDBusClient  *dbus_client = (GsmDBusClient *) client;
        g_debug ("GsmDBusClient: sending EndSession signal to %s", dbus_client->priv->bus_name);
        /* FIXME: unicast signal */
        g_signal_emit (dbus_client, signals[END_SESSION], 0, flags);
}

#if 0
static void
dbus_client_query_end_session (GsmClient *client,
                               guint      flags)
{
        GsmDBusClient  *dbus_client = (GsmDBusClient *) client;
        DBusMessage    *message;
        gboolean        ret;
        DBusConnection *connection;
        DBusError       local_error;
        DBusMessageIter iter;

        ret = FALSE;

        g_debug ("GsmDBusClient: sending QueryEndSession signal to %s", dbus_client->priv->bus_name);

        /* unicast the signal to only the registered bus name */
        message = dbus_message_new_signal (gsm_client_get_id (client),
                                           CLIENT_INTERFACE,
                                           "QueryEndSession");
        if (message == NULL) {
                goto out;
        }
        if (!dbus_message_set_destination (message, dbus_client->priv->bus_name)) {
                goto out;
        }

        dbus_message_iter_init_append (message, &iter);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_UINT32, &flags);

        dbus_error_init (&local_error);
        connection = dbus_bus_get (DBUS_BUS_SESSION, &local_error);
        if (dbus_error_is_set (&local_error)) {
                g_warning ("%s", local_error.message);
                dbus_error_free (&local_error);
                goto out;
        }

        if (!dbus_connection_send (connection, message, NULL)) {
                goto out;
        }

        ret = TRUE;

 out:
        if (message != NULL) {
                dbus_message_unref (message);
        }
}

static void
dbus_client_end_session (GsmClient *client,
                         guint      flags)
{
        GsmDBusClient  *dbus_client = (GsmDBusClient *) client;
        DBusMessage    *message;
        gboolean        ret;
        DBusConnection *connection;
        DBusError       local_error;
        DBusMessageIter iter;

        ret = FALSE;

        /* unicast the signal to only the registered bus name */
        message = dbus_message_new_signal (gsm_client_get_id (client),
                                           CLIENT_INTERFACE,
                                           "EndSession");
        if (message == NULL) {
                goto out;
        }
        if (!dbus_message_set_destination (message, dbus_client->priv->bus_name)) {
                goto out;
        }

        dbus_message_iter_init_append (message, &iter);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_UINT32, &flags);

        dbus_error_init (&local_error);
        connection = dbus_bus_get (DBUS_BUS_SESSION, &local_error);
        if (dbus_error_is_set (&local_error)) {
                g_warning ("%s", local_error.message);
                dbus_error_free (&local_error);
                goto out;
        }

        if (!dbus_connection_send (connection, message, NULL)) {
                goto out;
        }

        ret = TRUE;

 out:
        if (message != NULL) {
                dbus_message_unref (message);
        }
}
#endif

static void
gsm_dbus_client_class_init (GsmDBusClientClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        GsmClientClass *client_class = GSM_CLIENT_CLASS (klass);

        object_class->finalize             = gsm_dbus_client_finalize;
        object_class->constructor          = gsm_dbus_client_constructor;
        object_class->get_property         = gsm_dbus_client_get_property;
        object_class->set_property         = gsm_dbus_client_set_property;

        client_class->impl_stop              = dbus_client_stop;
        client_class->impl_query_end_session = dbus_client_query_end_session;
        client_class->impl_end_session       = dbus_client_end_session;

        signals [STOP] =
                g_signal_new ("stop",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmDBusClientClass, stop),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [QUERY_END_SESSION] =
                g_signal_new ("query-end-session",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmDBusClientClass, query_end_session),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__UINT,
                              G_TYPE_NONE,
                              1, G_TYPE_UINT);
        signals [END_SESSION] =
                g_signal_new ("end-session",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmDBusClientClass, end_session),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__UINT,
                              G_TYPE_NONE,
                              1, G_TYPE_UINT);

        g_object_class_install_property (object_class,
                                         PROP_BUS_NAME,
                                         g_param_spec_string ("bus-name",
                                                              "bus-name",
                                                              "bus-name",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_type_class_add_private (klass, sizeof (GsmDBusClientPrivate));
        dbus_g_object_type_install_info (GSM_TYPE_CLIENT, &dbus_glib_gsm_dbus_client_object_info);
        dbus_g_error_domain_register (GSM_DBUS_CLIENT_ERROR, NULL, GSM_DBUS_CLIENT_TYPE_ERROR);
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

void
gsm_dbus_client_end_session_response (GsmDBusClient         *client,
                                      gboolean               is_ok,
                                      const char            *reason,
                                      DBusGMethodInvocation *context)
{
        const char *sender;

        g_debug ("GsmDBusClient: got EndSessionResponse is-ok:%d reason=%s", is_ok, reason);

        /* make sure it is from our client */
        sender = dbus_g_method_get_sender (context);
        if (sender == NULL
            || client->priv->bus_name == NULL
            || strcmp (sender, client->priv->bus_name) != 0) {
                GError *error;

                error = g_error_new (GSM_DBUS_CLIENT_ERROR,
                                     GSM_DBUS_CLIENT_ERROR_NOT_CLIENT,
                                     "%s",
                                     "Not recognized as the session client");
                dbus_g_method_return_error (context, error);
                g_error_free (error);
                return;
        }

        gdm_client_end_session_response (GSM_CLIENT (client), is_ok, reason);
        dbus_g_method_return (context);
}
