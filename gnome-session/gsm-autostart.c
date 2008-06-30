/*
 * gsm-autostart.c: read autostart desktop entries
 *
 * Copyright (C) 2007 Vincent Untz <vuntz@gnome.org>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Authors:
 *      Vincent Untz <vuntz@gnome.org>
 */

#include <glib.h>

#include "gsm-keyfile.h"
#include "gsm-autostart.h"

static gboolean
gsm_autostart_is_hidden (GKeyFile *keyfile)
{
  char  *exec;
  char **only_show_in;
  char **not_show_in;
  char  *tryexec;

  if (gsm_key_file_get_boolean (keyfile, "Hidden", FALSE))
    return TRUE;

  exec = gsm_key_file_get_string (keyfile, "Exec");
  if (!exec || exec[0] == '\0')
    {
      g_free (exec);
      return TRUE;
    }
  g_free (exec);

  only_show_in = gsm_key_file_get_string_list (keyfile, "OnlyShowIn");
  if (only_show_in)
    {
      int i;

      for (i = 0; only_show_in[i] != NULL; i++)
        {
          if (g_ascii_strcasecmp (only_show_in[i], "GNOME") == 0)
            break;
        }

      if (only_show_in[i] == NULL)
        {
          g_strfreev (only_show_in);
          return TRUE;
        }
      g_strfreev (only_show_in);
    }

  not_show_in = gsm_key_file_get_string_list (keyfile, "NotShowIn");
  if (not_show_in)
    {
      int i;

      for (i = 0; not_show_in[i] != NULL; i++)
        {
          if (g_ascii_strcasecmp (not_show_in[i], "GNOME") == 0)
            {
              g_strfreev (not_show_in);
              return TRUE;
            }
        }

      g_strfreev (not_show_in);
    }

  tryexec = gsm_key_file_get_string (keyfile, "TryExec");
  if (tryexec)
    {
      char *program_path;

      program_path = g_find_program_in_path (tryexec);
      g_free (tryexec);

      if (!program_path)
        return TRUE;

      g_free (program_path);
    }

  return FALSE;
}

static gboolean
gsm_autostart_is_enabled (GKeyFile *keyfile)
{
  return gsm_key_file_get_boolean (keyfile, "X-GNOME-Autostart-enabled", TRUE);
}

gpointer
gsm_autostart_read_desktop_file (const gchar            *path,
                                 GsmAutostartCreateFunc  create_handler,
                                 gboolean                enabled_only)
{
  GKeyFile *keyfile;
  GError   *err;
  gpointer loaded;

  keyfile = g_key_file_new ();

  err = NULL;
  g_key_file_load_from_file (keyfile, path, G_KEY_FILE_NONE, &err);

  if (err)
    {
      g_error_free (err);
      g_key_file_free (keyfile);
      return NULL;
    }

  if (gsm_autostart_is_hidden (keyfile))
    {
      g_key_file_free (keyfile);
      return NULL;
    }

  if (!gsm_autostart_is_enabled (keyfile) &&
      enabled_only)
    {
      g_key_file_free (keyfile);
      return NULL;
    }

  loaded = create_handler (path, keyfile);

  g_key_file_free (keyfile);

  return loaded;
}

static void
gsm_autostart_read_desktop_files_from_dir (GHashTable             *clients,
                                           const gchar            *path,
                                           GsmAutostartCreateFunc  create_handler,
                                           GsmAutostartFreeFunc    free_handler,
                                           gboolean                enabled_only)
{
  const char *file;
  GDir       *dir;

  dir = g_dir_open (path, 0, NULL);
  if (!dir)
    return;

  while ((file = g_dir_read_name (dir)))
    {
      char     *desktop_file;
      char     *hash_key;
      gpointer  hash_client;
      gpointer loaded;

      if (!g_str_has_suffix (file, ".desktop"))
	continue;

      /* an entry with a filename cancels any previous entry with the same
       * filename */
      if (g_hash_table_lookup_extended (clients, file,
                                        (gpointer *) &hash_key,
                                        (gpointer *) &hash_client))
        {
          g_hash_table_remove (clients, file);
          g_free (hash_key);
          free_handler (hash_client);
        }

      desktop_file = g_build_filename (path, file, NULL);
      loaded = gsm_autostart_read_desktop_file (desktop_file, create_handler,
                                                enabled_only);
      g_free (desktop_file);

      if (loaded != NULL)
        g_hash_table_insert (clients, g_strdup (file), loaded);
    }

  g_dir_close (dir);
}

static gboolean
gsm_autostart_convert_hash_to_gslist_helper (gpointer key,
                                             gpointer value,
                                             gpointer user_data)
{
  GSList **result = (GSList **) user_data;

  *result = g_slist_prepend (*result, value);
  g_free (key);

  return TRUE;
}

GSList *
gsm_autostart_read_desktop_files (GsmAutostartCreateFunc create_handler,
                                  GsmAutostartFreeFunc   free_handler,
                                  gboolean               enabled_only)
{
  const char * const * system_dirs;
  char *path;
  gint i;
  gint len;
  GHashTable *clients;
  GSList     *list = NULL;

  clients = g_hash_table_new (g_str_hash, g_str_equal);

  /* support old place (/etc/xdg/autostart) */
  system_dirs = g_get_system_config_dirs ();
  for (len = 0; system_dirs[len] != NULL; len++);

  for (i = len - 1; i >= 0; i--)
    {
      path = g_build_filename (system_dirs[i], "autostart", NULL);
      gsm_autostart_read_desktop_files_from_dir (clients, path,
                                                 create_handler, free_handler,
                                                 enabled_only);
      g_free (path);
    }

  /* read directories */
  system_dirs = g_get_system_data_dirs ();
  for (len = 0; system_dirs[len] != NULL; len++);

  for (i = len - 1; i >= 0; i--)
    {
      path = g_build_filename (system_dirs[i], "gnome", "autostart", NULL);
      gsm_autostart_read_desktop_files_from_dir (clients, path,
                                                 create_handler, free_handler,
                                                 enabled_only);
      g_free (path);
    }

  path = g_build_filename (g_get_user_config_dir (), "autostart", NULL);
  gsm_autostart_read_desktop_files_from_dir (clients, path,
                                             create_handler, free_handler,
                                             enabled_only);
  g_free (path);

  /* convert the hash table into a GSList */
  g_hash_table_foreach_remove (clients,
                               (GHRFunc) gsm_autostart_convert_hash_to_gslist_helper,
                               &list);
  g_hash_table_destroy (clients);

  return list;
}
