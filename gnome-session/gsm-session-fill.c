/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006, 2010 Novell, Inc.
 * Copyright (C) 2008 Red Hat, Inc.
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
 */

#include <config.h>

#include "gsm-session-fill.h"

#include "gsm-system.h"
#include "gsm-manager.h"

#define GSM_KEYFILE_SESSION_GROUP "GNOME Session"

static void
load_standard_apps (GsmManager *manager,
                    GKeyFile   *keyfile)
{
        char *dir;
        const char * const *system_dirs;
        int    i;

        dir = g_build_filename (g_get_user_config_dir (), "autostart", NULL);
        gsm_manager_add_autostart_apps_from_dir (manager, dir);
        g_free (dir);

        system_dirs = g_get_system_data_dirs ();
        for (i = 0; system_dirs[i]; i++) {
                dir = g_build_filename (system_dirs[i], "gnome", "autostart", NULL);
                gsm_manager_add_autostart_apps_from_dir (manager, dir);
                g_free (dir);
        }

        system_dirs = g_get_system_config_dirs ();
        for (i = 0; system_dirs[i]; i++) {
                dir = g_build_filename (system_dirs[i], "autostart", NULL);
                gsm_manager_add_autostart_apps_from_dir (manager, dir);
                g_free (dir);
        }
}

static GKeyFile *
get_session_keyfile_if_valid (const char *path)
{
        GKeyFile  *keyfile;

        g_debug ("fill: *** Looking if %s is a valid session file", path);

        keyfile = g_key_file_new ();

        if (!g_key_file_load_from_file (keyfile, path, G_KEY_FILE_NONE, NULL)) {
                g_debug ("Cannot use session '%s': non-existing or invalid file.", path);
                goto error;
        }

        if (!g_key_file_has_group (keyfile, GSM_KEYFILE_SESSION_GROUP)) {
                g_warning ("Cannot use session '%s': no '%s' group.", path, GSM_KEYFILE_SESSION_GROUP);
                goto error;
        }

        return keyfile;

error:
        g_key_file_free (keyfile);
        return NULL;
}

/**
 * find_valid_session_keyfile:
 * @session: name of session
 *
 * We look for the session file in XDG_CONFIG_HOME, XDG_CONFIG_DIRS and
 * XDG_DATA_DIRS. This enables users and sysadmins to override a specific
 * session that is shipped in XDG_DATA_DIRS.
 */
static GKeyFile *
find_valid_session_keyfile (const char *session)
{
        GPtrArray          *dirs;
        const char * const *system_config_dirs;
        const char * const *system_data_dirs;
        int                 i;
        GKeyFile           *keyfile;
        char               *basename;

        dirs = g_ptr_array_new ();

        g_ptr_array_add (dirs, (gpointer) g_get_user_config_dir ());

        system_config_dirs = g_get_system_config_dirs ();
        for (i = 0; system_config_dirs[i]; i++)
                g_ptr_array_add (dirs, (gpointer) system_config_dirs[i]);

        system_data_dirs = g_get_system_data_dirs ();
        for (i = 0; system_data_dirs[i]; i++)
                g_ptr_array_add (dirs, (gpointer) system_data_dirs[i]);

        keyfile = NULL;
        basename = g_strdup_printf ("%s.session", session);

        for (i = 0; i < dirs->len; i++) {
                g_autofree gchar *path = g_build_filename (dirs->pdata[i], "gnome-session", "sessions", basename, NULL);
                keyfile = get_session_keyfile_if_valid (path);
                if (keyfile != NULL)
                        break;
        }

        if (dirs)
                g_ptr_array_free (dirs, TRUE);
        if (basename)
                g_free (basename);

        return keyfile;
}

static GKeyFile *
get_session_keyfile (const char *session)
{
        GKeyFile *keyfile;
        g_debug ("fill: *** Getting session '%s'", session);
        keyfile = find_valid_session_keyfile (session);
        return keyfile;
}

gboolean
gsm_session_fill (GsmManager  *manager,
                  const char  *session)
{
        g_autoptr (GKeyFile) keyfile = NULL;
        gboolean is_kiosk;

        keyfile = get_session_keyfile (session);
        if (!keyfile)
                return FALSE;

        is_kiosk = g_key_file_get_boolean (keyfile, GSM_KEYFILE_SESSION_GROUP,
                                           "Kiosk", NULL);

        _gsm_manager_set_active_session (manager, session, is_kiosk);

        if (!is_kiosk)
                load_standard_apps (manager, keyfile);

        return TRUE;
}
