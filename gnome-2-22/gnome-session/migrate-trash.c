/* Compile with:
 * gcc -o migrate-trash `pkg-config --cflags --libs glib-2.0` migrate-trash.c
 */

/* Migrate code from ~/.Trash (GNOME < 2.22) to the new trash fd.o spec
 *
 * Copyright (C) 2008 Novell, Inc.
 * Author: Vincent Untz <vuntz@gnome.org>
 *
 * Some code is based on glocalfile.c (from glib/gio), with this
 * copyright/license header:
 * 
 * Copyright (C) 2006-2007 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Alexander Larsson <alexl@redhat.com>
 */

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "migrate-trash.h"

#define OLD_TRASH_DIR ".Trash"

static gboolean
create_new_trash (char **info_dir,
                  char **files_dir)
{
  char *trashdir;
  char *infodir;
  char *filesdir;

  g_assert (info_dir && files_dir);

  trashdir = g_build_filename (g_get_user_data_dir (), "Trash", NULL);
  if (g_mkdir_with_parents (trashdir, 0700) < 0)
    {
      char *display_name;

      display_name = g_filename_display_name (trashdir);
      g_free (trashdir);

      g_printerr ("Unable to create trash dir %s: %s\n", display_name,
                  g_strerror (errno));
      g_free (display_name);

      return FALSE;
	}

  /* Trashdir points to the trash dir with the "info" and "files"
   * subdirectories */

  infodir = g_build_filename (trashdir, "info", NULL);
  filesdir = g_build_filename (trashdir, "files", NULL);
  g_free (trashdir);

  /* Make sure we have the subdirectories */
  if ((g_mkdir (infodir, 0700) == -1 && errno != EEXIST) ||
      (g_mkdir (filesdir, 0700) == -1 && errno != EEXIST))
    {
      g_free (infodir);
      g_free (filesdir);

		  g_printerr ("Unable to find or create trash directory\n");
      return FALSE;
    }  

  *info_dir = infodir;
  *files_dir = filesdir;

  return TRUE;
}

static char *
get_unique_filename (const char *basename, 
                     int         id)
{
  const char *dot;
      
  if (id == 1)
    return g_strdup (basename);

  dot = strchr (basename, '.');
  if (dot)
    return g_strdup_printf ("%.*s.%d%s", (int)(dot - basename), basename, id, dot);
  else
    return g_strdup_printf ("%s.%d", basename, id);
}

static char *
escape_trash_name (char *name)
{
  GString *str;
  const gchar hex[16] = "0123456789ABCDEF";
  
  str = g_string_new ("");

  while (*name != 0)
    {
      char c;

      c = *name++;

      if (g_ascii_isprint (c))
	g_string_append_c (str, c);
      else
	{
          g_string_append_c (str, '%');
          g_string_append_c (str, hex[((guchar)c) >> 4]);
          g_string_append_c (str, hex[((guchar)c) & 0xf]);
	}
    }

  return g_string_free (str, FALSE);
}


static gboolean
move_file (const char *old_trash_dir,
           const char *basename,
           const char *infodir,
           const char *filesdir)
{
  int   i;
  char *filename;
  char *trashname;
  char *infofile;
  char *trashfile;
  char *original_name;
  char *original_name_escaped;
  char  delete_time[32];
  char *data;
  int   fd;

  i = 1;
  trashname = NULL;
  infofile = NULL;
  do {
    char *infoname;

    g_free (trashname);
    g_free (infofile);
    
    trashname = get_unique_filename (basename, i++);
    infoname = g_strconcat (trashname, ".trashinfo", NULL);
    infofile = g_build_filename (infodir, infoname, NULL);
    g_free (infoname);

    fd = open (infofile, O_CREAT | O_EXCL, 0666);
  } while (fd == -1 && errno == EEXIST);

  if (fd == -1)
    {
      char *display_name;

      g_free (trashname);
      g_free (infofile);

      display_name = g_filename_display_name (basename);
      
      g_printerr (("Unable to create trashing info file for %s: %s\n"),
                  display_name, g_strerror (errno));

      g_free (display_name);

      return FALSE;
    }

  close (fd);

  /* TODO: Maybe we should verify that you can delete the file from the trash
     before moving it? OTOH, that is hard, as it needs a recursive scan */

  filename = g_build_filename (old_trash_dir, basename, NULL);
  trashfile = g_build_filename (filesdir, trashname, NULL);
  g_free (trashname);

  if (g_rename (filename, trashfile) == -1)
    {
      char *display_name;

      g_free (filename);
      g_free (infofile);
      g_free (trashfile);
      
      display_name = g_filename_display_name (basename);

		  g_printerr ("Unable to trash file %s: %s\n",
                  display_name, g_strerror (errno));

      g_free (display_name);

      return FALSE;
    }

  g_free (filename);
  g_free (trashfile);

  /* TODO: Do we need to update mtime/atime here after the move? */

  /* We don't know the original dirname of the file, so we'll just assume it
   * was the desktop. This way, the file is easy to find again. */
  original_name = g_build_filename (g_get_user_special_dir (G_USER_DIRECTORY_DESKTOP), basename, NULL);
  original_name_escaped = escape_trash_name (original_name);
  g_free (original_name);

  {
    time_t t;
    struct tm now;
    t = time (NULL);
    localtime_r (&t, &now);
    delete_time[0] = 0;
    strftime(delete_time, sizeof (delete_time), "%Y-%m-%dT%H:%M:%S", &now);
  }

  data = g_strdup_printf ("[Trash Info]\nPath=%s\nDeletionDate=%s\n",
			  original_name_escaped, delete_time);

  g_file_set_contents (infofile, data, -1, NULL);
  g_free (infofile);
  g_free (data);
  
  g_free (original_name_escaped);
  
  return TRUE;
}

int
migrate_trash (void)
{
  gboolean    had_problem;
  char       *old_trash_dir;
  char       *info_dir;
  char       *files_dir;
  GError     *error;
  GDir       *dir;
  const char *file;

  had_problem = FALSE;

  old_trash_dir = g_build_filename (g_get_home_dir (), OLD_TRASH_DIR, NULL);

  if (!g_file_test (old_trash_dir, G_FILE_TEST_IS_DIR))
    {
      g_free (old_trash_dir);
      return 0;
    }

  if (!create_new_trash (&info_dir, &files_dir))
    {
      g_free (old_trash_dir);
      return 1;
    }

  error = NULL;
  dir = g_dir_open (old_trash_dir, 0, &error);
  if (!dir)
    {
      char *display_name;

      display_name = g_filename_display_name (old_trash_dir);
      g_free (old_trash_dir);

      g_printerr ("Unable to list files from %s: %s\n",
                  display_name, error->message);

      g_free (display_name);
      g_error_free (error);

      return 1;
    }

  while ((file = g_dir_read_name (dir)) != NULL)
    {
      had_problem = !move_file (old_trash_dir, file, info_dir, files_dir)
                    || had_problem;
    }
  g_dir_close (dir);

  if (!had_problem)
    g_rmdir (old_trash_dir);

  g_free (old_trash_dir);

  return 0;
}

#if 0
int
main (int    argc,
      char **argv)
{
  return migrate_trash ();
}
#endif
