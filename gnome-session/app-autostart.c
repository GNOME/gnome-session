/* app-autostart.c
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

#include <ctype.h>
#include <string.h>

#include "app-autostart.h"
#include "gconf.h"

static gboolean is_disabled (GsmApp *app);

G_DEFINE_TYPE (GsmAppAutostart, gsm_app_autostart, GSM_TYPE_APP)

static void
gsm_app_autostart_init (GsmAppAutostart *app)
{
  ;
}

static void
gsm_app_autostart_class_init (GsmAppAutostartClass *klass)
{
  GsmAppClass *app_class = GSM_APP_CLASS (klass);

  app_class->is_disabled = is_disabled;
}

GsmApp *
gsm_app_autostart_new (const char *desktop_file,
		       const char *client_id)
{
  GsmApp *app;

  app = g_object_new (GSM_TYPE_APP_AUTOSTART,
		      "desktop-file", desktop_file,
		      "client-id", client_id,
		      NULL);
  if (!app->desktop_file)
    {
      g_object_unref (app);
      app = NULL;
    }

  return app;
}

static gboolean
is_disabled (GsmApp *app)
{
  char *condition;

  if (!egg_desktop_file_can_launch (app->desktop_file, "GNOME"))
    {
      g_debug ("app %s is disabled by TryExec, OnlyShowIn, or NotShowIn",
	       gsm_app_get_basename (app));
      return TRUE;
    }

  /* X-GNOME-Autostart-enabled key, used by old gnome-session */
  if (egg_desktop_file_has_key (app->desktop_file,
				"X-GNOME-Autostart-enabled", NULL) &&
      !egg_desktop_file_get_boolean (app->desktop_file,
				     "X-GNOME-Autostart-enabled", NULL))
    {
      g_debug ("app %s is disabled by X-GNOME-Autostart-enabled",
	       gsm_app_get_basename (app));
      return TRUE;
    }

  /* Hidden key, used by autostart spec */
  if (egg_desktop_file_get_boolean (app->desktop_file,
				    EGG_DESKTOP_FILE_KEY_HIDDEN, NULL))
    {
      g_debug ("app %s is disabled by Hidden",
	       gsm_app_get_basename (app));
      return TRUE;
    }

  /* Check AutostartCondition */
  condition = egg_desktop_file_get_string (app->desktop_file,
					   "AutostartCondition",
					   NULL);
  if (condition)
    {
      gboolean disabled;
      char *space, *key;
      int len;

      space = condition + strcspn (condition, " ");
      len = space - condition;
      key = space;
      while (isspace ((unsigned char)*key))
	key++;

      if (!g_ascii_strncasecmp (condition, "if-exists", len) && key)
	{
	  char *file = g_build_filename (g_get_user_config_dir (), key, NULL);
	  disabled = !g_file_test (file, G_FILE_TEST_EXISTS);
	  g_free (file);
	}
      else if (!g_ascii_strncasecmp (condition, "unless-exists ", len) && key)
	{
	  char *file = g_build_filename (g_get_user_config_dir (), key, NULL);
	  disabled = g_file_test (file, G_FILE_TEST_EXISTS);
	  g_free (file);
	}
      else if (!g_ascii_strncasecmp (condition, "GNOME", len))
	{
	  if (key)
	    {
	      disabled = !gconf_client_get_bool (gsm_gconf_get_client (),
						 key, NULL);
	    }
	  else
	    disabled = FALSE;
	}
      else
	disabled = TRUE;

      g_free (condition);
      if (disabled)
	{
	  g_debug ("app %s is disabled by AutostartCondition",
		   gsm_app_get_basename (app));
	  return TRUE;
	}
    }

  return FALSE;
}

