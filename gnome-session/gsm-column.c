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

/* NB: This widget only works as the child of a clist column button. */

#include <config.h>
#include <gnome.h>

#include "gsm-column.h"

static GtkLabelClass *parent_class = NULL;

static void gsm_column_destroy  (GtkObject *o);
static void parent_set          (GtkWidget *w, GtkWidget *old_parent);

static void
gsm_column_class_init (GsmColumnClass *klass)
{
  GtkObjectClass *object_class = (GtkObjectClass*) klass;
  GtkWidgetClass *widget_class = (GtkWidgetClass*) klass;

  parent_class = gtk_type_class (gtk_label_get_type ());

  widget_class->parent_set = parent_set;
  object_class->destroy    = gsm_column_destroy;
}

GtkTypeInfo gsm_column_info = 
{
  "GsmColumn",
  sizeof (GsmColumn),
  sizeof (GsmColumnClass),
  (GtkClassInitFunc) gsm_column_class_init,
  (GtkObjectInitFunc) NULL,
  (GtkArgSetFunc) NULL,
  (GtkArgGetFunc) NULL,
  (GtkClassInitFunc) NULL
};

guint
gsm_column_get_type (void)
{
  static guint type = 0;

  if (!type)
    type = gtk_type_unique (gtk_label_get_type (), &gsm_column_info);
  return type;
}

GtkWidget* 
gsm_column_new (gchar* title, GnomeUIInfo* uiinfo,
		GsmColumnValueFunc value_func, GsmColumnRowFunc row_func)
{
  GsmColumn* column = gtk_type_new (gsm_column_get_type());

  gtk_label_set_text (GTK_LABEL (column), title);
  column->uiinfo      = uiinfo;
  column->value_func  = value_func;
  column->row_func    = row_func;
  column->col         = -1;
  if (column->uiinfo)
    column->menu = gnome_popup_menu_new (uiinfo);
  return GTK_WIDGET (column);
}

static void 
gsm_column_destroy (GtkObject *o)
{
  GsmColumn* column = (GsmColumn*) o;

  g_return_if_fail(column != NULL);
  g_return_if_fail(GSM_IS_COLUMN(column));

  gtk_widget_destroy (column->menu);
  
  (*(GTK_OBJECT_CLASS (parent_class)->destroy))(o);
}

void
gsm_column_set_pixmap (GsmColumn* column, guint row, guint item)
{
  /* FIXME: protect against bad values ! */
  GtkWidget* menuitem = column->uiinfo[item].widget;
  GtkWidget* pixmap = GTK_PIXMAP_MENU_ITEM(menuitem)->pixmap;
  GtkCList *clist = (GtkCList*) (GTK_WIDGET(column)->parent->parent);

  gtk_clist_set_pixmap (clist, row, column->col, 
			GNOME_PIXMAP(pixmap)->pixmap, 
			GNOME_PIXMAP(pixmap)->mask);
} 

/* private functions */
static void
position_menu (GtkMenu *menu, gint *x, gint *y, gpointer data)
{
  GtkWidget *parent = GTK_WIDGET (data);
  gint screen_width;
  gint screen_height;
  gint twidth, theight;
  gint tx, ty;

  twidth = GTK_WIDGET (menu)->requisition.width;
  theight = GTK_WIDGET (menu)->requisition.height;

  screen_width = gdk_screen_width ();
  screen_height = gdk_screen_height ();

  gdk_window_get_origin (parent->window, &tx, &ty);

  if ((ty + parent->allocation.height + theight) <= screen_height)
    ty += parent->allocation.height;
  else if ((ty - theight) >= 0)
    ty -= theight;
  else
    ty += parent->allocation.height;
  
  if ((tx + twidth) > screen_width)
    {
      tx -= ((tx + twidth) - screen_width);
      if (tx < 0)
	tx = 0;
    }
  
  *x = tx;
  *y = ty;
}

static void
parent_clicked (GtkWidget* parent, gpointer data)
{
  GsmColumn* column = (GsmColumn*) data;
  GtkCList *clist = (GtkCList*) (parent->parent);

  g_return_if_fail (clist != NULL);
  g_return_if_fail (GTK_IS_CLIST(clist));

  if (! column->row_func || clist->selection)
    {
      gboolean do_rows = FALSE;
      gint value;

      if (column->uiinfo)
	{
	  value = gnome_popup_menu_do_popup_modal (column->menu, 
						   position_menu, parent, 
						   NULL, NULL);
	  do_rows = (value != -1);
	}
      else if (column->value_func)
	do_rows = column->value_func (&value);

      if (do_rows && column->row_func)
	{
	  GList *obj_list = NULL;
	  GList *list;

	  /* Copy objects in case things change */
	  for (list = clist->selection; list; list = list->next)
	    {
	      guint row = GPOINTER_TO_INT (list->data);
	      gpointer object = gtk_clist_get_row_data (clist, row);
	      obj_list = g_list_prepend (obj_list, object);
	    }

	  gtk_clist_freeze (clist);
	  for (list = obj_list; list; list = list->next)
	    column->row_func (list->data, value);
	  g_list_free (obj_list);
	  gtk_clist_unselect_all (clist);
	  gtk_clist_thaw (clist);
	}
    }
}

static void
parent_set (GtkWidget* widget, GtkWidget* old_parent)
{
  GsmColumn* column = (GsmColumn*)widget;
  GtkWidget* parent = widget->parent;
  GtkCList* clist;
  guint col;

  if (!parent)
    {
      gtk_signal_disconnect_by_func (GTK_OBJECT (old_parent), 
				     parent_clicked, (gpointer)column);
      column->col = -1;
      return;
    }
  
  g_return_if_fail (GTK_IS_BUTTON (parent));
  clist = (GtkCList*)parent->parent;
  g_return_if_fail (GTK_IS_CLIST (clist));
  gtk_signal_connect (GTK_OBJECT (parent), "clicked",
		      parent_clicked, (gpointer)column);
  
  for (col = 0; col < clist->columns; col++)
    if (clist->column[col].button == parent)
      {
	column->col = col;
	break;
      }
  gtk_clist_set_column_justification (clist, col, GTK_JUSTIFY_CENTER);
}
