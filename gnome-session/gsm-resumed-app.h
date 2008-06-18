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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef __GSM_RESUMED_APP_H__
#define __GSM_RESUMED_APP_H__

#include "gsm-app.h"

G_BEGIN_DECLS

#define GSM_TYPE_RESUMED_APP            (gsm_resumed_app_get_type ())
#define GSM_RESUMED_APP(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSM_TYPE_RESUMED_APP, GsmResumedApp))
#define GSM_RESUMED_APP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GSM_TYPE_RESUMED_APP, GsmResumedAppClass))
#define GSM_IS_RESUMED_APP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSM_TYPE_RESUMED_APP))
#define GSM_IS_RESUMED_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GSM_TYPE_RESUMED_APP))
#define GSM_RESUMED_APP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GSM_TYPE_RESUMED_APP, GsmResumedAppClass))

typedef struct _GsmResumedApp        GsmResumedApp;
typedef struct _GsmResumedAppClass   GsmResumedAppClass;
typedef struct _GsmResumedAppPrivate GsmResumedAppPrivate;

struct _GsmResumedApp
{
        GsmApp                parent;
        GsmResumedAppPrivate *priv;
};

struct _GsmResumedAppClass
{
        GsmAppClass parent_class;

        /* signals */

        /* virtual methods */

};

GType   gsm_resumed_app_get_type (void) G_GNUC_CONST;


GsmApp *gsm_resumed_app_new_from_session        (GKeyFile   *session_file,
                                                 const char *group,
                                                 gboolean    discard);

GsmApp *gsm_resumed_app_new_from_legacy_session (GKeyFile   *session_file,
                                                 int         n);

G_END_DECLS

#endif /* __GSM_RESUMED_APP_H__ */
