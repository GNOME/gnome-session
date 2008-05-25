/* commands.c
 * Copyright (C) 1999 Free Software Foundation, Inc.
 * Copyright (C) 2007 Vincent Untz.
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

#include <string.h>

#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include "eggdesktopfile.h"
#include "commands.h"
#include "util.h"

#define DESKTOP_ENTRY_GROUP  "Desktop Entry"
#define STARTUP_APP_ICON     "gnome-run"

#define REALLY_IDENTICAL_STRING(a, b) \
      ((a && b && !strcmp (a, b)) || (!a && !b))

static gboolean
system_desktop_entry_exists (const char  *basename,
                             char       **system_path)
{
  char *path;
  char **autostart_dirs;
  int i;

  autostart_dirs = gsm_util_get_autostart_dirs ();

  for (i = 0; autostart_dirs[i]; i++)
    {
      path = g_build_filename (autostart_dirs[i], basename, NULL);

      if (g_str_has_prefix (autostart_dirs[i], g_get_user_config_dir ()))
        continue;
 
      if (g_file_test (path, G_FILE_TEST_EXISTS))
        {
          if (system_path)
            *system_path = path;
          else
            g_free (path);

          g_strfreev (autostart_dirs);

          return TRUE;
        }

      g_free (path);
    }

  g_strfreev (autostart_dirs);

  return FALSE;
}

static void
update_desktop_file (GtkListStore *store, 
                     GtkTreeIter *iter, 
                     EggDesktopFile *new_desktop_file)
{
  EggDesktopFile *old_desktop_file;

  gtk_tree_model_get (GTK_TREE_MODEL (store), iter, 
                      STORE_COL_DESKTOP_FILE, &old_desktop_file,
                      -1);

  egg_desktop_file_free (old_desktop_file);

  gtk_list_store_set (store, iter, 
                      STORE_COL_DESKTOP_FILE, new_desktop_file,
                      -1);
}

static gboolean 
find_by_id (GtkListStore *store, const gchar *id)
{
  GtkTreeIter iter;
  gchar *iter_id = NULL;
	
  if (!gtk_tree_model_get_iter_first (GTK_TREE_MODEL (store), &iter))
    return FALSE;
  
  do
    {
      gtk_tree_model_get (GTK_TREE_MODEL (store), &iter,
          		  STORE_COL_ID, &iter_id,
          		  -1);

      if (!strcmp (iter_id, id))
        {
          return TRUE;
        }
    } while (gtk_tree_model_iter_next (GTK_TREE_MODEL (store), &iter));

  return FALSE;
}

static void
ensure_user_autostart_dir ()
{
  gchar *dir;
  
  dir = g_build_filename (g_get_user_config_dir (), "autostart", NULL);
  g_mkdir_with_parents (dir, S_IRWXU);

  g_free (dir);
}

static gboolean
key_file_to_file (GKeyFile *keyfile, 
                  const gchar *file,
                  GError **error)
{
  GError *write_error;
  gchar *filename;
  gchar *data;
  gsize length;
  gboolean res;

  g_return_val_if_fail (keyfile != NULL, FALSE);
  g_return_val_if_fail (file != NULL, FALSE);

  write_error = NULL;

  data = g_key_file_to_data (keyfile, &length, &write_error);

  if (write_error)
    {
      g_propagate_error (error, write_error);
      return FALSE;
    }

  if (!g_path_is_absolute (file))
    filename = g_filename_from_uri (file, NULL, &write_error);
  else
    filename = g_filename_from_utf8 (file, -1, NULL, NULL, &write_error);

  if (write_error)
    {
      g_propagate_error (error, write_error);
      g_free (data);
      return FALSE;
    }

  res = g_file_set_contents (filename, data, length, &write_error);

  g_free (filename);

  if (write_error)
    {
      g_propagate_error (error, write_error);
      g_free (data);
      return FALSE;
    }

  g_free (data);

  return res;
}

static void
key_file_set_locale_string (GKeyFile *keyfile,
                            const gchar *group,
                            const gchar *key,
                            const gchar *value)
{
  const char *locale;
  const char * const *langs_pointer;
  int i;

  locale = NULL;
  langs_pointer = g_get_language_names ();

  for (i = 0; langs_pointer[i] != NULL; i++)
    {
      /* Find first without encoding  */
      if (strchr (langs_pointer[i], '.') == NULL)
        {
          locale = langs_pointer[i];
          break;
        }
    }

  if (locale)
    g_key_file_set_locale_string (keyfile, group,
                                  key, locale, value);
  else
    g_key_file_set_string (keyfile, "Desktop Entry", key, value);
}

