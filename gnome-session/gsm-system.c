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

static guint signals[LAST_SIGNAL] = { 0 };

enum {
        PROP_0,
        PROP_ACTIVE,
        PROP_LOCKED,
        PROP_SESSION_CLASS,
};

G_DEFINE_ENUM_TYPE (GsmSessionClass, gsm_session_class,
    G_DEFINE_ENUM_VALUE (GSM_SESSION_CLASS_USER, "user"),
    G_DEFINE_ENUM_VALUE (GSM_SESSION_CLASS_GREETER, "greeter"),
    G_DEFINE_ENUM_VALUE (GSM_SESSION_CLASS_LOCK_SCREEN, "lock-screen"),
    G_DEFINE_ENUM_VALUE (GSM_SESSION_CLASS_BACKGROUND, "background")
)

typedef struct _GsmSystemPrivate
{
        gboolean        is_active;
        gboolean        is_locked;
        GsmSessionClass session_class;
} GsmSystemPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GsmSystem, gsm_system, G_TYPE_OBJECT)

static void
gsm_system_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
        GsmSystem *system = GSM_SYSTEM (object);
        GsmSystemPrivate *priv = gsm_system_get_instance_private (system);

        switch (prop_id) {
        case PROP_ACTIVE:
                priv->is_active = g_value_get_boolean (value);
                break;
        case PROP_LOCKED:
                priv->is_locked = g_value_get_boolean (value);
                break;
        case PROP_SESSION_CLASS:
                priv->session_class = g_value_get_enum (value);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
}

static void
gsm_system_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
        GsmSystem *system = GSM_SYSTEM (object);
        GsmSystemPrivate *priv = gsm_system_get_instance_private (system);

        switch (prop_id) {
        case PROP_ACTIVE:
                g_value_set_boolean (value, priv->is_active);
                break;
        case PROP_LOCKED:
                g_value_set_boolean (value, priv->is_locked);
                break;
        case PROP_SESSION_CLASS:
                g_value_set_enum (value, priv->session_class);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_system_init (GsmSystem *system)
{
}

static void
gsm_system_class_init (GsmSystemClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsm_system_get_property;
        object_class->set_property = gsm_system_set_property;

        signals[SHUTDOWN_PREPARED] =
                 g_signal_new ("shutdown-prepared",
                               GSM_TYPE_SYSTEM,
                               G_SIGNAL_RUN_LAST,
                               G_STRUCT_OFFSET (GsmSystemClass, shutdown_prepared),
                               NULL, NULL, NULL,
                               G_TYPE_NONE,
                               1, G_TYPE_BOOLEAN);

        g_object_class_install_property (object_class,
                                         PROP_ACTIVE,
                                         g_param_spec_boolean ("active",
                                                               "Active",
                                                               "Whether or not session is active",
                                                               TRUE,
                                                               G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_LOCKED,
                                         g_param_spec_boolean ("locked",
                                                               "Locked",
                                                               "Whether or not session is locked",
                                                               FALSE,
                                                               G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_SESSION_CLASS,
                                         g_param_spec_enum ("session-class",
                                                            "Session Class",
                                                            "The class of the session (user, greeter, lock-screen, background)",
                                                            GSM_TYPE_SESSION_CLASS,
                                                            GSM_SESSION_CLASS_USER,
                                                            G_PARAM_READWRITE));
}

gboolean
gsm_system_can_switch_user (GsmSystem *system)
{
        return GSM_SYSTEM_GET_CLASS (system)->can_switch_user (system);
}

GsmActionAvailability
gsm_system_can_shutdown (GsmSystem *system)
{
        return GSM_SYSTEM_GET_CLASS (system)->can_shutdown (system);
}

GsmActionAvailability
gsm_system_can_restart (GsmSystem *system)
{
        return GSM_SYSTEM_GET_CLASS (system)->can_restart (system);
}

GsmActionAvailability
gsm_system_can_suspend (GsmSystem *system)
{
        return GSM_SYSTEM_GET_CLASS (system)->can_suspend (system);
}

void
gsm_system_suspend (GsmSystem *system)
{
        GSM_SYSTEM_GET_CLASS (system)->suspend (system);
}

gboolean
gsm_system_can_restart_to_firmware_setup (GsmSystem *system)
{
        return GSM_SYSTEM_GET_CLASS (system)->can_restart_to_firmware_setup (system);
}

void
gsm_system_set_restart_to_firmware_setup (GsmSystem *system,
                                          gboolean   enable)
{
        GSM_SYSTEM_GET_CLASS (system)->set_restart_to_firmware_setup (system, enable);
}

void
gsm_system_set_session_idle (GsmSystem *system,
                             gboolean   is_idle)
{
        GSM_SYSTEM_GET_CLASS (system)->set_session_idle (system, is_idle);
}

void
gsm_system_set_inhibitors (GsmSystem        *system,
                           GsmInhibitorFlag  flags)
{
        GSM_SYSTEM_GET_CLASS (system)->set_inhibitors (system, flags);
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
        GsmSystemPrivate *priv = gsm_system_get_instance_private (system);

        return priv->is_active;
}

gboolean
gsm_system_is_locked (GsmSystem *system)
{
        GsmSystemPrivate *priv = gsm_system_get_instance_private (system);

        return priv->is_locked;
}

GsmSessionClass
gsm_system_get_session_class (GsmSystem *system)
{
        GsmSystemPrivate *priv = gsm_system_get_instance_private (system);

        return priv->session_class;
}

void
gsm_system_prepare_shutdown (GsmSystem *system,
                             gboolean   restart)
{
        GSM_SYSTEM_GET_CLASS (system)->prepare_shutdown (system, restart);
}

void
gsm_system_complete_shutdown (GsmSystem *system)
{
        GSM_SYSTEM_GET_CLASS (system)->complete_shutdown (system);
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
