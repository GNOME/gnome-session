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



/* Geometry of main window.  */
static char *geometry;

/* This is used as the client data for some callbacks.  It just holds
   pointers to whatever we might need.  */
struct info
{
  /* The property box.  */
  GnomePropertyBox *propertybox;
  /* The clist.  */
  GtkWidget *clist;
  /* Name of this program.  */
  char *argv0;
};



/* This is called when the dialog is dismissed.  */
static void
box_closed (GnomeDialog *dialog, gpointer client_data)
{
  gtk_main_quit ();
}

/* This is called when the Apply button is pressed.  */
static void
apply_properties (GnomePropertyBox *box, gint page, gpointer client_data)
{
  int i;
  GtkCList *clist = GTK_CLIST (client_data);

  if (page != 0)
    return;

  /* FIXME: this info should be per-session, not global to session
     manager.  */
  gnome_config_clean_section (PRELOAD_PREFIX);
  gnome_config_push_prefix (PRELOAD_PREFIX);

  /* This is quadratic in the number of list elements, because a
     linked list is used.  Hopefully the list is always short.  */
  for (i = 0; i < clist->rows; ++i)
    {
      gchar *text;
      char buf[20];

      gtk_clist_get_text (clist, i, 0, &text);
      sprintf (buf, "%d", i);
      gnome_config_set_string (buf, text);
    }

  gnome_config_set_int (PRELOAD_COUNT_KEY, i);
  gnome_config_pop_prefix ();
  gnome_config_sync ();
}

/* This is called when a row is selected.  */
static void
row_selected (GtkCList *list, gint row, gint column, GdkEvent *event,
	      gpointer client_data)
{
  gtk_widget_set_sensitive (GTK_WIDGET (client_data), 1);
}

/* This is called when a row is unselected.  */
static void
row_unselected (GtkCList *list, gint row, gint column, GdkEvent *event,
		gpointer client_data)
{
  if (list->selection == NULL)
    gtk_widget_set_sensitive (GTK_WIDGET (client_data), 0);
}

/* This is called when the user types into the entry field.  */
static void
entry_changed (GtkWidget *w, gpointer client_data)
{
  gnome_property_box_changed (GNOME_PROPERTY_BOX (client_data));
}

/* This is called when user pressed Return in the entry.  */
static void
entry_ok (GtkWidget *w, gpointer client_data)
{
  GtkEntry *entry = GTK_ENTRY (w);
  GtkCList *clist = GTK_CLIST (client_data);
  gchar *text;

  text = gtk_entry_get_text (entry);
  if (strcmp (text, ""))
    gtk_clist_append (clist, &text);

  gtk_entry_set_text (entry, "");
}

/* Remove selected items from clist.  */
static void
remove_items (GtkWidget *w, gpointer client_data)
{
  struct info *info = (struct info *) client_data;
  GtkCList *clist = GTK_CLIST (info->clist);

  gtk_clist_freeze (clist);

  while (clist->selection != NULL)
    {
      int row = (int) clist->selection->data;
      /* This modifies clist->selection, so eventually the loop will
	 terminate.  */
      gtk_clist_remove (clist, row);
    }

  gtk_clist_thaw (clist);
  gnome_property_box_changed (info->propertybox);
}

/* Fill in the clist with information from gnome_config.  */
static void
fill_clist (GtkCList *list)
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

  for (i = 0; i < count; ++i)
    {
      char *text, buf[20];

      sprintf (buf, "%d", i);
      text = gnome_config_get_string_with_default (buf, &def);
      if (! def)
	gtk_clist_append (list, &text);
      g_free (text);
    }
}

