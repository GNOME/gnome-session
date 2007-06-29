/* gnome-session-splash.c
 * Copyright (C) 2007 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <gtk/gtkmain.h>
#include <gdk/gdkx.h>
#include <gconf/gconf-client.h>
#include <libgnome/gnome-program.h>
#include <libgnomeui/gnome-ui-init.h>
#include <glib/gi18n.h>
#include "eggsmclient-libgnomeui.h"

#define SN_API_NOT_YET_FROZEN
#include <libsn/sn-monitor.h>

#include "splash-window.h"

static void
event_func (SnMonitorEvent *event, void *user_data)
{
  GsmSplashWindow *splash = user_data;
  SnStartupSequence *seq;

  seq = sn_monitor_event_get_startup_sequence (event);

  switch (sn_monitor_event_get_type (event))
    {
    case SN_MONITOR_EVENT_INITIATED:
      gsm_splash_window_start (splash,
			       sn_startup_sequence_get_name (seq),
			       sn_startup_sequence_get_icon_name (seq));
      break;

    case SN_MONITOR_EVENT_COMPLETED:
      /* GSM (ab)uses startup notification to tell us when to quit */
      /* FIXME: Nope, doesn't actually work; libsn eats the notification */
      if (!strcmp (sn_startup_sequence_get_id (seq), "splash"))
	{
	  gtk_main_quit ();
	  return;
	}

      gsm_splash_window_finish (splash,
				sn_startup_sequence_get_name (seq));
      break;

    default:
      break;
    }
}

static gboolean
splash_clicked (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
  gtk_main_quit ();
  return TRUE;
}

static GdkFilterReturn
filter_func (GdkXEvent *gdkxevent, GdkEvent *event, gpointer user_data)
{
  SnDisplay *sn_display = user_data;

  sn_display_process_event (sn_display, (XEvent *)gdkxevent);
  return GDK_FILTER_CONTINUE;
}

static void
push_trap_func (SnDisplay *display, Display *xdisplay)
{
  gdk_error_trap_push ();
}

static void
pop_trap_func (SnDisplay *display, Display *xdisplay)
{
  gdk_error_trap_pop ();
}

static GdkPixbuf *
load_pixbuf (const char *filename)
{
  GdkPixbuf *pixbuf;
  GError *error = NULL;
  char *path;

  if (!filename)
    return NULL;

  path = gnome_program_locate_file (NULL,
				    GNOME_FILE_DOMAIN_PIXMAP,
				    filename, TRUE,
				    NULL);
  if (!path)
    return NULL;

  pixbuf = gdk_pixbuf_new_from_file (path, &error);
  if (!pixbuf)
    {
      g_warning ("Failed to load image from '%s': %s\n",
		 path, error->message);
      g_error_free (error);
    }

  return pixbuf;
}

static GdkPixbuf *
load_splash_pixbuf (void)
{
  GConfClient *gconf;
  char *filename;
  GdkPixbuf *pixbuf;

  gconf = gconf_client_get_default ();
  filename = gconf_client_get_string (gconf,
				      "/apps/gnome-session/options/splash_image",
				      NULL);

  if (filename)
    {
      pixbuf = load_pixbuf (filename);
      g_free (filename);
    }
  else
    pixbuf = NULL;

  if (!pixbuf)
    pixbuf = load_pixbuf ("splash/gnome-splash.png");

  return pixbuf;
}

static void
setup_splash_window (void)
{
  GdkDisplay *display;
  GdkScreen *screen;
  GdkWindow *root;
  SnDisplay *sn_display;
  GtkWidget *splash_widget;
  GsmSplashWindow *splash;
  GdkPixbuf *background;

  /* Create the splash window */
  background = load_splash_pixbuf ();
  splash_widget = gsm_splash_window_new (background);
  splash = (GsmSplashWindow *)splash_widget;
  g_object_unref (background);

  /* Set up startup notification monitoring */
  display = gdk_display_get_default ();
  screen = gdk_display_get_default_screen (display);

  sn_display = sn_display_new (gdk_x11_display_get_xdisplay (display),
			       push_trap_func, pop_trap_func);
  sn_monitor_context_new (sn_display, gdk_screen_get_number (screen),
			  event_func, splash, NULL);

  root = gdk_screen_get_root_window (screen);
  gdk_window_set_events (root, gdk_window_get_events (root) | GDK_PROPERTY_CHANGE_MASK);
  gdk_window_add_filter (NULL, filter_func, sn_display);

  /* Display the splash */
  gtk_widget_show (splash_widget);
  g_signal_connect (splash, "button-release-event",
		    G_CALLBACK (splash_clicked), NULL);
}

static void
quit (EggSMClient *smclient, gpointer user_data)
{
  gtk_main_quit ();
}

int
main (int argc, char *argv[])
{
  GOptionContext *context;

  g_type_init ();

  context = g_option_context_new (_("- GNOME Splash Screen"));

  gnome_program_init ("gnome-session-splash", VERSION,
		      EGG_SM_CLIENT_LIBGNOMEUI_MODULE,
		      argc, argv,
		      GNOME_PARAM_GOPTION_CONTEXT, context,
		      NULL);

  g_signal_connect (egg_sm_client_get (), "quit",
		    G_CALLBACK (quit), NULL);

  setup_splash_window ();
  gtk_main ();

  return 0;
}
