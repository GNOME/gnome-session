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
#include <string.h>
#include <gnome.h>
#include <libgnomeui/gnome-window-icon.h>

#include "gsm-client-list.h"
#include "gsm-client-row.h"
#include "gsm-protocol.h"
#include "gsm-atk.h"

#include "session-properties.h"

GsmProtocol *protocol;

GtkWidget *entry;
GtkWidget *add_button;
GtkWidget *remove_button;
GtkWidget *scrolled_window;
GtkWidget *client_list;
GtkWidget *client_editor;
GtkWidget *session_list;

GtkWidget *app;
GtkWidget *paned;
GtkWidget *gnome_foot;
GtkWidget *left;

GtkWidget* close_button;
GtkWidget* apply_button;
GtkWidget* help_button;

DirtyCallbackFunc parent_dirty_cb;

/* capplet callback prototypes */
static void help (void);

/* table widget callback prototypes */
static void entry_changed_cb (GtkWidget *widget);
static void add_cb (GtkWidget *widget);
static void remove_cb (GtkWidget *widget);
static void selection_changed (GtkTreeSelection *selection);
static void dirty_cb (GtkWidget *widget);
/* UNUSED
static void initialized_cb (GtkWidget *widget);
*/

static GtkWidget*
create_right (void)
{
  /* client list */
  client_list = gsm_client_list_new ();

  /* scrolled window - disabled until the client_list "initialized" signal */
  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
				  GTK_POLICY_NEVER, GTK_POLICY_NEVER);
  gtk_container_add (GTK_CONTAINER (scrolled_window), client_list);

  return scrolled_window;
}

static GtkWidget*
create_table (void)
{
  GtkWidget *table, *vbox, *alignment;

  /* program entry box */
  entry = gtk_entry_new ();
  gtk_signal_connect (GTK_OBJECT (entry), "activate", 
		      GTK_SIGNAL_FUNC (add_cb), NULL);
  gtk_signal_connect (GTK_OBJECT (entry), "changed", 
		      GTK_SIGNAL_FUNC (entry_changed_cb), NULL);
  /* add/remove buttons */
  add_button = gtk_button_new_from_stock (GTK_STOCK_ADD);
  gtk_widget_set_sensitive (GTK_WIDGET (add_button), FALSE);
  gtk_signal_connect (GTK_OBJECT (add_button), "clicked",
		      GTK_SIGNAL_FUNC (add_cb), NULL);
  remove_button = gtk_button_new_from_stock (GTK_STOCK_REMOVE);
  gsm_atk_set_description (remove_button, _("Remove the currently selected client from the session."));
  gtk_widget_set_sensitive (GTK_WIDGET (remove_button), FALSE);
  gtk_signal_connect(GTK_OBJECT (remove_button), "clicked",
		     GTK_SIGNAL_FUNC (remove_cb), NULL);

  /* client list */
  client_list = gsm_client_list_new ();
  gsm_atk_set_description (client_list, _("The list of programs in the session."));
  gsm_client_list_live_session (GSM_CLIENT_LIST (client_list));
#if 0 /* this is currently broken */
  gtk_signal_connect(GTK_OBJECT (client_list), "initialized",
		     GTK_SIGNAL_FUNC (initialized_cb), NULL);
#endif
  g_signal_connect(gtk_tree_view_get_selection (GTK_TREE_VIEW (client_list)),
		   "changed", G_CALLBACK (selection_changed),
		   client_list);
  gtk_signal_connect(GTK_OBJECT(client_list), "dirty",
		     GTK_SIGNAL_FUNC (dirty_cb), NULL);

  client_editor = gsm_client_list_get_editor (GSM_CLIENT_LIST (client_list));

  /* scrolled window - disabled until the client_list "initialized" signal */
  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_widget_set_usize (GTK_WIDGET (scrolled_window), -2, 250);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
				  GTK_POLICY_NEVER, GTK_POLICY_NEVER);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window),
				       GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER (scrolled_window), client_list);

  /* table */
  vbox = gtk_vbox_new (FALSE, GNOME_PAD);
	  
  table = gtk_table_new (1, 4, TRUE);
/*    gtk_table_attach (GTK_TABLE (table), gtk_label_new (_("Program:")), */
/*  		    0, 1, 0, 1, GTK_FILL, GTK_FILL, 0, 0); */
/*    gtk_table_attach (GTK_TABLE (table), gtk_hbox_new (FALSE, 0),  */
/*  		    1, 2, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL, 0, 0); */
/*    gtk_table_attach (GTK_TABLE (table), add_button,  */
/*  		    2, 3, 0, 1, GTK_FILL, GTK_FILL, 0, 0);  */
/*    gtk_table_attach (GTK_TABLE (table), entry,  */
/*  		    0, 4, 1, 2, GTK_FILL|GTK_EXPAND, GTK_FILL, 0, 0); */
  gtk_table_attach (GTK_TABLE (table), client_editor, 
		    0, 2, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL, 0, 0);
  gtk_table_set_col_spacings (GTK_TABLE (table), GNOME_PAD);

  alignment = gtk_alignment_new (1.0, 0.5, 0.0, 0.0);
  gtk_container_add (GTK_CONTAINER (alignment), remove_button);
  
  gtk_table_attach (GTK_TABLE (table), alignment, 
		    3, 4, 0, 1, GTK_FILL, GTK_FILL, 0, 0);

  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), scrolled_window, TRUE, TRUE, 0);

  return vbox;
}

static gchar* selected_session = NULL;

