/* session-properties.c - a non-CORBA version of the session-properties-capplet

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

#include "gsm-client-list.h"
#include "gsm-atk.h"

#include "session-properties.h"
#include "session-properties-capplet.h"

static GtkWidget *remove_button;
GtkWidget *apply_button;
static GtkWidget *scrolled_window;
static GtkWidget *client_list;
static GtkWidget *client_editor;

static DirtyCallbackFunc parent_dirty_cb;

/* table widget callback prototypes */
static void remove_cb (GtkWidget *widget);
static void selection_changed (GtkTreeSelection *selection);
static void dirty_cb (GtkWidget *widget);

static GtkWidget*
create_table (void)
{
  GtkWidget *table, *vbox, *hbox,  *alignment, *label;

  remove_button = gtk_button_new_from_stock (GTK_STOCK_REMOVE);
  gsm_atk_set_description (remove_button, _("Remove the currently selected client from the session."));
  gtk_widget_set_sensitive (GTK_WIDGET (remove_button), FALSE);
  gtk_signal_connect(GTK_OBJECT (remove_button), "clicked",
		     GTK_SIGNAL_FUNC (remove_cb), NULL);

  apply_button = gtk_button_new_from_stock (GTK_STOCK_APPLY);
  gsm_atk_set_description (apply_button, _("Apply changes to the current session"));
  gtk_widget_set_sensitive (GTK_WIDGET (apply_button), FALSE);
  g_signal_connect (G_OBJECT (apply_button), "clicked", apply, NULL);

  /* client list */
  client_list = gsm_client_list_new ();
  gsm_atk_set_description (client_list, _("The list of programs in the session."));
  gsm_client_list_live_session (GSM_CLIENT_LIST (client_list));
  g_signal_connect(gtk_tree_view_get_selection (GTK_TREE_VIEW (client_list)),
		   "changed", G_CALLBACK (selection_changed),
		   client_list);
  gtk_signal_connect(GTK_OBJECT(client_list), "dirty",
		     GTK_SIGNAL_FUNC (dirty_cb), NULL);

  client_editor = gsm_client_list_get_editor (GSM_CLIENT_LIST (client_list));

  /* scrolled window - disabled until the client_list "initialized" signal */
  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
				  GTK_POLICY_NEVER, GTK_POLICY_NEVER);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window),
				       GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER (scrolled_window), client_list);

  vbox = gtk_vbox_new (FALSE, GNOME_PAD);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), GNOME_PAD);

  hbox = gtk_hbox_new (FALSE, GNOME_PAD);
  gtk_box_pack_start (GTK_BOX (hbox), client_editor, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), remove_button, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), apply_button, FALSE, FALSE, 0);
  
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

  alignment = gtk_alignment_new (0.0, 0.5, 0.0, 0.0);
  label = gtk_label_new_with_mnemonic (_("Currently running _programs:"));
  gtk_container_add (GTK_CONTAINER (alignment), label);
  gtk_box_pack_start (GTK_BOX (vbox), alignment, FALSE, FALSE, 0);
  
  gtk_box_pack_start (GTK_BOX (vbox), scrolled_window, TRUE, TRUE, 0);

  gtk_label_set_mnemonic_widget (GTK_LABEL (label), client_list);

  return vbox;
}

static gboolean init_settings = FALSE;

static const struct poptOption options[] = {
  {"init-session-settings", '\0', POPT_ARG_NONE, &init_settings, 0, N_("Initialize session settings"), NULL},
  {NULL, '\0', 0, NULL, 0}
};

GtkWidget *
session_properties_create_page (DirtyCallbackFunc dcf)
{
  GtkWidget *page;

  parent_dirty_cb = dcf;

  page = create_table ();

  gtk_widget_show_all (page);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
				  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

  return page;
}

/* CAPPLET CALLBACKS */
void
session_properties_apply (void)
{
  gsm_client_list_commit_changes (GSM_CLIENT_LIST (client_list));
}

/* Remove the selected clients. */
static void
remove_cb (GtkWidget *widget)
{
  gsm_client_list_remove_selection (GSM_CLIENT_LIST (client_list));
}

/* This is called when a client is selected.  */
static void
selection_changed (GtkTreeSelection *selection)
{
  gtk_widget_set_sensitive (GTK_WIDGET (remove_button),
			    gtk_tree_selection_get_selected (selection, NULL, NULL));
}

/* This is called when an change is made in the client list.  */
static void
dirty_cb (GtkWidget *widget)
{
  parent_dirty_cb ();
}
