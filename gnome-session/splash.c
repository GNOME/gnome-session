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
#include "manager.h"

typedef struct {
  GtkWidget *dialog;
  GtkWidget *progressbar;
  GtkWidget *label;
  gfloat max;
  gint timeout;
} SplashData;
static SplashData *sd = NULL;

#define SPLASHING (gnome_config_get_bool (GSM_OPTION_CONFIG_PREFIX SPLASH_SCREEN_KEY"="SPLASH_SCREEN_DEFAULT))
#define HINTING (gnome_config_get_bool ("Gnome/Login/RunHints=true"))

static gboolean
destroy_dialog (GtkWidget *w, GdkEventButton *event, gpointer data)
{
  gtk_widget_destroy (w);
  return TRUE;
}

static void
hint ()
{
  char *cmd[] = { "gnome-hint", NULL };
  if (HINTING)
    gnome_execute_async (NULL, 1, cmd);
}

static gboolean
splash_cleanup (GtkObject *o, gpointer data)
{
  g_free (sd);
  sd = NULL;
  hint ();
  return FALSE;
}

static gint
timeout_cb (gpointer data)
{
  if (sd && sd->dialog)
    gtk_widget_destroy (sd->dialog);

  return FALSE;
}

void
stop_splash ()
{
  if (!SPLASHING) {
    hint ();
    return;
  }

  if (!sd || sd->timeout)
    return;

  update_splash (_("done"), sd->max);
  sd->timeout = gtk_timeout_add (2000, timeout_cb, NULL);
}

static void
window_realize (GtkWidget *win)
{
  /* this is done because on startup the splash screen should 
     be on top of the hints which are at WIN_LAYER_ONTOP,
     in fact, the splash screen is only temporary and should
     be above everything */
  gnome_win_hints_set_layer (win, WIN_LAYER_ABOVE_DOCK);
  gdk_window_set_decorations (win->window, 0);
  gnome_win_hints_set_state (win, 
			     WIN_STATE_STICKY |
			     WIN_STATE_FIXED_POSITION);
  gnome_win_hints_set_hints (win, 
			     WIN_HINTS_SKIP_FOCUS |
			     WIN_HINTS_SKIP_WINLIST |
			     WIN_HINTS_SKIP_TASKBAR);
			     
}

void
start_splash (gfloat max)
{
  GtkWidget *box;
  GtkWidget *w;
  GtkWidget *frame;
  char *file;

  g_return_if_fail (sd == NULL);

  sd = g_malloc (sizeof (SplashData));

  sd->max = max;
  sd->timeout=0;

  sd->dialog = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_position (GTK_WINDOW (sd->dialog),
			   GTK_WIN_POS_CENTER);
  gtk_window_set_policy (GTK_WINDOW (sd->dialog),
			 FALSE, FALSE, FALSE);

  gtk_widget_add_events (sd->dialog, GDK_BUTTON_RELEASE_MASK);

  gtk_signal_connect_after (GTK_OBJECT (sd->dialog),
			    "realize",
			    GTK_SIGNAL_FUNC (window_realize),
			    NULL);

  gtk_signal_connect (GTK_OBJECT (sd->dialog),
		      "button-release-event",
		      GTK_SIGNAL_FUNC (destroy_dialog),
		      sd->dialog);

  gtk_signal_connect (GTK_OBJECT (sd->dialog),
		      "destroy",
		      GTK_SIGNAL_FUNC (splash_cleanup),
		      NULL);

  frame = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_OUT);
  gtk_container_add (GTK_CONTAINER (sd->dialog), frame);

  box = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (frame), box);
  
  file = gnome_pixmap_file ("splash/gnome-splash.png");
  w = gnome_pixmap_new_from_file (file);
  gtk_box_pack_start (GTK_BOX (box), w, FALSE, FALSE, 0);
  
  w = gtk_label_new (_("Starting GNOME"));
  sd->label = w;
  gtk_box_pack_start (GTK_BOX (box), w, FALSE, FALSE, 0);
  
  w = gtk_progress_bar_new();
  sd->progressbar = w;
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

  msg = g_strdup_printf (_("Starting GNOME... %s"), text);
  
  gtk_label_set_text (GTK_LABEL (sd->label), msg);
  g_free (msg);
  
  gtk_progress_set_percentage (GTK_PROGRESS (sd->progressbar),
			       (float)priority / sd->max);
}