static void
sess_select_row (GtkWidget *w, gint row)
{
  gchar* name;

  g_assert_not_reached ();

  gtk_clist_get_text (GTK_CLIST (w), row, 0, &name);

  gsm_client_list_saved_session (GSM_CLIENT_LIST (client_list), name);
#if 0
  gtk_signal_connect(GTK_OBJECT (client_list), "initialized",
		     GTK_SIGNAL_FUNC (initialized_cb), NULL);
#endif
}

static void
sess_start (GtkWidget *w)
{
  GtkWidget *viewport;

  gsm_client_list_start_session (GSM_CLIENT_LIST (client_list));
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (left),
				  GTK_POLICY_NEVER, GTK_POLICY_NEVER);
  gtk_container_remove (GTK_CONTAINER (left), session_list);
  viewport =
    gtk_viewport_new (gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (left)),
		      gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (left)));
  gtk_container_add (GTK_CONTAINER (viewport), gnome_foot);
  gtk_widget_show_all (viewport);

  gnome_dialog_set_sensitive (GNOME_DIALOG (app), 0, FALSE);
  /* FIXME: might be nice to be able to cancel ... */
  gnome_dialog_set_sensitive (GNOME_DIALOG (app), 1, FALSE);

  gtk_container_add (GTK_CONTAINER (left), viewport);
}

static void
sess_cancel (GtkWidget *w)
{
}

static GtkWidget*
create_left (void)
{
  gchar* foot_file = gnome_pixmap_file ("gnome-logo-large.png");
  gchar* title = _("Session");
  GtkRequisition req;

  gnome_foot = gnome_pixmap_new_from_file (foot_file);
  gtk_widget_size_request (gnome_foot, &req);

  g_assert_not_reached ();

  session_list = gtk_clist_new_with_titles(1, &title);
  gtk_widget_set_usize (session_list, req.width, req.height);
  gtk_signal_connect (GTK_OBJECT (session_list), "select_row",
		      GTK_SIGNAL_FUNC (sess_select_row), 
		      NULL);

  /* scrolled window */
  left = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (left),
				  GTK_POLICY_NEVER, GTK_POLICY_NEVER);
  gtk_container_add (GTK_CONTAINER (left), session_list);

  return left;
}

static void
last_session (GObject *obj,
	      char    *name)
{
  selected_session = name;
}

static void
saved_sessions (GObject *obj,
		GSList  *session_names)
{
  GSList *list;

  for (list = session_names; list; list = list->next)
    {
      gint row;
      gchar* name = (gchar*)list->data;
      row = gtk_clist_append (GTK_CLIST (session_list), &name);
      if (! strcmp (selected_session, name))
	gtk_clist_select_row (GTK_CLIST (session_list), row, 0);
    }
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (left),
				  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
}

static GtkWidget*
create_dialog (void)
{
  /* paned window */
  paned = gtk_hpaned_new();
#if 0
  gtk_paned_handle_size (GTK_PANED (paned), 10);
#endif
  gtk_paned_gutter_size (GTK_PANED (paned), 10);
  gtk_paned_add1 (GTK_PANED (paned), create_left ());
  gtk_paned_add2 (GTK_PANED (paned), create_right ());

  /* app*/
  app = gnome_dialog_new (_("Session Chooser"), NULL);
  gtk_container_add (GTK_CONTAINER (GNOME_DIALOG (app)->vbox), paned);
  gnome_dialog_append_button_with_pixmap (GNOME_DIALOG (app),
					  _("Start Session"),
					  GNOME_STOCK_BUTTON_OK);
  gnome_dialog_append_button_with_pixmap (GNOME_DIALOG (app),
					  _("Cancel Login"),
					  GNOME_STOCK_BUTTON_CANCEL);
  gnome_dialog_set_default (GNOME_DIALOG (app), 0);
  gnome_dialog_button_connect (GNOME_DIALOG (app), 0,
			       GTK_SIGNAL_FUNC (sess_start), NULL);
  gnome_dialog_button_connect (GNOME_DIALOG (app), 1,
			       GTK_SIGNAL_FUNC (sess_cancel), NULL);

  gtk_signal_connect (GTK_OBJECT (app), "delete_event",
		      GTK_SIGNAL_FUNC (gtk_main_quit), NULL);

  g_signal_connect (protocol, "last_session", 
		    G_CALLBACK (last_session), NULL);
  gsm_protocol_get_current_session (GSM_PROTOCOL (protocol));

  g_signal_connect (protocol, "saved_sessions", 
		    G_CALLBACK (saved_sessions), NULL);
  gsm_protocol_get_saved_sessions (GSM_PROTOCOL (protocol));

  return app;
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

static void
help (void)
{
  gnome_help_display ("session", NULL, NULL);
}

/* This is called when user has changed the entry  */
static void
entry_changed_cb (GtkWidget *widget)
{
	const gchar *value = gtk_entry_get_text (GTK_ENTRY (entry));
	gtk_widget_set_sensitive (GTK_WIDGET (add_button), (value && *value));
}

/* Add a new client. */
static void
add_cb (GtkWidget *widget)
{
  const gchar *command = gtk_entry_get_text (GTK_ENTRY (entry));

  if (gsm_client_list_add_program (GSM_CLIENT_LIST (client_list), command))
    {
      gtk_entry_set_text (GTK_ENTRY (entry), "");
      gtk_widget_set_sensitive (GTK_WIDGET (add_button), FALSE);
    }
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
  (parent_dirty_cb) ();
}

#if 0
/* This is called when the client_list has been filled by gnome-session */
static void
initialized_cb (GtkWidget *widget)
{
  gtk_widget_show_all (app);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
				  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
}
#endif
