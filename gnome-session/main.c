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
#include <signal.h>
#include <stdlib.h>

#include "libgnome/libgnome.h"
#include "libgnomeui/libgnomeui.h"
#include "manager.h"

/* Parsing function.  */

/* The name of the session to load.  */
static char *session = NULL;

/* If nonzero, we are debugging and don't want to load an initial
   session.  */
guint purge_delay = 30000;

/* If nonzero, we are debugging and don't want to load an initial
   session.  */
static int debugging = 0;

static const struct poptOption options[] = {
  {"debug", '\0', POPT_ARG_NONE, &debugging, 0, N_("Enable gsm debugging"), NULL},
  {"purge-delay", '\0', POPT_ARG_INT, &purge_delay, 0, N_("Millisecond period spent waiting for clients to register (0=forever)"), NULL},
  {NULL, '\0', 0, NULL, 0}
};

/* A separate function to ease the impending #if hell.  */
static void
ignore (int sig)
{
  struct sigaction act;

  act.sa_handler = SIG_IGN;
  sigemptyset (& act.sa_mask);
  act.sa_flags = 0;

  sigaction (sig, &act, NULL);
}

int
main (int argc, char *argv[])
{
  char *ep;
  poptContext ctx;
  char **leftovers;

  /* Initialize the i18n stuff */
  bindtextdomain (PACKAGE, GNOMELOCALEDIR);
  textdomain (PACKAGE);

  initialize_ice ();
  /* FIXME: this is debugging that can eventually be removed.  */
  fprintf (stderr, "SESSION_MANAGER=%s\n", getenv ("SESSION_MANAGER"));
  gnome_client_disable_master_connection ();
  gnome_init_with_popt_table("gnome-session", VERSION, argc, argv, options, 0,
			     &ctx);
  leftovers = poptGetArgs(ctx);
  if(leftovers) {
    if(leftovers[0] && leftovers[1]) {
      /* too many args */
      perror("Too many arguments.");
      poptPrintHelp(ctx, stderr, 0);
    } /* else if(!leftovers[0]) */ /* XXX do we need to require a
	                              session name on the cmdline here? */

    session = leftovers[0];
  }
  poptFreeContext(ctx);

  /* Make sure children see the right value for DISPLAY.  This is
     useful if --display was specified on the command line.  */
  ep = g_strconcat ("DISPLAY=", gdk_get_display (), NULL);
  putenv (ep);

  ignore (SIGPIPE);
  if (! debugging)
    read_session (session);

  gtk_main ();

  clean_ice ();

  return 0;
}
