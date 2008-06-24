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


#ifndef __GSM_INHIBITOR_STORE_H
#define __GSM_INHIBITOR_STORE_H

#include <glib-object.h>
#include "gsm-inhibitor.h"

G_BEGIN_DECLS

#define GSM_TYPE_INHIBITOR_STORE         (gsm_inhibitor_store_get_type ())
#define GSM_INHIBITOR_STORE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GSM_TYPE_INHIBITOR_STORE, GsmInhibitorStore))
#define GSM_INHIBITOR_STORE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GSM_TYPE_INHIBITOR_STORE, GsmInhibitorStoreClass))
#define GSM_IS_INHIBITOR_STORE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GSM_TYPE_INHIBITOR_STORE))
#define GSM_IS_INHIBITOR_STORE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GSM_TYPE_INHIBITOR_STORE))
#define GSM_INHIBITOR_STORE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GSM_TYPE_INHIBITOR_STORE, GsmInhibitorStoreClass))

typedef struct GsmInhibitorStorePrivate GsmInhibitorStorePrivate;

typedef struct
{
        GObject                   parent;
        GsmInhibitorStorePrivate *priv;
} GsmInhibitorStore;

typedef struct
{
        GObjectClass   parent_class;

        void          (* inhibitor_added)    (GsmInhibitorStore *inhibitor_store,
                                              guint              cookie);
        void          (* inhibitor_removed)  (GsmInhibitorStore *inhibitor_store,
                                              guint              cookie);
} GsmInhibitorStoreClass;

typedef enum
{
         GSM_INHIBITOR_STORE_ERROR_GENERAL
} GsmInhibitorStoreError;

#define GSM_INHIBITOR_STORE_ERROR gsm_inhibitor_store_error_quark ()

typedef gboolean (*GsmInhibitorStoreFunc) (guint        *cookie,
                                           GsmInhibitor *inhibitor,
                                           gpointer      user_data);

GQuark              gsm_inhibitor_store_error_quark              (void);
GType               gsm_inhibitor_store_get_type                 (void);

GsmInhibitorStore * gsm_inhibitor_store_new                      (void);

guint               gsm_inhibitor_store_size                     (GsmInhibitorStore    *store);
gboolean            gsm_inhibitor_store_add                      (GsmInhibitorStore    *store,
                                                                  GsmInhibitor         *inhibitor);
void                gsm_inhibitor_store_clear                    (GsmInhibitorStore    *store);
gboolean            gsm_inhibitor_store_remove                   (GsmInhibitorStore    *store,
                                                                  GsmInhibitor         *inhibitor);
void                gsm_inhibitor_store_foreach                  (GsmInhibitorStore    *store,
                                                                  GsmInhibitorStoreFunc func,
                                                                  gpointer              user_data);
guint               gsm_inhibitor_store_foreach_remove           (GsmInhibitorStore    *store,
                                                                  GsmInhibitorStoreFunc func,
                                                                  gpointer              user_data);
GsmInhibitor *      gsm_inhibitor_store_find                     (GsmInhibitorStore    *store,
                                                                  GsmInhibitorStoreFunc predicate,
                                                                  gpointer              user_data);
GsmInhibitor *      gsm_inhibitor_store_lookup                   (GsmInhibitorStore    *store,
                                                                  guint                *cookie);

G_END_DECLS

#endif /* __GSM_INHIBITOR_STORE_H */
