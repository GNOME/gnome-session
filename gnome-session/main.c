/* main.c - Main program for gnome-session.
   Written by Tom Tromey <tromey@cygnus.com>.  */

#include <config.h>

#include <stdio.h>
#include <gtk/gtk.h>

#include "libgnome/libgnome.h"
#include "manager.h"

/* The name of the session to load.  */
static char *session = NULL;

/* Parsing function.  */
static error_t parse_an_arg (int key, char *arg, struct argp_state *state);

/* Our argument parser.  */
static struct argp parser =
{
  NULL,
  parse_an_arg,
  N_("[SESSION]"),
  NULL,
  NULL,
  NULL,
  PACKAGE
};

static error_t
parse_an_arg (int key, char *arg, struct argp_state *state)
{
  if (key != ARGP_KEY_ARG)
    return ARGP_ERR_UNKNOWN;

  if (session)
    argp_usage (state);

  session = arg;
  return 0;
}

int
main (int argc, char *argv[])
{
  argp_program_version = VERSION;

  /* Initialize the i18n stuff */
  bindtextdomain (PACKAGE, GNOMELOCALEDIR);
  textdomain (PACKAGE);

  initialize_ice ();
  /* FIXME: this is debugging that can eventually be removed.  */
  fprintf (stderr, "SESSION_MANAGER=%s\n", getenv ("SESSION_MANAGER"));

  gnome_init ("gnome-session", &parser, argc, argv, 0, NULL);

  read_session (session);

  gtk_main ();

  return 0;
}
