/*
 * gsm-keyfile.h: extensions to GKeyFile
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

#ifndef GSM_KEYFILE_H
#define GSM_KEYFILE_H

#include <glib/gkeyfile.h>

G_BEGIN_DECLS

GKeyFile *gsm_key_file_new_desktop  (void);
gboolean  gsm_key_file_to_file      (GKeyFile       *keyfile,
                                     const gchar    *file,
                                     GError        **error);
gboolean gsm_key_file_get_boolean   (GKeyFile       *keyfile,
                                     const gchar    *key,
                                     gboolean        default_value);
#define gsm_key_file_get_string(key_file, key) \
        g_key_file_get_string (key_file, "Desktop Entry", key, NULL)
#define gsm_key_file_get_locale_string(key_file, key) \
        g_key_file_get_locale_string(key_file, "Desktop Entry", key, NULL, NULL)
#define gsm_key_file_get_string_list(key_file, key) \
        g_key_file_get_string_list (key_file, "Desktop Entry", key, NULL, NULL)
#define gsm_key_file_set_boolean(key_file, key, value) \
        g_key_file_set_boolean (key_file, "Desktop Entry", key, value)
#define gsm_key_file_set_string(key_file, key, value) \
        g_key_file_set_string (key_file, "Desktop Entry", key, value)
void    gsm_key_file_set_locale_string (GKeyFile    *keyfile,
                                        const gchar *key,
                                        const gchar *value);
#define gsm_key_file_remove_key(key_file, key) \
        g_key_file_remove_key (key_file, "Desktop Entry", key, NULL)
void gsm_key_file_remove_locale_key (GKeyFile    *keyfile,
                                     const gchar *key);
void gsm_key_file_remove_all_locale_key (GKeyFile    *keyfile,
                                         const gchar *key);

char *gsm_key_file_make_exec_uri (const char *exec);

G_END_DECLS

#endif /* GSM_KEYFILE_H */
