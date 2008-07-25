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

#include "gsm-inhibitor-store.h"

#define GSM_INHIBITOR_STORE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSM_TYPE_INHIBITOR_STORE, GsmInhibitorStorePrivate))

struct GsmInhibitorStorePrivate
{
        GHashTable *inhibitors;
};

enum {
        INHIBITOR_ADDED,
        INHIBITOR_REMOVED,
        LAST_SIGNAL
};

enum {
        PROP_0,
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gsm_inhibitor_store_class_init    (GsmInhibitorStoreClass *klass);
static void     gsm_inhibitor_store_init          (GsmInhibitorStore      *inhibitor_store);
static void     gsm_inhibitor_store_finalize      (GObject             *object);

G_DEFINE_TYPE (GsmInhibitorStore, gsm_inhibitor_store, G_TYPE_OBJECT)

GQuark
gsm_inhibitor_store_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("gsm_inhibitor_store_error");
        }

        return ret;
}

guint
gsm_inhibitor_store_size (GsmInhibitorStore    *store)
{
        return g_hash_table_size (store->priv->inhibitors);
}

gboolean
gsm_inhibitor_store_remove (GsmInhibitorStore    *store,
                            GsmInhibitor         *inhibitor)
{
        GsmInhibitor *found;
        gboolean      removed;
        guint         cookie;

        g_return_val_if_fail (store != NULL, FALSE);

        cookie = gsm_inhibitor_get_cookie (inhibitor);

        found = g_hash_table_lookup (store->priv->inhibitors,
                                     GUINT_TO_POINTER (cookie));
        if (found == NULL) {
                return FALSE;
        }

        g_signal_emit (store, signals [INHIBITOR_REMOVED], 0, cookie);

        removed = g_hash_table_remove (store->priv->inhibitors,
                                       GUINT_TO_POINTER (cookie));
        g_assert (removed);

        return TRUE;
}

void
gsm_inhibitor_store_foreach (GsmInhibitorStore    *store,
                             GsmInhibitorStoreFunc func,
                             gpointer              user_data)
{
        g_return_if_fail (store != NULL);
        g_return_if_fail (func != NULL);

        g_hash_table_find (store->priv->inhibitors,
                           (GHRFunc)func,
                           user_data);
}

GsmInhibitor *
gsm_inhibitor_store_find (GsmInhibitorStore    *store,
                          GsmInhibitorStoreFunc predicate,
                          gpointer              user_data)
{
        GsmInhibitor *inhibitor;

        g_return_val_if_fail (store != NULL, NULL);
        g_return_val_if_fail (predicate != NULL, NULL);

        inhibitor = g_hash_table_find (store->priv->inhibitors,
                                       (GHRFunc)predicate,
                                       user_data);
        return inhibitor;
}

GsmInhibitor *
gsm_inhibitor_store_lookup (GsmInhibitorStore *store,
                            guint              cookie)
{
        GsmInhibitor *inhibitor;

        g_return_val_if_fail (store != NULL, NULL);

        inhibitor = g_hash_table_lookup (store->priv->inhibitors,
                                         GUINT_TO_POINTER (cookie));

        return inhibitor;
}

typedef struct
{
        GsmInhibitorStoreFunc func;
        gpointer              user_data;
        GsmInhibitorStore    *store;
} WrapperData;

static gboolean
foreach_remove_wrapper (guint        *cookie,
                        GsmInhibitor *inhibitor,
                        WrapperData  *data)
{
        gboolean res;

        res = (data->func) (cookie, inhibitor, data->user_data);
        if (res) {
                g_signal_emit (data->store, signals [INHIBITOR_REMOVED], 0, gsm_inhibitor_get_cookie (inhibitor));
        }

        return res;
}

