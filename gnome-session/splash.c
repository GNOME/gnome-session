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
  GnomeCanvasItem *label_shadow;
  GSList *icon_items;
  GSList *icon_pixbufs;
  GtkWidget *hbox;
  int count;
  gfloat max;
  gint timeout;
  gint icon_timeout;
  GSList *icons;
  double scalefactor;
  int width;
  int height;
  GHashTable *hash;
} SplashData;
static SplashData *sd = NULL;

/* entry in a splash app table */
typedef struct {
	char *human_name;
	char *exe;
	char *icon;
} SplashApp;

static const SplashApp splash_map_table[] = {
	{ N_("Audio Settings"),               "sound-properties",               "gnome-mixer.png" },
	{ N_("Screensaver"),                  "screensaver-properties-capplet", "gnome-ccscreensaver.png" },
	{ N_("Screensaver"),                  "xscreensaver-demo",              "gnome-ccscreensaver.png" },
	{ N_("Sawfish Window Manager"),       "sawmill",                        "gnome-ccwindowmanager.png" },
	{ N_("Sawfish Window Manager"),       "sawfish",                        "gnome-ccwindowmanager.png" },
	{ N_("Enlightenment Window Manager"), "enlightenment",                  "gnome-ccwindowmanager.png" },
	{ N_("Background Settings"),          "background-properties-capplet",  "gnome-ccbackground.png" },
	{ N_("Keyboard Bell"),                "bell-properties-capplet",        "gnome-cckeyboard-bell.png" },
	{ N_("Mouse Settings"),               "mouse-properties-capplet",       "gnome-mouse.png" },
	{ N_("Keyboard Settings"),            "keyboard-properties",            "gnome-cckeyboard.png" },
	{ N_("The Panel"),                    "panel",                          "gnome-panel.png" },
	{ N_("Session Manager Proxy"),        "gnome-smproxy",                  "gnome-session.png" },
	{ N_("Window Manager"),               "gnome-wm",                       "gnome-ccwindowmanager.png" },
	{ N_("Desktop"),                      "gmc",                            "gnome-ccdesktop.png" },
	{ N_("Nautilus"),                     "nautilus",                       "gnome-ccdesktop.png" },
	{ NULL }
};

#define SPLASHING (gnome_config_get_bool (GSM_OPTION_CONFIG_PREFIX SPLASH_SCREEN_KEY"="SPLASH_SCREEN_DEFAULT))
#define HINTING (gnome_config_get_bool ("Gnome/Login/RunHints=true"))

#define BASE_WIDTH 480
#define BASE_ICONSIZE 36

#define ICONSIZE(sd) ((int)((sd)->scalefactor*BASE_ICONSIZE))

static SplashApp *
get_splash_app (const char *exe)
{
	SplashApp *app;

	if (sd == NULL)
		return NULL;

	if (sd->hash == NULL) {
		int i;
		sd->hash = g_hash_table_new (g_str_hash, g_str_equal);
		for (i = 0; splash_map_table[i].human_name != NULL; i++)
			g_hash_table_insert (sd->hash,
					     (gpointer)splash_map_table[i].exe,
					     (gpointer)&splash_map_table[i]);
	}
	app = g_hash_table_lookup (sd->hash, exe);

	return app;
}

static void
cleanup_app_table (void)
{
	if (sd != NULL &&
	    sd->hash != NULL) {
		g_hash_table_destroy (sd->hash);
		sd->hash = NULL;
	}
}

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
  sd->dialog = NULL;

  cleanup_app_table ();

  if (sd->timeout != 0)
	  gtk_timeout_remove (sd->timeout);
  sd->timeout = 0;
  if (sd->icon_timeout != 0)
	  gtk_timeout_remove (sd->icon_timeout);
  sd->icon_timeout = 0;

  g_slist_foreach (sd->icon_pixbufs, (GFunc)gdk_pixbuf_unref, NULL);
  g_slist_free (sd->icon_pixbufs);
  sd->icon_pixbufs = NULL;

  g_slist_free (sd->icon_items);
  sd->icon_items = NULL;

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
	if (sd != NULL) {
		sd->timeout = 0;

		if (sd->dialog != NULL)
			gtk_widget_destroy (sd->dialog);
		/* Note: sd is probably dead here */
	}

	return FALSE;
}

