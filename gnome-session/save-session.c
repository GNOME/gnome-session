/* save-session.c - Small program to talk to session manager.

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
#include <string.h>
#include <gtk/gtk.h>

#include <libgnome/libgnome.h>
#include <libgnomeui/libgnomeui.h>

#include <libgnomeui/gnome-window-icon.h>

#include <X11/SM/SMlib.h>

/* True if killing.  */
static gboolean zap = FALSE;

/* True if we should use dialog boxes */
static gboolean gui = FALSE; 

static IceConn ice_conn = NULL;

static const struct poptOption options[] = {
  {"kill", '\0', POPT_ARG_NONE, &zap, 0, N_("Kill session"),     NULL},
  {"gui",  '\0', POPT_ARG_NONE, &gui, 0, N_("Use dialog boxes"), NULL},
  {NULL,   '\0', 0, NULL, 0}
};

static int exit_status = 0;

static void
ping_reply (IceConn ice_conn, IcePointer clientData)
{
  gtk_main_quit ();
}

static void
ice_ping (void)
{
  /* We can't exit immediately, because the trash mode above
   * might be discarded. So we do the equivalent of an XSync.
   */
  if (ice_conn)
    IcePing (ice_conn, ping_reply, NULL);
  else
    g_warning ("save complete, but we don't have an ice connection.");
}

static void
save_complete (GnomeClient* client, gpointer data)
{
  /* We could expose more of the arguments to the user if we wanted
     to.  Some of them aren't particularly useful.  Interestingly,
     there is no way to request a shutdown without a save.  */
  gnome_client_request_save (client, GNOME_SAVE_BOTH, zap,
			     GNOME_INTERACT_ANY, 0, 1);
  
  
  ice_ping ();
}

static void
die_cb (GnomeClient *client, gpointer data)
{
  ice_ping ();
}

static void
cancelled_cb (GnomeClient *client, gpointer data)
{
  ice_ping ();

}

static void
display_error (const char *message)
{
  if (gui) 
    {
      GtkWidget *dialog;
      
      dialog = gtk_message_dialog_new (NULL, 0, GTK_MESSAGE_ERROR,
				       GTK_BUTTONS_OK, message);
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
    } else
      g_printerr ("%s\n", message);
}


/*
 * we do this so we can pick up our IceConnection to do a ping on
 */
static void
ice_connection_watch (IceConn connection, IcePointer client_data,
		      Bool opening, IcePointer *watch_data)
{
  if (opening)
    {
      if (!ice_conn)
	ice_conn = connection;
      else
	g_message ("Second ICE connection opened: ignoring.");
    }
  else
    {
      if (ice_conn == connection)
	  ice_conn = NULL;
      else
	g_message ("Second ICE connection closed: ignoring.");
    }
}

int
main (int argc, char *argv[])
{
  GnomeClient *client;

  /* Initialize the i18n stuff */
  bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);


  IceAddConnectionWatch (ice_connection_watch, NULL);

  gnome_program_init ("gnome-session-save", VERSION, LIBGNOMEUI_MODULE,
		      argc, argv,
		      GNOME_PARAM_POPT_TABLE, options,
		      GNOME_PROGRAM_STANDARD_PROPERTIES,
		      NULL);

  gnome_window_icon_set_default_from_file (GNOME_ICONDIR"/mc/i-floppy.png");

  client = gnome_master_client ();
  if (!GNOME_CLIENT_CONNECTED (client))
    {
      display_error (_("Could not connect to the session manager"));

      return 1;
    }
	
  gnome_client_set_restart_style (client, GNOME_RESTART_NEVER);

  /* Wait until our request is acknowledged:
   * gnome-session queues requests but does not honour them if the
   * requesting client is dead when the save starts. */
  g_signal_connect (client, "save_complete",
		    G_CALLBACK (save_complete), GINT_TO_POINTER (1));
  g_signal_connect (client, "die",
		    G_CALLBACK (die_cb), GINT_TO_POINTER (2));
  g_signal_connect (client, "shutdown_cancelled",
		    G_CALLBACK (cancelled_cb), GINT_TO_POINTER (3));

  gtk_main ();

  return exit_status;
}
