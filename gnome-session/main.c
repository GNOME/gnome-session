/* main.c - Main program for gnome-session.

   Copyright (C) 1998, 1999 Tom Tromey

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

#include <libgnome/libgnome.h>
#include <libgnomeui/libgnomeui.h>
#include <libgnomeui/gnome-window-icon.h>
#include "manager.h"
#include "session.h"
#include "splash.h"

/* Parsing function.  */

/* The name of the session to load.  */
static const char *session = NULL;

/* Flag indicating chooser should be used. */
gboolean choosing = FALSE;

/* Flag indicating that the normal CONFIG_PREFIX should not be used. */
gboolean failsafe = FALSE;

/* Flag indicating Trash session should be used for saves. */
gboolean trashing = FALSE;

/* Wait period for clients to register. */
guint purge_delay = 30000;

/* Wait period for clients to save. */
guint warn_delay = 10000;

/* Wait period for clients to die during shutdown. */
guint suicide_delay = 10000;

static const struct poptOption options[] = {
  {"choose-session", '\0', POPT_ARG_NONE, &choosing, 0, N_("Start chooser and pick the session"), NULL},
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
  gboolean splashing;
  char *ep;
  poptContext ctx;
  const char **leftovers;
  Session *the_session;
  
  /* We do this as a separate executable, and do it first so that we
   * make sure we have a absolutely clean setup if the user blows
   * their configs away with the Ctrl-Shift hotkey.
   */
  system ("gnome-login-check");

  /* Initialize the i18n stuff */
  bindtextdomain (PACKAGE, GNOMELOCALEDIR);
  textdomain (PACKAGE);

  gnomelib_init ("gnome-session", VERSION);
  initialize_ice ();
  fprintf (stderr, "SESSION_MANAGER=%s\n", getenv ("SESSION_MANAGER"));
  gnome_client_disable_master_connection ();
  gnome_init_with_popt_table("gnome-session", VERSION, argc, argv, options, 0,
			     &ctx);
  gnome_window_icon_set_default_from_file (GNOME_ICONDIR"/gnome-session.png");
  leftovers = poptGetArgs(ctx);
  if(leftovers && leftovers[0]) 
    session = leftovers[0];
  poptFreeContext(ctx);

  /* Make sure children see the right value for DISPLAY.  This is
     useful if --display was specified on the command line.  */
  ep = g_strconcat ("DISPLAY=", gdk_get_display (), NULL);
  putenv (ep);

  ignore (SIGPIPE);

  /* Read in config options */
  gnome_config_push_prefix (GSM_OPTION_CONFIG_PREFIX);
  splashing = gnome_config_get_bool
    (SPLASH_SCREEN_KEY "=" SPLASH_SCREEN_DEFAULT);
  trashing = gnome_config_get_bool (TRASH_MODE_KEY "=" TRASH_MODE_DEFAULT);
  gnome_config_pop_prefix ();

  if (choosing || (session && !strcmp(session, CHOOSER_SESSION)))
    {
      session = CHOOSER_SESSION;
      choosing = TRUE;
    }

  if (!session)
    session = get_last_session ();

  set_session_name (session);
  the_session = read_session (session);

  /* fall back to the default session if the session is empty */
  if (!the_session->client_list)
    {
      free_session (the_session);
      failsafe = TRUE;
      the_session = read_session (session);
    }

  if (splashing)
    start_splash (49.0);

  start_session (the_session);

  gtk_main ();

  clean_ice ();

  /* If a logout command was set, the following will not return */
  execute_logout ();
  
  return 0;
}
