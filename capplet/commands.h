/* commands.h
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

#ifndef __SESSION_PROPERTIES_COMMANDS_H__ 
#define __SESSION_PROPERTIES_COMMANDS_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

enum
{
  STORE_COL_ENABLED = 0,
  STORE_COL_ICON_NAME,
  STORE_COL_DESCRIPTION,
  STORE_COL_NAME,
  STORE_COL_COMMAND,
  STORE_COL_COMMENT,
  STORE_COL_DESKTOP_FILE,
  STORE_COL_ID,
  STORE_COL_ACTIVATABLE, 
  STORE_NUM_COLS              
};

GtkTreeModel*   spc_command_get_store            (void);

gboolean        spc_command_enable_app           (GtkListStore *store, 
                                                  GtkTreeIter  *iter);

gboolean        spc_command_disable_app          (GtkListStore *store, 
                                                  GtkTreeIter  *iter);

void            spc_command_add_app              (GtkListStore *store,
                                                  GtkTreeIter  *iter);

void            spc_command_delete_app           (GtkListStore *store, 
                                                  GtkTreeIter  *iter);

void            spc_command_update_app           (GtkListStore *store,
                                                  GtkTreeIter  *iter);

char*           spc_command_get_app_description  (const char *name,
                                                  const char *comment);

G_END_DECLS

#endif /* __SESSION_PROPERTIES_COMMANDS_H__ */
