/* splash.c - show a splash screen on startup

   Copyright (C) 1999 Jacob Berkman
   Copyright 2000 Helix Code, Inc.

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
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk-pixbuf/gnome-canvas-pixbuf.h>

typedef struct {
  GtkWidget *dialog;
  GnomeCanvasItem *label;
  GtkWidget *hbox;
  GHashTable *hash;
  int count;
  gfloat max;
  gint timeout;
  gint icon_timeout;
  GSList *icons;
  double scalefactor;
  int height;
} SplashData;
static SplashData *sd = NULL;

#define SPLASHING (gnome_config_get_bool (GSM_OPTION_CONFIG_PREFIX SPLASH_SCREEN_KEY"="SPLASH_SCREEN_DEFAULT))
#define HINTING (gnome_config_get_bool ("Gnome/Login/RunHints=true"))

#define BASE_WIDTH 480
#define BASE_ICONSIZE 36

#define ICONSIZE(sd) ((int)((sd)->scalefactor*BASE_ICONSIZE))

static gboolean
hide_dialog (GtkWidget *w, GdkEventButton *event, gpointer data)
{
  gtk_widget_hide (w);
  return TRUE;
}

static void
hint (void)
{
  char *cmd[] = { "gnome-hint", "--session-login", NULL };
  if (HINTING)
    gnome_execute_async (NULL, 2, cmd);
}

static gboolean
splash_cleanup (GtkObject *o, gpointer data)
{
  g_hash_table_destroy (sd->hash);
  sd->hash = NULL;

  if (sd->timeout != 0)
	  gtk_timeout_remove (sd->icon_timeout);
  sd->icon_timeout = 0;

  g_slist_foreach (sd->icons, (GFunc)g_free, NULL);
  g_slist_free (sd->icons);
  sd->icons = NULL;

  g_free (sd);
  sd = NULL;

  hint ();

  return FALSE;
}

static gboolean
timeout_cb (gpointer data)
{
  if (sd && sd->dialog)
    gtk_widget_destroy (sd->dialog);

  return FALSE;
}

static gboolean
icon_cb (gpointer data)
{
  char *text, *msg;

  if (!sd)
    {
      sd->icon_timeout = 0;
      return FALSE;
    }

  if (!sd->icons)
    return TRUE;

  text = sd->icons->data;
  sd->icons = g_slist_remove (sd->icons, text);

  if (strcmp (text, "done")) {
    char **item = g_hash_table_lookup (sd->hash, g_basename (text));
    char *pix = NULL;

    if (item) {
      pix = g_strconcat (GNOME_ICONDIR"/", item[2], NULL);
      g_free (text);
      text = g_strdup (_(item[0]));
    }

    if (!pix || !g_file_exists (pix)) {
      g_free (pix);
      pix = g_strdup (GNOME_ICONDIR"/gnome-unknown.png");
    }
    
    if (g_file_exists (pix)) {
      GdkPixbuf *pb;
      GnomeCanvasItem *item;

      pb = gdk_pixbuf_new_from_file (pix);
      if (pb != NULL) {
        GdkPixbuf *pb2;
        pb2 = gdk_pixbuf_scale_simple (pb,
				       ICONSIZE (sd),
				       ICONSIZE (sd),
				       GDK_INTERP_BILINEAR);
        item = gnome_canvas_item_new (GNOME_CANVAS_GROUP (GNOME_CANVAS (sd->hbox)->root),
				      gnome_canvas_pixbuf_get_type (),
				      "pixbuf", pb2,
				      "x", (gdouble)(3.0 + 42.0 * sd->count * sd->scalefactor),
				      "y", (gdouble)(sd->height - 15.0 - 42.0 * sd->scalefactor),
				      NULL);
        gdk_pixbuf_unref (pb2);
	sd->count++;
      }
      gdk_pixbuf_unref (pb);
    }

    g_free (pix);
  }

  msg = g_strdup_printf (_("Starting GNOME: %s"), text);
  gnome_canvas_item_set (sd->label, "text", msg, NULL);

  g_free (msg);
  g_free (text);

  return TRUE;
}

void
stop_splash (void)
{
  if (!SPLASHING) {
    hint ();
    return;
  }

  if (!sd || sd->timeout)
    return;

  update_splash ("done", sd->max);
  sd->timeout = gtk_timeout_add (2000, timeout_cb, NULL);
}

void
start_splash (gfloat max)
{
  GtkWidget *frame;
  int i;
  static char *map[][3] = {
    { N_("Audio Settings"),               "sound-properties",               "gnome-mixer.png" },
    { N_("Screensaver"),                  "screensaver-properties-capplet", "gnome-ccscreensaver.png" },
    { N_("Screensaver"),                  "xscreensaver-demo",              "gnome-ccscreensaver.png" },
    { N_("Sawfish Window Manager"),       "sawmill",                        "sawfish.png" },
    { N_("Sawfish Window Manager"),       "sawfish",                        "sawfish.png" },
    { N_("Enlightenment Window Manager"), "enlightenment",                  "gnome-ccwindowmanager.png" },
    { N_("Background Settings"),          "background-properties-capplet",  "gnome-ccbackground.png" },
  /*{ N_("Keyboard Bell"),                "bell-properties-capplet",        "gnome-cckeyboard-bell.png" },*/
    { N_("Mouse Settings"),               "mouse-properties-capplet",       "gnome-mouse.png" },
    { N_("Keyboard Settings"),            "keyboard-properties",            "gnome-cckeyboard.png" },
    { N_("The Panel"),                    "panel",                          "gnome-panel.png" },
    { N_("Session Manager Proxy"),        "gnome-smproxy",                  "gnome-session.png" },
    { N_("Window Manager"),               "gnome-wm",                       "gnome-ccwindowmanager.png" },
    { N_("Desktop"),                      "gmc",                            "gnome-ccdesktop.png" },
    { NULL }
  };

  g_return_if_fail (sd == NULL);

  sd = g_malloc0 (sizeof (SplashData));

  sd->scalefactor = 1.0;

  sd->hash = g_hash_table_new (g_str_hash, g_str_equal);
  for (i=0; map[i][0]; i++)
    g_hash_table_insert (sd->hash, map[i][1], map[i]);

  sd->max = max;

  sd->dialog = gtk_window_new (GTK_WINDOW_POPUP);

  gtk_window_set_position (GTK_WINDOW (sd->dialog),
			   GTK_WIN_POS_CENTER);
  gtk_window_set_policy (GTK_WINDOW (sd->dialog),
			 FALSE, FALSE, FALSE);

  gtk_widget_add_events (sd->dialog, GDK_BUTTON_RELEASE_MASK);

  gtk_signal_connect (GTK_OBJECT (sd->dialog),
		      "button-release-event",
		      GTK_SIGNAL_FUNC (hide_dialog),
		      NULL);

  gtk_signal_connect (GTK_OBJECT (sd->dialog),
		      "destroy",
		      GTK_SIGNAL_FUNC (splash_cleanup),
		      NULL);

  frame = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_OUT);
  gtk_container_add (GTK_CONTAINER (sd->dialog), frame);

  gtk_widget_push_visual (gdk_rgb_get_visual ());
  gtk_widget_push_colormap (gdk_rgb_get_cmap ());

  sd->hbox = gnome_canvas_new_aa ();

  gtk_widget_pop_colormap ();
  gtk_widget_pop_visual ();

  gtk_container_add (GTK_CONTAINER (frame), sd->hbox);

  {
    GdkPixbuf *pb;
    int height, width;

    pb = gdk_pixbuf_new_from_file (GNOME_ICONDIR "/splash/gnome-splash.png");

    if (pb == NULL) {
	    height = 220;
	    width = 480;
    } else {
	    height = gdk_pixbuf_get_height (pb);
	    width  = gdk_pixbuf_get_width  (pb);
    }

    /* used for scaling icons and other stuff */
    sd->scalefactor = (double)width / (double)BASE_WIDTH;
    sd->height = height;

    gtk_widget_set_usize (sd->hbox, width, height);
    gnome_canvas_set_scroll_region (GNOME_CANVAS (sd->hbox), 0, 0, width, height);
    gtk_window_set_default_size (GTK_WINDOW (sd->dialog), width, height);
				 
    if (pb != NULL) {
	    gnome_canvas_item_new (GNOME_CANVAS_GROUP (GNOME_CANVAS (sd->hbox)->root),
				   GNOME_TYPE_CANVAS_PIXBUF, "pixbuf", pb, NULL);

	    gdk_pixbuf_unref (pb);
    }

    sd->label = gnome_canvas_item_new (GNOME_CANVAS_GROUP (GNOME_CANVAS (sd->hbox)->root),
				       GNOME_TYPE_CANVAS_TEXT,
				       "text", _("Starting GNOME"),
				       "x", (gdouble)(width / 2),
				       "y", (gdouble)(height - 7.5),
				       "font", "-adobe-helvetica-medium-r-normal-*-8-*-*-*-p-*-*-*",
				       "anchor", GTK_ANCHOR_CENTER,
				       "fill_color", "white",
				       NULL);
  }

  gtk_widget_show_all (sd->dialog);

  sd->icon_timeout = gtk_timeout_add (100, icon_cb, sd);
}

void
update_splash (const gchar *text, gfloat priority)
{
  if (!sd || !text || !text[0])
    return;
  
  if (priority > sd->max) {
    stop_splash ();
    return;
  }

  if (!strcmp (text, "rm"))
    return;

  sd->icons = g_slist_append (sd->icons, g_strdup (text));
}
