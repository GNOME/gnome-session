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

/* True if killing.  */
static int zap = 0;

static const struct poptOption options[] = {
  {"kill", '\0', POPT_ARG_NONE, &zap, 0, N_("Kill session"), NULL},
  {NULL, '\0', 0, NULL, 0}
};

static int exit_status = 0;

static void
save_complete (GnomeClient* client, gpointer data)
{
  exit_status = (data != NULL);
  gtk_main_quit ();
}

int
main (int argc, char *argv[])
{
  GnomeClient *client;

  /* Initialize the i18n stuff */
  bindtextdomain (PACKAGE, GNOMELOCALEDIR);
  textdomain (PACKAGE);

  gnome_init_with_popt_table("save-session", VERSION, argc, argv, options, 0, NULL);

  client = gnome_master_client ();

  if (! GNOME_CLIENT_CONNECTED (client))
    {
      fprintf (stderr,
	       _("save-session: couldn't connect to session manager\n"));
      return 1;
    }

  gnome_client_set_restart_style (client, GNOME_RESTART_NEVER);
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

  return exit_status;
}
