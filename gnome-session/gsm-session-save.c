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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <config.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "gsm-util.h"
#include "gsm-client.h"

#include "gsm-session-save.h"

static gboolean
save_one_client (char     *id,
                 GObject  *object,
                 GError  **error)
{
        GsmClient  *client;
        GKeyFile   *keyfile;
        char       *path = NULL;
        char       *filename = NULL;
        char       *contents = NULL;
        const char *saved_session_dir;
        gsize       length = 0;
        GError     *local_error;

        client = GSM_CLIENT (object);

        local_error = NULL;

        keyfile = gsm_client_save (client, &local_error);

        if (keyfile == NULL || local_error) {
                goto out;
        }

        contents = g_key_file_to_data (keyfile, &length, &local_error);

        if (local_error) {
                goto out;
        }

        saved_session_dir = gsm_util_get_saved_session_dir ();

        if (saved_session_dir == NULL) {
                goto out;
        }

        filename = g_strdup_printf ("%s.desktop",
                                    gsm_client_peek_startup_id (client));

        path = g_build_filename (saved_session_dir, filename, NULL);

        g_file_set_contents (path,
                             contents,
                             length,
                             &local_error);

        if (local_error) {
                goto out;
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
                g_propagate_error (error, local_error);
                g_error_free (local_error);

                return TRUE;
        }

        return FALSE;
}

void
gsm_session_save (GsmStore  *client_store,
                  GError   **error)
{
        gsm_session_clear_saved_session ();

        g_debug ("GsmSessionSave: Saving session");

        gsm_store_foreach (client_store,
                           (GsmStoreFunc) save_one_client,
                           error);

        if (*error) {
                g_warning ("GsmSessionSave: error saving session: %s", (*error)->message);
                gsm_session_clear_saved_session ();
        }
}

gboolean
gsm_session_clear_saved_session ()
{
        GDir       *dir;
        const char *saved_session_dir;
        const char *filename;
        gboolean    result = TRUE;
        GError     *error;

        saved_session_dir = gsm_util_get_saved_session_dir ();

        g_debug ("GsmSessionSave: clearing currectly saved session at %s", saved_session_dir);

        if (saved_session_dir == NULL) {
                return FALSE;
        }

        error = NULL;
        dir = g_dir_open (saved_session_dir, 0, &error);
        if (error) {
                g_warning ("GsmSessionSave: error loading saved session directory: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        while ((filename = g_dir_read_name (dir))) {
                char *path = g_build_filename (saved_session_dir,
                                               filename, NULL);

                g_debug ("GsmSessionSave: removing '%s' from saved session", path);

                result = result && (g_unlink (path) == 0);

                g_free (path);
        }

        return result;
}
