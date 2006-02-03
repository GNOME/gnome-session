/*
 * Copyright (C) 2003 Sun Microsystems, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Authors:
 *      Mark McLoughlin <mark@skynet.ie>
 */

#include <config.h>

#include "gsm-remote-desktop.h"

#include <string.h>
#include <time.h>
#include <libbonobo.h>
#include <gconf/gconf-client.h>
#include <gdk/gdkdisplay.h>
#include "util.h"

#define REMOTE_DESKTOP_DIR "/desktop/gnome/remote_access"
#define REMOTE_DESKTOP_KEY REMOTE_DESKTOP_DIR "/enabled"
#define REMOTE_DESKTOP_IID "OAFIID:GNOME_RemoteDesktopServer"

typedef struct
{
  Bonobo_Unknown obj;
  gboolean       activating;
  time_t         start_time;
  guint          attempts;
  guint          shutdown_timeout;

  GConfClient   *client;
  gboolean       enabled;
  guint          listener;
} RemoteDesktopData;

static void remote_desktop_restart (RemoteDesktopData *data);

static gboolean
remote_desktop_shutdown_timeout (RemoteDesktopData *data)
{
  if (data->activating)
    {
      gsm_verbose (G_STRLOC ": remote desktop server activating, delaying shutdown\n");
      return TRUE;
    }

  gsm_verbose (G_STRLOC ": shutting down remote desktop server\n");

  if (data->obj != CORBA_OBJECT_NIL)
    bonobo_object_release_unref (data->obj, NULL);
  data->obj = CORBA_OBJECT_NIL;
    
  data->shutdown_timeout = 0;

  return FALSE;
}

static void
remote_desktop_shutdown (RemoteDesktopData *data)
{
  if (!data->activating && data->obj == CORBA_OBJECT_NIL)
    return;

  if (data->shutdown_timeout)
    return;

  gsm_verbose (G_STRLOC ": shutting down remote desktop server in 30 seconds\n");

  data->shutdown_timeout = g_timeout_add (30 * 1000,
					  (GSourceFunc) remote_desktop_shutdown_timeout,
					  data);
}

static void
remote_desktop_cnx_broken (ORBitConnection   *cnx,
			   RemoteDesktopData *data)
{
  g_return_if_fail (data->obj != CORBA_OBJECT_NIL);
  g_return_if_fail (data->activating == FALSE);

  data->obj = CORBA_OBJECT_NIL;

  if (data->shutdown_timeout)
    {
      g_source_remove (data->shutdown_timeout);
      data->shutdown_timeout = 0;
      return;
    }

  if (data->enabled)
    {
      gsm_warning (G_STRLOC ": remote desktop server died, restarting\n");

      remote_desktop_restart (data);
    }
}

static void
remote_desktop_obj_activated (CORBA_Object   object, 
			      const char    *error_reason, 
			      gpointer       user_data)
{
  RemoteDesktopData *data = user_data;

  g_return_if_fail (data->obj == CORBA_OBJECT_NIL);
  g_return_if_fail (data->activating == TRUE);

  data->activating = FALSE;
  data->obj        = object;

  if (object == CORBA_OBJECT_NIL)
    {
      if (error_reason)
	{
	  gsm_warning (G_STRLOC ": activation of %s failed: %s\n",
		       REMOTE_DESKTOP_IID, error_reason);
	}
      else
	{
	  gsm_warning (G_STRLOC ": activation of %s failed: Unknown Error\n",
		       REMOTE_DESKTOP_IID);
	}

      if (data->enabled)
	remote_desktop_restart (data);
      return;
    }

  gsm_verbose (G_STRLOC ": activated %s; object: %p\n", REMOTE_DESKTOP_IID, object);

  ORBit_small_listen_for_broken (object,
				 G_CALLBACK (remote_desktop_cnx_broken),
				 data);
}

/* We don't want bonobo-activation's behaviour of using
 * the value of $DISPLAY since this may contain the
 * screen number.
 *
 * FIXME: should this be the default behaviour for
 *        bonobo-activation too ?
 */
