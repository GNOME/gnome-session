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

#include "libgnome/libgnome.h"
#include "libgnomeui/libgnomeui.h"

#include "manager.h"
#include "gsm-protocol.h"

#include <X11/SM/SMlib.h>

/* True if killing.  */
static int zap = 0;

/* True if we should use dialog boxes */
static int gui = 0; 

/* True if the session manager is currently in trash mode */
static int save_trashing = FALSE;

static const struct poptOption options[] = {
  {"kill", '\0', POPT_ARG_NONE, &zap, 0, N_("Kill session"),     NULL},
  {"gui",  '\0', POPT_ARG_NONE, &gui, 0, N_("Use dialog boxes"), NULL},
  {NULL,   '\0', 0, NULL, 0}
};

static int exit_status = 0;

/* The protocol object for communicating with the session */
static GsmProtocol *protocol;

static void
ping_reply (IceConn ice_conn, IcePointer clientData)
{
  gtk_main_quit ();
}

static void
save_complete (GnomeClient* client, gpointer data)
{
  /* this seems to be called twice */
  static int been_here;
  if (been_here) return;
    been_here = TRUE;
    
  /* Set this back if we aren't shutting down */
  if (save_trashing && !zap)
    gsm_protocol_set_trash_mode (protocol, TRUE);

  exit_status = (data != NULL);

  /* We can't exit immediately, because the trash mode above
   * might be discarded. So we do the equivalent of an XSync.
   */
  IcePing (SmcGetIceConnection (protocol->smc_conn), ping_reply, NULL);
}

int
main (int argc, char *argv[])
{
  GnomeClient *client;
  GtkWidget *dialog;
  char *s;

  /* Initialize the i18n stuff */
  bindtextdomain (PACKAGE, GNOMELOCALEDIR);
  textdomain (PACKAGE);

  gnome_init_with_popt_table("save-session", VERSION, argc, argv, options, 0, NULL);

  client = gnome_master_client ();
  if (! GNOME_CLIENT_CONNECTED (client))
    {
      s = _("Could not connect to the session manager");
      if (gui)
	{
	  dialog = gnome_message_box_new (s, GNOME_MESSAGE_BOX_ERROR,
					  GNOME_STOCK_BUTTON_OK, NULL);
	  gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
	}
      else
	g_printerr ("%s\n", s);
      return 1;
    }

  protocol = (GsmProtocol *)gsm_protocol_new (client);
  if (!protocol)
    {
      s = _("Could not connect to gnome-session");
      if (gui)
	{
	  dialog = gnome_message_box_new (s, GNOME_MESSAGE_BOX_ERROR,
					  GNOME_STOCK_BUTTON_OK, NULL);
	  gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
	}
      else
	g_printerr ("%s\n", s);
      exit (1);
    }

  /* We make this call immediately, as a convenient way
   * of putting ourselves into command mode; if we
   * don't do this, then the "event loop" that
   * GsmProtocol creates will leak memory all over the
   * place.
   *
   * We ignore the resulting "last_session" signal.
   */
  gsm_protocol_get_last_session (GSM_PROTOCOL (protocol));
	
  gnome_config_push_prefix (GSM_OPTION_CONFIG_PREFIX);
  save_trashing = gnome_config_get_bool (TRASH_MODE_KEY "=" TRASH_MODE_DEFAULT);
  gnome_config_pop_prefix ();

  gnome_client_set_restart_style (client, GNOME_RESTART_NEVER);

  if (save_trashing)
    gsm_protocol_set_trash_mode (protocol, FALSE);

  /* We could expose more of the arguments to the user if we wanted
     to.  Some of them aren't particularly useful.  Interestingly,
     there is no way to request a shutdown without a save.  */
  gnome_client_request_save (client, GNOME_SAVE_BOTH, zap,
			     GNOME_INTERACT_ANY, 0, 1);

  /* Wait until our request is acknowledged:
   * gnome-session queues requests but does not honour them if the
   * requesting client is dead when the save starts. */
  gtk_signal_connect (GTK_OBJECT (client), "save_complete",
		      GTK_SIGNAL_FUNC (save_complete), NULL);
  gtk_signal_connect (GTK_OBJECT (client), "die",
		      GTK_SIGNAL_FUNC (save_complete), NULL);
  gtk_signal_connect (GTK_OBJECT (client), "shutdown_cancelled",
		      GTK_SIGNAL_FUNC (save_complete), (gpointer)1);

  gtk_main ();

  if (gui)
    {      
      dialog = gnome_message_box_new (_("Your session has been saved"),
				      GNOME_MESSAGE_BOX_INFO,
				      GNOME_STOCK_BUTTON_OK, NULL);
      gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
    }

  return exit_status;
}
