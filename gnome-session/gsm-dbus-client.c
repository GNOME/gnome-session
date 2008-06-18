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

#include "gsm-dbus-client.h"
#include "gsm-marshal.h"

#include "gsm-manager.h"

#define GSM_DBUS_CLIENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSM_TYPE_DBUS_CLIENT, GsmDBusClientPrivate))

struct GsmDBusClientPrivate
{
        char *bus_name;
};

enum {
        PROP_0,
        PROP_BUS_NAME,
};

G_DEFINE_ABSTRACT_TYPE (GsmDBusClient, gsm_dbus_client, GSM_TYPE_CLIENT)

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

static void
gsm_dbus_client_class_init (GsmDBusClientClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        GsmClientClass *client_class = GSM_CLIENT_CLASS (klass);

        object_class->finalize             = gsm_dbus_client_finalize;
        object_class->constructor          = gsm_dbus_client_constructor;
        object_class->get_property         = gsm_dbus_client_get_property;
        object_class->set_property         = gsm_dbus_client_set_property;

        g_object_class_install_property (object_class,
                                         PROP_BUS_NAME,
                                         g_param_spec_string ("bus-name",
                                                              "bus-name",
                                                              "bus-name",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_type_class_add_private (klass, sizeof (GsmDBusClientPrivate));
}
