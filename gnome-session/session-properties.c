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
  GdkFont   *font;
  gint       width;

  /* Name of this program.  */
  char *argv0;

  /* Config prefix holding the current session details */
  char *prefix; 

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
  GtkWidget *table;
  GtkWidget *scrolled_window;

  /* create the GNOME property box */
  startup_info.propertybox = GNOME_PROPERTY_BOX (gnome_property_box_new ());
  gtk_signal_connect (GTK_OBJECT (startup_info.propertybox), "close",
		      GTK_SIGNAL_FUNC (box_closed), NULL);
  gtk_signal_connect (GTK_OBJECT (startup_info.propertybox), "apply",
		      GTK_SIGNAL_FUNC (apply_properties), NULL);
  gtk_window_set_title (GTK_WINDOW (startup_info.propertybox), 
			_("Gnome Session Properties"));

  /* table */
  table = gtk_table_new (4, 2, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), GNOME_PAD);
  gtk_table_set_row_spacings (GTK_TABLE (table), GNOME_PAD);
  gtk_container_set_border_width (GTK_CONTAINER (table), GNOME_PAD);
  gnome_property_box_append_page (startup_info.propertybox,
				  GTK_WIDGET (table),
				  gtk_label_new (_("Startup Programs")));

  /* label for the exec entry */
  gtk_table_attach (GTK_TABLE (table), gtk_label_new (_("Program:")),
		    0, 1, 0, 1, 
		    GTK_FILL, GTK_FILL, 0, 0);

  /* exec string entry box */
  startup_info.entry = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), startup_info.entry, 1, 2, 0, 1, 
		    GTK_FILL|GTK_EXPAND, GTK_FILL, 0, 0);
  gtk_signal_connect (GTK_OBJECT (startup_info.entry), "activate", 
		      GTK_SIGNAL_FUNC (program_add_cb), NULL);
  gtk_signal_connect (GTK_OBJECT (startup_info.entry), "changed", 
		      GTK_SIGNAL_FUNC (program_entry_changed_cb), NULL);

  /* add/remove buttons */
  startup_info.add_button = gnome_stock_button (GNOME_STOCK_PIXMAP_ADD);
  gtk_table_attach (GTK_TABLE (table), startup_info.add_button, 0, 1, 1, 2, 
  		    GTK_FILL, 0, 0, 0); 
  gtk_widget_set_sensitive (GTK_WIDGET (startup_info.add_button), FALSE);
  gtk_signal_connect (GTK_OBJECT (startup_info.add_button), "clicked",
		      GTK_SIGNAL_FUNC (program_add_cb), NULL);

  startup_info.remove_button = gnome_stock_button (GNOME_STOCK_PIXMAP_REMOVE);
  gtk_table_attach (GTK_TABLE (table), startup_info.remove_button, 0, 1, 2, 3, 
  		    GTK_FILL, 0, 0, 0);
  gtk_widget_set_sensitive (GTK_WIDGET (startup_info.remove_button), FALSE);
  gtk_signal_connect(GTK_OBJECT (startup_info.remove_button), "clicked",
		     GTK_SIGNAL_FUNC (program_remove_cb), NULL);
  gtk_table_attach (GTK_TABLE (table), gtk_vbox_new (FALSE, 0), 0, 1, 3, 4,
  		    GTK_FILL, GTK_FILL|GTK_EXPAND, 0, 0);

  /* startup program list */
  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
				  GTK_POLICY_AUTOMATIC,
				  GTK_POLICY_AUTOMATIC);
  gtk_table_attach (GTK_TABLE (table), scrolled_window, 1, 2, 1, 4, 
		    GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 0, 0);

  startup_info.clist = gtk_clist_new (1);
  gtk_container_add (GTK_CONTAINER (scrolled_window), startup_info.clist);
  gtk_clist_set_selection_mode (GTK_CLIST (startup_info.clist), 
				GTK_SELECTION_MULTIPLE);
  gtk_signal_connect(GTK_OBJECT(startup_info.clist), "select_row",
		     GTK_SIGNAL_FUNC (program_row_selected_cb), NULL);
  gtk_signal_connect(GTK_OBJECT(startup_info.clist), "unselect_row",
		     GTK_SIGNAL_FUNC (program_row_unselected_cb), NULL);

}


