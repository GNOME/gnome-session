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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __GSM_UTIL_H__
#define __GSM_UTIL_H__

#include <glib.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>

G_BEGIN_DECLS

#define IS_STRING_EMPTY(x) ((x)==NULL||(x)[0]=='\0')

gchar**     gsm_util_get_app_dirs                   (void);

gboolean    gsm_util_text_is_blank                  (const char *str);

void        gsm_util_init_error                     (gboolean    fatal,
                                                     const char *format, ...) G_GNUC_PRINTF (2, 3);

void        gsm_util_setenv                         (const char *variable,
                                                     const char *value);
const char * const * gsm_util_listenv               (void);
const char * const * gsm_util_get_variable_blacklist(void);

gboolean    gsm_util_export_activation_environment  (GError     **error);
gboolean    gsm_util_export_user_environment        (GError     **error);

gboolean    gsm_util_launch_app                     (GDesktopAppInfo  *app,
                                                     GError          **error);

G_END_DECLS

#endif /* __GSM_UTIL_H__ */
