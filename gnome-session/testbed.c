/* testbed.c - Test wrapper for session manager.
   Written by Tom Tromey <tromey@cygnus.com>.  */

#include <stdio.h>
#include <gtk/gtk.h>

#include "manager.h"

#ifdef DONT_ASK /* Just a stupid local problem, to be fixed.  */
void gdk_pixmap_unref () { }
#endif

int
main (int argc, char *argv[])
{
  initialize_ice ();

  fprintf (stderr, "SESSION_MANAGER=%s\n", getenv ("SESSION_MANAGER"));

  gtk_init (&argc, &argv);
  gtk_main ();
}
