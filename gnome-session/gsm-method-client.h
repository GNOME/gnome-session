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

#ifndef __GSM_METHOD_CLIENT_H__
#define __GSM_METHOD_CLIENT_H__

#include "gsm-dbus-client.h"

G_BEGIN_DECLS

#define GSM_TYPE_METHOD_CLIENT            (gsm_method_client_get_type ())
#define GSM_METHOD_CLIENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSM_TYPE_METHOD_CLIENT, GsmMethodClient))
#define GSM_METHOD_CLIENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GSM_TYPE_METHOD_CLIENT, GsmMethodClientClass))
#define GSM_IS_METHOD_CLIENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSM_TYPE_METHOD_CLIENT))
#define GSM_IS_METHOD_CLIENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GSM_TYPE_METHOD_CLIENT))
#define GSM_METHOD_CLIENT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GSM_TYPE_METHOD_CLIENT, GsmMethodClientClass))

typedef struct _GsmMethodClient        GsmMethodClient;
typedef struct _GsmMethodClientClass   GsmMethodClientClass;

typedef struct GsmMethodClientPrivate  GsmMethodClientPrivate;

struct _GsmMethodClient
{
        GsmDBusClient           parent;
        GsmMethodClientPrivate *priv;
};

struct _GsmMethodClientClass
{
        GsmDBusClientClass parent_class;

};

GType          gsm_method_client_get_type           (void) G_GNUC_CONST;

GsmClient     *gsm_method_client_new                (const char *client_id,
                                                     const char *bus_name);

G_END_DECLS

#endif /* __GSM_METHOD_CLIENT_H__ */