static void
refresh_gui_data ()
{
  int i, count;
  gboolean unaware, corrupt;

  startup_info.font = gtk_widget_get_style (GTK_WIDGET (startup_info.clist))->font;
  startup_info.width = 40 * gdk_string_width (startup_info.font, "n");

  gnome_config_push_prefix (startup_info.prefix);
  count = gnome_config_get_int (CLIENT_COUNT_KEY "=0");
  gnome_config_pop_prefix ();

  gtk_clist_freeze (GTK_CLIST (startup_info.clist));
  gtk_clist_clear (GTK_CLIST (startup_info.clist));

  /* scan the session for session unaware clients.
   * These are with RestartCommands but no client ids. */
  for (i = 0; i < count; ++i)
    {
      gchar *value, buf[128];

      g_snprintf (buf, sizeof(buf), "%s%d,", startup_info.prefix, i);
      gnome_config_push_prefix (buf);

      g_free (gnome_config_get_string_with_default ("id", &unaware));

      if (unaware)
	{
	  value = gnome_config_get_string_with_default ("RestartCommand", 
							&corrupt);
	  /* let gnome-session deal with the nasty cases */
	  if (corrupt)
	      exit (0);

	  gtk_clist_append (GTK_CLIST (startup_info.clist), (gchar **) &value);
	  startup_info.width = MAX(gdk_string_width(startup_info.font, value), 
				   startup_info.width);
	  g_free (value);
	}
      gnome_config_pop_prefix ();
    }
  gtk_clist_set_column_width (GTK_CLIST (startup_info.clist), 
			      0, startup_info.width);
  gtk_clist_thaw (GTK_CLIST (startup_info.clist));
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

  geom = gnome_geometry_string (GTK_WIDGET (startup_info.propertybox)->window);
  args[argc++] = "--geometry";
  args[argc++] = geom;

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
  gchar* name;

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

  gnome_config_push_prefix (GSM_CONFIG_PREFIX);
  name = gnome_config_get_string (CURRENT_SESSION_KEY "=" DEFAULT_SESSION);
  gnome_config_pop_prefix ();

  startup_info.argv0 = argv[0];
  startup_info.prefix = g_strconcat (CONFIG_PREFIX, name, "/", NULL);
  g_free (name);

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
  gtk_widget_show_all (GTK_WIDGET (startup_info.propertybox));

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
  int old_i, new_i, last_i, i;
  void * iter;
  gchar *key, *value, name[1024];
  gboolean aware = FALSE;
  
  /* Only write to the session file when gnome-session is idle.
   * This is a fail safe against an unlikely but dangerous race.
   * gnome-session will wait for us to finish before starting a save. */
  if (gnome_master_client()->state != GNOME_CLIENT_DISCONNECTED &&
      gnome_master_client()->state != GNOME_CLIENT_IDLE)
    return;

  /* The algorithm to write this file is an awful lot easier when
   * we can iterate through its contents from start to finish.
   * Therefore, we copy the session over into another section in
   * reverse order before doing the real work */
  iter = gnome_config_init_iterator (startup_info.prefix);
  gnome_config_push_prefix (RESERVED_SECTION);
  
  /* Step over CLIENT_COUNT_KEY */
  iter = gnome_config_iterator_next (iter, &key, &value);

  while ((iter = gnome_config_iterator_next (iter, &key, &value)))
      gnome_config_set_string (key, value);

  gnome_config_pop_prefix ();
  gnome_config_clean_section (startup_info.prefix);

  new_i = last_i = -1;

  iter = gnome_config_init_iterator (RESERVED_SECTION);
  gnome_config_push_prefix (startup_info.prefix);

  /* Remove all non-aware clients from the saved default session */
  while ((iter = gnome_config_iterator_next (iter, &key, &value)))
    {
      sscanf (key, "%d,%1023s", &old_i, name);
      
      if (old_i != last_i)
	{
	  last_i = old_i;
	  
	  if ((aware = !strcmp (name, "id")))
	    ++new_i;
	}
      
      if (aware)
	{
	  if (old_i != new_i)
	    {
	      g_free (key);
	      key = g_strdup_printf ("%d,%s", new_i, name);
	    }
	  gnome_config_set_string (key, value);
	}
      g_free (key);
      g_free (value);
    }

  /* Replace them with the ones in our list. */
  for (i = 0; i < GTK_CLIST(startup_info.clist)->rows; i++)
    {
      char buf[40];

      gtk_clist_get_text (GTK_CLIST(startup_info.clist), i, 0, &value);
      g_snprintf (buf, sizeof(buf), "%d,RestartCommand", ++new_i);
      gnome_config_set_string (buf, value);
    }

  gnome_config_set_int (CLIENT_COUNT_KEY, ++new_i);
  gnome_config_pop_prefix ();
  gnome_config_clean_section (RESERVED_SECTION);
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

/* This is called when user pressed Return in the entry.  */
static void
program_add_cb (GtkWidget *widget)
{
  gchar *value;

  value = gtk_entry_get_text (GTK_ENTRY (startup_info.entry));
  if (strcmp (value, ""))
    {
      gtk_clist_freeze (GTK_CLIST (startup_info.clist));
      gtk_clist_append (GTK_CLIST (startup_info.clist), (gchar **) &value);
      startup_info.width = MAX(gdk_string_width(startup_info.font, value), 
			       startup_info.width);
      gtk_clist_set_column_width (GTK_CLIST (startup_info.clist), 
				  0, startup_info.width);
      gtk_clist_thaw (GTK_CLIST (startup_info.clist));
      gtk_entry_set_text (GTK_ENTRY (startup_info.entry), "");
      gnome_property_box_changed (startup_info.propertybox);
      gtk_widget_set_sensitive (GTK_WIDGET (startup_info.add_button), FALSE);
    }
}

/* This is called when user has changed the entry  */
static void
program_entry_changed_cb (GtkWidget *widget)
{
  gtk_widget_set_sensitive (GTK_WIDGET (startup_info.add_button), TRUE);
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
