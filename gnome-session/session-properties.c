/* session-properties.c - Edit session properties.

   Copyright (C) 1998 Tom Tromey

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
   02111-1307, USA.  */

#include <config.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <libgnome/libgnome.h>
#include <libgnomeui/libgnomeui.h>

#include "session.h"


/* Geometry of main window */
static char *geometry;


/* This is used as the client data for some callbacks.  It just holds
 * pointers to whatever we might need.
 */
struct _startup_info
{
  GnomePropertyBox *propertybox;

  GtkWidget *clist;
  GtkWidget *entry;
  GtkWidget *add_button;
  GtkWidget *remove_button;

  /* Name of this program.  */
  char *argv0;
} startup_info;


/* prototypes */
static void create_gui ();
static void refresh_gui_data ();
static int session_save (GnomeClient *client,
			 int phase, 
			 GnomeSaveStyle save_style,
			 int is_shutdown, 
			 GnomeInteractStyle interact_style,
			 int is_fast);
static void session_die (GnomeClient *client);


/* callback prototypes */
static void box_closed (GnomeDialog *dialog);
static void apply_properties (GnomePropertyBox *box);

static void program_row_selected_cb (GtkWidget *widget, gint row);
static void program_row_unselected_cb (GtkWidget *widget, gint row);
static void program_entry_changed_cb (GtkWidget *widget);
static void program_add_cb (GtkWidget *widget);
static void program_remove_cb (GtkWidget *widget);


static void
create_gui ()
{
  GtkWidget *label;
  GtkWidget *button;
  GtkWidget *table;
  GtkWidget *scrolled_window;

  /* create the GNOME property box */
  startup_info.propertybox = GNOME_PROPERTY_BOX (gnome_property_box_new ());
  gtk_signal_connect (
      GTK_OBJECT (startup_info.propertybox),
      "close",
      GTK_SIGNAL_FUNC (box_closed), 
      NULL);
  gtk_signal_connect (
      GTK_OBJECT (startup_info.propertybox),
      "apply",
      GTK_SIGNAL_FUNC (apply_properties),
      NULL);
  gtk_window_set_policy (GTK_WINDOW (startup_info.propertybox), TRUE, TRUE, FALSE);
  gtk_window_set_title (GTK_WINDOW (startup_info.propertybox), _("Gnome Session Properties"));
  gtk_widget_show_all (GTK_WIDGET (startup_info.propertybox));

  /* table */
  table = gtk_table_new (3, 2, FALSE);
  gtk_container_set_border_width (GTK_CONTAINER (table), 5);
  gtk_notebook_append_page (
      GTK_NOTEBOOK (startup_info.propertybox->notebook),
      GTK_WIDGET (table),
      gtk_label_new (_("Startup Programs")));
  gtk_widget_show (table);

  /* label for the exec entry */
  label = gtk_label_new ("Program:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1, 
		    GTK_FILL, GTK_FILL|GTK_EXPAND, 10, 0);
  gtk_widget_show (label);

  /* exec string entry box */
  startup_info.entry = gtk_entry_new ();
  gtk_table_attach (
      GTK_TABLE (table), 
      startup_info.entry, 
      1, 2, 0, 1,
      GTK_FILL | GTK_EXPAND, GTK_FILL,
      0, 0);
  gtk_signal_connect (
      GTK_OBJECT (startup_info.entry),
      "changed",
      GTK_SIGNAL_FUNC (program_entry_changed_cb),
      NULL);
  gtk_signal_connect (
      GTK_OBJECT (startup_info.entry),
      "activate",
      GTK_SIGNAL_FUNC (program_add_cb),
      NULL);
  gtk_widget_show(startup_info.entry);

  /* add/remove buttons */
  startup_info.add_button = gtk_button_new_with_label ("Add");
  gtk_table_attach (GTK_TABLE (table), startup_info.add_button,
		    0, 1, 1, 2, GTK_FILL, GTK_EXPAND, 5, 5);
  gtk_signal_connect (
      GTK_OBJECT (startup_info.add_button),
      "clicked",
      GTK_SIGNAL_FUNC (program_add_cb),
      NULL);
  gtk_widget_show (startup_info.add_button);

  startup_info.remove_button = gtk_button_new_with_label ("Remove");
  gtk_table_attach (GTK_TABLE (table), startup_info.remove_button, 
		    0, 1, 2, 3, GTK_FILL, GTK_EXPAND, 5, 5);
  gtk_signal_connect(
      GTK_OBJECT (startup_info.remove_button),
      "clicked",
      GTK_SIGNAL_FUNC (program_remove_cb),
      NULL);
  gtk_widget_show (startup_info.remove_button);

  /* scrolled window for clist */
  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (
      GTK_SCROLLED_WINDOW (scrolled_window),
      GTK_POLICY_AUTOMATIC,
      GTK_POLICY_AUTOMATIC);
  gtk_table_attach (GTK_TABLE (table), scrolled_window, 
      1, 2, 1, 3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 0, 5);
  gtk_widget_show (scrolled_window);

  /* startup program list */
  startup_info.clist = gtk_clist_new (1);
  gtk_clist_set_selection_mode (GTK_CLIST (startup_info.clist), GTK_SELECTION_MULTIPLE);
  gtk_container_add (GTK_CONTAINER (scrolled_window), startup_info.clist);
  gtk_signal_connect(
      GTK_OBJECT(startup_info.clist),
      "select_row",
      GTK_SIGNAL_FUNC (program_row_selected_cb),
      NULL);
  gtk_signal_connect(
      GTK_OBJECT(startup_info.clist),
      "unselect_row",
      GTK_SIGNAL_FUNC (program_row_unselected_cb),
      NULL);
  gtk_widget_show(startup_info.clist);
}


