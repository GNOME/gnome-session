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
#include "ice.h"
#include "save.h"
#include "command.h"
#include "splash.h"

/* Flag indicating autosave - user won't be prompted on logout to 
 * save the session */
gboolean autosave = FALSE;

/* Flag indicating that an independant session save is taking place */
gboolean session_save = FALSE;

/* Flag indicating the save tick box on logout - user when prompted at 
 * log out as to  whether to save session or not - only applicable if
 * autosave is FALSE */
gboolean save_selected = FALSE;

/* Flag indicating that the normal CONFIG_PREFIX should not be used */
gboolean failsafe = FALSE;

/* Wait period for clients to register. */
guint purge_delay = 30000;

/* Wait period for clients to save. */
guint warn_delay = 10000;

/* Wait period for clients to die during shutdown. */
guint suicide_delay = 10000;

gchar *session_name = NULL;

static const struct poptOption options[] = {
  {"choose-session", '\0', POPT_ARG_STRING, &session_name, 0, N_("Specify a session name to load"), NULL},
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

/* Set language environment variables based on what GDM is setting
 */
static void
set_lang (void)
{
  char *gdm_lang;
  char *env_string;
  char *short_lang;
  char *p;

  gdm_lang = g_getenv("GDM_LANG");
  if (gdm_lang)
    {
      short_lang = g_strdup (gdm_lang);
      p = strchr(short_lang, '_');
      if (p)
	*p = '\0';

      env_string = g_strconcat ("LANG=", gdm_lang, NULL);
      putenv (env_string);

      /* env_string = g_strconcat ("LANGUAGE=", short_lang, NULL);
      putenv (env_string); */

      /* env_string = g_strconcat ("LC_ALL=", gdm_lang, NULL);
      putenv (env_string); */

      g_free (short_lang);
    }
}


int
main (int argc, char *argv[])
{
  gboolean splashing;
  char *ep;
  poptContext ctx;
  char *session_name_env;
  Session *the_session;
  
  set_lang();

  /* Initialize the i18n stuff */
  bindtextdomain (PACKAGE, GNOMELOCALEDIR);
  textdomain (PACKAGE);

  /* We do this as a separate executable, and do it first so that we
   * make sure we have a absolutely clean setup if the user blows
   * their configs away with the Ctrl-Shift hotkey.
   */
  system ("gnome-login-check");

  gnomelib_init ("gnome-session", VERSION);
  initialize_ice ();
  fprintf (stderr, "SESSION_MANAGER=%s\n", getenv ("SESSION_MANAGER"));
  gnome_client_disable_master_connection ();
  gnome_init_with_popt_table("gnome-session", VERSION, argc, argv, options, 0,
			     &ctx);
  gnome_window_icon_set_default_from_file (GNOME_ICONDIR"/gnome-session.png");
  poptFreeContext(ctx);

  /* Make sure children see the right value for DISPLAY.  This is
     useful if --display was specified on the command line.  */
  ep = g_strconcat ("DISPLAY=", gdk_get_display (), NULL);
  putenv (ep);

  ignore (SIGPIPE);

  /* Read in config options */
  gnome_config_push_prefix (GSM_OPTION_CONFIG_PREFIX);
  splashing = gnome_config_get_bool (SPLASH_SCREEN_KEY "=" SPLASH_SCREEN_DEFAULT);
  autosave = gnome_config_get_bool (AUTOSAVE_MODE_KEY "=" AUTOSAVE_MODE_DEFAULT);
 

  /* If the session wasn't set on the command line, but we see a
   * GDM_GNOME_SESSION env var, use that.  The user is using the GDM chooser
   */
  if (session_name == NULL &&
      g_getenv ("GDM_GNOME_SESSION") != NULL) {
    session_name = g_strdup (g_getenv ("GDM_GNOME_SESSION"));
  }

  /* If the session name hasn't been specified from the command line */ 
  if(session_name == NULL) {
	/* If there is no key specified, fall back to the default session */	
	session_name = gnome_config_get_string (CURRENT_SESSION_KEY "=" DEFAULT_SESSION);
  }
   
  gnome_config_pop_prefix ();
   
  if(failsafe)
	session_name = FAILSAFE_SESSION;
  
  session_name_env = g_strconcat ("GNOME_SESSION_NAME=", g_strdup (session_name), NULL);
  putenv (session_name_env);
  the_session = read_session (session_name);

  if (splashing)
    start_splash (49.0);

  start_session (the_session);

  gtk_main ();

  clean_ice ();

  /* If a logout command was set, the following will not return */
  execute_logout ();
  
  return 0;
}
