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

#include "gsm-systemd.h"


enum {
        REQUEST_COMPLETED,
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
        signals [REQUEST_COMPLETED] =
                g_signal_new ("request-completed",
                              GSM_TYPE_SYSTEM,
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmSystemInterface, request_completed),
                              NULL, NULL, NULL,
                              G_TYPE_NONE,
                              1, G_TYPE_POINTER);
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

typedef GObject GsmSystemNull;
typedef GObjectClass GsmSystemNullClass;

static void do_nothing (void) { }
static gboolean return_false (void) { return FALSE; }

static void
gsm_system_null_init_iface (GsmSystemInterface *iface)
{
        iface->can_switch_user   = (void *) return_false;
        iface->can_stop          = (void *) return_false;
        iface->can_restart       = (void *) return_false;
        iface->can_restart_to_firmware_setup = (void *) return_false;
        iface->set_restart_to_firmware_setup = (void *) do_nothing;
        iface->can_suspend       = (void *) return_false;
        iface->can_hibernate     = (void *) return_false;
        iface->attempt_stop      = (void *) do_nothing;
        iface->attempt_restart   = (void *) do_nothing;
        iface->suspend           = (void *) do_nothing;
        iface->hibernate         = (void *) do_nothing;
        iface->set_session_idle  = (void *) do_nothing;
        iface->set_inhibitors    = (void *) do_nothing;
        iface->prepare_shutdown  = (void *) do_nothing;
        iface->complete_shutdown = (void *) do_nothing;
}

static void
gsm_system_null_init (GsmSystemNull *gsn)
{
}

static void
gsm_system_null_get_property (GObject *object, guint prop_id,
                              GValue *value, GParamSpec *pspec)
{
        g_value_set_boolean (value, TRUE);
}

static void
gsm_system_null_class_init (GsmSystemNullClass *class)
{
        class->get_property = gsm_system_null_get_property;
        class->set_property = (void *) do_nothing;

        g_object_class_override_property (class, 1, "active");
}

static GType gsm_system_null_get_type (void);
G_DEFINE_TYPE_WITH_CODE (GsmSystemNull, gsm_system_null, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GSM_TYPE_SYSTEM, gsm_system_null_init_iface))

GQuark
gsm_system_error_quark (void)
{
        static GQuark error_quark = 0;

        if (error_quark == 0) {
                error_quark = g_quark_from_static_string ("gsm-system-error");
        }

        return error_quark;
}

gboolean
gsm_system_can_switch_user (GsmSystem *system)
{
        return GSM_SYSTEM_GET_IFACE (system)->can_switch_user (system);
}

gboolean
gsm_system_can_stop (GsmSystem *system)
{
        return GSM_SYSTEM_GET_IFACE (system)->can_stop (system);
}

gboolean
gsm_system_can_restart (GsmSystem *system)
{
        return GSM_SYSTEM_GET_IFACE (system)->can_restart (system);
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

gboolean
gsm_system_can_suspend (GsmSystem *system)
{
        return GSM_SYSTEM_GET_IFACE (system)->can_suspend (system);
}

gboolean
gsm_system_can_hibernate (GsmSystem *system)
{
        return GSM_SYSTEM_GET_IFACE (system)->can_hibernate (system);
}

void
gsm_system_attempt_stop (GsmSystem *system)
{
        GSM_SYSTEM_GET_IFACE (system)->attempt_stop (system);
}

void
gsm_system_attempt_restart (GsmSystem *system)
{
        GSM_SYSTEM_GET_IFACE (system)->attempt_restart (system);
}

void
gsm_system_suspend (GsmSystem *system)
{
        GSM_SYSTEM_GET_IFACE (system)->suspend (system);
}

void
gsm_system_hibernate (GsmSystem *system)
{
        GSM_SYSTEM_GET_IFACE (system)->hibernate (system);
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
                system = GSM_SYSTEM (gsm_systemd_new ());
                if (system != NULL) {
                        g_debug ("Using systemd for session tracking");
                }
        }

        if (system == NULL) {
                system = g_object_new (gsm_system_null_get_type (), NULL);
                g_warning ("Using null backend for session tracking");
        }

        return g_object_ref (system);
}
