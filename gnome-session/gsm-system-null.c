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
        GsmSystem parent;
};

G_DEFINE_FINAL_TYPE (GsmSystemNull, gsm_system_null, GSM_TYPE_SYSTEM)

static void do_nothing (void) { }
static gboolean return_false (void) { return FALSE; }
static GsmActionAvailability return_unavailable (void) { return GSM_ACTION_UNAVAILABLE; }

static void
gsm_system_null_class_init (GsmSystemNullClass *class)
{
        GsmSystemClass *system_class = GSM_SYSTEM_CLASS (class);

        system_class->can_switch_user   = (void *) return_false;
        system_class->can_shutdown      = (void *) return_unavailable;
        system_class->can_restart       = (void *) return_unavailable;
        system_class->can_suspend       = (void *) return_unavailable;
        system_class->suspend           = (void *) do_nothing;
        system_class->can_restart_to_firmware_setup = (void *) return_false;
        system_class->set_restart_to_firmware_setup = (void *) do_nothing;
        system_class->set_session_idle  = (void *) do_nothing;
        system_class->set_inhibitors    = (void *) do_nothing;
        system_class->prepare_shutdown  = (void *) do_nothing;
        system_class->complete_shutdown = (void *) do_nothing;
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
