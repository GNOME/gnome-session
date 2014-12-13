/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Red Hat, Inc.
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
 *
 * Authors:
 *    Ray Strode <rstrode@redhat.com>
 */

#ifndef __GSM_END_SESSION_DIALOG_H__
#define __GSM_END_SESSION_DIALOG_H__

#include <glib.h>
#include <glib-object.h>

#include "gsm-store.h"

G_BEGIN_DECLS

#define GSM_TYPE_END_SESSION_DIALOG            (gsm_end_session_dialog_get_type ())
#define GSM_END_SESSION_DIALOG(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSM_TYPE_END_SESSION_DIALOG, GsmEndSessionDialog))
#define GSM_END_SESSION_DIALOG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GSM_TYPE_END_SESSION_DIALOG, GsmEndSessionDialogClass))
#define GSM_IS_END_SESSION_DIALOG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSM_TYPE_END_SESSION_DIALOG))
#define GSM_IS_END_SESSION_DIALOG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GSM_TYPE_END_SESSION_DIALOG))
#define GSM_END_SESSION_DIALOG_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GSM_TYPE_END_SESSION_DIALOG, GsmEndSessionDialogClass))

typedef struct _GsmEndSessionDialog        GsmEndSessionDialog;
typedef struct _GsmEndSessionDialogClass   GsmEndSessionDialogClass;
typedef struct _GsmEndSessionDialogPrivate GsmEndSessionDialogPrivate;

typedef enum
{
    GSM_END_SESSION_DIALOG_TYPE_LOGOUT = 0,
    GSM_END_SESSION_DIALOG_TYPE_SHUTDOWN,
    GSM_END_SESSION_DIALOG_TYPE_RESTART
} GsmEndSessionDialogType;

struct _GsmEndSessionDialog
{
        GObject                     parent;

        GsmEndSessionDialogPrivate *priv;
};

struct _GsmEndSessionDialogClass
{
        GObjectClass parent_class;

        void (* end_session_dialog_opened)             (GsmEndSessionDialog *dialog);
        void (* end_session_dialog_open_failed)        (GsmEndSessionDialog *dialog);
        void (* end_session_dialog_closed)             (GsmEndSessionDialog *dialog);
        void (* end_session_dialog_canceled)           (GsmEndSessionDialog *dialog);

        void (* end_session_dialog_confirmed_logout)   (GsmEndSessionDialog *dialog);
        void (* end_session_dialog_confirmed_shutdown) (GsmEndSessionDialog *dialog);
        void (* end_session_dialog_confirmed_reboot)   (GsmEndSessionDialog *dialog);
};

GType                gsm_end_session_dialog_get_type (void);

GsmEndSessionDialog *gsm_end_session_dialog_new      (void);

gboolean             gsm_end_session_dialog_open     (GsmEndSessionDialog     *dialog,
                                                      GsmEndSessionDialogType  type,
                                                      GsmStore                *inhibitors);
void                 gsm_end_session_dialog_close    (GsmEndSessionDialog     *dialog);

G_END_DECLS

#endif /* __GSM_END_SESSION_DIALOG_H__ */