static struct info *
setup ()
{
  GnomePropertyBox *propertybox;
  GtkWidget *w, *del_button, *bbox, *clist, *sw, *box;
  GtkBox *page;
  struct info *info;

  propertybox = GNOME_PROPERTY_BOX (gnome_property_box_new ());

  page = GTK_BOX (gtk_vbox_new (FALSE, GNOME_PAD));

  gtk_notebook_append_page (GTK_NOTEBOOK (propertybox->notebook),
			    GTK_WIDGET (page), gtk_label_new (_("Startup")));

  box = gtk_hbox_new (FALSE, GNOME_PAD);
  w = gtk_label_new (_("Programs to invoke at session startup:"));
  gtk_box_pack_start (GTK_BOX (box), w, FALSE, FALSE, GNOME_PAD_SMALL);
  gtk_box_pack_start (page, box, FALSE, FALSE, GNOME_PAD_SMALL);

  del_button = gtk_button_new_with_label (_("Delete"));
  sw = gtk_scrolled_window_new (NULL, NULL);
  clist = gtk_clist_new (1);
  gtk_container_add (GTK_CONTAINER (sw), cl);
  
  /* We allocate this but never free it.  It doesn't matter.  */
  info = (struct info *) malloc (sizeof (struct info));
  info->propertybox = propertybox;
  info->clist = clist;
  info->argv0 = NULL;

  gtk_widget_set_sensitive (del_button, 0);
  gtk_signal_connect (GTK_OBJECT (del_button), "clicked",
		      GTK_SIGNAL_FUNC (remove_items), (gpointer) info);

  /* FIXME: put clist into a multi-select mode.  */
  gtk_scrolled_window_set_policy (
	  GTK_SCROLLED_WINDOW (sw),
	  GTK_POLICY_AUTOMATIC,
	  GTK_POLICY_AUTOMATIC);
  
  gtk_box_pack_start (page, sw, FALSE, FALSE, GNOME_PAD_SMALL);
  gtk_signal_connect (GTK_OBJECT (clist), "select_row",
		      GTK_SIGNAL_FUNC (row_selected), (gpointer) del_button);
  gtk_signal_connect (GTK_OBJECT (clist), "unselect_row",
		      GTK_SIGNAL_FUNC (row_unselected), (gpointer) del_button);

  bbox = gtk_hbutton_box_new ();
  gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_END);
  /* FIXME: put some space around the Delete button.  */
  gtk_box_pack_start (GTK_BOX (bbox), del_button, FALSE, FALSE,
		      GNOME_PAD_SMALL);
  gtk_box_pack_start (page, bbox, FALSE, FALSE, GNOME_PAD_SMALL);

  w = gtk_entry_new ();
  gtk_box_pack_start (page, w, FALSE, FALSE, GNOME_PAD_SMALL);
  gtk_signal_connect (GTK_OBJECT (w), "changed",
		      GTK_SIGNAL_FUNC (entry_changed), (gpointer) propertybox);
  gtk_signal_connect (GTK_OBJECT (w), "activate",
		      GTK_SIGNAL_FUNC (entry_ok), (gpointer) clist);

  fill_clist (GTK_CLIST (clist));

  gtk_signal_connect (GTK_OBJECT (propertybox), "close",
		      GTK_SIGNAL_FUNC (box_closed), NULL);
  gtk_signal_connect (GTK_OBJECT (propertybox), "apply",
		      GTK_SIGNAL_FUNC (apply_properties), (gpointer) clist);
  gtk_window_set_title (GTK_WINDOW (propertybox),
			_("Gnome Session Properties"));
  gtk_widget_show_all (GTK_WIDGET (propertybox));

  return info;
}



/*
 * Session management.
 */

static int
session_save (GnomeClient *client, int phase, GnomeSaveStyle save_style,
	      int is_shutdown, GnomeInteractStyle interact_style,
	      int is_fast, gpointer client_data)
{
  struct info *info = (struct info *) client_data;
  char *args[3], *geom;
  int argc;

  argc = 0;

  args[argc++] = info->argv0;

  /* Geometry of main window.  */
  geom = gnome_geometry_string (GTK_WIDGET (info->propertybox)->window);
  args[argc++] = "--geometry";
  args[argc++] = geom;

  /* FIXME: we could also save current editing state: contents of
     clist, entry, maybe even selection (and thus state of Delete
     button).  That's more ambitious.  */

  gnome_client_set_restart_command (client, argc, args);
  g_free (geom);

  return TRUE;
}

static void
session_die (GnomeClient *client, gpointer client_data)
{
  gtk_main_quit ();
}

static const struct poptOption options[] = {
  {"geometry", '\0', POPT_ARG_STRING, &geometry, 0, N_("Geometry of window"),
   N_("GEOMETRY")},
  {NULL, '\0', 0, NULL, 0}
};

int
main (int argc, char *argv[])
{
  struct info *info;
  GnomeClient *client;

  /* Initialize the i18n stuff */
  bindtextdomain (PACKAGE, GNOMELOCALEDIR);
  textdomain (PACKAGE);

  gnome_init_with_popt_table ("session-properties", VERSION, argc,
			      argv, options, 0, NULL);

  info = setup ();
  info->argv0 = argv[0];

  client = gnome_master_client ();
  if (client)
    {
      gtk_signal_connect (GTK_OBJECT (client), "save_yourself",
			  GTK_SIGNAL_FUNC (session_save), (gpointer) info);
      gtk_signal_connect (GTK_OBJECT (client), "die",
			  GTK_SIGNAL_FUNC (session_die), NULL);
    }

  gtk_main ();

  return 0;
}
