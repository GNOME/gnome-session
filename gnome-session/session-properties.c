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

GtkObject *protocol;

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

GtkWidget* try_button;
GtkWidget* revert_button;
GtkWidget* ok_button;
GtkWidget* cancel_button;
GtkWidget* help_button;

/* capplet callback prototypes */
static void try (void);
static void revert (void);
static void ok (void);
static void cancel (void);
static void help (void);

/* table widget callback prototypes */
static void entry_changed_cb (GtkWidget *widget);
static void add_cb (GtkWidget *widget);
static void remove_cb (GtkWidget *widget);
static void select_cb (GtkCList *clist);
static void unselect_cb (GtkCList *clist);
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
  add_button = gnome_stock_button (GNOME_STOCK_PIXMAP_ADD);
  gtk_widget_set_sensitive (GTK_WIDGET (add_button), FALSE);
  gtk_signal_connect (GTK_OBJECT (add_button), "clicked",
		      GTK_SIGNAL_FUNC (add_cb), NULL);
  remove_button = gnome_stock_button (GNOME_STOCK_PIXMAP_REMOVE);
  gtk_widget_set_sensitive (GTK_WIDGET (remove_button), FALSE);
  gtk_signal_connect(GTK_OBJECT (remove_button), "clicked",
		     GTK_SIGNAL_FUNC (remove_cb), NULL);

  /* client list */
  client_list = gsm_client_list_new ();
  gsm_client_list_live_session (GSM_CLIENT_LIST (client_list));
#if 0 /* this is currently broken */
  gtk_signal_connect(GTK_OBJECT (client_list), "initialized",
		     GTK_SIGNAL_FUNC (initialized_cb), NULL);
#endif
  gtk_signal_connect(GTK_OBJECT(client_list), "select_row",
		     GTK_SIGNAL_FUNC (select_cb), NULL);
  gtk_signal_connect(GTK_OBJECT(client_list), "unselect_row",
		     GTK_SIGNAL_FUNC (unselect_cb), NULL);
  gtk_signal_connect(GTK_OBJECT(client_list), "dirty",
		     GTK_SIGNAL_FUNC (dirty_cb), NULL);

  client_editor = gsm_client_list_get_editor (GSM_CLIENT_LIST (client_list));

  /* scrolled window - disabled until the client_list "initialized" signal */
  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_widget_set_usize (GTK_WIDGET (scrolled_window), -2, 250);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
				  GTK_POLICY_NEVER, GTK_POLICY_NEVER);
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

static GtkWidget*
create_buttons (GtkWidget * table)
{
  GtkWidget* frame, *table2;
  GtkWidget* bbox;

  /* frame */
  frame = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);
  gtk_container_set_border_width (GTK_CONTAINER (frame), GNOME_PAD_SMALL);
  gtk_container_add (GTK_CONTAINER (frame), table);
  
  /* buttons */
  try_button = gtk_button_new_with_label (_("Try"));
  gtk_signal_connect (GTK_OBJECT (try_button), "clicked", 
		      GTK_SIGNAL_FUNC (try), NULL);
  revert_button = gtk_button_new_with_label (_("Revert"));
  gtk_signal_connect (GTK_OBJECT (revert_button), "clicked", 
		      GTK_SIGNAL_FUNC (revert), NULL);
  ok_button = gtk_button_new_with_label (_("OK"));
  gtk_signal_connect (GTK_OBJECT (ok_button), "clicked", 
		      GTK_SIGNAL_FUNC (ok), NULL);
  cancel_button = gtk_button_new_with_label (_("Cancel"));
  gtk_signal_connect (GTK_OBJECT (cancel_button), "clicked", 
		      GTK_SIGNAL_FUNC (cancel), NULL);
  help_button = gtk_button_new_with_label (_("Help"));
  gtk_signal_connect (GTK_OBJECT (help_button), "clicked", 
		      GTK_SIGNAL_FUNC (help), NULL);
  gtk_widget_set_sensitive (try_button, FALSE);
  gtk_widget_set_sensitive (revert_button, FALSE);
  gtk_widget_set_sensitive (ok_button, FALSE);

  /* button box */
  bbox = gtk_hbutton_box_new ();
  gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_SPREAD);
  gtk_button_box_set_spacing (GTK_BUTTON_BOX (bbox), GNOME_PAD_SMALL);
  gtk_button_box_set_child_size (GTK_BUTTON_BOX (bbox), GNOME_PAD_SMALL, -1);
  gtk_container_set_border_width (GTK_CONTAINER (bbox), GNOME_PAD_SMALL);

  gtk_container_add (GTK_CONTAINER (bbox), try_button);
  gtk_container_add (GTK_CONTAINER (bbox), revert_button);
  gtk_container_add (GTK_CONTAINER (bbox), ok_button);
  gtk_container_add (GTK_CONTAINER (bbox), cancel_button);
  gtk_container_add (GTK_CONTAINER (bbox), help_button);

  /* table */
  table2 = gtk_table_new (3, 1, FALSE);
  gtk_table_attach (GTK_TABLE (table2), frame,
		    0, 1, 0, 1, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND, 0, 0);
  gtk_table_attach (GTK_TABLE (table2), gtk_hseparator_new (),
		    0, 1, 1, 2, GTK_FILL|GTK_EXPAND,GTK_FILL, 0, 0);
  gtk_table_attach (GTK_TABLE (table2), bbox,
		    0, 1, 2, 3, GTK_FILL|GTK_EXPAND,GTK_FILL, 0, 0);
  return table2;
}  

