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

#ifndef __GSM_SYSTEM_NULL_H__
#define __GSM_SYSTEM_NULL_H__

#include "gsm-system.h"

G_BEGIN_DECLS

#define GSM_TYPE_SYSTEM_NULL (gsm_system_null_get_type ())
G_DECLARE_FINAL_TYPE (GsmSystemNull, gsm_system_null, GSM, SYSTEM_NULL, GObject)

GsmSystem *gsm_system_null_new (void) G_GNUC_MALLOC;

G_END_DECLS

#endif /* __GSM_SYSTEM_NULL_H__ */
