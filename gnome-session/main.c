/* main.c - Main program for gnome-session.

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
