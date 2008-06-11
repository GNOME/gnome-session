/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Novell, Inc.
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

#ifndef __GSM_APP_H__
#define __GSM_APP_H__

#include <glib-object.h>
#include <sys/types.h>

#include "eggdesktopfile.h"

#include "gsm-manager.h"
#include "gsm-client.h"

G_BEGIN_DECLS

#define GSM_TYPE_APP            (gsm_app_get_type ())
#define GSM_APP(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSM_TYPE_APP, GsmApp))
#define GSM_APP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GSM_TYPE_APP, GsmAppClass))
#define GSM_IS_APP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSM_TYPE_APP))
#define GSM_IS_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GSM_TYPE_APP))
#define GSM_APP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GSM_TYPE_APP, GsmAppClass))

typedef struct _GsmApp        GsmApp;
typedef struct _GsmAppClass   GsmAppClass;
typedef struct _GsmAppPrivate GsmAppPrivate;

struct _GsmApp
{
        GObject         parent;

        EggDesktopFile *desktop_file;
        GsmManagerPhase phase;

        pid_t           pid;
        char           *startup_id;
        char           *client_id;
};

struct _GsmAppClass
{
        GObjectClass parent_class;

        /* signals */
        void        (*exited)       (GsmApp *app, int status);
        void        (*registered)   (GsmApp *app);

        /* virtual methods */
        const char *(*get_basename) (GsmApp *app);
        gboolean    (*is_disabled)  (GsmApp *app);
        pid_t       (*launch)       (GsmApp *app, GError **err);
        void        (*set_client)   (GsmApp *app, GsmClient *client);
};

GType            gsm_app_get_type        (void) G_GNUC_CONST;

const char      *gsm_app_get_basename    (GsmApp     *app);
GsmManagerPhase  gsm_app_get_phase       (GsmApp     *app);
gboolean         gsm_app_provides        (GsmApp     *app,
                                          const char *service);
gboolean         gsm_app_is_disabled     (GsmApp     *app);
pid_t            gsm_app_launch          (GsmApp     *app,
                                          GError    **err);
void             gsm_app_set_client      (GsmApp     *app,
                                          GsmClient  *client);

void             gsm_app_registered      (GsmApp     *app);

G_END_DECLS

#endif /* __GSM_APP_H__ */
