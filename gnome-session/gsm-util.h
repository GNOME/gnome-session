/* gsm-util.h
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

#ifndef __GSM_UTIL_H__
#define __GSM_UTIL_H__

#include <glib.h>

G_BEGIN_DECLS

#define IS_STRING_EMPTY(x) ((x)==NULL||(x)[0]=='\0')

char *      gsm_util_find_desktop_file_for_app_name (const char *app_name,
                                                     gboolean    look_in_saved_session,
                                                     gboolean    autostart_first);

gchar      *gsm_util_get_empty_tmp_session_dir      (void);

const char *gsm_util_get_saved_session_dir          (void);

gchar**     gsm_util_get_app_dirs                   (void);

gchar**     gsm_util_get_autostart_dirs             (void);
void        gsm_util_set_autostart_dirs             (char **dirs);

gchar **    gsm_util_get_desktop_dirs               (gboolean include_saved_session,
                                                     gboolean autostart_first);

gboolean    gsm_util_text_is_blank                  (const char *str);

void        gsm_util_init_error                     (gboolean    fatal,
                                                     const char *format, ...) G_GNUC_PRINTF (2, 3);

char *      gsm_util_generate_startup_id            (void);

void        gsm_util_setenv                         (const char *variable,
                                                     const char *value);
const char * const * gsm_util_listenv               (void);

gboolean    gsm_util_export_activation_environment  (GError     **error);
#ifdef HAVE_SYSTEMD
gboolean    gsm_util_export_user_environment        (GError     **error);
#endif

void        gsm_quit                                (void);

G_END_DECLS

#endif /* __GSM_UTIL_H__ */
