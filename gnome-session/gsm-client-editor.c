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

#include <glib/gi18n.h>

#include <gtk/gtk.h>

#include "gsm-client-editor.h"
#include "gsm-client-row.h"
#include "gsm-atk.h"
#include "gsm-marshal.h"

enum {
  CHANGED,
  NSIGNALS
};

static guint gsm_client_editor_signals[NSIGNALS];

G_DEFINE_TYPE (GsmClientEditor, gsm_client_editor, GTK_TYPE_HBOX);

static void gsm_client_editor_destroy  (GtkObject *o);
static void change (GsmClientEditor* client_editor);

static void
gsm_client_editor_init (GsmClientEditor *editor)
{
}

static void
gsm_client_editor_class_init (GsmClientEditorClass *klass)
{
  GtkObjectClass *object_class = (GtkObjectClass*) klass;

  gsm_client_editor_signals[CHANGED] =
    g_signal_new ("changed",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (GsmClientEditorClass, changed),
		  NULL, NULL,
		  gsm_marshal_VOID__INT_INT,
		  G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);

  klass->changed = NULL;

  object_class->destroy = gsm_client_editor_destroy;
}

enum {
  STYLE_PIXBUF_COL,
  STYLE_TEXT_COL
};

static GtkWidget *
gsm_client_editor_get_style_combo (void)
{
  GtkTreeIter      iter;
  GtkListStore    *store;
  GtkWidget       *combo;
  GtkCellRenderer *renderer;
  gint i;

  store = gtk_list_store_new (2, GDK_TYPE_PIXBUF, G_TYPE_STRING);

  for (i = 0; i < GSM_NSTYLES; i++)
    {
       gtk_list_store_append (store, &iter);
       gtk_list_store_set (store, &iter,
                           STYLE_PIXBUF_COL, style_data[i].pixbuf,
                           STYLE_TEXT_COL, style_data[i].name,
                           -1);
    }

  combo = gtk_combo_box_new_with_model (GTK_TREE_MODEL (store));

  renderer = gtk_cell_renderer_pixbuf_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo), renderer, FALSE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo), renderer,
                                  "pixbuf", STYLE_PIXBUF_COL,
                                  NULL);

  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo), renderer, TRUE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo), renderer,
                                  "text", STYLE_TEXT_COL,
                                  NULL);

  return combo;
}

GtkWidget* 
gsm_client_editor_new (void)
{
  GsmClientEditor* client_editor = g_object_new (GSM_TYPE_CLIENT_EDITOR, NULL);
  GtkHBox* hbox = (GtkHBox*) client_editor;
  GtkAdjustment *adjustment;
  GtkWidget *label;

  adjustment = GTK_ADJUSTMENT (gtk_adjustment_new (50.0, 0.0, 99.0, 
						   1.0, 10.0, 0.0));
  client_editor->spin_button = gtk_spin_button_new (adjustment, 1.0, 0);
  gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (client_editor->spin_button), TRUE);
  label = gtk_label_new_with_mnemonic (_("_Order:"));
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), client_editor->spin_button);
  gsm_atk_set_description (client_editor->spin_button, _("The order in which applications are started in the session."));
  gtk_box_pack_start (GTK_BOX (hbox), label,
		      FALSE, FALSE, 4);
  gtk_box_pack_start (GTK_BOX (hbox), client_editor->spin_button, 
		      FALSE, FALSE, 4);

  client_editor->style_menu = gsm_client_editor_get_style_combo ();
  gtk_combo_box_set_active (GTK_COMBO_BOX (client_editor->style_menu), 0);
  gsm_atk_set_description (client_editor->style_menu, _("What happens to the application when it exits."));
  label = gtk_label_new_with_mnemonic (_("_Style:"));
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), client_editor->style_menu);
  gtk_box_pack_end (GTK_BOX (hbox), client_editor->style_menu, 
		    FALSE, FALSE, 4);
  gtk_box_pack_end (GTK_BOX (hbox), label,
		      FALSE, FALSE, 4);

  g_signal_connect_swapped (client_editor->spin_button, 
			    "value-changed", G_CALLBACK (change), 
			    client_editor);
  g_signal_connect_swapped (client_editor->style_menu,
			    "changed",  G_CALLBACK (change),
			    client_editor);

  return GTK_WIDGET (client_editor);
}

static void 
gsm_client_editor_destroy (GtkObject *o)
{
  GsmClientEditor* client_editor = (GsmClientEditor*) o;

  g_return_if_fail(client_editor != NULL);
  g_return_if_fail(GSM_IS_CLIENT_EDITOR(client_editor));

  (*(GTK_OBJECT_CLASS (gsm_client_editor_parent_class)->destroy))(o);
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
      gtk_combo_box_set_active (GTK_COMBO_BOX (client_editor->style_menu),
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
      gint style;

      style = gtk_combo_box_get_active (GTK_COMBO_BOX (client_editor->style_menu));

      g_signal_emit (client_editor,
		     gsm_client_editor_signals[CHANGED], 0, order, style);
      client_editor->client->style = style;
      client_editor->client->order = order;
    }
}
