/* save-session.c - Small program to talk to session manager.
   Written by Tom Tromey <tromey@cygnus.com>.  */

#include <config.h>

#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>

#include "libgnome/libgnome.h"
#include "libgnomeui/gnome-client.h"

static void
usage ()
{
  /* FIXME: real usage per GNU stds.  */
  fprintf (stderr, "usage: save-session [--kill]\n");
  exit (1);
}

int
main (int argc, char *argv[])
{
  int zap = 0, i;
  GnomeClient *client;

  for (i = 1; i < argc; ++i)
    {
      if (! strcmp (argv[i], "--kill"))
	zap = 1;
      else
	usage ();
    }

  gnome_init ("session", &argc, &argv);

  client = gnome_client_new (argc, argv);
  gnome_client_set_restart_style (client, GNOME_RESTART_NEVER);

  /* We could expose more of the arguments to the user if we wanted
     to.  Some of them aren't particularly useful.  Interestingly,
     there is no way to request a shutdown without a save.  */
  gnome_client_request_save (client, GNOME_SAVE_BOTH, zap,
			     GNOME_INTERACT_ANY, 0, 1);

  gnome_client_disconnect (client);
  return 0;
}
