/* ice.c - Handle session manager/ICE integration.
   Written by Tom Tromey <tromey@cygnus.com>.  */

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
  IceAcceptStatus status;

  if (shutdown_in_progress_p ())
    return;

  /* Ignore results for now.  There's nothing we can really do with
     them.  */
  IceAcceptConnection ((IceListenObj) client_data, &status);
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

  /* FIXME: Version number.  */
  if (! SmsInitialize ("GnomeSM", "0.1", new_client, NULL,
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
