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
#include <time.h>
#include <unistd.h>

#include <glib/gi18n.h>

#include <orbit/orbit.h>

#include <gconf/gconf-client.h>

#include <libgnomeui/gnome-client.h>
#include <libgnomeui/gnome-ui-init.h>
#include <libgnome/gnome-config.h>

#include "manager.h"
#include "ice.h"
#include "save.h"
#include "command.h"
#include "splash-widget.h"
#include "util.h"
#include "gsm-dbus.h"
#include "gsm-sound.h"
#include "gsm-gsd.h"
#include "gsm-keyring.h"
#include "gsm-xrandr.h"
#include "gsm-at-startup.h"
#include "gsm-remote-desktop.h"

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
gint purge_delay = 120000;

/* Wait period for clients to save. */
gint warn_delay = 120000;

/* Wait period for clients to die during shutdown. */
gint suicide_delay = 10000;

gchar *session_name = NULL;

static const GOptionEntry options[] = {
  {"choose-session", '\0', 0, G_OPTION_ARG_STRING, &session_name, N_("Specify a session name to load"), N_("NAME")},
  {"failsafe", '\0', 0, G_OPTION_ARG_NONE, &failsafe, N_("Only read saved sessions from the default.session file"), NULL},
  {"purge-delay", '\0', 0, G_OPTION_ARG_INT, &purge_delay, N_("Millisecond period spent waiting for clients to register (0=forever)"), N_("DELAY")},
  {"warn-delay", '\0', 0, G_OPTION_ARG_INT, &warn_delay, N_("Millisecond period spent waiting for clients to respond (0=forever)"), N_("DELAY")},
  {"suicide-delay", '\0', 0, G_OPTION_ARG_INT, &suicide_delay, N_("Millisecond period spent waiting for clients to die (0=forever)"), N_("DELAY")},
  {NULL}
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
  char       *short_lang;
  char       *p;

  gdm_lang = g_getenv("GDM_LANG");
  if (gdm_lang)
    {
      short_lang = g_strdup (gdm_lang);
      p = strchr(short_lang, '_');
      if (p)
	*p = '\0';

      g_setenv ("LANG", gdm_lang, TRUE);

      /* g_setenv ("LANGUAGE", short_lang, TRUE); */
      /* g_setenv ("LC_ALL", gdm_lang, TRUE); */

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

  env_string = g_strdup_printf ("%s/gtk/gtkrc:%s/.gtkrc-1.2-gnome2",
                                SYSCONFDIR, g_get_home_dir ());
  g_setenv ("GTK_RC_FILES", env_string, TRUE);
  g_free (env_string);
}

#define ROOTSESSION_RESPONSE_CONTINUE 1
#define ROOTSESSION_RESPONSE_QUIT 3
static gboolean
gsm_check_for_root (void)
{
  GtkWidget *dlg;
  gint       response;

  if (geteuid () != 0)
    return FALSE;

  dlg = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
                                GTK_BUTTONS_NONE,
                                _("This session is running as a privileged user"));
  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dlg),
                                            _("Running a session as a privileged user should be avoided for security reasons. If possible, you should log in as a normal user."));

  /* FIXME: would be nice to have a icon for Continue */
  gtk_dialog_add_buttons (GTK_DIALOG (dlg),
                          _("_Continue"), ROOTSESSION_RESPONSE_CONTINUE,
                          GTK_STOCK_QUIT, ROOTSESSION_RESPONSE_QUIT,
                          NULL);
  gtk_dialog_set_default_response (GTK_DIALOG (dlg),
                                   ROOTSESSION_RESPONSE_QUIT);
  gtk_window_set_title (GTK_WINDOW (dlg), "");
  gtk_window_set_position (GTK_WINDOW (dlg), GTK_WIN_POS_CENTER);

  response = gtk_dialog_run (GTK_DIALOG (dlg));
  gtk_widget_destroy (dlg);

  return !(response == ROOTSESSION_RESPONSE_CONTINUE);
}

#define FALLBACK_TIME_UTILITY "time-admin"
#define CLOCKSESSION_RESPONSE_IGNORE 1
#define CLOCKSESSION_RESPONSE_ADJUST 3

struct check_clock {
  GtkWidget  *dialog;
  char      **argv_tool;
  guint       timeout_id;
  guint       child_id;
};

static gboolean
gsm_check_time (void)
{
  time_t now;

  now = time (NULL);
  /* accept a one week error: no need to be too strict */
  return (now > GNOME_COMPILE_TIME - 3600*24*7);
}

