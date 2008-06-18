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

#include "gsm-method-client.h"
#include "gsm-marshal.h"

#include "gsm-manager.h"

#define GSM_METHOD_CLIENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSM_TYPE_METHOD_CLIENT, GsmMethodClientPrivate))

struct GsmMethodClientPrivate
{
        gpointer dummy;
};

enum {
        PROP_0,
};

G_DEFINE_TYPE (GsmMethodClient, gsm_method_client, GSM_TYPE_DBUS_CLIENT)

static GObject *
gsm_method_client_constructor (GType                  type,
                               guint                  n_construct_properties,
                               GObjectConstructParam *construct_properties)
{
        GsmMethodClient *client;

        client = GSM_METHOD_CLIENT (G_OBJECT_CLASS (gsm_method_client_parent_class)->constructor (type,
                                                                                              n_construct_properties,
                                                                                              construct_properties));



        return G_OBJECT (client);
}

static void
gsm_method_client_init (GsmMethodClient *client)
{
        client->priv = GSM_METHOD_CLIENT_GET_PRIVATE (client);
}

static void
gsm_method_client_set_property (GObject       *object,
                                guint          prop_id,
                                const GValue  *value,
                                GParamSpec    *pspec)
{
        GsmMethodClient *self;

        self = GSM_METHOD_CLIENT (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_method_client_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
        GsmMethodClient *self;

        self = GSM_METHOD_CLIENT (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_method_client_finalize (GObject *object)
{
        G_OBJECT_CLASS (gsm_method_client_parent_class)->finalize (object);
}

static gboolean
gsm_method_client_stop (GsmClient *client,
                        GError   **error)
{
        return FALSE;
}

static void
gsm_method_client_class_init (GsmMethodClientClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        GsmClientClass *client_class = GSM_CLIENT_CLASS (klass);

        object_class->finalize             = gsm_method_client_finalize;
        object_class->constructor          = gsm_method_client_constructor;
        object_class->get_property         = gsm_method_client_get_property;
        object_class->set_property         = gsm_method_client_set_property;

        client_class->stop                 = gsm_method_client_stop;

        g_type_class_add_private (klass, sizeof (GsmMethodClientPrivate));
}

GsmClient *
gsm_method_client_new (const char *client_id,
                       const char *bus_name)
{
        GsmMethodClient *method;

        method = g_object_new (GSM_TYPE_METHOD_CLIENT,
                               "client-id", client_id,
                               "bus-name", bus_name,
                               NULL);

        return GSM_CLIENT (method);
}
