/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 * gnome-session-config-genreator.c - Generator to create systemd definition from session descriptions

   Copyright (C) 2006, 2010 Novell, Inc.
   Copyright (C) 2008,2020 Red Hat, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>

#include <glib.h>
#include <glib/gstdio.h>

#define GSM_KEYFILE_SESSION_GROUP "GNOME Session"
#define GSM_KEYFILE_REQUIRED_COMPONENTS_KEY "RequiredComponents"

static void
try_generate_session_config (const char *path, const char *output_dir_prefix, const char *output_dir_postfix)
{
        const char * session_types[] = { "", "-x11", "-wayland", NULL };
        const char **type;
        g_autoptr(GKeyFile) keyfile = NULL;
        g_auto(GStrv) list = NULL;
        gsize      len;

        g_debug ("fill: *** Looking if %s is a valid session file", path);

        keyfile = g_key_file_new ();

        if (!g_key_file_load_from_file (keyfile, path, G_KEY_FILE_NONE, NULL)) {
                g_debug ("Cannot use session '%s': non-existing or invalid file.", path);
                return;
        }

        if (!g_key_file_has_group (keyfile, GSM_KEYFILE_SESSION_GROUP)) {
                g_warning ("Cannot use session '%s': no '%s' group.", path, GSM_KEYFILE_SESSION_GROUP);
                return;
        }

        list = g_key_file_get_string_list (keyfile,
                                           GSM_KEYFILE_SESSION_GROUP,
                                           GSM_KEYFILE_REQUIRED_COMPONENTS_KEY,
                                           &len, NULL);
        if (len == 0) {
                g_warning ("Session '%s': no component in the session.", path);
                return;
        }

        for (type = session_types; *type != NULL; type++) {
                g_autoptr(GError) error = NULL;
                g_autoptr(GPtrArray) lines = NULL;
                g_autofree char *output_dir = NULL;
                g_autofree char *output_file = NULL;
                g_autofree char *contents = NULL;
                gsize i;

                lines = g_ptr_array_new_with_free_func (g_free);

                g_ptr_array_add (lines, g_strdup ("[Unit]"));
                g_ptr_array_add (lines, g_strdup ("# Auto-generated for gnome-session"));
                for (i = 0; i < len; i++)
                        g_ptr_array_add (lines, g_strdup_printf ("Wants=%s%s.target", list[i], *type));
                g_ptr_array_add (lines, NULL);

                contents = g_strjoinv("\n", (GStrv) lines->pdata);

                output_dir = g_strconcat (output_dir_prefix, *type, output_dir_postfix, NULL);
                g_mkdir (output_dir, 0777);

                output_file = g_build_filename (output_dir, "session.conf", NULL);

                if (!g_file_set_contents (output_file, contents, -1, &error)) {
                        g_warning ("Could not write generated configuration to %s: %s",
                                   output_file, error->message);
                } else {
                        g_debug ("Generated %s", output_file);
                }
        }
}


/**
 * find_valid_session_keyfile:
 *
 * We look for the session file in XDG_CONFIG_HOME, XDG_CONFIG_DIRS and
 * XDG_DATA_DIRS. This enables users and sysadmins to override a specific
 * session that is shipped in XDG_DATA_DIRS.
 *
 * Copied from find_valid_session_keyfiles()
 */
static void
find_valid_session_keyfiles (const char *generator_dir)
{
        g_autoptr(GPtrArray) dirs = NULL;
        const char * const *system_config_dirs;
        const char * const *system_data_dirs;
        int                 i;

        dirs = g_ptr_array_new ();

        g_ptr_array_add (dirs, (gpointer) g_get_user_config_dir ());

        system_config_dirs = g_get_system_config_dirs ();
        for (i = 0; system_config_dirs[i]; i++)
                g_ptr_array_add (dirs, (gpointer) system_config_dirs[i]);

        system_data_dirs = g_get_system_data_dirs ();
        for (i = 0; system_data_dirs[i]; i++)
                g_ptr_array_add (dirs, (gpointer) system_data_dirs[i]);

        /* Iterate backwards so that earlier directories are prefered
         * (resulting in the same priority as gnome-session opening the files). */
        for (i = dirs->len - 1; i >= 0; i--) {
                GDir *dir = NULL;
                g_autofree char *dir_path = NULL;
                const char *session_file;

                dir_path = g_build_filename (dirs->pdata[i], "gnome-session", "sessions", NULL);
                dir = g_dir_open (dir_path, 0, NULL);
                if (dir == NULL)
                        continue;

                while ((session_file = g_dir_read_name (dir))) {
                        g_autofree char *path = NULL;
                        g_autofree char *session_name = NULL;
                        g_autofree char *output_dir_prefix = NULL;
                        g_autofree char *output_dir_postfix = NULL;

                        if (!g_str_has_suffix (session_file, ".session"))
                                continue;

                        /* Session name is the file name with the last 8 characters chopped off */
                        session_name = g_strndup (session_file, strlen(session_file) - 8);

                        path = g_build_filename (dir_path, session_file, NULL);
                        output_dir_prefix = g_build_filename (generator_dir, "gnome-session", NULL);
                        output_dir_postfix = g_strdup_printf ("@%s.target.d", session_name);;

                        try_generate_session_config (path, output_dir_prefix, output_dir_postfix);
                }

                g_dir_close (dir);
        }
}

int
main (int argc, char *argv[])
{
        const char *generator_normal G_GNUC_UNUSED;
        const char *generator_early;
        const char *generator_late G_GNUC_UNUSED;

        if (argc != 4) {
                g_warning ("Need exactly three directory arguments!");
                return 1;
        }

        generator_normal = argv[1];
        generator_early = argv[2];
        generator_late = argv[3];

        /* NOTE: We don't bother to initialise i18n */

        find_valid_session_keyfiles (generator_early);

        return 0;
}
