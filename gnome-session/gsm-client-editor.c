/* gsm-client-editor.c - a widget to edit the details of a GsmClientRow.

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

#include <config.h>
#include <gnome.h>

#include "gsm-client-editor.h"
#include "gsm-client-row.h"

enum {
  CHANGED,
  NSIGNALS
};

static guint gsm_client_editor_signals[NSIGNALS];
static GtkHBoxClass *parent_class = NULL;

static void gsm_client_editor_destroy  (GtkObject *o);
static void change (GsmClientEditor* client_editor);

static void
gsm_client_editor_class_init (GsmClientEditorClass *klass)
{
  GtkObjectClass *object_class = (GtkObjectClass*) klass;

  gsm_client_editor_signals[CHANGED] =
    gtk_signal_new ("changed",
		    GTK_RUN_LAST,
		    object_class->type,
		    GTK_SIGNAL_OFFSET (GsmClientEditorClass, changed),
		    gtk_marshal_NONE__INT_INT,
		    GTK_TYPE_NONE, 2, GTK_TYPE_INT, GTK_TYPE_INT); 

  gtk_object_class_add_signals (object_class, gsm_client_editor_signals, 
				NSIGNALS);

  parent_class = gtk_type_class (gtk_hbox_get_type ());

  klass->changed = NULL;

  object_class->destroy    = gsm_client_editor_destroy;
}

GtkTypeInfo gsm_client_editor_info = 
{
  "GsmClientEditor",
  sizeof (GsmClientEditor),
  sizeof (GsmClientEditorClass),
  (GtkClassInitFunc) gsm_client_editor_class_init,
  (GtkObjectInitFunc) NULL,
  NULL, NULL,
  (GtkClassInitFunc) NULL
};

guint
gsm_client_editor_get_type (void)
{
  static guint type = 0;

  if (!type)
    type = gtk_type_unique (gtk_hbox_get_type (), &gsm_client_editor_info);
  return type;
}

GtkWidget* 
gsm_client_editor_new (void)
{
  GsmClientEditor* client_editor = gtk_type_new (gsm_client_editor_get_type());
  GtkHBox* hbox = (GtkHBox*) client_editor;
  GtkAdjustment *adjustment;
  GnomeUIInfo* data = (GnomeUIInfo*)g_memdup (style_data, 
					      (guint)sizeof (style_data));
  GtkWidget *menu = gnome_popup_menu_new (data);

  g_free (data);
  adjustment = GTK_ADJUSTMENT (gtk_adjustment_new (50.0, 0.0, 99.0, 
						   1.0, 10.0, 0.0));
  client_editor->spin_button = gtk_spin_button_new (adjustment, 1.0, 0);
  gtk_box_pack_start (GTK_BOX (hbox), gtk_label_new (_("Order: ")), 
		      FALSE, FALSE, GNOME_PAD_SMALL);
  gtk_box_pack_start (GTK_BOX (hbox), client_editor->spin_button, 
		      FALSE, FALSE, GNOME_PAD_SMALL);

  client_editor->style_menu = gtk_option_menu_new ();
  gtk_option_menu_set_menu (GTK_OPTION_MENU(client_editor->style_menu), menu);

  gtk_box_pack_end (GTK_BOX (hbox), client_editor->style_menu, 
		    FALSE, FALSE, GNOME_PAD_SMALL);
  gtk_box_pack_end (GTK_BOX (hbox), gtk_label_new (_("Style: ")), 
		      FALSE, FALSE, GNOME_PAD_SMALL);

  gtk_signal_connect_object (GTK_OBJECT (client_editor->spin_button), 
			     "changed", GTK_SIGNAL_FUNC (change), 
			     GTK_OBJECT (client_editor));
  gtk_signal_connect_object (GTK_OBJECT (menu), 
			     "deactivate",  GTK_SIGNAL_FUNC (change), 
			     GTK_OBJECT (client_editor));

  return GTK_WIDGET (client_editor);
}

static void 
gsm_client_editor_destroy (GtkObject *o)
{
  GsmClientEditor* client_editor = (GsmClientEditor*) o;

  g_return_if_fail(client_editor != NULL);
  g_return_if_fail(GSM_IS_CLIENT_EDITOR(client_editor));

  (*(GTK_OBJECT_CLASS (parent_class)->destroy))(o);
}

void
gsm_client_editor_set_client (GsmClientEditor* client_editor, 
			      GsmClient* client)
{
  g_return_if_fail (client_editor != NULL);
  g_return_if_fail (GSM_IS_CLIENT_EDITOR (client_editor));

  client_editor->client = NULL;

  if (client)
    {
      g_return_if_fail (GSM_IS_CLIENT (client));
      gtk_spin_button_set_value (GTK_SPIN_BUTTON (client_editor->spin_button), 
				 client->order);
      gtk_option_menu_set_history (GTK_OPTION_MENU (client_editor->style_menu),
				   client->style);
    }

  client_editor->client = client;
}

/* private functions */
static void
change (GsmClientEditor* client_editor)
{
  if (client_editor->client)
    {
      GtkSpinButton *spin = (GtkSpinButton*)client_editor->spin_button;
      gint order = gtk_spin_button_get_value_as_int (spin);
      GtkOptionMenu *omenu = GTK_OPTION_MENU (client_editor->style_menu);
      GList *list;
      gint style;
      
      for (list = GTK_MENU_SHELL (omenu->menu)->children, style = 0; list; list = list->next, style++)
	if (omenu->menu_item == list->data) 
	  break;

      gtk_signal_emit ((GtkObject*)client_editor, 
		       gsm_client_editor_signals[CHANGED], order, style); 
      client_editor->client->style = style;
      client_editor->client->order = order;
    }
}