static void
refresh_gui_data ()
{
  int i, count;
  gboolean def;

  gnome_config_push_prefix (PRELOAD_PREFIX);
  count = gnome_config_get_int_with_default (PRELOAD_COUNT_KEY "=-1", &def);
  if (def)
    {
      /* If we got the default value, then there are no preloads.  */
      return;
    }

  /* load list of programs to execute */
  for (i = 0; i < count; ++i)
    {
      char *text, buf[20];

      sprintf (buf, "%d", i);
      text = gnome_config_get_string_with_default (buf, &def);
      if (! def)
	{
	  gtk_clist_append (GTK_CLIST (startup_info.clist), (const gchar **) &text);
	}
      g_free (text);
    }
}


/* session management */
static int
session_save (GnomeClient *client, 
	      int phase, 
	      GnomeSaveStyle save_style,
	      int is_shutdown, 
	      GnomeInteractStyle interact_style,
	      int is_fast)
{
  char *args[3], *geom;
  int argc;

  argc = 0;

  args[argc++] = startup_info.argv0;

  /* Geometry of main window.  */
  geom = gnome_geometry_string (GTK_WIDGET (startup_info.propertybox)->window);
  args[argc++] = "--geometry";
  args[argc++] = geom;

  /* FIXME: we could also save current editing state: contents of
   * clist, entry, maybe even selection (and thus state of Delete
   * button).  That's more ambitious.
   */

  gnome_client_set_restart_command (client, argc, args);
  g_free (geom);

  return TRUE;
}


static void
session_die (GnomeClient *client)
{
  gtk_main_quit ();
}


static const struct poptOption options[] = {
  {"geometry", '\0', POPT_ARG_STRING, &geometry, 0, N_("Geometry of window"), N_("GEOMETRY")},
  {NULL, '\0', 0, NULL, 0}
};


int
main (int argc, char *argv[])
{
  GnomeClient *client;

  /* Initialize the i18n stuff */
  bindtextdomain (PACKAGE, GNOMELOCALEDIR);
  textdomain (PACKAGE);

  gnome_init_with_popt_table (
      "session-properties",
      VERSION, 
      argc,
      argv, 
      options, 
      0, 
      NULL);

  /* add the startup page to the gnome property box and
   * initalize the glibal startup_info structure
   */
  startup_info.argv0 = argv[0];
  create_gui ();
  refresh_gui_data ();

  client = gnome_master_client ();
  if (client)
    {
      gtk_signal_connect (
          GTK_OBJECT (client),
	  "save_yourself",
	  GTK_SIGNAL_FUNC (session_save),
	  NULL);
      gtk_signal_connect (
          GTK_OBJECT (client),
	  "die",
	  GTK_SIGNAL_FUNC (session_die),
	  NULL);
    }

  gtk_main ();
  return 0;
}



/* CALLBACKS */
/* This is called when the dialog is dismissed.  */
static void
box_closed (GnomeDialog *dialog)
{
  gtk_main_quit ();
}


/* This is called when the Apply button is pressed.  */
static void
apply_properties (GnomePropertyBox *box)
{
  int i;

  /* FIXME: this info should be per-session, not global to session
   * manager.
   */
  gnome_config_clean_section (PRELOAD_PREFIX);
  gnome_config_push_prefix (PRELOAD_PREFIX);

  /* This is quadratic in the number of list elements, because a
   * linked list is used.  Hopefully the list is always short.
   */
  for (i = 0; i < GTK_CLIST(startup_info.clist)->rows; ++i)
    {
      gchar *text;
      char buf[20];

      gtk_clist_get_text (GTK_CLIST(startup_info.clist), i, 0, &text);
      sprintf (buf, "%d", i);
      gnome_config_set_string (buf, text);
    }

  gnome_config_set_int (PRELOAD_COUNT_KEY, i);
  gnome_config_pop_prefix ();
  gnome_config_sync ();
}


/* STARTUP PAGE CALLBACKS */
/* This is called when a row is selected.  */
static void
program_row_selected_cb (GtkWidget *widget, gint row)
{
  gtk_widget_set_sensitive (GTK_WIDGET (startup_info.remove_button), TRUE);
}


/* This is called when a row is unselected.  */
static void
program_row_unselected_cb (GtkWidget *widget, gint row)
{
  if (GTK_CLIST (startup_info.clist)->selection == NULL)
    {
      gtk_widget_set_sensitive (GTK_WIDGET (startup_info.remove_button), FALSE);
    }
}


/* This is called when the user types into the entry field.  */
static void
program_entry_changed_cb (GtkWidget *widget)
{
  gnome_property_box_changed (GNOME_PROPERTY_BOX (startup_info.propertybox));
}


/* This is called when user pressed Return in the entry.  */
static void
program_add_cb (GtkWidget *widget)
{
  gchar *text;

  text = gtk_entry_get_text (GTK_ENTRY (startup_info.entry));
  if (strcmp (text, ""))
    {
      gtk_clist_append (GTK_CLIST (startup_info.clist), (const gchar **) &text);
    }

  gtk_entry_set_text (GTK_ENTRY (startup_info.entry), "");
}


/* Remove selected items from clist.  */
static void
program_remove_cb (GtkWidget *widget)
{
  gtk_clist_freeze (GTK_CLIST (startup_info.clist));

  while (GTK_CLIST (startup_info.clist)->selection != NULL)
    {
      gint row = GPOINTER_TO_INT (GTK_CLIST (startup_info.clist)->selection->data);
      /* This modifies clist->selection, so eventually the loop will
       * terminate.
       */
      gtk_clist_remove (GTK_CLIST (startup_info.clist), row);
    }

  gtk_clist_thaw (GTK_CLIST (startup_info.clist));
  gnome_property_box_changed (startup_info.propertybox);
}
