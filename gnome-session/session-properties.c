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

#include "gsm-client-list.h"

GtkWidget *entry;
GtkWidget *add_button;
GtkWidget *remove_button;
GtkWidget *scrolled_window;
GtkWidget *client_list;

GtkWidget *app;

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
static void initialized_cb (GtkWidget *widget);

static GtkWidget*
create_table (void)
{
  GtkWidget *table;

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
  if (!client_list)
    {
      g_warning ("Could not connect to gnome-session.");
      exit (1);
    }
  gtk_signal_connect(GTK_OBJECT(client_list), "select_row",
		     GTK_SIGNAL_FUNC (select_cb), NULL);
  gtk_signal_connect(GTK_OBJECT(client_list), "unselect_row",
		     GTK_SIGNAL_FUNC (unselect_cb), NULL);
  gtk_signal_connect(GTK_OBJECT(client_list), "dirty",
		     GTK_SIGNAL_FUNC (dirty_cb), NULL);
  gtk_signal_connect(GTK_OBJECT(client_list), "initialized",
		     GTK_SIGNAL_FUNC (initialized_cb), NULL);

  /* scrolled window - disabled until the client_list "initialized" signal */
  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
				  GTK_POLICY_NEVER, GTK_POLICY_NEVER);
  gtk_container_add (GTK_CONTAINER (scrolled_window), client_list);

  /* table */
  table = gtk_table_new (3, 4, FALSE);
  gtk_table_attach (GTK_TABLE (table), gtk_label_new (_("Program:")),
		    0, 1, 0, 1, GTK_FILL, GTK_FILL, 0, 0);
  gtk_table_attach (GTK_TABLE (table), gtk_hbox_new (FALSE, 0), 
		    1, 2, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL, 0, 0);
  gtk_table_attach (GTK_TABLE (table), add_button, 
		    2, 3, 0, 1, GTK_FILL, GTK_FILL, 0, 0); 
  gtk_table_attach (GTK_TABLE (table), remove_button, 
		    3, 4, 0, 1, GTK_FILL, GTK_FILL, 0, 0);
  gtk_table_attach (GTK_TABLE (table), entry, 
		    0, 4, 1, 2, GTK_FILL|GTK_EXPAND, GTK_FILL, 0, 0);
  gtk_table_attach (GTK_TABLE (table), scrolled_window, 
		    0, 4, 2, 3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND,0, 0);
  gtk_table_set_col_spacings (GTK_TABLE (table), GNOME_PAD);
  gtk_table_set_row_spacings (GTK_TABLE (table), GNOME_PAD);

  return table;
}

static GtkWidget*
create_buttons (GtkWidget * table)
{
  GtkWidget* frame;
  GtkWidget* bbox, *vbox;

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
  table = gtk_table_new (3, 1, FALSE);
  gtk_table_attach (GTK_TABLE (table), frame,
		    0, 1, 0, 1, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND, 0, 0);
  gtk_table_attach (GTK_TABLE (table), gtk_hseparator_new (),
		    0, 1, 1, 2, GTK_FILL|GTK_EXPAND,GTK_FILL, 0, 0);
  gtk_table_attach (GTK_TABLE (table), bbox,
		    0, 1, 2, 3, GTK_FILL|GTK_EXPAND,GTK_FILL, 0, 0);
  return table;
}  

static GtkWidget*
create_app (GtkWidget *table)
{
  /* app*/
  app = gnome_app_new ("Session", "Session Properties");
  gnome_app_set_contents (GNOME_APP (app), table);

  gtk_signal_connect (GTK_OBJECT (app), "delete_event",
		      GTK_SIGNAL_FUNC (gtk_main_quit), NULL);
  return app;
}

int
main (int argc, char *argv[])
{
  bindtextdomain (PACKAGE, GNOMELOCALEDIR);
  textdomain (PACKAGE);

  gnome_init ("session-properties", VERSION, argc, argv);

  gtk_signal_connect (GTK_OBJECT (gnome_master_client ()), "die",
		      GTK_SIGNAL_FUNC (gtk_main_quit), NULL);

  create_app (create_buttons (create_table()));

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
  gchar* file = gnome_help_file_find_file(program_invocation_short_name, 
					  "index.html");
  if (file)
    gnome_help_goto (NULL, file);
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
      gtk_widget_grab_focus (GTK_WIDGET (entry));
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

/* This is called when the client_list has been filled by gnome-session */
static void
initialized_cb (GtkWidget *widget)
{
  gtk_widget_show_all (app);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
				  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
}
