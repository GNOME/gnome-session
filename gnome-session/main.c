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
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>  /* For have_ipv6() */
#include <netdb.h>

#include <gconf/gconf-client.h>

#include <libgnome/libgnome.h>
#include <libgnomeui/libgnomeui.h>

#include <libgnomeui/gnome-window-icon.h>

#include "manager.h"
#include "ice.h"
#include "save.h"
#include "command.h"
#include "splash-widget.h"
#include "util.h"
#include "gsm-sound.h"
#include "gsm-gsd.h"
#include "gsm-keyring.h"
#include "gsm-xrandr.h"
#include "gsm-at-startup.h"

/* Flag indicating autosave - user won't be prompted on logout to 
 * save the session */
gboolean autosave = FALSE;

gboolean logout_prompt = TRUE;

/* Flag indicating that an independant session save is taking place */
gboolean session_save = FALSE;

/* Flag indicating the save tick box on logout - user when prompted at 
 * log out as to  whether to save session or not - only applicable if
 * autosave is FALSE */
gboolean save_selected = FALSE;

/* Flag indicating that the normal CONFIG_PREFIX should not be used */
gboolean failsafe = FALSE;

/* Wait period for clients to register. */
guint purge_delay = 120000;

/* Wait period for clients to save. */
guint warn_delay = 120000;

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
  const char *gdm_lang;
  char       *env_string;
  char       *short_lang;
  char       *p;

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

/* Point GTK_RC_FILES at a separate file that we change in
 * in gnome-settings-daemon.
 */
static void
set_gtk1_theme_rcfile (void)
{
  char *env_string;

  env_string = g_strdup_printf ("GTK_RC_FILES=" SYSCONFDIR "/gtk/gtkrc:%s/.gtkrc-1.2-gnome2", g_get_home_dir ());

  putenv (env_string);
}

static void
update_boolean (GConfClient *client,
		guint cnxn_id,
		GConfEntry *entry,
		gpointer user_data)
{
  GConfValue *value;
  gboolean *bool_value = user_data;

  value = gconf_entry_get_value (entry);
  *bool_value = gconf_value_get_bool (value);

  gsm_verbose ("%s is now %s.\n", 
	       gconf_entry_get_key (entry), 
	       *bool_value ? "On" : "Off");
}

/* returns the hostname */
static gchar *
get_hostname (gboolean readable)
{
  static gboolean init = FALSE;
  static gchar *result = NULL;
  
  if (!init)
    {
      char hostname[256];
      
      if (gethostname (hostname, sizeof (hostname)) == 0)
	result = g_strdup (hostname);
		
      init = TRUE;
    }
	
  if (result)
    return result;
  else
    return readable ? "(Unknown)" : NULL;
}

#ifdef ENABLE_IPV6
/*Check whether the node is IPv6 enabled.*/
static gboolean
have_ipv6 (void)
{
  int s;

  s = socket (AF_INET6, SOCK_STREAM, 0);
  if (s != -1) {
    close (s);
    return TRUE;
  }
  return FALSE;
}
#endif

/* Check if a DNS lookup on `hostname` can be done */
static gboolean
check_for_dns (void)
{
  char *hostname;
  
  hostname = get_hostname (FALSE);
	
  if (!hostname)
    return FALSE;

#ifdef ENABLE_IPV6
  if (have_ipv6 ())
    {
      struct addrinfo hints, *result = NULL;

      memset (&hints, 0, sizeof(hints));
      hints.ai_socktype = SOCK_STREAM;
      hints.ai_flags = AI_CANONNAME;

      if (getaddrinfo (hostname, NULL, &hints, &result) != 0)
	return FALSE;

      if (g_ascii_strncasecmp (result->ai_canonname, hostname, 0) != 0)
	return FALSE;
    } 
  else
#endif
    {
      /*
       * FIXME:
       *  we should probably be a lot more robust here
       */
      if (!gethostbyname (hostname))
	return FALSE;
    }

  return TRUE;
}

enum {
  RESPONSE_LOG_IN,
  RESPONSE_TRY_AGAIN
};
    
static void
gnome_login_check (void)
{
  GtkWidget *tmp_msgbox = NULL;

  while (!check_for_dns ())
    {
      if (!tmp_msgbox)
	{
	  tmp_msgbox = gtk_message_dialog_new (NULL, 0,
					       GTK_MESSAGE_WARNING,
					       GTK_BUTTONS_NONE,
					       _("Could not look up internet address for %s.\n"
						 "This will prevent GNOME from operating correctly.\n"
						 "It may be possible to correct the problem by adding\n"
						 "%s to the file /etc/hosts."),
					       get_hostname(TRUE), get_hostname(TRUE));
  
	  gtk_dialog_add_buttons (GTK_DIALOG (tmp_msgbox),
				  _("Log in Anyway"), RESPONSE_LOG_IN,
				  _("Try Again"), RESPONSE_TRY_AGAIN,
				  NULL);
	  
	  gtk_window_set_position (GTK_WINDOW (tmp_msgbox), GTK_WIN_POS_CENTER);
	}

      gtk_dialog_set_default_response (GTK_DIALOG (tmp_msgbox), RESPONSE_TRY_AGAIN);

      if (RESPONSE_TRY_AGAIN != gtk_dialog_run (GTK_DIALOG (tmp_msgbox)))
	{
	  gtk_widget_destroy (tmp_msgbox);
  	  break;
	}
    }
}

