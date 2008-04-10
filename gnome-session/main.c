/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * main.c: gnome-session startup
 *
 * Copyright (C) 2006 Novell, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libintl.h>
#include <signal.h>
#include <stdlib.h>

#include <glib/gi18n.h>
#include <glib/goption.h>
#include <gtk/gtkmain.h>
#include <gtk/gtkmessagedialog.h>

#include "dbus.h"
#include "gconf.h"
#include "gsm.h"
#include "session.h"
#include "xsmp.h"

GsmSession *global_session;

static gboolean failsafe;

static GOptionEntry entries[] = {
  { "failsafe", 'f', 0, G_OPTION_ARG_NONE, &failsafe,
    N_("Do not load user-specified applications"),
    NULL },
  { NULL, 0, 0, 0, NULL, NULL, NULL }
};

/**
 * gsm_initialization_error:
 * @fatal: whether or not the error is fatal to the login session
 * @format: printf-style error message format
 * @...: error message args
 *
 * Displays the error message to the user. If @fatal is %TRUE, gsm
 * will exit after displaying the message.
 *
 * This should be called for major errors that occur before the
 * session is up and running. (Notably, it positions the dialog box
 * itself, since no window manager will be running yet.)
 **/
void
gsm_initialization_error (gboolean fatal, const char *format, ...)
{
  GtkWidget *dialog;
  char *msg;
  va_list args;

  va_start (args, format);
  msg = g_strdup_vprintf (format, args);
  va_end (args);

  /* If option parsing failed, Gtk won't have been initialized... */
  if (!gdk_display_get_default ())
    {
      if (!gtk_init_check (NULL, NULL))
	{
	  /* Oh well, no X for you! */
	  g_printerr (_("Unable to start login session (and unable connect to the X server)"));
	  g_printerr (msg);
	  exit (1);
	}
    }

  dialog = gtk_message_dialog_new (NULL, 0, GTK_MESSAGE_ERROR,
				   GTK_BUTTONS_CLOSE, "%s", msg);

  g_free (msg);
  
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);
  gtk_dialog_run (GTK_DIALOG (dialog));

  gtk_widget_destroy (dialog);

  /* Blah blah blah etc FIXME */

  /* FIXME: shutdown gconf, dbus, etc? */
}

int
main (int argc, char **argv)
{
  struct sigaction sa;
  GError *err = NULL;
  char *display_str;

  bindtextdomain (GETTEXT_PACKAGE, LOCALE_DIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  sa.sa_handler = SIG_IGN;
  sa.sa_flags = 0;
  sigemptyset (&sa.sa_mask);
  sigaction (SIGPIPE, &sa, 0);

  gtk_init_with_args (&argc, &argv,
		      _(" - the GNOME session manager"),
		      entries, /* FIXME GETTEXT_PACKAGE */ NULL,
		      &err);
  if (err)
    gsm_initialization_error (TRUE, "%s", err->message);

  /* Set DISPLAY explicitly for all our children, in case --display
   * was specified on the command line.
   */
  display_str = gdk_get_display ();
  g_setenv ("DISPLAY", display_str, TRUE);
  g_free (display_str);

  /* Start up gconfd and dbus-daemon (in parallel) if they're not
   * already running. This requires us to initialize XSMP too, because
   * we want $SESSION_MANAGER to be set before launching dbus-daemon.
   */
  gsm_gconf_init ();
  gsm_xsmp_init ();
  gsm_dbus_init ();

  /* Now make sure they succeeded. (They'll call
   * gsm_initialization_error() if they failed.)
   */
  gsm_gconf_check ();
  gsm_dbus_check ();

  global_session = gsm_session_new (failsafe);

  gsm_xsmp_run ();
  gsm_dbus_run ();

  gsm_session_start (global_session);

  gtk_main ();
  //gsm_session_free (session);

  return 0;
}
