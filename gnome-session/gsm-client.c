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

#include "gsm-client.h"

static guint32 client_serial = 1;

typedef struct
{
        char            *id;
        char            *startup_id;
        char            *app_id;
} GsmClientPrivate;

typedef enum {
        PROP_STARTUP_ID = 1,
        PROP_APP_ID,
} GsmClientProperty;

static GParamSpec *props[PROP_APP_ID + 1] = { NULL, };

enum {
        DISCONNECTED,
        END_SESSION_RESPONSE,
        LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GsmClient, gsm_client, G_TYPE_OBJECT)

static guint32
get_next_client_serial (void)
{
        guint32 serial;

        serial = client_serial++;

        if ((gint32)client_serial < 0) {
                client_serial = 1;
        }

        return serial;
}

static GObject *
gsm_client_constructor (GType                  type,
                        guint                  n_construct_properties,
                        GObjectConstructParam *construct_properties)
{
        GsmClientPrivate *priv;
        GsmClient *client;

        client = GSM_CLIENT (G_OBJECT_CLASS (gsm_client_parent_class)->constructor (type,
                                                                                    n_construct_properties,
                                                                                    construct_properties));
        priv = gsm_client_get_instance_private (client);

        g_free (priv->id);
        priv->id = g_strdup_printf ("/org/gnome/SessionManager/Client%u", get_next_client_serial ());

        return G_OBJECT (client);
}

static void
gsm_client_init (GsmClient *client)
{
}

static void
gsm_client_finalize (GObject *object)
{
        GsmClient *client;
        GsmClientPrivate *priv;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSM_IS_CLIENT (object));

        client = GSM_CLIENT (object);
        priv = gsm_client_get_instance_private (client);

        g_return_if_fail (priv != NULL);

        g_free (priv->id);
        g_free (priv->startup_id);
        g_free (priv->app_id);

        G_OBJECT_CLASS (gsm_client_parent_class)->finalize (object);
}

static void
gsm_client_set_startup_id (GsmClient  *client,
                           const char *startup_id)
{
        GsmClientPrivate *priv = gsm_client_get_instance_private (client);

        g_return_if_fail (GSM_IS_CLIENT (client));

        g_free (priv->startup_id);

        if (startup_id != NULL) {
                priv->startup_id = g_strdup (startup_id);
        } else {
                priv->startup_id = g_strdup ("");
        }
        g_object_notify_by_pspec (G_OBJECT (client), props[PROP_STARTUP_ID]);
}

void
gsm_client_set_app_id (GsmClient  *client,
                       const char *app_id)
{
        GsmClientPrivate *priv = gsm_client_get_instance_private (client);

        g_return_if_fail (GSM_IS_CLIENT (client));

        g_free (priv->app_id);

        if (app_id != NULL) {
                priv->app_id = g_strdup (app_id);
        } else {
                priv->app_id = g_strdup ("");
        }
        g_object_notify_by_pspec (G_OBJECT (client), props[PROP_APP_ID]);
}