static inline void
setup_per_display_activation (void)
{
  char *display_name, *p;

  display_name = g_strdup (gdk_display_get_name (gdk_display_get_default ()));

  /* Strip off the screen portion of the display */
  p = strrchr (display_name, ':');
  if (p)
    {
      p = strchr (p, '.');
      if (p)
        p [0] = '\0';
    }

  bonobo_activation_set_activation_env_value ("DISPLAY", display_name);

  g_free (display_name);
}

static void
remote_desktop_restart (RemoteDesktopData *data)
{
  CORBA_Environment ev;
  time_t            now;

  if (data->shutdown_timeout)
    {
      gsm_verbose (G_STRLOC ": cancelling remote desktop server shutdown\n");
      g_source_remove (data->shutdown_timeout);
      data->shutdown_timeout = 0;
    }

  if (data->activating || data->obj != CORBA_OBJECT_NIL)
    return;
  
  gsm_verbose (G_STRLOC ": starting remote desktop server, %s\n",
	       REMOTE_DESKTOP_IID);

  now = time (NULL);
  if (now > data->start_time + 120)
    {
      data->attempts   = 0;
      data->start_time = now;
    }

  if (data->attempts++ > 10)
    {
      gsm_warning (G_STRLOC ": Failed to activate remote desktop server: tried too many times\n");
      return;
    }

  data->activating = TRUE;

  CORBA_exception_init (&ev);
  
  setup_per_display_activation ();

  bonobo_activation_activate_from_id_async
      ( REMOTE_DESKTOP_IID, 0,
	&remote_desktop_obj_activated, data, &ev );

  if (BONOBO_EX (&ev))
    {
      gsm_warning (G_STRLOC ": activation of %s failed: %s\n",
		   REMOTE_DESKTOP_IID,
		   bonobo_exception_general_error_get (&ev));
    }

  CORBA_exception_free (&ev);
}

static void
remote_desktop_enabled_notify (GConfClient       *client,
			       guint              cnx_id,
			       GConfEntry        *entry,
			       RemoteDesktopData *data)
{
  if (!entry->value || entry->value->type != GCONF_VALUE_BOOL)
    return;

  data->enabled = gconf_value_get_bool (entry->value);

  if (data->enabled)
    remote_desktop_restart (data);
  else
    remote_desktop_shutdown (data);
}

static RemoteDesktopData remote_desktop_data = { NULL, };

void
gsm_remote_desktop_start (void)
{
  remote_desktop_data.client = gconf_client_get_default ();

  remote_desktop_data.enabled = gconf_client_get_bool (remote_desktop_data.client,
						       REMOTE_DESKTOP_KEY,
						       NULL);

  gconf_client_add_dir (remote_desktop_data.client,
			REMOTE_DESKTOP_DIR,
			GCONF_CLIENT_PRELOAD_NONE,
			NULL);

  remote_desktop_data.listener =
    gconf_client_notify_add (remote_desktop_data.client,
			     REMOTE_DESKTOP_KEY,
			     (GConfClientNotifyFunc) remote_desktop_enabled_notify,
			     &remote_desktop_data, NULL, NULL);

  if (remote_desktop_data.enabled)
    remote_desktop_restart (&remote_desktop_data);
}

void
gsm_remote_desktop_cleanup (void)
{
  if (remote_desktop_data.shutdown_timeout)
    g_source_remove (remote_desktop_data.shutdown_timeout);
  remote_desktop_data.shutdown_timeout = 0;

  if (remote_desktop_data.obj)
    ORBit_small_unlisten_for_broken (remote_desktop_data.obj,
				     G_CALLBACK (remote_desktop_cnx_broken));

  remote_desktop_shutdown_timeout (&remote_desktop_data);

  gconf_client_notify_remove (remote_desktop_data.client, remote_desktop_data.listener);
  remote_desktop_data.listener = 0;

  g_object_unref (remote_desktop_data.client);
  remote_desktop_data.client = NULL;
}
