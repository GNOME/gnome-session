/* ui.c
 * Copyright (C) 1999 Free Software Foundation, Inc.
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

#ifndef __SESSION_PROPERTIES_CAPPLET_H__ 
#define __SESSION_PROPERTIES_CAPPLET_H__

#include <gtk/gtk.h>

#include <gconf/gconf-client.h>

G_BEGIN_DECLS

#define SPC_GCONF_CONFIG_PREFIX   "/apps/gnome-session/options"
#define SPC_GCONF_AUTOSAVE_KEY    SPC_GCONF_CONFIG_PREFIX "/auto_save_session" 

void   spc_ui_build   (GConfClient *client);

G_END_DECLS

#endif /* __SESSION_PROPERTIES_CAPPLET_H__ */
