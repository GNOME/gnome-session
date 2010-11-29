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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <config.h>

#include "gsm-session-fill.h"

#include "gsm-consolekit.h"
#include "gsm-manager.h"
#include "gsm-util.h"

#define GSM_DEFAULT_SESSION "gnome"

#define GSM_KEYFILE_SESSION_GROUP "GNOME Session"
#define GSM_KEYFILE_REQUIRED_KEY "Required"
#define GSM_KEYFILE_DEFAULT_KEY "DefaultApps"

/* This doesn't contain the required components, so we need to always
 * call append_required_apps() after a call to append_default_apps(). */
static void
append_default_apps (GsmManager *manager,
                     GKeyFile   *keyfile,
                     char      **autostart_dirs)
{
        char **default_apps;
        int    i;

        g_debug ("fill: *** Adding default apps");

        g_assert (keyfile != NULL);
        g_assert (autostart_dirs != NULL);

        default_apps = g_key_file_get_string_list (keyfile,
                                                   GSM_KEYFILE_SESSION_GROUP, GSM_KEYFILE_DEFAULT_KEY,
                                                   NULL, NULL);

        if (!default_apps)
                return;

        for (i = 0; default_apps[i] != NULL; i++) {
                char *app_path;

                if (IS_STRING_EMPTY (default_apps[i]))
                        continue;

                app_path = gsm_util_find_desktop_file_for_app_name (default_apps[i], autostart_dirs);
                if (app_path != NULL) {
                        gsm_manager_add_autostart_app (manager, app_path, NULL);
                        g_free (app_path);
                }
        }

        g_strfreev (default_apps);
}

static void
append_required_apps (GsmManager *manager,
                      GKeyFile   *keyfile)
{
        char **required_components;
        int    i;

        g_debug ("fill: *** Adding required apps");

        required_components = g_key_file_get_string_list (keyfile,
                                                          GSM_KEYFILE_SESSION_GROUP, GSM_KEYFILE_REQUIRED_KEY,
                                                          NULL, NULL);

        if (required_components == NULL) {
                g_warning ("No required applications specified");
                return;
        }

        for (i = 0; required_components[i] != NULL; i++) {
                char *key;
                char *value;
                char *app_path;

                key = g_strdup_printf ("%s-%s", GSM_KEYFILE_REQUIRED_KEY, required_components[i]);
                value = g_key_file_get_string (keyfile,
                                               GSM_KEYFILE_SESSION_GROUP, key,
                                               NULL);
                g_free (key);

                if (IS_STRING_EMPTY (value)) {
                        g_free (value);
                        continue;
                }

                g_debug ("fill: %s looking for component: '%s'", required_components[i], value);
                app_path = gsm_util_find_desktop_file_for_app_name (value, NULL);
                if (app_path != NULL) {
                        gsm_manager_add_autostart_app (manager, app_path, required_components[i]);
                } else {
                        g_warning ("Unable to find provider '%s' of required component '%s'",
                                   value, required_components[i]);
                }
                g_free (app_path);

                g_free (value);
        }

        g_debug ("fill: *** Done adding required apps");

        g_strfreev (required_components);
}

static void
maybe_load_saved_session_apps (GsmManager *manager)
{
        GsmConsolekit *consolekit;
        char *session_type;

        consolekit = gsm_get_consolekit ();
        session_type = gsm_consolekit_get_current_session_type (consolekit);

        if (g_strcmp0 (session_type, GSM_CONSOLEKIT_SESSION_TYPE_LOGIN_WINDOW) != 0) {
                gsm_manager_add_autostart_apps_from_dir (manager, gsm_util_get_saved_session_dir ());
        }

        g_object_unref (consolekit);
        g_free (session_type);
}

static void
load_standard_apps (GsmManager *manager,
                    GKeyFile   *keyfile)
{
        char **autostart_dirs;
        int    i;

        autostart_dirs = gsm_util_get_autostart_dirs ();

        if (!gsm_manager_get_failsafe (manager)) {
                maybe_load_saved_session_apps (manager);

                for (i = 0; autostart_dirs[i]; i++) {
                        gsm_manager_add_autostart_apps_from_dir (manager,
                                                                 autostart_dirs[i]);
                }
        }

        /* We do this at the end in case a saved session contains an
         * application that already provides one of the components. */
        append_default_apps (manager, keyfile, autostart_dirs);
        append_required_apps (manager, keyfile);

        g_strfreev (autostart_dirs);
}

static void
load_override_apps (GsmManager *manager,
                    char      **override_autostart_dirs)
{
        int i;
        for (i = 0; override_autostart_dirs[i]; i++) {
                gsm_manager_add_autostart_apps_from_dir (manager, override_autostart_dirs[i]);
        }
}

static GKeyFile *
get_session_keyfile_if_valid (const char *path)
{
        GKeyFile  *keyfile;
        gsize      len;
        char     **list;

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

        list = g_key_file_get_string_list (keyfile,
                                           GSM_KEYFILE_SESSION_GROUP, GSM_KEYFILE_REQUIRED_KEY,
                                           &len, NULL);
        if (list != NULL) {
                int i;
                char *key;
                char *value;

                for (i = 0; list[i] != NULL; i++) {
                        key = g_strdup_printf ("%s-%s", GSM_KEYFILE_REQUIRED_KEY, list[i]);
                        value = g_key_file_get_string (keyfile,
                                                       GSM_KEYFILE_SESSION_GROUP, key,
                                                       NULL);
                        g_free (key);

                        if (IS_STRING_EMPTY (value)) {
                                g_free (value);
                                break;
                        }

                        g_free (value);
                }

                if (list[i] != NULL) {
                        g_warning ("Cannot use session '%s': required component '%s' is not defined.", path, list[i]);
                        g_strfreev (list);
                        goto error;
                }

                g_strfreev (list);
        }

        /* we don't want an empty session, so if there's no required app, check
         * that we do have some default apps */
        if (len == 0) {
                list = g_key_file_get_string_list (keyfile,
                                                   GSM_KEYFILE_SESSION_GROUP, GSM_KEYFILE_DEFAULT_KEY,
                                                   &len, NULL);
                if (list)
                        g_strfreev (list);
                if (len == 0) {
                        g_warning ("Cannot use session '%s': no application in the session.", path);
                        goto error;
                }
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
        char               *path;

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
        path = NULL;

        for (i = 0; i < dirs->len; i++) {
                path = g_build_filename (dirs->pdata[i], "gnome-session", "sessions", basename, NULL);
                keyfile = get_session_keyfile_if_valid (path);
                if (keyfile != NULL)
                        break;
        }

        if (dirs)
                g_ptr_array_free (dirs, TRUE);
        if (basename)
                g_free (basename);
        if (path)
                g_free (path);

        return keyfile;
}

gboolean
gsm_session_fill (GsmManager  *manager,
                  char       **override_autostart_dirs,
                  const char  *session)
{
        GKeyFile *keyfile;

        if (override_autostart_dirs != NULL) {
                load_override_apps (manager, override_autostart_dirs);
                return TRUE;
        }

        if (IS_STRING_EMPTY (session))
                session = GSM_DEFAULT_SESSION;

        keyfile = find_valid_session_keyfile (session);

        if (!keyfile)
                return FALSE;

        load_standard_apps (manager, keyfile);

        g_key_file_free (keyfile);

        return TRUE;
}
