/* testbed.c - Test wrapper for session manager.
   Written by Tom Tromey <tromey@cygnus.com>.  */

#include <stdio.h>
#include <gtk/gtk.h>

#include "libgnome/libgnome.h"
#include "manager.h"

int
main (int argc, char *argv[])
{
  char *session = NULL;

  initialize_ice ();

  fprintf (stderr, "SESSION_MANAGER=%s\n", getenv ("SESSION_MANAGER"));

  gnome_init ("gsm-testbed", &argc, &argv);

  /* FIXME: real argument handling.  */
  if (argc >= 2)
    session = argv[1];

  read_session (session);

  gtk_main ();

  return 0;
}
