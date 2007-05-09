/*
 * gsm-keyfile.c: extensions to GKeyFile
 * Based on code I wrote for gnome-panel
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

#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>

#include "gsm-keyfile.h"

GKeyFile *
gsm_key_file_new_desktop (void)
{
  GKeyFile *retval;

  retval = g_key_file_new ();

  g_key_file_set_string (retval, "Desktop Entry", "Type", "Application");
  g_key_file_set_string (retval, "Desktop Entry", "Encoding", "UTF-8");
  g_key_file_set_string (retval, "Desktop Entry", "Version", "1.0");
  /* Name is mandatory and might not be set by the caller */
  g_key_file_set_string (retval, "Desktop Entry", "Name", _("No Name"));

  return retval;
}

//FIXME: kill this when bug #309224 is fixed
gboolean
gsm_key_file_to_file (GKeyFile     *keyfile,
			     const gchar  *file,
			     GError      **error)
{
  gchar   *filename;
  GError  *write_error;
  gchar   *data;
  gsize    length;
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

gboolean
gsm_key_file_get_boolean (GKeyFile       *keyfile,
				 const gchar    *key,
				 gboolean        default_value)
{
  GError   *error;
  gboolean  retval;

  error = NULL;
  retval = g_key_file_get_boolean (keyfile, "Desktop Entry", key, &error);
  if (error != NULL)
    {
      retval = default_value;
      g_error_free (error);
    }

  return retval;
}

void
gsm_key_file_set_locale_string (GKeyFile    *keyfile,
				       const gchar *key,
				       const gchar *value)
{
  const char         *locale;
  const char * const *langs_pointer;
  int                 i;

  locale = NULL;
  langs_pointer = g_get_language_names ();
  for (i = 0; langs_pointer[i] != NULL; i++)
    {
      /* find first without encoding  */
      if (strchr (langs_pointer[i], '.') == NULL)
        {
          locale = langs_pointer[i]; 
          break;
        }
    }

  if (locale)
    g_key_file_set_locale_string (keyfile, "Desktop Entry",
                                  key, locale, value);
  else
    g_key_file_set_string (keyfile, "Desktop Entry", key, value);
}

void
gsm_key_file_remove_locale_key (GKeyFile    *keyfile,
				       const gchar *key)
{
  const char * const *langs_pointer;
  int                 i;
  char               *locale_key;

  locale_key = NULL;
  langs_pointer = g_get_language_names ();
  for (i = 0; langs_pointer[i] != NULL; i++)
    {
      /* find first without encoding  */
      if (strchr (langs_pointer[i], '.') == NULL)
        {
          locale_key = g_strdup_printf ("%s[%s]", key, langs_pointer[i]);
          if (g_key_file_has_key (keyfile, "Desktop Entry", locale_key, NULL))
            break;

          g_free (locale_key);
          locale_key = NULL;
        }
    }

  if (locale_key)
    {
      g_key_file_remove_key (keyfile, "Desktop Entry", locale_key, NULL);
      g_free (locale_key);
    }
  else
    g_key_file_remove_key (keyfile, "Desktop Entry", key, NULL);
}

char *
gsm_key_file_make_exec_uri (const char *exec)
{
  GString    *str;
  const char *c;

  if (!exec)
    return g_strdup ("");

  if (!strchr (exec, ' '))
    return g_strdup (exec);

  str = g_string_new_len (NULL, strlen (exec));

  str = g_string_append_c (str, '"');
  for (c = exec; *c != '\0'; c++)
    {
      /* FIXME: GKeyFile will add an additional backslach so we'll
       * end up with toto\\" instead of toto\"
       * We could use g_key_file_set_value(), but then we don't
       * benefit from the other escaping that glib is doing...
       */
      if (*c == '"')
        str = g_string_append (str, "\\\"");
      else
        str = g_string_append_c (str, *c);
    }
  str = g_string_append_c (str, '"');

  return g_string_free (str, FALSE);
}
