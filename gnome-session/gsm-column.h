/* gsm-column.c - a menu-like editable column for a clist.

   Copyright 1999 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. 

   Authors: Felix Bellaby */

/* NB: This widget only works as the child of a clist column title button. */
/* It associates each cell in a clist column with an integer value and
 * allows the user to select that value using a menu attached to the clist
 * title or using some other widget activated by pressing the title button. */

#ifndef GSM_COLUMN_H
#define GSM_COLUMN_H

#include <gnome.h>

#define GSM_IS_COLUMN(obj)      GTK_CHECK_TYPE (obj, gsm_column_get_type ())
#define GSM_COLUMN(obj)         GTK_CHECK_CAST (obj, gsm_column_get_type (), GsmColumn)
#define GSM_COLUMN_CLASS(klass) GTK_CHECK_CLASS_CAST (klass, gsm_column_get_type (), GsmColumnClass)

typedef struct _GsmColumn GsmColumn;
typedef struct _GsmColumnClass GsmColumnClass;

typedef gboolean (*GsmColumnValueFunc) (guint* value);
typedef void     (*GsmColumnRowFunc) (GtkObject* object, guint value);

struct _GsmColumn {
  GtkLabel label;

  GnomeUIInfo*         uiinfo;  
  GsmColumnValueFunc   value_func;
  GsmColumnRowFunc     row_func;

  GtkWidget*           menu;
  guint                col;
};

struct _GsmColumnClass {
  GtkLabelClass parent_class;
};

guint gsm_column_get_type  (void);

/* Creates a client column widget */
GtkWidget* gsm_column_new (gchar* title, GnomeUIInfo* uiinfo,
			   GsmColumnValueFunc value_func, 
			   GsmColumnRowFunc row_func);

/* Sets a pixmap in the clist row to match a menu item */ 
void gsm_column_set_pixmap (GsmColumn* column, guint row, guint item);

#endif /* GSM_COLUMN_H */