static void
gsm_check_clock_set_label (GtkWidget *dialog)
{
  char       date[64];
  char      *text;
  time_t     now;
  struct tm *tim;

  now = time (NULL);
  tim = localtime (&now);
  strftime (date, sizeof (date), "%x", tim);

  text = g_markup_printf_escaped (_("The session might encounter issues if the computer clock is not properly configured. Please consider adjusting it.\n\nCurrent date is <b>%s</b>."), date);
  gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog),
                                              text);
  g_free (text);
}

static gboolean
gsm_check_clock_config_tool_exists (struct check_clock *check,
                                    const char         *config_tool)
{
  char **argv;
  char  *path;

  if (!config_tool || config_tool[0] == '\0')
    return FALSE;

  argv = NULL;
  if (!g_shell_parse_argv (config_tool, NULL, &argv, NULL))
    return FALSE;

  if (!(path = g_find_program_in_path (argv [0]))) {
    g_strfreev (argv);
    return FALSE;
  }

  g_free (path);

  check->argv_tool = argv;

  return TRUE;
}

static void
gsm_check_clock_main_quit (struct check_clock *check)
{
  if (check->dialog)
    gtk_widget_destroy (check->dialog);
  check->dialog = NULL;

  if (check->argv_tool)
    g_strfreev (check->argv_tool);
  check->argv_tool = NULL;

  if (check->timeout_id)
    g_source_remove (check->timeout_id);
  check->timeout_id = 0;

  if (check->child_id)
    g_source_remove (check->child_id);
  check->child_id = 0;

  gtk_main_quit ();
}

static gboolean
gsm_check_clock_update (gpointer data)
{
  struct check_clock *check;

  check = (struct check_clock *) data;

  if (gsm_check_time ())
    {
      gsm_check_clock_main_quit (check);
      return FALSE;
    }

  if (check->dialog)
    gsm_check_clock_set_label (check->dialog);

  return TRUE;
}

static void
gsm_check_clock_tool_exited (GPid     pid,
                             gint     status,
                             gpointer data)
{
  gsm_check_clock_main_quit ((struct check_clock *) data);

  g_spawn_close_pid (pid);
}

static void
gsm_check_clock_response (GtkWidget          *dialog,
                          int                 response,
                          struct check_clock *check)
{
  gtk_widget_destroy (dialog);
  check->dialog = NULL;

  if (response == CLOCKSESSION_RESPONSE_ADJUST)
    {
      GtkWidget *dialog;
      GPid       pid;
      GError    *err;

      err = NULL;

      if (g_spawn_async (NULL,
                         check->argv_tool,
                         NULL,
                         G_SPAWN_SEARCH_PATH|G_SPAWN_DO_NOT_REAP_CHILD,
                         NULL,
                         NULL,
                         &pid,
                         &err))
        {
          check->child_id = g_child_watch_add (pid,
                                               gsm_check_clock_tool_exited,
                                               check);
          return;
        }

	dialog = gtk_message_dialog_new (NULL,
                                         GTK_DIALOG_MODAL,
					 GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_OK,
					 _("Failed to launch time configuration tool: %s"),
					 err->message);
	g_error_free (err);
	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);

        gtk_dialog_run (GTK_DIALOG (dialog));

        gsm_check_clock_main_quit (check);
    }
  else
    gsm_check_clock_main_quit (check);

}

