#include <config.h>

#include <gconf/gconf-client.h>

#include "gsm-xrandr.h"
#include "util.h"

#ifdef HAVE_RANDR
#include <stdio.h>
#include <unistd.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/extensions/Xrandr.h>
#endif

#ifdef HAVE_RANDR
static int
get_resolution (GConfClient *client, int screen, char *keys[], int *width, int *height)
{
  int i;
  char *key;
  char *val;
  int w, h;

  val = NULL;
  for (i = 0; keys[i] != NULL; i++)
    {
      key = g_strdup_printf ("%s/%d/resolution", keys[i], screen);
      val = gconf_client_get_string (client, key, NULL);
      g_free (key);

      if (val != NULL)
	break;
    }
  
  if (val == NULL)
    return -1;

  if (sscanf (val, "%dx%d", &w, &h) != 2) {
    g_free (val);
    return -1;
  }

  g_free (val);

  *width = w;
  *height = h;
  
  return i;
}

static int
get_rate (GConfClient *client, char *display, int screen)
{
  char *key;
  int val;
  GError *error = NULL;

  key = g_strdup_printf ("%s/%d/rate", display, screen);
  val = gconf_client_get_int (client, key, &error);
  g_free (key);

  if (error == NULL)
    return val;

  g_error_free (error);
  return 0;
}

static int
find_closest_size (XRRScreenSize *sizes, int nsizes, int width, int height)
{
  int closest;
  int closest_width, closest_height;
  int i;

  closest = 0;
  closest_width = sizes[0].width;
  closest_height = sizes[0].height;
  for (i = 1; i < nsizes; i++)
    {
      if (ABS (sizes[i].width - width) < ABS (closest_width - width) ||
	  (sizes[i].width == closest_width &&
	   ABS (sizes[i].height - height) < ABS (closest_height - height)))
	{
	  closest = i;
	  closest_width = sizes[i].width;
	  closest_height = sizes[i].height;
	}
    }
  
  return closest;
}

#endif

void
gsm_set_display_properties (void)
{
#ifdef HAVE_RANDR
  GdkDisplay *display;
  Display *xdisplay;
  int major, minor;
  int event_base, error_base;
  int n_screens;
  GdkScreen *screen;
  GdkWindow *root_window;
  int width, height, rate;
  GConfClient *client;
#ifdef HOST_NAME_MAX
  char hostname[HOST_NAME_MAX + 1];
#else
  char hostname[256];
#endif
  char *specific_path;
  char *keys[3];
  int i, residx;

  display = gdk_display_get_default ();
  xdisplay = gdk_x11_display_get_xdisplay (display);

  /* Check if XRandR is supported on the display */
  if (!XRRQueryExtension (xdisplay, &event_base, &error_base) ||
      XRRQueryVersion (xdisplay, &major, &minor) == 0)
    return;
  
  if (major != 1 || minor < 1)
    {
      g_message ("Display has unsupported version of XRandR (%d.%d), not setting resolution.", major, minor);
      return;
    }

  client = gsm_get_conf_client ();
  
  i = 0;
  specific_path = NULL;
  if (gethostname (hostname, sizeof (hostname)) == 0)
    {
      specific_path = g_strconcat ("/desktop/gnome/screen/", hostname,  NULL);
      keys[i++] = specific_path;
    }
  keys[i++] = "/desktop/gnome/screen/default";
  keys[i++] = NULL;
  
  n_screens = gdk_display_get_n_screens (display);
  for (i = 0; i < n_screens; i++)
    {
      screen = gdk_display_get_screen (display, i);
      root_window = gdk_screen_get_root_window (screen);
      residx = get_resolution (client, i, keys, &width, &height);

      if (residx != -1)
	{
	  XRRScreenSize *sizes;
	  int nsizes, j;
	  int closest;
	  short *rates;
	  int nrates;
	  int status;
	  int current_size;
	  short current_rate;
	  XRRScreenConfiguration *config;
	  Rotation current_rotation;

	  config = XRRGetScreenInfo (xdisplay, gdk_x11_drawable_get_xid (GDK_DRAWABLE (root_window)));

	  rate = get_rate (client, keys[residx], i);
	  
	  sizes = XRRConfigSizes (config, &nsizes);
	  closest = find_closest_size (sizes, nsizes, width, height);
	  
	  rates = XRRConfigRates (config, closest, &nrates);
	  for (j = 0; j < nrates; j++)
	    {
	      if (rates[j] == rate)
		break;
	    }
	  /* Rate not supported, let X pick */
	  if (j == nrates)
	    rate = 0;

	  current_size = XRRConfigCurrentConfiguration (config, &current_rotation);
	  current_rate = XRRConfigCurrentRate (config);

	  if (closest != current_size ||
	      rate != current_rate)
	    {
	      status = XRRSetScreenConfigAndRate (xdisplay, 
						  config,
						  gdk_x11_drawable_get_xid (GDK_DRAWABLE (root_window)),
						  closest,
						  current_rotation,
						  rate,
						  GDK_CURRENT_TIME);
	    }

	   XRRFreeScreenConfigInfo (config);
	}
    }
  
  g_free (specific_path);

  /* We need to make sure we process the screen resize event. */
  gdk_display_sync (display);
  while (gtk_events_pending ())
    gtk_main_iteration();
  
#endif
}