static void
gsm_client_set_property (GObject       *object,
                         guint          prop_id,
                         const GValue  *value,
                         GParamSpec    *pspec)
{
        GsmClient *self;

        self = GSM_CLIENT (object);

        switch ((GsmClientProperty) prop_id) {
        case PROP_STARTUP_ID:
                gsm_client_set_startup_id (self, g_value_get_string (value));
                break;
        case PROP_APP_ID:
                gsm_client_set_app_id (self, g_value_get_string (value));
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
        GsmClientPrivate *priv = gsm_client_get_instance_private (self);

        switch ((GsmClientProperty) prop_id) {
        case PROP_STARTUP_ID:
                g_value_set_string (value, priv->startup_id);
                break;
        case PROP_APP_ID:
                g_value_set_string (value, priv->app_id);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static gboolean
default_stop (GsmClient *client,
              GError   **error)
{
        g_return_val_if_fail (GSM_IS_CLIENT (client), FALSE);

        g_warning ("Stop not implemented");

        return TRUE;
}

static void
gsm_client_dispose (GObject *object)
{
        GsmClient *client;
        GsmClientPrivate *priv;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSM_IS_CLIENT (object));

        client = GSM_CLIENT (object);
        priv = gsm_client_get_instance_private (client);

        g_debug ("GsmClient: disposing %s", priv->id);

        G_OBJECT_CLASS (gsm_client_parent_class)->dispose (object);
}

static void
gsm_client_class_init (GsmClientClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsm_client_get_property;
        object_class->set_property = gsm_client_set_property;
        object_class->constructor = gsm_client_constructor;
        object_class->finalize = gsm_client_finalize;
        object_class->dispose = gsm_client_dispose;

        klass->impl_stop = default_stop;

        signals[DISCONNECTED] =
                g_signal_new ("disconnected",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmClientClass, disconnected),
                              NULL, NULL, NULL,
                              G_TYPE_NONE,
                              0);
        signals[END_SESSION_RESPONSE] =
                g_signal_new ("end-session-response",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmClientClass, end_session_response),
                              NULL, NULL, NULL,
                              G_TYPE_NONE,
                              4, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_STRING);

        props[PROP_STARTUP_ID] =
                g_param_spec_string ("startup-id",
                                     "startup-id",
                                     "startup-id",
                                     "",
                                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
        props[PROP_APP_ID] =
                g_param_spec_string ("app-id",
                                     "app-id",
                                     "app-id",
                                     "",
                                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

        g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);
}

const char *
gsm_client_peek_id (GsmClient *client)
{
        GsmClientPrivate *priv = gsm_client_get_instance_private (client);

        g_return_val_if_fail (GSM_IS_CLIENT (client), NULL);

        return priv->id;
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
        GsmClientPrivate *priv = gsm_client_get_instance_private (client);

        g_return_val_if_fail (GSM_IS_CLIENT (client), NULL);

        return priv->app_id;
}

const char *
gsm_client_peek_startup_id (GsmClient *client)
{
        GsmClientPrivate *priv = gsm_client_get_instance_private (client);

        g_return_val_if_fail (GSM_IS_CLIENT (client), NULL);

        return priv->startup_id;
}

/**
 * gsm_client_get_app_name:
 * @client: a #GsmClient.
 *
 * Returns: a copy of the application name of the client, or %NULL if no such
 * name is known.
 **/
char *
gsm_client_get_app_name (GsmClient *client)
{
        g_return_val_if_fail (GSM_IS_CLIENT (client), NULL);

        return GSM_CLIENT_GET_CLASS (client)->impl_get_app_name (client);
}

gboolean
gsm_client_cancel_end_session (GsmClient *client,
                               GError   **error)
{
        g_return_val_if_fail (GSM_IS_CLIENT (client), FALSE);

        return GSM_CLIENT_GET_CLASS (client)->impl_cancel_end_session (client, error);
}


gboolean
gsm_client_query_end_session (GsmClient                *client,
                              GsmClientEndSessionFlag   flags,
                              GError                  **error)
{
        g_return_val_if_fail (GSM_IS_CLIENT (client), FALSE);

        return GSM_CLIENT_GET_CLASS (client)->impl_query_end_session (client, flags, error);
}

gboolean
gsm_client_end_session (GsmClient                *client,
                        GsmClientEndSessionFlag   flags,
                        GError                  **error)
{
        g_return_val_if_fail (GSM_IS_CLIENT (client), FALSE);

        return GSM_CLIENT_GET_CLASS (client)->impl_end_session (client, flags, error);
}

gboolean
gsm_client_stop (GsmClient *client,
                 GError   **error)
{
        g_return_val_if_fail (GSM_IS_CLIENT (client), FALSE);

        return GSM_CLIENT_GET_CLASS (client)->impl_stop (client, error);
}

void
gsm_client_disconnected (GsmClient *client)
{
        g_signal_emit (client, signals[DISCONNECTED], 0);
}

void
gsm_client_end_session_response (GsmClient  *client,
                                 gboolean    is_ok,
                                 gboolean    do_last,
                                 gboolean    cancel,
                                 const char *reason)
{
        g_signal_emit (client, signals[END_SESSION_RESPONSE], 0,
                       is_ok, do_last, cancel, reason);
}
