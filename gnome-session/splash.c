/* splash.c - show a splash screen on startup

   Copyright (C) 1999 Jacob Berkman

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
#include <gnome.h>
#include "splash.h"

typedef struct {
  GtkWidget *dialog;
  GtkWidget *progessbar;
  GtkWidget *label;
  gfloat max;
} SplashData;
static SplashData *sd = NULL;

static gboolean
destroy_dialog (GtkWidget *w, GdkEventButton *event, gpointer data)
{
  g_message ("destroy_dialog ()");
  gtk_widget_destroy (w);
  return TRUE;
}

static gboolean
splash_cleanup (GtkObject *o, gpointer data)
{
  g_message ("cleanup!");
  g_free (sd);
  sd = NULL;
  return FALSE;
}

void
stop_splash ()
{
  g_message ("stop_splash()");
  if (!sd)
    return;
  gtk_widget_destroy (sd->dialog);
}

void
start_splash (gfloat max)
{
  GtkWidget *box;
  GtkWidget *w;
  char *file;

  g_return_if_fail (sd == NULL);

  sd = g_malloc (sizeof (SplashData));

  sd->max = max;
  
  sd->dialog = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_position (GTK_WINDOW (sd->dialog),
			   GTK_WIN_POS_CENTER);
  gtk_window_set_policy (GTK_WINDOW (sd->dialog),
			 FALSE, FALSE, FALSE);

  gtk_widget_add_events (sd->dialog, GDK_BUTTON_RELEASE_MASK);

  gtk_signal_connect (GTK_OBJECT (sd->dialog),
		      "button-release-event",
		      GTK_SIGNAL_FUNC (destroy_dialog),
		      sd->dialog);

  gtk_signal_connect (GTK_OBJECT (sd->dialog),
		      "destroy",
		      GTK_SIGNAL_FUNC (splash_cleanup),
		      NULL);
  
  box = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (sd->dialog), box);
  
  file = gnome_pixmap_file ("gnome-splash.png");
  w = gnome_pixmap_new_from_file (file);
  gtk_box_pack_start (GTK_BOX (box), w, FALSE, FALSE, 0);
  
  w = gtk_label_new (_("Starting GNOME"));
  sd->label = w;
  gtk_box_pack_start (GTK_BOX (box), w, FALSE, FALSE, 0);
  
  w = gtk_progress_bar_new();
  sd->progessbar = w;
  gtk_box_pack_start (GTK_BOX (box), w, FALSE, FALSE, 0);
  
  gtk_widget_show_all (sd->dialog);
}

void
update_splash (const gchar *text, gfloat priority)
{
  char *msg;
  
  if (!sd)
    return;
  
  if (priority > sd->max) {
    stop_splash ();
    return;
  }

  msg = g_strdup_printf (_("Starting GNOME: %s"), text);
  
  gtk_label_set_text (GTK_LABEL (sd->label), msg);
  g_free (msg);
  
  gtk_progress_set_percentage (GTK_PROGRESS (sd->progessbar),
			       (float)priority / sd->max);
}
