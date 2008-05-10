/* util.h
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

#include <stdlib.h>
#include <ctype.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/goption.h>
#include <gtk/gtkmain.h>
#include <gtk/gtkmessagedialog.h>

#include "util.h"

gchar **
gsm_util_get_autostart_dirs ()
{
  GPtrArray *dirs;
  const char * const *system_config_dirs;
  const char * const *system_data_dirs;
  gint i;

  dirs = g_ptr_array_new ();

  g_ptr_array_add (dirs, 
                   g_build_filename (g_get_user_config_dir (), 
                                     "autostart", NULL));

  system_data_dirs = g_get_system_data_dirs ();
  for (i = 0; system_data_dirs[i]; i++)
    {
      g_ptr_array_add (dirs, 
                       g_build_filename (system_data_dirs[i], 
                                         "gnome", "autostart", NULL));

      g_ptr_array_add (dirs, 
                       g_build_filename (system_data_dirs[i], 
                                         "autostart", NULL));
    }

  system_config_dirs = g_get_system_config_dirs ();
  for (i = 0; system_config_dirs[i]; i++)
    {
      g_ptr_array_add (dirs, 
                       g_build_filename (system_config_dirs[i], 
                                         "autostart", NULL));
    }

  g_ptr_array_add (dirs, NULL);

  return (char **) g_ptr_array_free (dirs, FALSE);
}

gchar **
gsm_util_get_app_dirs ()
{
  GPtrArray *dirs;
  const char * const *system_data_dirs;
  gint i;

  dirs = g_ptr_array_new ();

  system_data_dirs = g_get_system_data_dirs ();
  for (i = 0; system_data_dirs[i]; i++)
    {
      g_ptr_array_add (dirs, 
                       g_build_filename (system_data_dirs[i], "applications", 
                                         NULL));

      g_ptr_array_add (dirs, 
                       g_build_filename (system_data_dirs[i], "gnome", "wm-properties", 
                                         NULL));
    }

  g_ptr_array_add (dirs, NULL);

  return (char **) g_ptr_array_free (dirs, FALSE);
}

gboolean
gsm_util_text_is_blank (const gchar *str)
{
  if (str == NULL)
    return TRUE;

  while (*str) 
    {
      if (!isspace(*str))
        return FALSE;

      str++;
    }

  return TRUE;
}
