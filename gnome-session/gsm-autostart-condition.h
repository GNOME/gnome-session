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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __GSM_AUTOSTART_CONDITION_H__
#define __GSM_AUTOSTART_CONDITION_H__

#include <glib.h>
#include <gio/gio.h>

typedef enum {
        GSM_CONDITION_NONE           = 0,
        GSM_CONDITION_IF_EXISTS      = 1,
        GSM_CONDITION_UNLESS_EXISTS  = 2,
        GSM_CONDITION_GSETTINGS      = 3,
        GSM_CONDITION_IF_SESSION     = 4,
        GSM_CONDITION_UNLESS_SESSION = 5,
        GSM_CONDITION_UNKNOWN        = 6
} GsmAutostartCondition;

char *   gsm_autostart_condition_resolve_file_path (const char *file);
gboolean gsm_autostart_condition_resolve_settings  (const char *condition,
                                                    GSettings **settings,
                                                    char **settings_key);

gboolean gsm_autostart_condition_parse (const char             *condition_string,
                                        GsmAutostartCondition  *condition_kindp,
                                        char                  **keyp);

#endif /* __GSM_AUTOSTART_CONDITION_H__ */
