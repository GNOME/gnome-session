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

#define GSM_TYPE_MANAGER         (gsm_manager_get_type ())
G_DECLARE_DERIVABLE_TYPE (GsmManager, gsm_manager, GSM, MANAGER, GObject)

struct _GsmManagerClass
{
        GObjectClass   parent_class;

        void          (* phase_changed)       (GsmManager      *manager,
                                               const char      *phase);
};

typedef enum {
        /* gsm's own startup/initialization phase */
        GSM_MANAGER_PHASE_STARTUP = 0,
        /* gnome-initial-setup */
        GSM_MANAGER_PHASE_EARLY_INITIALIZATION,
        /* gnome-keyring-daemon */
        GSM_MANAGER_PHASE_PRE_DISPLAY_SERVER,
        /* wayland compositor and XWayland */
        GSM_MANAGER_PHASE_DISPLAY_SERVER,
        /* xrandr setup, gnome-settings-daemon, etc */
        GSM_MANAGER_PHASE_INITIALIZATION,
        /* window/compositing managers */
        GSM_MANAGER_PHASE_WINDOW_MANAGER,
        /* apps that will create _NET_WM_WINDOW_TYPE_PANEL windows */
        GSM_MANAGER_PHASE_PANEL,
        /* apps that will create _NET_WM_WINDOW_TYPE_DESKTOP windows */
        GSM_MANAGER_PHASE_DESKTOP,
        /* everything else */
        GSM_MANAGER_PHASE_APPLICATION,
        /* done launching */
        GSM_MANAGER_PHASE_RUNNING,
        /* shutting down */
        GSM_MANAGER_PHASE_QUERY_END_SESSION,
        GSM_MANAGER_PHASE_END_SESSION,
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

GsmManager *        gsm_manager_new                            (GsmStore       *client_store,
                                                                gboolean        failsafe,
                                                                gboolean        systemd_managed);
GsmManager *        gsm_manager_get                            (void);

gboolean            gsm_manager_get_failsafe                   (GsmManager     *manager);
gboolean            gsm_manager_get_systemd_managed            (GsmManager     *manager);

gboolean            gsm_manager_add_autostart_app              (GsmManager     *manager,
                                                                const char     *path);
gboolean            gsm_manager_add_required_app               (GsmManager     *manager,
                                                                const char     *path);
gboolean            gsm_manager_add_autostart_apps_from_dir    (GsmManager     *manager,
                                                                const char     *path);
gboolean            gsm_manager_add_legacy_session_apps        (GsmManager     *manager,
                                                                const char     *path);

void                gsm_manager_start                          (GsmManager     *manager);

char *              _gsm_manager_get_default_session           (GsmManager     *manager);

void                _gsm_manager_set_active_session            (GsmManager     *manager,
                                                                const char     *session_name,
                                                                gboolean        is_fallback);

void                _gsm_manager_set_renderer                  (GsmManager     *manager,
                                                                const char     *renderer);

gboolean            gsm_manager_logout                         (GsmManager     *manager,
                                                                guint           logout_mode,
                                                                GError        **error);

gboolean            gsm_manager_set_phase                      (GsmManager     *manager,
                                                                GsmManagerPhase phase);

G_END_DECLS

#endif /* __GSM_MANAGER_H */
