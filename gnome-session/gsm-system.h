/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Jon McCann <jmccann@redhat.com>
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
 *	Jon McCann <jmccann@redhat.com>
 */

#ifndef __GSM_SYSTEM_H__
#define __GSM_SYSTEM_H__

#include <glib.h>
#include <glib-object.h>

#include "gsm-inhibitor-flag.h"

G_BEGIN_DECLS

typedef enum _GsmActionAvailability
{
        GSM_ACTION_UNAVAILABLE,
        GSM_ACTION_BLOCKED,
        GSM_ACTION_CHALLANGE,
        GSM_ACTION_AVAILABLE,
} GsmActionAvailability;

typedef enum _GsmSessionClass
{
        GSM_SESSION_CLASS_USER,
        GSM_SESSION_CLASS_GREETER,
        GSM_SESSION_CLASS_LOCK_SCREEN,
        GSM_SESSION_CLASS_BACKGROUND,
} GsmSessionClass;

#define GSM_TYPE_SESSION_CLASS (gsm_session_class_get_type ())
GType gsm_session_class_get_type (void) G_GNUC_CONST;

#define GSM_TYPE_SYSTEM (gsm_system_get_type ())
G_DECLARE_DERIVABLE_TYPE (GsmSystem, gsm_system, GSM, SYSTEM, GObject)

struct _GsmSystemClass
{
        GObjectClass parent_class;

        void                  (* shutdown_prepared)             (GsmSystem *system,
                                                                 gboolean   success);
        GsmActionAvailability (* can_shutdown)                  (GsmSystem *system);
        GsmActionAvailability (* can_restart)                   (GsmSystem *system);
        GsmActionAvailability (* can_suspend)                   (GsmSystem *system);
        void                  (* suspend)                       (GsmSystem *system);
        gboolean              (* can_switch_user)               (GsmSystem *system);
        gboolean              (* can_restart_to_firmware_setup) (GsmSystem *system);
        void                  (* set_restart_to_firmware_setup) (GsmSystem *system,
                                                                 gboolean   enable);
        void                  (* set_session_idle)              (GsmSystem *system,
                                                                 gboolean   is_idle);
        void                  (* set_inhibitors)                (GsmSystem        *system,
                                                                 GsmInhibitorFlag  flags);
        void                  (* prepare_shutdown)              (GsmSystem *system,
                                                                 gboolean   restart);
        void                  (* complete_shutdown)             (GsmSystem *system);
};

GsmSystem *            gsm_get_system                           (void);
GsmActionAvailability  gsm_system_can_shutdown                  (GsmSystem *system);
GsmActionAvailability  gsm_system_can_restart                   (GsmSystem *system);
GsmActionAvailability  gsm_system_can_suspend                   (GsmSystem *system);
void                   gsm_system_suspend                       (GsmSystem *system);
gboolean               gsm_system_can_switch_user               (GsmSystem *system);
gboolean               gsm_system_can_restart_to_firmware_setup (GsmSystem *system);
void                   gsm_system_set_restart_to_firmware_setup (GsmSystem *system,
                                                                 gboolean   enable);
void                   gsm_system_set_session_idle              (GsmSystem *system,
                                                                 gboolean   is_idle);
gboolean               gsm_system_is_active                     (GsmSystem *system);
gboolean               gsm_system_is_locked                     (GsmSystem *system);
GsmSessionClass        gsm_system_get_session_class             (GsmSystem *system);
void                   gsm_system_set_inhibitors                (GsmSystem        *system,
                                                                 GsmInhibitorFlag  flags);
void                   gsm_system_prepare_shutdown              (GsmSystem  *system,
                                                                 gboolean    restart);
void                   gsm_system_complete_shutdown             (GsmSystem  *system);

G_END_DECLS

#endif /* __GSM_SYSTEM_H__ */