static void
delete_desktop_file (GtkListStore *store, GtkTreeIter *iter)
{
  EggDesktopFile *desktop_file;
  GFile *source;
  char *basename, *path;

  gtk_tree_model_get (GTK_TREE_MODEL (store), iter,
      		      STORE_COL_DESKTOP_FILE, &desktop_file,
      		      -1);

  source = g_file_new_for_uri (egg_desktop_file_get_source (desktop_file));

  path = g_file_get_path (source);
  basename = g_file_get_basename (source);

  if (g_str_has_prefix (path, g_get_user_config_dir ()) && 
      !system_desktop_entry_exists (basename, NULL))
    {
      if (g_file_test (path, G_FILE_TEST_EXISTS))
        g_remove (path);
    }
  else
    {
      /* Two possible cases:
       * a) We want to remove a system wide startup desktop file.
       *    We can't do that, so we will create a user desktop file
       *    with the hidden flag set.
       * b) We want to remove a startup desktop file that is both
       *    system and user. So we have to mark it as hidden.
       */
      GKeyFile *keyfile;
      GError   *error;
      char     *user_path;

      ensure_user_autostart_dir ();

      keyfile = g_key_file_new ();

      error = NULL;

      g_key_file_load_from_file (keyfile, path,
                                 G_KEY_FILE_KEEP_COMMENTS|G_KEY_FILE_KEEP_TRANSLATIONS,
                                 &error);

      if (error)
        {
          g_error_free (error);
          g_key_file_free (keyfile);
        }

      g_key_file_set_boolean (keyfile, DESKTOP_ENTRY_GROUP,
                              "Hidden", TRUE);

      user_path = g_build_filename (g_get_user_config_dir (),
                                    "autostart", basename, NULL);

      if (!key_file_to_file (keyfile, user_path, NULL))
        g_warning ("Could not save %s file", user_path);

      g_key_file_free (keyfile);

      g_free (user_path);
    }

  g_object_unref (source);
  g_free (path);
  g_free (basename);
}

static void
write_desktop_file (EggDesktopFile *desktop_file,
                    GtkListStore   *store, 
                    GtkTreeIter    *iter, 
                    gboolean        enabled)
{
  GKeyFile *keyfile;
  GFile *source;
  GError *error;
  gchar *path, *name, *command, *comment;
  gboolean path_changed = FALSE;

  ensure_user_autostart_dir ();

  keyfile = g_key_file_new ();

  source = g_file_new_for_uri (egg_desktop_file_get_source (desktop_file));

  path = g_file_get_path (source);

  error = NULL;

  g_key_file_load_from_file (keyfile, path,
                             G_KEY_FILE_KEEP_COMMENTS|G_KEY_FILE_KEEP_TRANSLATIONS,
                             &error);

  if (error)
    goto out;

  if (!g_str_has_prefix (path, g_get_user_config_dir ()))
    {
      /* It's a system-wide file, save it to the user's home */
      gchar *basename;

      basename = g_file_get_basename (source);

      g_free (path);

      path = g_build_filename (g_get_user_config_dir (),
                               "autostart", basename, NULL);

      g_free (basename);

      path_changed = TRUE;
    }

  gtk_tree_model_get (GTK_TREE_MODEL (store), iter,
                      STORE_COL_NAME, &name,
                      STORE_COL_COMMAND, &command,
                      STORE_COL_COMMENT, &comment,
                      -1);

  key_file_set_locale_string (keyfile, DESKTOP_ENTRY_GROUP, 
                              "Name", name);

  key_file_set_locale_string (keyfile, DESKTOP_ENTRY_GROUP, 
                              "Comment", comment);

  g_key_file_set_string (keyfile, DESKTOP_ENTRY_GROUP,
                         "Exec", command);

  g_key_file_set_boolean (keyfile,
                          DESKTOP_ENTRY_GROUP,
                          "X-GNOME-Autostart-enabled", 
                          enabled);

  if (!key_file_to_file (keyfile, path, &error))
    goto out;

  if (path_changed)
    {
      EggDesktopFile *new_desktop_file;

      new_desktop_file = egg_desktop_file_new (path, &error);

      if (error)
        goto out;
 
      update_desktop_file (store, iter, new_desktop_file);
    }

out:
  if (error)
    {
      g_warning ("Error when writing desktop file %s: %s", 
                 path, error->message); 

      g_error_free (error);
    }

  g_free (path);
  g_free (name);
  g_free (comment);
  g_free (command);
  g_object_unref (source);
  g_key_file_free (keyfile);
}

