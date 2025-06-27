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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __GSM_INHIBITOR_H__
#define __GSM_INHIBITOR_H__

#include <glib-object.h>
#include <sys/types.h>

#include "gsm-inhibitor-flag.h"

G_BEGIN_DECLS

#define GSM_TYPE_INHIBITOR            (gsm_inhibitor_get_type ())
#define GSM_INHIBITOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSM_TYPE_INHIBITOR, GsmInhibitor))
#define GSM_INHIBITOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GSM_TYPE_INHIBITOR, GsmInhibitorClass))
#define GSM_IS_INHIBITOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSM_TYPE_INHIBITOR))
#define GSM_IS_INHIBITOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GSM_TYPE_INHIBITOR))
#define GSM_INHIBITOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GSM_TYPE_INHIBITOR, GsmInhibitorClass))

typedef struct _GsmInhibitor        GsmInhibitor;
typedef struct _GsmInhibitorClass   GsmInhibitorClass;

typedef struct GsmInhibitorPrivate GsmInhibitorPrivate;

struct _GsmInhibitor
{
        GObject              parent;
        GsmInhibitorPrivate *priv;
};

struct _GsmInhibitorClass
{
        GObjectClass parent_class;
};

typedef enum
{
        GSM_INHIBITOR_ERROR_GENERAL = 0,
        GSM_INHIBITOR_ERROR_NOT_SET,
        GSM_INHIBITOR_NUM_ERRORS
} GsmInhibitorError;

#define GSM_INHIBITOR_ERROR gsm_inhibitor_error_quark ()
GQuark         gsm_inhibitor_error_quark          (void);

GType          gsm_inhibitor_get_type             (void) G_GNUC_CONST;

GsmInhibitor * gsm_inhibitor_new                  (const char    *app_id,
                                                   guint          flags,
                                                   const char    *reason,
                                                   const char    *bus_name,
                                                   guint          cookie);
GsmInhibitor * gsm_inhibitor_new_for_client       (const char    *client_id,
                                                   const char    *app_id,
                                                   guint          flags,
                                                   const char    *reason,
                                                   const char    *bus_name,
                                                   guint          cookie);

const char *   gsm_inhibitor_peek_id              (GsmInhibitor  *inhibitor);
const char *   gsm_inhibitor_peek_app_id          (GsmInhibitor  *inhibitor);
const char *   gsm_inhibitor_peek_client_id       (GsmInhibitor  *inhibitor);
const char *   gsm_inhibitor_peek_reason          (GsmInhibitor  *inhibitor);
const char *   gsm_inhibitor_peek_bus_name        (GsmInhibitor  *inhibitor);
guint          gsm_inhibitor_peek_cookie          (GsmInhibitor  *inhibitor);
guint          gsm_inhibitor_peek_flags           (GsmInhibitor  *inhibitor);

G_END_DECLS

#endif /* __GSM_INHIBITOR_H__ */