static GtkWidget*
create_app (void)
{
  /* app*/
  app = gnome_app_new ("Session", _("Session Properties"));
  gnome_app_set_contents (GNOME_APP (app), create_buttons (create_table ()));

  gtk_signal_connect (GTK_OBJECT (app), "delete_event",
		      GTK_SIGNAL_FUNC (gtk_main_quit), NULL);
  return app;
}

static gchar* selected_session = NULL;

static void
sess_select_row (GtkWidget *w, gint row)
{
  gchar* name;

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

  session_list = gtk_clist_new_with_titles(1, &title);
  gtk_widget_set_usize (session_list, req.width, req.height);
  gtk_signal_connect (GTK_OBJECT (session_list), "select_row",
		      sess_select_row, NULL);

  /* scrolled window */
  left = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (left),
				  GTK_POLICY_NEVER, GTK_POLICY_NEVER);
  gtk_container_add (GTK_CONTAINER (left), session_list);

  return left;
}

static void
last_session (GtkWidget *w, gchar* name)
{
  selected_session = name;
}

static void
saved_sessions (GtkWidget *w, GSList* session_names)
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
  gtk_paned_handle_size (GTK_PANED (paned), 10);
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

  gtk_signal_connect (GTK_OBJECT (protocol), "last_session", 
		      GTK_SIGNAL_FUNC (last_session), NULL);
  gsm_protocol_get_current_session (GSM_PROTOCOL (protocol));

  gtk_signal_connect (GTK_OBJECT (protocol), "saved_sessions", 
		      GTK_SIGNAL_FUNC (saved_sessions), NULL);
  gsm_protocol_get_saved_sessions (GSM_PROTOCOL (protocol));

  return app;
}

static gboolean init_settings = FALSE;

static const struct poptOption options[] = {
  {"init-session-settings", '\0', POPT_ARG_NONE, &init_settings, 0, N_("Initialize session settings"), NULL},
  {NULL, '\0', 0, NULL, 0}
};

int
main (int argc, char *argv[])
{
  bindtextdomain (PACKAGE, GNOMELOCALEDIR);
  textdomain (PACKAGE);

  gnome_init_with_popt_table ("session-properties", VERSION, argc, argv, 
			      options, 0, NULL);
  gnome_window_icon_set_default_from_file (GNOME_ICONDIR"/gnome-session.png");
  gtk_signal_connect (GTK_OBJECT (gnome_master_client ()), "die",
		      GTK_SIGNAL_FUNC (gtk_main_quit), NULL);

  /*
   * The following 2 lines represent an ugly hack for multivisual
   * machines.  Imlib has changed slightly to use the best visual
   * it can find (i.e. the gdkrgb visual), however, since we're
   * rendering pixmaps into GTK widgets, which use the default
   * visual....
   *
   * The following 2 lines should be removed someday when sanity
   * prevails.
   *
   */

  gtk_widget_set_default_colormap (gdk_rgb_get_cmap ());
  gtk_widget_set_default_visual (gdk_rgb_get_visual ());

  protocol = gsm_protocol_new (gnome_master_client());
  if (!protocol)
    {
      g_warning ("Could not connect to gnome-session.");
      exit (1);
    }
  if (init_settings)
    create_dialog ();
  else
    create_app ();
  /* show these here since things are broken */
  gtk_widget_show_all (app);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
				  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

  gtk_main ();

  return 0;
}

/* CAPPLET CALLBACKS */
static void
try (void)
{
  gsm_client_list_commit_changes (GSM_CLIENT_LIST (client_list));
  gtk_widget_set_sensitive (try_button, FALSE);
  gtk_widget_set_sensitive (revert_button, TRUE);
}

static void
revert (void)
{
  gsm_client_list_revert_changes (GSM_CLIENT_LIST (client_list));
  gtk_widget_set_sensitive (try_button, FALSE);
  gtk_widget_set_sensitive (revert_button, FALSE);
}

static void
ok (void)
{
  try();
  gtk_main_quit();
}

static void
cancel (void)
{
  revert();
  gtk_main_quit();  
}

static void
help (void)
{
	GnomeHelpMenuEntry help_entry = {
		"session",
		"index.html"
	};
	gnome_help_display(NULL, &help_entry);
}

/* This is called when user has changed the entry  */
static void
entry_changed_cb (GtkWidget *widget)
{
  gchar *value = gtk_entry_get_text (GTK_ENTRY (entry));
  gtk_widget_set_sensitive (GTK_WIDGET (add_button), (value && *value));
}

/* Add a new client. */
static void
add_cb (GtkWidget *widget)
{
  gchar *command = gtk_entry_get_text (GTK_ENTRY (entry));

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
select_cb (GtkCList *clist)
{
  gtk_widget_set_sensitive (GTK_WIDGET (remove_button), TRUE);
}

/* This is called when no clients are selected.  */
static void
unselect_cb (GtkCList *clist)
{
  if (! clist->selection)
    {
      gtk_widget_set_sensitive (GTK_WIDGET (remove_button), FALSE);
    }
}

/* This is called when an change is made in the client list.  */
static void
dirty_cb (GtkWidget *widget)
{
  gtk_widget_set_sensitive (try_button, TRUE);
  gtk_widget_set_sensitive (revert_button, TRUE);
  gtk_widget_set_sensitive (ok_button, TRUE);
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