static gboolean 
append_app (GtkListStore *store, EggDesktopFile *desktop_file)
{
  GtkTreeIter iter;
  GFile *source;
  gchar *basename, *description, *name, *comment, *command, *icon_name;
  gboolean enabled = TRUE;

  source = g_file_new_for_uri (egg_desktop_file_get_source (desktop_file));

  basename = g_file_get_basename (source);

  if (egg_desktop_file_has_key (desktop_file,
				"Hidden", NULL))
    {
      if (egg_desktop_file_get_boolean (desktop_file, 
                                        "Hidden", NULL))
        return FALSE; 
    }

  /* Check for duplicate apps */
  if (find_by_id (store, basename))
    return TRUE;

  name = egg_desktop_file_get_locale_string (desktop_file,
                                             "Name", NULL, NULL);

  comment = NULL;

  if (egg_desktop_file_has_key (desktop_file,
				"Comment", NULL))
    {
      comment = 
         egg_desktop_file_get_locale_string (desktop_file,
                                             "Comment", NULL, NULL); 
    }

  description = spc_command_get_app_description (name, comment);
 
  command = egg_desktop_file_get_string (desktop_file,
                                         "Exec", NULL);

  icon_name = NULL;

  if (egg_desktop_file_has_key (desktop_file,
				"Icon", NULL))
    {
      icon_name = 
         egg_desktop_file_get_string (desktop_file,
                                      "Icon", NULL); 
    }

  if (icon_name == NULL || *icon_name == '\0')
    {
      icon_name = g_strdup (STARTUP_APP_ICON);
    }

  if (egg_desktop_file_has_key (desktop_file,
				"X-GNOME-Autostart-enabled", NULL))
    {
      enabled = egg_desktop_file_get_boolean (desktop_file, 
                                              "X-GNOME-Autostart-enabled", 
                                              NULL);
    }

  gtk_list_store_append (store, &iter);

  gtk_list_store_set (store, &iter,
                      STORE_COL_ENABLED, enabled,
                      STORE_COL_ICON_NAME, icon_name,
                      STORE_COL_DESCRIPTION, description,
                      STORE_COL_NAME, name,
                      STORE_COL_COMMAND, command,
                      STORE_COL_COMMENT, comment,
                      STORE_COL_DESKTOP_FILE, desktop_file,
                      STORE_COL_ID, basename,
                      STORE_COL_ACTIVATABLE, TRUE,
                      -1);

  g_object_unref (source);
  g_free (basename);
  g_free (name);
  g_free (comment);
  g_free (description);
  g_free (icon_name);

  return TRUE;
}

static gint
compare_app (gconstpointer a, gconstpointer b)
{
  if (!strcmp (a, b))
    return 0;

  return 1;
}

