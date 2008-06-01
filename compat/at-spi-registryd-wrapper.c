/*
 * at-spi-registryd-wrapper: a wrapper to make at-spi-registryd
 * conform to the startup item interfaces. (This should go away when
 * at-spi-registryd supports session management directly.)
 *
 * Most of this code comes from the old gnome-session/gsm-at-startup.c.
 *
 * Copyright (C) 2003 Sun Microsystems, Inc.
 * Copyright (C) 2007 Novell, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

#include <dbus/dbus-glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>

static Atom AT_SPI_IOR;

static GdkFilterReturn 
registry_ior_watch (GdkXEvent *xevent, GdkEvent *event, gpointer data)
{
  XEvent *xev = (XEvent *)xevent;

  if (xev->xany.type == PropertyNotify &&
      xev->xproperty.atom == AT_SPI_IOR)
    {
      gtk_main_quit ();

      return GDK_FILTER_REMOVE;
    }

  return GDK_FILTER_CONTINUE;
}

static gboolean
show_error (DBusGProxy *gsm)
{
  const char *message;

  message = _("Assistive technology support has been requested for this session, but the accessibility registry was not found. Please ensure that the AT-SPI package is installed. Your session has been started without assistive technology support.");
  dbus_g_proxy_call (gsm, "InitializationError", NULL,
		     G_TYPE_STRING, message,
		     G_TYPE_BOOLEAN, FALSE,
		     G_TYPE_INVALID,
		     G_TYPE_INVALID);

  exit (1);

  /* not reached */
  return FALSE;
}

static void
set_gtk_modules (DBusGProxy *gsm)
{
  const char *old;
  char *value;
  gboolean found_gail;
  gboolean found_atk_bridge;
  GError *error = NULL;
  int i;

  found_gail = FALSE;
  found_atk_bridge = FALSE;

  old = g_getenv ("GTK_MODULES");
  if (old != NULL)
    {
      char **old_modules, **modules;

      old_modules = g_strsplit (old, ":", -1);
      for (i = 0; old_modules[i]; i++)
        {
          if (!strcmp (old_modules[i], "gail"))
            found_gail = TRUE;
          else if (!strcmp (old_modules[i], "atk-bridge"))
            found_atk_bridge = TRUE;
        }

      modules = g_new (char *, i + (found_gail ? 0 : 1) +
		       (found_atk_bridge ? 0 : 1) + 1);
      for (i = 0; old_modules[i]; i++)
	modules[i] = old_modules[i];
      if (!found_gail)
	modules[i++] = "gail";
      if (!found_atk_bridge)
	modules[i++] = "atk-bridge";
      modules[i] = NULL;

      value = g_strjoinv (":", modules);
      g_strfreev (modules);
    }
  else
    value = g_strdup ("gail:atk-bridge");

  if (!dbus_g_proxy_call (gsm, "Setenv", &error,
			  G_TYPE_STRING, "GTK_MODULES",
			  G_TYPE_STRING, value,
			  G_TYPE_INVALID,
			  G_TYPE_INVALID))
    {
      g_warning ("Could not set GTK_MODULES: %s", error->message);
      g_error_free (error);
    }

  g_free (value);
}

int
main (int argc, char **argv)
{
  GdkDisplay *display;
  GdkWindow *root;
  GError *error = NULL;
  DBusGConnection *connection;
  DBusGProxy *gsm;

  gtk_init (&argc, &argv);

  connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
  if (!connection)
    g_error ("couldn't get D-Bus connection: %s", error->message);
  gsm = dbus_g_proxy_new_for_name (connection,
				   "org.gnome.SessionManager",
				   "/org/gnome/SessionManager",
				   "org.gnome.SessionManager");

  display = gdk_display_get_default ();
  root = gdk_screen_get_root_window (gdk_display_get_default_screen (display));

  AT_SPI_IOR = XInternAtom (gdk_x11_display_get_xdisplay (display),
			    "AT_SPI_IOR", False); 

  gdk_window_set_events (root, GDK_PROPERTY_CHANGE_MASK);
  gdk_window_add_filter (root, registry_ior_watch, NULL);

  if (!g_spawn_command_line_async (AT_SPI_REGISTRYD_DIR "/at-spi-registryd", &error))
    {
      show_error (gsm);
      /* not reached */
    }

  gtk_main ();

  gdk_window_remove_filter (root, registry_ior_watch, NULL);

  set_gtk_modules (gsm);
  g_object_unref (gsm);

  return 0;
}
