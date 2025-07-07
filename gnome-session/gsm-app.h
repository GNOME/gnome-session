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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __GSM_APP_H__
#define __GSM_APP_H__

#include <glib-object.h>
#include <gio/gdesktopappinfo.h>
#include <sys/types.h>

#include "gsm-manager.h"
#include "gsm-client.h"

G_BEGIN_DECLS

#define GSM_TYPE_APP (gsm_app_get_type ())
G_DECLARE_FINAL_TYPE (GsmApp, gsm_app, GSM, APP, GObject)

GsmApp          *gsm_app_new                            (GDesktopAppInfo  *info,
                                                         GError          **error);
GsmApp          *gsm_app_new_for_path                   (const char  *path,
                                                         GError     **error);
const char      *gsm_app_peek_app_id                    (GsmApp     *app);
GsmManagerPhase  gsm_app_peek_phase                     (GsmApp     *app);
gboolean         gsm_app_peek_is_disabled               (GsmApp     *app);

gboolean         gsm_app_start                          (GsmApp     *app,
                                                         GError    **error);

G_END_DECLS

#endif /* __GSM_APP_H__ */
