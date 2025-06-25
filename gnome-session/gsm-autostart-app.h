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

#ifndef __GSM_AUTOSTART_APP_H__
#define __GSM_AUTOSTART_APP_H__

#include "gsm-app.h"

G_BEGIN_DECLS

#define GSM_TYPE_AUTOSTART_APP            (gsm_autostart_app_get_type ())
G_DECLARE_DERIVABLE_TYPE (GsmAutostartApp, gsm_autostart_app, GSM, AUTOSTART_APP, GsmApp)

struct _GsmAutostartAppClass
{
        GsmAppClass parent_class;

        /* signals */
        void     (*condition_changed)  (GsmApp  *app,
                                        gboolean condition);
};

GsmApp *gsm_autostart_app_new                (const char *desktop_file,
                                              GError    **error);

#define GSM_AUTOSTART_APP_SYSTEMD_KEY     "X-GNOME-HiddenUnderSystemd"
#define GSM_AUTOSTART_APP_ENABLED_KEY     "X-GNOME-Autostart-enabled"
#define GSM_AUTOSTART_APP_PHASE_KEY       "X-GNOME-Autostart-Phase"
#define GSM_AUTOSTART_APP_STARTUP_ID_KEY  "X-GNOME-Autostart-startup-id"
#define GSM_AUTOSTART_APP_AUTORESTART_KEY "X-GNOME-AutoRestart"
#define GSM_AUTOSTART_APP_DBUS_NAME_KEY   "X-GNOME-DBus-Name"
#define GSM_AUTOSTART_APP_DBUS_PATH_KEY   "X-GNOME-DBus-Path"
#define GSM_AUTOSTART_APP_DBUS_ARGS_KEY   "X-GNOME-DBus-Start-Arguments"
#define GSM_AUTOSTART_APP_DISCARD_KEY     "X-GNOME-Autostart-discard-exec"

G_END_DECLS

#endif /* __GSM_AUTOSTART_APP_H__ */
