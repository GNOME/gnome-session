/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 William Jon McCann <mccann@jhu.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "gsm-client-store.h"
#include "gsm-client.h"

#define GSM_CLIENT_STORE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSM_TYPE_CLIENT_STORE, GsmClientStorePrivate))

struct GsmClientStorePrivate
{
        GHashTable *clients;
        gboolean    locked;
};

enum {
        CLIENT_ADDED,
        CLIENT_REMOVED,
        LAST_SIGNAL
};

enum {
        PROP_0,
        PROP_LOCKED,
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gsm_client_store_class_init    (GsmClientStoreClass *klass);
static void     gsm_client_store_init          (GsmClientStore      *client_store);
static void     gsm_client_store_finalize      (GObject             *object);

G_DEFINE_TYPE (GsmClientStore, gsm_client_store, G_TYPE_OBJECT)

GQuark
gsm_client_store_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("gsm_client_store_error");
        }

        return ret;
}

guint
gsm_client_store_size (GsmClientStore    *store)
{
        return g_hash_table_size (store->priv->clients);
}

void
gsm_client_store_clear (GsmClientStore    *store)
{
        g_return_if_fail (store != NULL);
        g_debug ("GsmClientStore: Clearing client store");
        g_hash_table_remove_all (store->priv->clients);
}

static gboolean
remove_client (char              *id,
                GsmClient        *client,
                GsmClient        *client_to_remove)
{
        if (client == client_to_remove) {
                return TRUE;
        }
        return FALSE;
}

gboolean
gsm_client_store_remove (GsmClientStore    *store,
                         GsmClient         *client)
{
        g_return_val_if_fail (store != NULL, FALSE);

        gsm_client_store_foreach_remove (store,
                                          (GsmClientStoreFunc)remove_client,
                                          client);
        return FALSE;
}

void
gsm_client_store_foreach (GsmClientStore    *store,
                          GsmClientStoreFunc func,
                          gpointer           user_data)
{
        g_return_if_fail (store != NULL);
        g_return_if_fail (func != NULL);

        g_hash_table_find (store->priv->clients,
                           (GHRFunc)func,
                           user_data);
}

GsmClient *
gsm_client_store_find (GsmClientStore    *store,
                       GsmClientStoreFunc predicate,
                       gpointer           user_data)
{
        GsmClient *client;

        g_return_val_if_fail (store != NULL, NULL);
        g_return_val_if_fail (predicate != NULL, NULL);

        client = g_hash_table_find (store->priv->clients,
                                     (GHRFunc)predicate,
                                     user_data);
        return client;
}

GsmClient *
gsm_client_store_lookup (GsmClientStore    *store,
                         const char        *id)
{
        GsmClient *client;

        g_return_val_if_fail (store != NULL, NULL);
        g_return_val_if_fail (id != NULL, NULL);

        client = g_hash_table_lookup (store->priv->clients,
                                      id);

        return client;
}

guint
gsm_client_store_foreach_remove (GsmClientStore    *store,
                                 GsmClientStoreFunc func,
                                 gpointer           user_data)
{
        guint ret;

        g_return_val_if_fail (store != NULL, 0);
        g_return_val_if_fail (func != NULL, 0);

        ret = g_hash_table_foreach_remove (store->priv->clients,
                                           (GHRFunc)func,
                                           user_data);

        return ret;
}

gboolean
gsm_client_store_add (GsmClientStore *store,
                      GsmClient      *client)
{
        const char *id;

        g_return_val_if_fail (store != NULL, FALSE);
        g_return_val_if_fail (client != NULL, FALSE);

        id = gsm_client_get_id (client);

        /* If we're shutting down, we don't accept any new session
           clients. */
        if (store->priv->locked) {
                return FALSE;
        }

        g_debug ("GsmClientStore: Adding client %s to store", id);

        g_hash_table_insert (store->priv->clients,
                             g_strdup (id),
                             g_object_ref (client));

        g_signal_emit (store, signals [CLIENT_ADDED], 0, id);

        return TRUE;
}

void
gsm_client_store_set_locked (GsmClientStore *store,
                             gboolean        locked)
{
        g_return_if_fail (GSM_IS_CLIENT_STORE (store));

        store->priv->locked = locked;
}

static void
gsm_client_store_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
        GsmClientStore *self;

        self = GSM_CLIENT_STORE (object);

        switch (prop_id) {
        case PROP_LOCKED:
                gsm_client_store_set_locked (self, g_value_get_boolean (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_client_store_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
        GsmClientStore *self;

        self = GSM_CLIENT_STORE (object);

        switch (prop_id) {
        case PROP_LOCKED:
                g_value_set_boolean (value, self->priv->locked);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_client_store_class_init (GsmClientStoreClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsm_client_store_get_property;
        object_class->set_property = gsm_client_store_set_property;
        object_class->finalize = gsm_client_store_finalize;

        signals [CLIENT_ADDED] =
                g_signal_new ("client-added",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmClientStoreClass, client_added),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [CLIENT_REMOVED] =
                g_signal_new ("client-removed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmClientStoreClass, client_removed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        g_object_class_install_property (object_class,
                                         PROP_LOCKED,
                                         g_param_spec_boolean ("locked",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_type_class_add_private (klass, sizeof (GsmClientStorePrivate));
}

static void
client_unref (GsmClient *client)
{
        g_debug ("GsmClientStore: Unreffing client: %p", client);
        g_object_unref (client);
}

static void
gsm_client_store_init (GsmClientStore *store)
{

        store->priv = GSM_CLIENT_STORE_GET_PRIVATE (store);

        store->priv->clients = g_hash_table_new_full (g_str_hash,
                                                      g_str_equal,
                                                      g_free,
                                                      (GDestroyNotify) client_unref);
}

static void
gsm_client_store_finalize (GObject *object)
{
        GsmClientStore *store;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSM_IS_CLIENT_STORE (object));

        store = GSM_CLIENT_STORE (object);

        g_return_if_fail (store->priv != NULL);

        g_hash_table_destroy (store->priv->clients);

        G_OBJECT_CLASS (gsm_client_store_parent_class)->finalize (object);
}

GsmClientStore *
gsm_client_store_new (void)
{
        GObject *object;

        object = g_object_new (GSM_TYPE_CLIENT_STORE,
                               NULL);

        return GSM_CLIENT_STORE (object);
}