static void
gsm_shutdown_gconfd (void)
{
  GError *error;
  char   *command;
  int     status;

  command = g_strjoin (" ", GCONFTOOL_CMD, "--shutdown", NULL);

  status = 0;
  error  = NULL;
  if (!g_spawn_command_line_sync (command, NULL, NULL, &status, &error))
    {
      g_warning ("Failed to execute '%s' on logout: %s\n",
		 command, error->message);
      g_error_free (error);
    }

  if (status)
    {
      g_warning ("Running '%s' at logout returned an exit status of '%d'",
		 command, status);
    }

  g_free (command);
}

int
main (int argc, char *argv[])
{
  char *ep;
  char *session_name_env;
  Session *the_session;
  GConfClient *gconf_client;
  gboolean splashing;
  gboolean a_t_support;
  GError *err;
  int status;
  
  if (getenv ("GSM_VERBOSE_DEBUG"))
    gsm_set_verbose (TRUE);
  
  set_lang();
  set_gtk1_theme_rcfile ();

  /* Initialize the i18n stuff */
  bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  /* gnome_login_check() needs gtk initialized. however, these do
   * sanity checks for gnome stuff, so we don't want to initialize
   * gnome yet
   */
  gtk_init (&argc, &argv);

  /* We need to do this as early as possible */
  gsm_set_display_properties ();
  
  /* Make sure the splash screen has the SM_CLIENT_ID
   * property set so smproxy doesn't try and session
   * manage us.
   * See http://bugzilla.gnome.org/show_bug.cgi?id=118063
   */
  gdk_set_sm_client_id ("gnome-session-dummy-id");

  gnome_login_check ();

  err = NULL;
  g_spawn_command_line_sync (GCONF_SANITY_CHECK, NULL, NULL, &status, &err);
  if (err != NULL)
    {
      g_printerr ("Failed to run gconf-sanity-check-2: %s\n",
                  err->message);
      g_error_free (err);
      /* who knows what's wrong, just continue */
    }
  else
    {
      if (WIFEXITED (status) && WEXITSTATUS (status) != 0)
        {
          g_printerr ("gconf-sanity-check-2 did not pass, logging back out\n");
          exit (1);
        }
    }
  
  gnome_program_init("gnome-session", VERSION, LIBGNOMEUI_MODULE, argc, argv, 
		  GNOME_PARAM_POPT_TABLE, options,
		  NULL);

 /* FIXME: it would be nice to skip this check if we're debugging the
 session manager. */

  if (GNOME_CLIENT_CONNECTED (gnome_master_client ()))
  {
    fprintf (stderr, "gnome-session: you're already running a session manager\n");
    exit (1);
  }

  initialize_ice ();
  fprintf (stderr, "SESSION_MANAGER=%s\n", getenv ("SESSION_MANAGER"));
  gnome_window_icon_set_default_from_file (GNOME_ICONDIR"/gnome-session.png");


  /* Make sure children see the right value for DISPLAY.  This is
     useful if --display was specified on the command line.  */
  ep = g_strconcat ("DISPLAY=", gdk_get_display (), NULL);
  putenv (ep);

  ignore (SIGPIPE);

  /* Need DISPLAY set */
  gsm_keyring_daemon_start ();
  
  /* Read in config options */  
  gconf_client = gconf_client_get_default ();

  gconf_client_add_dir (gconf_client, GSM_GCONF_CONFIG_PREFIX, GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);
  splashing      = gconf_client_get_bool (gconf_client, SPLASH_SCREEN_KEY, NULL);
  autosave       = gconf_client_get_bool (gconf_client, AUTOSAVE_MODE_KEY, NULL);
  logout_prompt  = gconf_client_get_bool (gconf_client, LOGOUT_PROMPT_KEY, NULL);
  a_t_support    = gconf_client_get_bool (gconf_client, ACCESSIBILITY_KEY, NULL);

  gconf_client_notify_add (gconf_client,
			   AUTOSAVE_MODE_KEY,
			   update_boolean,
			   &autosave, NULL, NULL);

  gconf_client_notify_add (gconf_client,
			   LOGOUT_PROMPT_KEY,
			   update_boolean,
			   &logout_prompt, NULL, NULL);

  g_object_unref (gconf_client);

  gnome_config_push_prefix (GSM_OPTION_CONFIG_PREFIX);

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
	/* if key was specified but is blank, just use the default */
	if (!*session_name) {
		g_free (session_name);
		session_name = g_strdup (DEFAULT_SESSION);
	}
  }
   
  gnome_config_pop_prefix ();
   
  if(failsafe)
	session_name = FAILSAFE_SESSION;
  
  session_name_env = g_strconcat ("GNOME_DESKTOP_SESSION_ID=", session_name, NULL);
  putenv (session_name_env);
  the_session = read_session (session_name);

  gsm_sound_login ();

  gsm_gsd_start ();

  if (splashing)
    splash_start ();

  start_session (the_session);

  if (a_t_support) /* the ATs are happier if the session has started */
    gsm_assistive_technologies_start ();

  gtk_main ();

  gsm_sound_logout ();

  gsm_keyring_daemon_stop ();
  
  gsm_shutdown_gconfd ();

  clean_ice ();

  /* If a logout command was set, the following will not return */
  execute_logout ();
  
  return 0;
}
