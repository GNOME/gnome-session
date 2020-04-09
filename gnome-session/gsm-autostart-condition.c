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

#include "gsm-autostart-condition.h"
#include <ctype.h>

char *
gsm_autostart_condition_resolve_file_path (const char *file)
{
        if (g_path_is_absolute (file))
                return g_strdup (file);
        return g_build_filename (g_get_user_config_dir (), file, NULL);
}


gboolean
gsm_autostart_condition_resolve_settings (const char *condition,
                                          GSettings **settings,
                                          char **settings_key)
{
        g_auto(GStrv) elems = NULL;
        g_autoptr(GSettingsSchema) schema = NULL;
        g_autoptr(GSettingsSchemaKey) schema_key = NULL;
        GSettingsSchemaSource *source;
        const GVariantType *key_type;

        elems = g_strsplit (condition, " ", 2);
        if (elems == NULL)
                return FALSE;
        if (elems[0] == NULL || elems[1] == NULL)
                return FALSE;

        source = g_settings_schema_source_get_default ();
        schema = g_settings_schema_source_lookup (source, elems[0], TRUE);
        if (schema == NULL)
                return FALSE;

        if (!g_settings_schema_has_key (schema, elems[1]))
                return FALSE;

        schema_key = g_settings_schema_get_key (schema, elems[1]);
        g_assert (schema_key != NULL);

        key_type = g_settings_schema_key_get_value_type (schema_key);

        g_settings_schema_key_unref (schema_key);
        g_assert (key_type != NULL);

        if (!g_variant_type_equal (key_type, G_VARIANT_TYPE_BOOLEAN))
                return FALSE;

        *settings = g_settings_new_full (schema, NULL, NULL);
        *settings_key = g_strdup(elems[1]);

        return TRUE;
}

gboolean
gsm_autostart_condition_parse (const char             *condition_string,
                               GsmAutostartCondition  *condition_kindp,
                               char                  **keyp)
{
        const char *space;
        const char *key;
        int         len;
        guint       kind;

        space = condition_string + strcspn (condition_string, " ");
        len = space - condition_string;
        key = space;
        while (isspace ((unsigned char)*key)) {
                key++;
        }

        kind = GSM_CONDITION_UNKNOWN;

        if (!g_ascii_strncasecmp (condition_string, "if-exists", len) && key) {
                kind = GSM_CONDITION_IF_EXISTS;
        } else if (!g_ascii_strncasecmp (condition_string, "unless-exists", len) && key) {
                kind = GSM_CONDITION_UNLESS_EXISTS;
        } else if (!g_ascii_strncasecmp (condition_string, "GSettings", len)) {
                kind = GSM_CONDITION_GSETTINGS;
        } else if (!g_ascii_strncasecmp (condition_string, "GNOME3", len)) {
                condition_string = key;
                space = condition_string + strcspn (condition_string, " ");
                len = space - condition_string;
                key = space;
                while (isspace ((unsigned char)*key)) {
                        key++;
                }
                if (!g_ascii_strncasecmp (condition_string, "if-session", len) && key) {
                        kind = GSM_CONDITION_IF_SESSION;
                } else if (!g_ascii_strncasecmp (condition_string, "unless-session", len) && key) {
                        kind = GSM_CONDITION_UNLESS_SESSION;
                }
        }

        if (kind == GSM_CONDITION_UNKNOWN) {
                key = NULL;
        }

        if (keyp != NULL) {
                *keyp = g_strdup (key);
        }

        if (condition_kindp != NULL) {
                *condition_kindp = kind;
        }

        return (kind != GSM_CONDITION_UNKNOWN);
}