guint
gsm_inhibitor_store_foreach_remove (GsmInhibitorStore    *store,
                                    GsmInhibitorStoreFunc func,
                                    gpointer              user_data)
{
        guint       ret;
        WrapperData data;

        g_return_val_if_fail (store != NULL, 0);
        g_return_val_if_fail (func != NULL, 0);

        data.store = store;
        data.user_data = user_data;
        data.func = func;

        ret = g_hash_table_foreach_remove (store->priv->inhibitors,
                                           (GHRFunc)foreach_remove_wrapper,
                                           &data);

        return ret;
}

static gboolean
_remove_all (guint        *cookie,
             GsmInhibitor *inhibitor,
             gpointer      data)
{
        return TRUE;
}

void
gsm_inhibitor_store_clear (GsmInhibitorStore    *store)
{
        g_return_if_fail (store != NULL);

        g_debug ("GsmInhibitorStore: Clearing inhibitor store");

        gsm_inhibitor_store_foreach_remove (store,
                                            _remove_all,
                                            NULL);
}

gboolean
gsm_inhibitor_store_add (GsmInhibitorStore *store,
                         GsmInhibitor      *inhibitor)
{
        guint cookie;

        g_return_val_if_fail (store != NULL, FALSE);
        g_return_val_if_fail (inhibitor != NULL, FALSE);

        cookie = gsm_inhibitor_get_cookie (inhibitor);

        g_debug ("GsmInhibitorStore: Adding inhibitor %u to store", cookie);
        g_hash_table_insert (store->priv->inhibitors,
                             GUINT_TO_POINTER (cookie),
                             g_object_ref (inhibitor));

        g_signal_emit (store, signals [INHIBITOR_ADDED], 0, cookie);

        return TRUE;
}

static void
gsm_inhibitor_store_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
        GsmInhibitorStore *self;

        self = GSM_INHIBITOR_STORE (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_inhibitor_store_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
        GsmInhibitorStore *self;

        self = GSM_INHIBITOR_STORE (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_inhibitor_store_class_init (GsmInhibitorStoreClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsm_inhibitor_store_get_property;
        object_class->set_property = gsm_inhibitor_store_set_property;
        object_class->finalize = gsm_inhibitor_store_finalize;

        signals [INHIBITOR_ADDED] =
                g_signal_new ("inhibitor-added",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmInhibitorStoreClass, inhibitor_added),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__UINT,
                              G_TYPE_NONE,
                              1, G_TYPE_UINT);
        signals [INHIBITOR_REMOVED] =
                g_signal_new ("inhibitor-removed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmInhibitorStoreClass, inhibitor_removed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__UINT,
                              G_TYPE_NONE,
                              1, G_TYPE_UINT);

        g_type_class_add_private (klass, sizeof (GsmInhibitorStorePrivate));
}

static void
inhibitor_unref (GsmInhibitor *inhibitor)
{
        g_debug ("GsmInhibitorStore: Unreffing inhibitor: %p", inhibitor);
        g_object_unref (inhibitor);
}

static void
gsm_inhibitor_store_init (GsmInhibitorStore *store)
{

        store->priv = GSM_INHIBITOR_STORE_GET_PRIVATE (store);

        store->priv->inhibitors = g_hash_table_new_full (g_direct_hash,
                                                         g_direct_equal,
                                                         NULL,
                                                         (GDestroyNotify) inhibitor_unref);
}

static void
gsm_inhibitor_store_finalize (GObject *object)
{
        GsmInhibitorStore *store;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSM_IS_INHIBITOR_STORE (object));

        store = GSM_INHIBITOR_STORE (object);

        g_return_if_fail (store->priv != NULL);

        g_hash_table_destroy (store->priv->inhibitors);

        G_OBJECT_CLASS (gsm_inhibitor_store_parent_class)->finalize (object);
}

GsmInhibitorStore *
gsm_inhibitor_store_new (void)
{
        GObject *object;

        object = g_object_new (GSM_TYPE_INHIBITOR_STORE,
                               NULL);

        return GSM_INHIBITOR_STORE (object);
}
