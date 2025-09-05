/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 William Jon McCann <jmccann@redhat.com>
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */


#ifndef __GSM_MANAGER_H
#define __GSM_MANAGER_H

#include <glib-object.h>

#include "gsm-store.h"
#include "gsm-manager-logout-mode.h"

G_BEGIN_DECLS

/* UUIDs for log messages */
#define GSM_MANAGER_STARTUP_SUCCEEDED_MSGID     "0ce153587afa4095832d233c17a88001"
#define GSM_MANAGER_UNRECOVERABLE_FAILURE_MSGID "10dd2dc188b54a5e98970f56499d1f73"

#define GSM_TYPE_MANAGER         (gsm_manager_get_type ())
G_DECLARE_FINAL_TYPE (GsmManager, gsm_manager, GSM, MANAGER, GObject)

typedef enum {
        /* We're waiting for systemd to start session services, the compositor, etc */
        GSM_MANAGER_PHASE_INITIALIZATION = 0,
        /* The session is running, but we're still launching the user's autostart apps */
        GSM_MANAGER_PHASE_APPLICATION,
        /* The session is running */
        GSM_MANAGER_PHASE_RUNNING,
        /* Someone requested for the session to end,  */
        GSM_MANAGER_PHASE_QUERY_END_SESSION,
        /* The session is shutting down */
        GSM_MANAGER_PHASE_END_SESSION,
        /* The session has shut down, and gnome-session is about to exit */
        GSM_MANAGER_PHASE_EXIT
} GsmManagerPhase;

typedef enum
{
        GSM_MANAGER_ERROR_GENERAL = 0,
        GSM_MANAGER_ERROR_NOT_IN_INITIALIZATION,
        GSM_MANAGER_ERROR_NOT_IN_RUNNING,
        GSM_MANAGER_ERROR_ALREADY_REGISTERED,
        GSM_MANAGER_ERROR_NOT_REGISTERED,
        GSM_MANAGER_ERROR_INVALID_OPTION,
        GSM_MANAGER_ERROR_LOCKED_DOWN,
        GSM_MANAGER_NUM_ERRORS
} GsmManagerError;

#define GSM_MANAGER_ERROR gsm_manager_error_quark ()
GQuark              gsm_manager_error_quark                    (void);

GsmManager *        gsm_manager_new                            (void);
GsmManager *        gsm_manager_get                            (void);

gboolean            gsm_manager_get_dbus_disconnected          (GsmManager *manager);

gboolean            gsm_manager_add_autostart_app              (GsmManager     *manager,
                                                                const char     *path);
gboolean            gsm_manager_add_autostart_apps_from_dir    (GsmManager     *manager,
                                                                const char     *path);
gboolean            gsm_manager_add_legacy_session_apps        (GsmManager     *manager,
                                                                const char     *path);

void                gsm_manager_start                          (GsmManager     *manager);

void                _gsm_manager_set_active_session            (GsmManager     *manager,
                                                                const char     *session_name,
                                                                gboolean        is_kiosk);

gboolean            gsm_manager_logout                         (GsmManager     *manager,
                                                                guint           logout_mode,
                                                                GError        **error);

gboolean            gsm_manager_set_phase                      (GsmManager     *manager,
                                                                GsmManagerPhase phase);

void                gsm_quit                                   (void);

G_END_DECLS

#endif /* __GSM_MANAGER_H */
