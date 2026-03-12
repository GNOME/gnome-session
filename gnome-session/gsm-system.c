/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <glib-object.h>
#include <glib/gi18n.h>

#include "gsm-system.h"

#include "gsm-system-null.h"
#include "gsm-systemd.h"


enum {
        SHUTDOWN_PREPARED,
        LAST_SIGNAL
};

enum {
        PROP_0,
        PROP_ACTIVE
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_INTERFACE (GsmSystem, gsm_system, G_TYPE_OBJECT)

static void
gsm_system_default_init (GsmSystemInterface *iface)
{
        GParamSpec *pspec;
        signals[SHUTDOWN_PREPARED] =
                 g_signal_new ("shutdown-prepared",
                               GSM_TYPE_SYSTEM,
                               G_SIGNAL_RUN_LAST,
                               G_STRUCT_OFFSET (GsmSystemInterface, shutdown_prepared),
                               NULL, NULL, NULL,
                               G_TYPE_NONE,
                               1, G_TYPE_BOOLEAN);
        pspec = g_param_spec_boolean ("active",
                                      "Active",
                                      "Whether or not session is active",
                                      TRUE,
                                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
        g_object_interface_install_property (iface, pspec);
}

gboolean
gsm_system_can_switch_user (GsmSystem *system)
{
        return GSM_SYSTEM_GET_IFACE (system)->can_switch_user (system);
}

GsmActionAvailability
gsm_system_can_shutdown (GsmSystem *system)
{
        return GSM_SYSTEM_GET_IFACE (system)->can_shutdown (system);
}

GsmActionAvailability
gsm_system_can_restart (GsmSystem *system)
{
        return GSM_SYSTEM_GET_IFACE (system)->can_restart (system);
}

GsmActionAvailability
gsm_system_can_suspend (GsmSystem *system)
{
        return GSM_SYSTEM_GET_IFACE (system)->can_suspend (system);
}

void
gsm_system_suspend (GsmSystem *system)
{
        return GSM_SYSTEM_GET_IFACE (system)->suspend (system);
}

gboolean
gsm_system_can_restart_to_firmware_setup (GsmSystem *system)
{
        return GSM_SYSTEM_GET_IFACE (system)->can_restart_to_firmware_setup (system);
}

void
gsm_system_set_restart_to_firmware_setup (GsmSystem *system,
                                          gboolean   enable)
{
        GSM_SYSTEM_GET_IFACE (system)->set_restart_to_firmware_setup (system, enable);
}

void
gsm_system_set_session_idle (GsmSystem *system,
                             gboolean   is_idle)
{
        GSM_SYSTEM_GET_IFACE (system)->set_session_idle (system, is_idle);
}

void
gsm_system_set_inhibitors (GsmSystem        *system,
                           GsmInhibitorFlag  flags)
{
        GSM_SYSTEM_GET_IFACE (system)->set_inhibitors (system, flags);
}

/**
 * gsm_system_is_active:
 *
 * Returns: %TRUE if the current session is in the foreground
 * Since: 3.8
 */
gboolean
gsm_system_is_active (GsmSystem *system)
{
        gboolean is_active;
        g_object_get ((GObject*)system, "active", &is_active, NULL);
        return is_active;
}

void
gsm_system_prepare_shutdown  (GsmSystem *system,
                              gboolean   restart)
{
        GSM_SYSTEM_GET_IFACE (system)->prepare_shutdown (system, restart);
}

void
gsm_system_complete_shutdown (GsmSystem *system)
{
        GSM_SYSTEM_GET_IFACE (system)->complete_shutdown (system);
}

GsmSystem *
gsm_get_system (void)
{
        static GsmSystem *system = NULL;

        if (system == NULL) {
                system = gsm_systemd_new ();
                if (system != NULL) {
                        g_debug ("Using systemd for session tracking");
                }
        }

        if (system == NULL) {
                system = gsm_system_null_new ();
                g_warning ("Using null backend for session tracking");
        }

        return g_object_ref (system);
}
