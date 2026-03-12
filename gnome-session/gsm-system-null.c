/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2026 Red Hat, Inc.
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

#include "gsm-system-null.h"

struct _GsmSystemNull
{
        GObject parent;
};

static void gsm_system_null_init_iface (GsmSystemInterface *iface);

G_DEFINE_TYPE_WITH_CODE (GsmSystemNull, gsm_system_null, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GSM_TYPE_SYSTEM, gsm_system_null_init_iface))

static void do_nothing (void) { }
static gboolean return_false (void) { return FALSE; }
static GsmActionAvailability return_unavailable (void) { return GSM_ACTION_UNAVAILABLE; }

static void
gsm_system_null_init_iface (GsmSystemInterface *iface)
{
        iface->can_switch_user   = (void *) return_false;
        iface->can_shutdown      = (void *) return_unavailable;
        iface->can_restart       = (void *) return_unavailable;
        iface->can_suspend       = (void *) return_unavailable;
        iface->suspend           = (void *) do_nothing;
        iface->can_restart_to_firmware_setup = (void *) return_false;
        iface->set_restart_to_firmware_setup = (void *) do_nothing;
        iface->set_session_idle  = (void *) do_nothing;
        iface->set_inhibitors    = (void *) do_nothing;
        iface->prepare_shutdown  = (void *) do_nothing;
        iface->complete_shutdown = (void *) do_nothing;
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
        GObjectClass *object_class;

        object_class = G_OBJECT_CLASS (class);

        object_class->get_property = gsm_system_null_get_property;
        object_class->set_property = (void *) do_nothing;

        g_object_class_override_property (object_class, 1, "active");
}

static void
gsm_system_null_init (GsmSystemNull *gsm_system_null)
{
}

GsmSystem *
gsm_system_null_new (void)
{
        return g_object_new (GSM_TYPE_SYSTEM_NULL, NULL);
}