/* check if another icon will fit, and if no, make room */
static void
check_icon_fit (void)
{
	int i;
	GSList *ili, *pli;

	if (sd == NULL ||
	    (3 + 42 * (sd->count + 1) * sd->scalefactor) <= sd->width - 3)
		return;

	/* make everything fit into 80% of the splash */
	sd->scalefactor =
		(((double)sd->width - 6.0) / (42.0 * (sd->count + 1))) * 0.80;
	/* for sanity, things could crash if this goes too low, this will
	 * make 3 pixel icons, prolly if the splash screen is too tiny */
	if (sd->scalefactor < 0.1)
		sd->scalefactor = 0.1;
	for (i = 0, ili = sd->icon_items, pli = sd->icon_pixbufs;
	     ili != NULL && pli != NULL;
	     i++, ili = ili->next, pli = pli->next) {
		GdkPixbuf *pb = pli->data;
		GdkPixbuf *pb2;
		GnomeCanvasItem *item = ili->data;
		ili->data = NULL;

		gtk_object_destroy (GTK_OBJECT (item));

		pb2 = gdk_pixbuf_scale_simple (pb,
					       ICONSIZE (sd),
					       ICONSIZE (sd),
					       GDK_INTERP_BILINEAR);

		item = gnome_canvas_item_new (GNOME_CANVAS_GROUP (GNOME_CANVAS (sd->hbox)->root),
					      gnome_canvas_pixbuf_get_type (),
					      "pixbuf", pb2,
					      "x", (gdouble)(3.0 + 42.0 * i * sd->scalefactor),
					      "y", (gdouble)(sd->height - 15.0 - 42.0 * sd->scalefactor),
					      NULL);
		gdk_pixbuf_unref (pb2);

		ili->data = item;
	}
}

static void
redo_shadow (SplashData *sd)
{
	double x, y, width, height;
	gtk_object_get (GTK_OBJECT (sd->label),
			"x", &x,
			"y", &y,
			"text_width", &width,
			"text_height", &height,
			NULL);
	gnome_canvas_item_set (sd->label_shadow,
			       "x1", x - (width/2.0) - 3.0,
			       "y1", y - (height/2.0) - 1.0,
			       "x2", x + (width/2.0) + 3.0,
			       "y2", y + (height/2.0) + 1.0,
			       NULL);
}


static gboolean
icon_cb (gpointer data)
{
  char *text, *msg;

  if (sd == NULL ||
      sd->icons == NULL) {
	  sd->icon_timeout = 0;
	  return FALSE;
  }

  text = sd->icons->data;
  sd->icons = g_slist_remove (sd->icons, text);

  /* do not translate "done" */
  if (strcmp (text, "done") != 0) {
    SplashApp *app = get_splash_app (g_basename (text));
    char *pix = NULL;

    if (app != NULL) {
      pix = g_strconcat (GNOME_ICONDIR "/", app->icon, NULL);
      g_free (text);
      text = g_strdup (_(app->human_name));
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

	check_icon_fit ();

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
	sd->icon_items = g_slist_append (sd->icon_items, item);
	sd->icon_pixbufs = g_slist_append (sd->icon_pixbufs, pb);
      }
    }

    g_free (pix);

    msg = g_strdup_printf (_("Starting GNOME: %s"), text);
  } else {
    msg = g_strdup (_("Starting GNOME: done"));
  }

  gnome_canvas_item_set (sd->label, "text", msg, NULL);
  redo_shadow (sd);

  g_free (msg);
  g_free (text);

  if (sd->icons == NULL) {
	  sd->icon_timeout = 0;
	  return FALSE;
  } else {
	  return TRUE;
  }
}

static void
queue_icon_action (void)
{
	if (sd != NULL &&
	    sd->icon_timeout == 0) {
		sd->icon_timeout = gtk_timeout_add (100, icon_cb, sd);
	}
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

  /* do not translate "done" it's used as a delimiter */
  update_splash ("done", sd->max);
  sd->timeout = gtk_timeout_add (2000, timeout_cb, NULL);
}

