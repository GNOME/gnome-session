/*
 * gsm-autostart.h: read autostart desktop entries
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

#ifndef GSM_AUTOSTART_H
#define GSM_AUTOSTART_H

#include <glib/gkeyfile.h>

G_BEGIN_DECLS

typedef gpointer (*GsmAutostartCreateFunc) (const char *path,
					    GKeyFile   *keyfile);
typedef void     (*GsmAutostartFreeFunc)   (gpointer data);

gpointer
gsm_autostart_read_desktop_file (const gchar            *path,
                                 GsmAutostartCreateFunc  create_handler,
                                 gboolean                enabled_only);

GSList *
gsm_autostart_read_desktop_files (GsmAutostartCreateFunc create_handler,
                                  GsmAutostartFreeFunc   free_handler,
                                  gboolean               enabled_only);

G_END_DECLS

#endif /* GSM_AUTOSTART_H */
