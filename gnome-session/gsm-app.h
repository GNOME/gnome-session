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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __GSM_APP_H__
#define __GSM_APP_H__

#include <glib-object.h>
#include <sys/types.h>


#include "gsm-manager.h"
#include "gsm-client.h"

G_BEGIN_DECLS

#define GSM_TYPE_APP            (gsm_app_get_type ())
G_DECLARE_DERIVABLE_TYPE (GsmApp, gsm_app, GSM, APP, GObject)

struct _GsmAppClass
{
        GObjectClass parent_class;

        /* signals */
        void        (*exited)       (GsmApp *app,
                                     guchar  exit_code);
        void        (*died)         (GsmApp *app,
                                     int     signal);

        /* virtual methods */
        gboolean    (*impl_start)                     (GsmApp     *app,
                                                       GError    **error);
        gboolean    (*impl_restart)                   (GsmApp     *app,
                                                       GError    **error);
        gboolean    (*impl_stop)                      (GsmApp     *app,
                                                       GError    **error);
        gboolean    (*impl_provides)                  (GsmApp     *app,
                                                       const char *service);
        char **     (*impl_get_provides)              (GsmApp     *app);
        gboolean    (*impl_has_autostart_condition)   (GsmApp     *app,
                                                       const char *service);
        gboolean    (*impl_is_running)                (GsmApp     *app);

        gboolean    (*impl_get_autorestart)           (GsmApp     *app);
        const char *(*impl_get_app_id)                (GsmApp     *app);
        gboolean    (*impl_is_disabled)               (GsmApp     *app);
        gboolean    (*impl_is_conditionally_disabled) (GsmApp     *app);

        gboolean    (*impl_save_to_keyfile)           (GsmApp     *app,
                                                       GKeyFile   *keyfile,
                                                       GError    **error);
};

typedef enum
{
        GSM_APP_ERROR_GENERAL = 0,
        GSM_APP_ERROR_RESTART_LIMIT,
        GSM_APP_ERROR_START,
        GSM_APP_ERROR_STOP,
        GSM_APP_NUM_ERRORS
} GsmAppError;

#define GSM_APP_ERROR gsm_app_error_quark ()

GQuark           gsm_app_error_quark                    (void);

gboolean         gsm_app_peek_autorestart               (GsmApp     *app);

const char      *gsm_app_peek_id                        (GsmApp     *app);
const char      *gsm_app_peek_app_id                    (GsmApp     *app);
const char      *gsm_app_peek_startup_id                (GsmApp     *app);
GsmManagerPhase  gsm_app_peek_phase                     (GsmApp     *app);
gboolean         gsm_app_peek_is_disabled               (GsmApp     *app);
gboolean         gsm_app_peek_is_conditionally_disabled (GsmApp     *app);

gboolean         gsm_app_start                          (GsmApp     *app,
                                                         GError    **error);
gboolean         gsm_app_restart                        (GsmApp     *app,
                                                         GError    **error);
gboolean         gsm_app_stop                           (GsmApp     *app,
                                                         GError    **error);
gboolean         gsm_app_is_running                     (GsmApp     *app);

void             gsm_app_exited                         (GsmApp     *app,
                                                         guchar      exit_code);
void             gsm_app_died                           (GsmApp     *app,
                                                         int         signal);

gboolean         gsm_app_provides                       (GsmApp     *app,
                                                         const char *service);
char           **gsm_app_get_provides                   (GsmApp     *app);
gboolean         gsm_app_has_autostart_condition        (GsmApp     *app,
                                                         const char *condition);
gboolean         gsm_app_get_registered                 (GsmApp     *app);
void             gsm_app_set_registered                 (GsmApp     *app,
                                                         gboolean  registered);

gboolean         gsm_app_save_to_keyfile                (GsmApp    *app,
                                                         GKeyFile  *keyfile,
                                                         GError   **error);

G_END_DECLS

#endif /* __GSM_APP_H__ */
