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

#include <libgnome/libgnome.h>
#include <libgnomeui/libgnomeui.h>

#include "session.h"

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

static void
setup ()
{
  GnomePropertyBox *propertybox;
  GtkWidget *w, *del_button, *bbox, *clist, *box;
  GtkBox *page;

  propertybox = GNOME_PROPERTY_BOX (gnome_property_box_new ());

  /* FIXME: don't use a constant.  */
  page = GTK_BOX (gtk_vbox_new (FALSE, 4));

  gtk_notebook_append_page (GTK_NOTEBOOK (propertybox->notebook),
			    GTK_WIDGET (page), gtk_label_new (_("Startup")));

  /* FIXME: don't use a constant.  */
  box = gtk_hbox_new (FALSE, 4);
  w = gtk_label_new (_("Programs to invoke at session startup:"));
  gtk_box_pack_start (GTK_BOX (box), w, FALSE, FALSE, 0);
  gtk_box_pack_start (page, box, FALSE, FALSE, 0);

  del_button = gtk_button_new_with_label (_("Delete"));

  clist = gtk_clist_new (1);
  gtk_clist_set_policy (GTK_CLIST (clist), GTK_POLICY_AUTOMATIC,
			GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start (page, clist, FALSE, FALSE, 0);
  gtk_signal_connect (GTK_OBJECT (clist), "select_row",
		      GTK_SIGNAL_FUNC (row_selected), (gpointer) del_button);
  gtk_signal_connect (GTK_OBJECT (clist), "unselect_row",
		      GTK_SIGNAL_FUNC (row_unselected), (gpointer) del_button);

  bbox = gtk_hbutton_box_new ();
  gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_END);
  /* FIXME: put some space around the Delete button.  */
  gtk_box_pack_start (GTK_BOX (bbox), del_button, FALSE, FALSE, 0);
  gtk_box_pack_start (page, bbox, FALSE, FALSE, 0);

  w = gtk_entry_new ();
  gtk_box_pack_start (page, w, FALSE, FALSE, 0);
  gtk_signal_connect (GTK_OBJECT (w), "changed",
		      GTK_SIGNAL_FUNC (entry_changed), (gpointer) propertybox);
  /* FIXME: if user types Enter in entry, then should accept it and
     add to list.  */

  fill_clist (GTK_CLIST (clist));

  gtk_signal_connect (GTK_OBJECT (propertybox), "close",
		      GTK_SIGNAL_FUNC (box_closed), NULL);
  gtk_signal_connect (GTK_OBJECT (propertybox), "apply",
		      GTK_SIGNAL_FUNC (apply_properties), (gpointer) clist);
  gtk_window_set_title (GTK_WINDOW (propertybox),
			_("Gnome Session Properties"));
  gtk_widget_show_all (GTK_WIDGET (propertybox));
}

int
main (int argc, char *argv[])
{
  argp_program_version = VERSION;

  /* Initialize the i18n stuff */
  bindtextdomain (PACKAGE, GNOMELOCALEDIR);
  textdomain (PACKAGE);

  gnome_init ("session-properties", NULL, argc, argv, 0, NULL);

  setup ();

  gtk_main ();

  return 0;
}
