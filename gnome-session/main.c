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
#include "session.h"

/* Parsing function.  */

/* The name of the session to load.  */
static char *session = NULL;

/* Flag indicating chooser should be used. */
gboolean choosing = FALSE;

/* Flag indicating that the normal CONFIG_PREFIX should not be used. */
gboolean failsafe = FALSE;

/* Flag indicating Trash session should be used for saves. */
gboolean trashing = FALSE;

/* Wait period for clients to register. */
guint purge_delay = 30000;

/* Wait period for clients to save. */
guint warn_delay = 5000;

/* Wait period for clients to die during shutdown. */
guint suicide_delay = 10000;

static const struct poptOption options[] = {
  {"choose-session", '\0', POPT_ARG_NONE, &choosing, 0, N_("Start chooser and pick the session"), NULL},
  {"trash-saves", '\0', POPT_ARG_NONE, &trashing, 0, N_("Disable normal operation by saving into the Trash session"), NULL},
  {"failsafe", '\0', POPT_ARG_NONE, &failsafe, 0, N_("Only read saved sessions from the default.session file"), NULL},
  {"purge-delay", '\0', POPT_ARG_INT, &purge_delay, 0, N_("Millisecond period spent waiting for clients to register (0=forever)"), NULL},
  {"warn-delay", '\0', POPT_ARG_INT, &warn_delay, 0, N_("Millisecond period spent waiting for clients to respond (0=forever)"), NULL},
  {"suicide-delay", '\0', POPT_ARG_INT, &suicide_delay, 0, N_("Millisecond period spent waiting for clients to die (0=forever)"), NULL},
  {NULL, '\0', 0, NULL, 0}
};

/* A separate function to ease the impending #if hell.  */
static void
sig_ignore_handler (int num)
{
  return;
}

static void
ignore (int sig)
{
  struct sigaction act;

  act.sa_handler = sig_ignore_handler;
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
  char *chooser;

  /* Initialize the i18n stuff */
  bindtextdomain (PACKAGE, GNOMELOCALEDIR);
  textdomain (PACKAGE);

  initialize_ice ();
  /* fprintf (stderr, "SESSION_MANAGER=%s\n", getenv ("SESSION_MANAGER")); */
  gnome_client_disable_master_connection ();
  gnome_init_with_popt_table("gnome-session", VERSION, argc, argv, options, 0,
			     &ctx);
  leftovers = poptGetArgs(ctx);
  if(leftovers && leftovers[0]) 
    session = leftovers[0];
  poptFreeContext(ctx);

  /* Make sure children see the right value for DISPLAY.  This is
     useful if --display was specified on the command line.  */
  ep = g_strconcat ("DISPLAY=", gdk_get_display (), NULL);
  putenv (ep);

  ignore (SIGPIPE);

  if (choosing || (session && !strcmp(session, CHOOSER_SESSION)))
    {
      session = CHOOSER_SESSION;
      choosing = TRUE;
    }

  if (!session)
    session = get_last_session ();

  set_session_name (session);

  start_session (read_session (session));

  gtk_main ();

  clean_ice ();

  return 0;
}
