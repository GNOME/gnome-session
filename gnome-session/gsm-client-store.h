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


#ifndef __GSM_CLIENT_STORE_H
#define __GSM_CLIENT_STORE_H

#include <glib-object.h>
#include "gsm-client.h"

G_BEGIN_DECLS

#define GSM_TYPE_CLIENT_STORE         (gsm_client_store_get_type ())
#define GSM_CLIENT_STORE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GSM_TYPE_CLIENT_STORE, GsmClientStore))
#define GSM_CLIENT_STORE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GSM_TYPE_CLIENT_STORE, GsmClientStoreClass))
#define GSM_IS_CLIENT_STORE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GSM_TYPE_CLIENT_STORE))
#define GSM_IS_CLIENT_STORE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GSM_TYPE_CLIENT_STORE))
#define GSM_CLIENT_STORE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GSM_TYPE_CLIENT_STORE, GsmClientStoreClass))

typedef struct GsmClientStorePrivate GsmClientStorePrivate;

typedef struct
{
        GObject                parent;
        GsmClientStorePrivate *priv;
} GsmClientStore;

typedef struct
{
        GObjectClass   parent_class;

        void          (* client_added)    (GsmClientStore *client_store,
                                           const char     *id);
        void          (* client_removed)  (GsmClientStore *client_store,
                                           const char     *id);
} GsmClientStoreClass;

typedef enum
{
         GSM_CLIENT_STORE_ERROR_GENERAL
} GsmClientStoreError;

#define GSM_CLIENT_STORE_ERROR gsm_client_store_error_quark ()

typedef gboolean (*GsmClientStoreFunc) (const char *id,
                                        GsmClient  *client,
                                        gpointer    user_data);

GQuark              gsm_client_store_error_quark              (void);
GType               gsm_client_store_get_type                 (void);

GsmClientStore *    gsm_client_store_new                      (void);

gboolean            gsm_client_store_get_locked               (GsmClientStore    *store);
void                gsm_client_store_set_locked               (GsmClientStore    *store,
                                                               gboolean           locked);

guint               gsm_client_store_size                     (GsmClientStore    *store);
gboolean            gsm_client_store_add                      (GsmClientStore    *store,
                                                               GsmClient         *client);
void                gsm_client_store_clear                    (GsmClientStore    *store);
gboolean            gsm_client_store_remove                   (GsmClientStore    *store,
                                                               GsmClient         *client);
void                gsm_client_store_foreach                  (GsmClientStore    *store,
                                                               GsmClientStoreFunc func,
                                                               gpointer           user_data);
guint               gsm_client_store_foreach_remove           (GsmClientStore    *store,
                                                               GsmClientStoreFunc func,
                                                               gpointer           user_data);
GsmClient *         gsm_client_store_find                     (GsmClientStore    *store,
                                                               GsmClientStoreFunc predicate,
                                                               gpointer           user_data);
GsmClient *         gsm_client_store_lookup                   (GsmClientStore    *store,
                                                               const char        *id);


G_END_DECLS

#endif /* __GSM_CLIENT_STORE_H */