static void
append_autostart_apps (GtkListStore *store, const char *path, GList **removed_apps)
{
  GDir *dir;
  const char *name;

  dir = g_dir_open (path, 0, NULL);
  if (!dir)
    return;

  while ((name = g_dir_read_name (dir)))
    {
      EggDesktopFile *desktop_file;
      GError *error = NULL;
      char *desktop_file_path;

      if (!g_str_has_suffix (name, ".desktop"))
	continue;

      if (removed_apps && 
          g_list_find_custom (*removed_apps, name, compare_app)) 
        continue;

      desktop_file_path = g_build_filename (path, name, NULL);

      desktop_file = egg_desktop_file_new (desktop_file_path, &error);

      if (!error)
	{
	  g_debug ("read %s", desktop_file_path);

	  if (!append_app (store, desktop_file))
            {
              if (removed_apps)
                *removed_apps = 
                   g_list_prepend (*removed_apps, g_strdup (name));
            }
	}
      else
        {
	  g_warning ("could not read %s: %s\n", desktop_file_path, error->message);

          g_error_free (error);
          error = NULL;
        }

      g_free (desktop_file_path);
    }

  g_dir_close (dir);
}

GtkTreeModel *
spc_command_get_store ()
{
  GtkListStore *store;
  GList *removed_apps;
  char **autostart_dirs;
  gint i;

  store = gtk_list_store_new (STORE_NUM_COLS,
                              G_TYPE_BOOLEAN,
                              G_TYPE_STRING,
                              G_TYPE_STRING,
                              G_TYPE_STRING,
                              G_TYPE_STRING,
                              G_TYPE_STRING,
                              G_TYPE_POINTER,
                              G_TYPE_STRING,
                              G_TYPE_BOOLEAN);

  autostart_dirs = gsm_util_get_autostart_dirs ();
 
  removed_apps = NULL;
 
  for (i = 0; autostart_dirs[i]; i++)
    {
      append_autostart_apps (store, autostart_dirs[i], &removed_apps);
    }

  g_strfreev (autostart_dirs);
  g_list_foreach (removed_apps, (GFunc) g_free, NULL);
  g_list_free (removed_apps);

  return GTK_TREE_MODEL (store);
}

gboolean
spc_command_enable_app (GtkListStore *store, 
                                       GtkTreeIter  *iter)
{
  EggDesktopFile *desktop_file;
  GFile *source;
  char *system_path, *basename, *path; 
  char *name, *comment;
  gboolean enabled;

  gtk_tree_model_get (GTK_TREE_MODEL (store), iter,
      		      STORE_COL_ENABLED, &enabled,
      		      STORE_COL_NAME, &name,
      		      STORE_COL_COMMENT, &comment,
      		      STORE_COL_DESKTOP_FILE, &desktop_file,
      		      -1);

  if (enabled)
    return TRUE;

  source = g_file_new_for_uri (egg_desktop_file_get_source (desktop_file));

  path = g_file_get_path (source);

  basename = g_file_get_basename (source);

  if (system_desktop_entry_exists (basename, &system_path))
    {
      EggDesktopFile *system_desktop_file;

      char *original_name, *original_comment;

      system_desktop_file = egg_desktop_file_new (system_path, NULL);

      original_name = 
         egg_desktop_file_get_locale_string (system_desktop_file,
                                             "Name", NULL, NULL); 
 
      original_comment = 
         egg_desktop_file_get_locale_string (system_desktop_file,
                                             "Comment", NULL, NULL);
 
      if (REALLY_IDENTICAL_STRING (name, original_name) &&
          REALLY_IDENTICAL_STRING (comment, original_comment))
        {
          gchar *user_file =
            g_build_filename (g_get_user_config_dir (), 
                              "autostart", basename, NULL);

          if (g_file_test (user_file, G_FILE_TEST_EXISTS))
            g_remove (user_file);

          g_free (user_file);

          update_desktop_file (store, iter, system_desktop_file);
        }
      else
        {
          write_desktop_file (desktop_file, store, iter, TRUE);
          egg_desktop_file_free (system_desktop_file);
        }

      g_free (original_name);
      g_free (original_comment);
    }
  else
    {
      write_desktop_file (desktop_file, store, iter, TRUE);
    }

  g_free (name);
  g_free (comment);
  g_free (basename);

  return TRUE;
}

