/* ice.c - Handle session manager/ICE integration.

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

#include <X11/ICE/ICElib.h>
#include <X11/ICE/ICEutil.h>
#include <gdk/gdk.h>

#include "libgnomeui/gnome-ice.h"

#include "manager.h"
#include "auth.h"

/* Number of transports.  */
static int num_transports;

/* Authentication records (don't ask me).  */
static IceAuthDataEntry *auth_entries;

/* This is called when a client tries to connect.  We accept most
   connections.  We reject connections while doing a shutdown.  */
static void
accept_connection (gpointer client_data, gint source,
		   GdkInputCondition conditon)
{
  IceConn connection;
  IceAcceptStatus status;
  IceConnectStatus status2 = IceConnectPending;

  if (shutdown_in_progress_p ())
    return;

  connection = IceAcceptConnection ((IceListenObj) client_data, &status);
  if (status != IceAcceptSuccess)
    return;

  /* Must wait until connection leaves pending state.  */
  /* FIXME: after a certain amount of time, just reject the
     connection.  */
  while (status2 == IceConnectPending)
    {
      gtk_main_iteration ();
      status2 = IceConnectionStatus (connection);
    }

  if (status2 != IceConnectAccepted)
    IceCloseConnection (connection);
}

/* FIXME: error message text could be returned somehow.  Or at least
   printed.  */
int
initialize_ice (void)
{
  char error[256];
  IceListenObj *listeners;
  int i;
  char *ids, *p;

  gnome_ice_init ();

  /* We don't care about the previous IO error handler.  */
  IceSetIOErrorHandler (io_error_handler);

  if (! SmsInitialize ("GnomeSM", VERSION, new_client, NULL,
		       HostBasedAuthProc, sizeof error, error))
    return 0;

  if (! IceListenForConnections (&num_transports, &listeners,
				 sizeof error, error))
    return 0;

  if (! SetAuthentication (num_transports, listeners, &auth_entries))
    return 0;

  for (i = 0; i < num_transports; ++i)
    gdk_input_add (IceGetListenConnectionNumber (listeners[i]),
		   GDK_INPUT_READ, accept_connection,
		   (gpointer) listeners[i]);

  ids = IceComposeNetworkIdList (num_transports, listeners);
#define ENVNAME "SESSION_MANAGER"
  p = g_new (char, sizeof ENVNAME + strlen (ids) + 1);
  sprintf (p, "%s=%s", ENVNAME, ids);
  putenv (p);

  return 1;
}

void
clean_ice (void)
{
  FreeAuthenticationData (num_transports, auth_entries);
}
