/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 * gsm-session-save.c
 * Copyright (C) 2008 Lucas Rocha.
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

#include <config.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "gsm-app.h"
#include "gsm-util.h"
#include "gsm-autostart-app.h"
#include "gsm-client.h"

#include "gsm-session-save.h"

#define GSM_MANAGER_SCHEMA        "org.gnome.SessionManager"
#define KEY_AUTOSAVE_ONE_SHOT     "auto-save-session-one-shot"


static gboolean gsm_session_clear_saved_session (const char *directory,
                                                 GHashTable *discard_hash);

typedef struct {
        const char  *dir;
        GHashTable  *discard_hash;
        GsmStore    *app_store;
        GError     **error;
} SessionSaveData;

static gboolean
_app_has_app_id (const char   *id,
                 GsmApp       *app,
                 const char   *app_id_a)
{
        const char *app_id_b;

        app_id_b = gsm_app_peek_app_id (app);
        return g_strcmp0 (app_id_a, app_id_b) == 0;
}

static gboolean
save_one_client (char            *id,
                 GObject         *object,
                 SessionSaveData *data)
{
        GsmClient  *client;
        GKeyFile   *keyfile;
        GsmApp     *app;
        const char *app_id;
        char       *path = NULL;
        char       *filename = NULL;
        char       *contents = NULL;
        gsize       length = 0;
        char       *discard_exec;
        GError     *local_error;

        client = GSM_CLIENT (object);

        local_error = NULL;

        app = (GsmApp *)gsm_store_find (data->app_store,
                                        (GsmStoreFunc)_app_has_app_id,
                                        (char *)app_id);
        keyfile = gsm_client_save (client, app, &local_error);

        if (keyfile == NULL || local_error) {
                goto out;
        }

        contents = g_key_file_to_data (keyfile, &length, &local_error);

        if (local_error) {
                goto out;
        }

        app_id = gsm_client_peek_app_id (client);
        if (!IS_STRING_EMPTY (app_id)) {
                if (g_str_has_suffix (app_id, ".desktop"))
                        filename = g_strdup (app_id);
                else
                        filename = g_strdup_printf ("%s.desktop", app_id);

                path = g_build_filename (data->dir, filename, NULL);
        }

        if (!path || g_file_test (path, G_FILE_TEST_EXISTS)) {
                if (filename)
                        g_free (filename);
                if (path)
                        g_free (path);

                filename = g_strdup_printf ("%s.desktop",
                                            gsm_client_peek_startup_id (client));
                path = g_build_filename (data->dir, filename, NULL);
        }

        g_file_set_contents (path,
                             contents,
                             length,
                             &local_error);

        if (local_error) {
                goto out;
        }

        discard_exec = g_key_file_get_string (keyfile,
                                              G_KEY_FILE_DESKTOP_GROUP,
                                              GSM_AUTOSTART_APP_DISCARD_KEY,
                                              NULL);
        if (discard_exec) {
                g_hash_table_insert (data->discard_hash,
                                     discard_exec, discard_exec);
        }

        g_debug ("GsmSessionSave: saved client %s to %s", id, filename);

out:
        if (keyfile != NULL) {
                g_key_file_free (keyfile);
        }

        g_free (contents);
        g_free (filename);
        g_free (path);

        /* in case of any error, stop saving session */
        if (local_error) {
                g_propagate_error (data->error, local_error);
                g_error_free (local_error);

                return TRUE;
        }

        return FALSE;
}

void
gsm_session_save (GsmStore  *client_store,
                  GsmStore  *app_store,
                  GError   **error)
{
        GSettings       *settings;
        const char      *save_dir;
        SessionSaveData  data;

        g_debug ("GsmSessionSave: Saving session");

        /* Clear one shot key autosave in the event its set (so that it's actually
         * one shot only)
         */
        settings = g_settings_new (GSM_MANAGER_SCHEMA);
        g_settings_set_boolean (settings, KEY_AUTOSAVE_ONE_SHOT, FALSE);
        g_object_unref (settings);

        save_dir = gsm_util_get_saved_session_dir ();
        if (save_dir == NULL) {
                g_warning ("GsmSessionSave: cannot create saved session directory");
                return;
        }

        data.dir = save_dir;
        data.discard_hash = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                   g_free, NULL);
        data.app_store = app_store;

        /* remove old saved session */
        gsm_session_clear_saved_session (save_dir, data.discard_hash);
        data.error = error;

        gsm_store_foreach (client_store,
                           (GsmStoreFunc) save_one_client,
                           &data);

        g_hash_table_destroy (data.discard_hash);
}

static gboolean
gsm_session_clear_one_client (const char *filename,
                              GHashTable *discard_hash)
{
        gboolean  result = TRUE;
        GKeyFile *key_file;
        char     *discard_exec = NULL;
        char    **envp;

        g_debug ("GsmSessionSave: removing '%s' from saved session", filename);

        envp = (char **) gsm_util_listenv ();
        key_file = g_key_file_new ();
        if (g_key_file_load_from_file (key_file, filename,
                                       G_KEY_FILE_NONE, NULL)) {
                char **argv;
                int    argc;

                discard_exec = g_key_file_get_string (key_file,
                                                      G_KEY_FILE_DESKTOP_GROUP,
                                                      GSM_AUTOSTART_APP_DISCARD_KEY,
                                                      NULL);
                if (!discard_exec)
                        goto out;

                if (discard_hash && g_hash_table_lookup (discard_hash, discard_exec))
                        goto out;

                if (!g_shell_parse_argv (discard_exec, &argc, &argv, NULL))
                        goto out;

                result = g_spawn_async (NULL, argv, envp, G_SPAWN_SEARCH_PATH,
                                        NULL, NULL, NULL, NULL) && result;

                g_strfreev (argv);
        } else {
                result = FALSE;
        }

out:
        if (key_file)
                g_key_file_free (key_file);
        if (discard_exec)
                g_free (discard_exec);

        result = (g_unlink (filename) == 0) && result;

        return result;
}

static gboolean
gsm_session_clear_saved_session (const char *directory,
                                 GHashTable *discard_hash)
{
        GDir       *dir;
        const char *filename;
        gboolean    result = TRUE;
        GError     *error;

        g_debug ("GsmSessionSave: clearing currently saved session at %s",
                 directory);

        if (directory == NULL) {
                return FALSE;
        }

        error = NULL;
        dir = g_dir_open (directory, 0, &error);
        if (error) {
                g_warning ("GsmSessionSave: error loading saved session directory: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        while ((filename = g_dir_read_name (dir))) {
                char *path = g_build_filename (directory,
                                               filename, NULL);

                result = gsm_session_clear_one_client (path, discard_hash)
                         && result;

                g_free (path);
        }

        g_dir_close (dir);

        return result;
}

void
gsm_session_save_clear (void)
{
        const char *save_dir;

        g_debug ("GsmSessionSave: Clearing saved session");

        save_dir = gsm_util_get_saved_session_dir ();
        if (save_dir == NULL) {
                g_warning ("GsmSessionSave: cannot create saved session directory");
                return;
        }

        gsm_session_clear_saved_session (save_dir, NULL);
}