gboolean
spc_command_disable_app (GtkListStore *store, 
                                        GtkTreeIter  *iter)
{
  EggDesktopFile *desktop_file;
  const gchar *source;
  gchar *basename;
  gboolean enabled;

  gtk_tree_model_get (GTK_TREE_MODEL (store), iter,
      		      STORE_COL_ENABLED, &enabled,
      		      STORE_COL_DESKTOP_FILE, &desktop_file,
      		      -1);

  if (!enabled)
    return TRUE;

  source = egg_desktop_file_get_source (desktop_file);

  basename = g_path_get_basename (source);

  write_desktop_file (desktop_file, store, iter, FALSE);

  g_free (basename);

  return TRUE;
}

void
spc_command_add_app (GtkListStore *store,
                                    GtkTreeIter *iter)
{
  EggDesktopFile *desktop_file;
  GKeyFile *keyfile;
  char **argv;
  char *basename, *orig_filename, *filename;
  char *name, *command, *comment, *description, *icon;
  int argc;
  int i = 2;

  gtk_tree_model_get (GTK_TREE_MODEL (store), iter,
                      STORE_COL_NAME, &name,
                      STORE_COL_COMMAND, &command,
                      STORE_COL_COMMENT, &comment,
                      STORE_COL_ICON_NAME, &icon,
                      -1);

  g_shell_parse_argv (command, &argc, &argv, NULL);

  basename = g_path_get_basename (argv[0]);

  orig_filename = g_build_filename (g_get_user_config_dir (), 
                                    "autostart", basename, NULL);

  filename = g_strdup_printf ("%s.desktop", orig_filename);

  while (g_file_test (filename, G_FILE_TEST_EXISTS))
    {
      char *tmp = g_strdup_printf ("%s-%d.desktop", orig_filename, i);

      g_free (filename);
      filename = tmp;

      i++;
    }

  g_free (orig_filename);

  keyfile = g_key_file_new ();

  g_key_file_set_string (keyfile, DESKTOP_ENTRY_GROUP,
                         "Type", "Application");  

  g_key_file_set_string (keyfile, DESKTOP_ENTRY_GROUP,
                         "Name", name);  

  g_key_file_set_string (keyfile, DESKTOP_ENTRY_GROUP,
                         "Exec", command);  

  if (icon == NULL)
    {
      icon = g_strdup (STARTUP_APP_ICON);
    }
  
  g_key_file_set_string (keyfile, DESKTOP_ENTRY_GROUP,
                         "Icon", icon);

  if (comment)
    g_key_file_set_string (keyfile, DESKTOP_ENTRY_GROUP,
                           "Comment", comment);  

  description = spc_command_get_app_description (name, comment);
 
  if (!key_file_to_file (keyfile, filename, NULL))
    {
      g_warning ("Could not save %s file", filename);
    }

  desktop_file = egg_desktop_file_new_from_key_file (keyfile,
                                                     filename,
                                                     NULL);

  g_free (basename);

  basename = g_path_get_basename (filename);

  gtk_list_store_set (store, iter,
                      STORE_COL_ENABLED, TRUE, 
                      STORE_COL_ICON_NAME, icon, 
                      STORE_COL_DESKTOP_FILE, desktop_file,
                      STORE_COL_ID, basename, 
                      STORE_COL_ACTIVATABLE, TRUE,
                      -1);

  g_key_file_free (keyfile);
  g_strfreev (argv);
  g_free (name);
  g_free (command);
  g_free (comment);
  g_free (description);
  g_free (basename);
  g_free (icon);
}

void
spc_command_delete_app (GtkListStore *store, 
                                       GtkTreeIter  *iter)
{
  delete_desktop_file (store, iter);
}

void
spc_command_update_app (GtkListStore *store, 
                                       GtkTreeIter *iter)
{
  EggDesktopFile *desktop_file;
  gboolean enabled;

  gtk_tree_model_get (GTK_TREE_MODEL (store), iter,
      		      STORE_COL_ENABLED, &enabled,
      		      STORE_COL_DESKTOP_FILE, &desktop_file,
      		      -1);

  write_desktop_file (desktop_file, store, iter, enabled);
}

char *
spc_command_get_app_description (const char *name, 
                                                const char *comment)
{
  return g_strdup_printf ("<b>%s</b>\n%s", name,
                          (!gsm_util_text_is_blank (comment) ? 
                           comment : _("No description")));
}