static void
gsm_check_clock (void)
{
  struct check_clock check;

  if (gsm_check_time ())
    return;

  check.child_id = 0;

  if (!gsm_check_clock_config_tool_exists (&check, TIME_UTILITY) &&
      !gsm_check_clock_config_tool_exists (&check, FALLBACK_TIME_UTILITY))
    check.argv_tool = NULL;

  check.dialog = gtk_message_dialog_new (NULL,
                                         GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
                                         GTK_BUTTONS_NONE,
                                         _("The computer clock appears to be wrong"));
  gsm_check_clock_set_label (check.dialog);

  if (check.argv_tool)
    {
      /* FIXME: would be nice to have a icons for the buttons */
      gtk_dialog_add_buttons (GTK_DIALOG (check.dialog),
                              _("_Ignore"), CLOCKSESSION_RESPONSE_IGNORE,
                              _("_Adjust the Clock"), CLOCKSESSION_RESPONSE_ADJUST,
                              NULL);
      gtk_dialog_set_default_response (GTK_DIALOG (check.dialog),
                                       CLOCKSESSION_RESPONSE_ADJUST);
    }
  else
    {
      gtk_dialog_add_button (GTK_DIALOG (check.dialog),
                             GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE);
    }

  gtk_window_set_title (GTK_WINDOW (check.dialog), "");
  gtk_window_set_position (GTK_WINDOW (check.dialog), GTK_WIN_POS_CENTER);

  g_signal_connect (check.dialog, "response",
                    G_CALLBACK (gsm_check_clock_response), &check);

  gtk_widget_show (check.dialog);

  check.timeout_id = g_timeout_add (1000 * 60,
                                    gsm_check_clock_update, &check);

  gtk_main ();
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

      if (!result->ai_canonname)
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
	break;
    }

    if (tmp_msgbox)
	gtk_widget_destroy (tmp_msgbox);
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
  Session *the_session;
  gboolean splashing;
  const char *env_a_t_support;
  gboolean a_t_support;
  GError *err;
  int status;
  char *display_str;
  char **versions;
  GConfClient *gconf_client;
  GOptionContext *goption_context;
  gboolean dbus_daemon_owner;
  
  if (g_getenv ("GSM_VERBOSE_DEBUG"))
    gsm_set_verbose (TRUE);

  /* Help eradicate the critical warnings in unstable releases of GNOME */
  versions = g_strsplit (VERSION, ".", 3);
  if (versions && versions [0] && versions [1])
    {
      int major;
      major = atoi (versions [1]);
      if ((major % 2) != 0)
	{
          g_setenv ("G_DEBUG", "fatal_criticals", FALSE);
	  g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL);
	}
    }
  g_strfreev (versions);
      
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

  gtk_window_set_default_icon_name ("session-properties");

  if (gsm_check_for_root ())
    return 0;

  /* We need to do this as early as possible */
  gsm_set_display_properties ();
  
  if (ORBit_proto_use ("IPv4") || ORBit_proto_use ("IPv6"))
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
  
  /* Read the config option early, so that we know if a11y is set or not */ 
  gconf_client = gsm_get_conf_client ();
  gconf_client_add_dir (gconf_client, GSM_GCONF_CONFIG_PREFIX, GCONF_CLIENT_PRELOAD_ONELEVEL, NULL); 

  env_a_t_support = g_getenv (ACCESSIBILITY_ENV);
  if (env_a_t_support)
    a_t_support = atoi (env_a_t_support);
  else
    a_t_support = gconf_client_get_bool (gconf_client, ACCESSIBILITY_KEY, NULL);

  if (a_t_support)
    {
      gsm_assistive_registry_start ();
      gsm_at_set_gtk_modules ();
    }

  goption_context = g_option_context_new (N_("- Manage the GNOME session"));
  g_option_context_set_translation_domain (goption_context, GETTEXT_PACKAGE);
  g_option_context_add_main_entries (goption_context, options, GETTEXT_PACKAGE);

  gnome_program_init("gnome-session", VERSION, LIBGNOMEUI_MODULE, argc, argv, 
		  GNOME_PARAM_GOPTION_CONTEXT, goption_context,
		  NULL);

 /* FIXME: it would be nice to skip this check if we're debugging the
 session manager. */

  if (GNOME_CLIENT_CONNECTED (gnome_master_client ()))
  {
    fprintf (stderr, "gnome-session: you're already running a session manager\n");
    exit (1);
  }

  initialize_ice ();
  fprintf (stderr, "SESSION_MANAGER=%s\n", g_getenv ("SESSION_MANAGER"));

  /* Make sure children see the right value for DISPLAY.  This is
     useful if --display was specified on the command line.  */
  display_str = gdk_get_display ();
  g_setenv ("DISPLAY", display_str, TRUE);
  g_free (display_str);

  ignore (SIGPIPE);

  /* Need DISPLAY set */
  gsm_keyring_daemon_start ();
  dbus_daemon_owner = gsm_dbus_daemon_start ();
  gsm_gsd_start ();

  /* FIXME is it useful to do it so late? gsd migt not work, eg */
  gsm_check_clock ();
  
  /* Read the rest of config options */  

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
   
  if (failsafe) {
	  if (session_name)
		  g_free (session_name);
	  session_name = g_strdup (FAILSAFE_SESSION);
  }

  g_setenv ("GNOME_DESKTOP_SESSION_ID", session_name, TRUE);
  the_session = read_session (session_name);

  gsm_sound_login ();

  gsm_remote_desktop_start ();

  if (splashing)
    splash_start ();

  start_session (the_session);

  gtk_main ();

  gsm_remote_desktop_cleanup ();

  gsm_sound_logout ();

  gsm_keyring_daemon_stop ();

  if (dbus_daemon_owner)
    gsm_dbus_daemon_stop ();

  g_object_unref (gconf_client);
  gsm_shutdown_gconfd ();

  clean_ice ();

  g_free (session_name);

  return 0;
}