void
start_splash (gfloat max)
{
  GtkWidget *frame;

  g_return_if_fail (sd == NULL);

  sd = g_new0 (SplashData, 1);

  sd->icon_items = NULL;

  sd->scalefactor = 1.0;

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
    char *filename;
    int maj, minor, pl;

    /* Find a splahsscreen by looking at "gnome-splash-major.minor.pl.png",
       "gnome-splash-major.minor.png" and "gnome-splash.png" */
    if (sscanf (VERSION, "%d.%d.%d", &maj, &minor, &pl) != 3) {
	    pl = 0;
	    if (sscanf (VERSION, "%d.%d", &maj, &minor) != 2) {
		    maj = minor = pl = 0; /* sort of illegal version */
	    }
    }

    filename = g_strdup_printf ("%s/splash/gnome-splash-%d.%d.%d.png",
				GNOME_ICONDIR, maj, minor, pl);
    pb = gdk_pixbuf_new_from_file (filename);
    g_free (filename);
    if (pb == NULL) {
	    filename = g_strdup_printf ("%s/splash/gnome-splash-%d.%d.png",
					GNOME_ICONDIR, maj, minor);
	    pb = gdk_pixbuf_new_from_file (filename);
	    g_free (filename);
    }

    if (pb == NULL)
	    pb = gdk_pixbuf_new_from_file (GNOME_ICONDIR "/splash/gnome-splash.png");

    if (pb == NULL) {
	    height = 220;
	    width = BASE_WIDTH;
    } else {
	    height = gdk_pixbuf_get_height (pb);
	    width  = gdk_pixbuf_get_width  (pb);
    }

    /* used for scaling icons and other stuff */
    sd->scalefactor = (double)width / (double)BASE_WIDTH;

    /* some sanity into the madness */
    if (sd->scalefactor > 1.0)
	    sd->scalefactor = 1.0;
    else if (sd->scalefactor < 0.5)
	    sd->scalefactor = 0.5;

    sd->width = width;
    sd->height = height;

    gtk_widget_set_usize (sd->hbox, width, height);
    gnome_canvas_set_scroll_region (GNOME_CANVAS (sd->hbox), 0, 0, width, height);
    gtk_window_set_default_size (GTK_WINDOW (sd->dialog), width, height);
				 
    if (pb != NULL) {
	    gnome_canvas_item_new (GNOME_CANVAS_GROUP (GNOME_CANVAS (sd->hbox)->root),
				   GNOME_TYPE_CANVAS_PIXBUF, "pixbuf", pb, NULL);

	    gdk_pixbuf_unref (pb);
    } else {
	    gnome_canvas_item_new (GNOME_CANVAS_GROUP (GNOME_CANVAS (sd->hbox)->root),
				   GNOME_TYPE_CANVAS_TEXT,
				   "text", _("GNOME"),
				   "fontset", _("-adobe-helvetica-bold-r-normal-*-*-240-*-*-p-*-*-*"),
				   "x", (gdouble)(width / 2),
				   "y", (gdouble)(height / 2),
				   "anchor", GTK_ANCHOR_CENTER,
				   "fill_color", "black",
				   NULL);
    }

    sd->label_shadow = gnome_canvas_item_new (GNOME_CANVAS_GROUP (GNOME_CANVAS (sd->hbox)->root),
					      GNOME_TYPE_CANVAS_RECT,
					      /* fake */
					      "x1", (gdouble)0.0,
					      "y1", (gdouble)0.0,
					      "x2", (gdouble)1.0,
					      "y2", (gdouble)1.0,
					      "fill_color_rgba", 0x00000080,
					      NULL);
    sd->label = gnome_canvas_item_new (GNOME_CANVAS_GROUP (GNOME_CANVAS (sd->hbox)->root),
				       GNOME_TYPE_CANVAS_TEXT,
				       "text", _("Starting GNOME"),
				       "x", (gdouble)(width / 2),
				       "y", (gdouble)(height - 7.5),
				       "fontset", _("-adobe-helvetica-medium-r-normal-*-8-*-*-*-p-*-*-*"),
				       "anchor", GTK_ANCHOR_CENTER,
				       "fill_color", "white",
				       NULL);
    redo_shadow (sd);
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
  queue_icon_action ();
}
