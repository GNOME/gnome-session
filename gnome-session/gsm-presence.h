/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Red Hat, Inc.
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

#ifndef __GSM_PRESENCE_H__
#define __GSM_PRESENCE_H__

#include <glib-object.h>
#include <sys/types.h>

#include "gsm-presence-flag.h"

G_BEGIN_DECLS

#define GSM_TYPE_PRESENCE            (gsm_presence_get_type ())
#define GSM_PRESENCE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSM_TYPE_PRESENCE, GsmPresence))
#define GSM_PRESENCE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GSM_TYPE_PRESENCE, GsmPresenceClass))
#define GSM_IS_PRESENCE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSM_TYPE_PRESENCE))
#define GSM_IS_PRESENCE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GSM_TYPE_PRESENCE))
#define GSM_PRESENCE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GSM_TYPE_PRESENCE, GsmPresenceClass))

typedef struct _GsmPresence        GsmPresence;
typedef struct _GsmPresenceClass   GsmPresenceClass;

typedef struct GsmPresencePrivate GsmPresencePrivate;

struct _GsmPresence
{
        GObject             parent;
        GsmPresencePrivate *priv;
};

struct _GsmPresenceClass
{
        GObjectClass parent_class;

        void          (* status_changed)        (GsmPresence     *presence,
                                                 guint            status);
};

typedef enum
{
        GSM_PRESENCE_ERROR_GENERAL = 0,
        GSM_PRESENCE_NUM_ERRORS
} GsmPresenceError;

#define GSM_PRESENCE_ERROR gsm_presence_error_quark ()
GQuark         gsm_presence_error_quark          (void);

GType          gsm_presence_get_type             (void) G_GNUC_CONST;

GsmPresence *  gsm_presence_new                  (void);

void           gsm_presence_set_idle_enabled     (GsmPresence  *presence,
                                                  gboolean      enabled);
void           gsm_presence_set_idle_timeout     (GsmPresence  *presence,
                                                  guint         n_seconds);

G_END_DECLS

#endif /* __GSM_PRESENCE_H__ */
