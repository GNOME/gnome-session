/*
 * Copyright (C) 2003 Sun Microsystems, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author:
 *    Mark McLoughlin <mark@skynet.ie>
 */

#include <config.h>

#include <gtk/gtk.h>
#include <libgnome/gnome-program.h>
#include <libgnomeui/gnome-client.h>

#include "gsm-protocol.h"

static GSList   *clients;
static gboolean  do_list;

static GsmClient *
client_factory (void)
{
  GsmClient *client;

  client = g_object_new (GSM_TYPE_CLIENT, NULL);

  clients = g_slist_prepend (clients, client);

  return client;
}

static void
client_removed ()
{
  gtk_main_quit ();
}

static void
session_initialized (GsmSession *session,
		     const char *program)
{
  GSList *l;

  for (l = clients; l; l = l->next)
    {
      GsmClient *client = l->data;

      if (do_list && strcmp (client->program, "gnome-session-remove-client"))
	g_print ("  %s\n", client->program);

      if (program && !strcmp (client->program, program))
	{
	  gsm_client_commit_remove (client);
	  g_signal_connect (client, "remove",
			    G_CALLBACK (client_removed), NULL);
	}
    }

  if (!program)
    gtk_main_quit ();
}

int
main (int argc, char **argv)
{
  GnomeClient *client;
  GsmProtocol *protocol;
  GsmSession  *session;
  char        *program_name = NULL;

  bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  gnome_program_init ("gnome-session-remove-client",
		      VERSION, LIBGNOMEUI_MODULE,
                      argc, argv,
		      GNOME_PROGRAM_STANDARD_PROPERTIES,
                      NULL);

  if (argc != 2 && argc != 3)
    {
      fprintf (stderr, "usage: gnome-session-remove-client [--list] [<program>]\n");
      return 1;
    }

  --argc;
  ++argv;
  while (argc > 0)
    {
      if (!strcmp (argv [0], "--list"))
	do_list = TRUE;
      else
	program_name = g_strdup (argv [0]);
      --argc;
      ++argv;
    }

  client = gnome_master_client ();
  if (!GNOME_CLIENT_CONNECTED (client))
    {
      fprintf (stderr, "Error: could not connect to the session manager\n");

      return 1;
    }

  gnome_client_set_restart_style (client, GNOME_RESTART_NEVER);

  protocol = gsm_protocol_new (client);
  gsm_protocol_get_current_session (GSM_PROTOCOL (protocol));

  session = gsm_session_live ((GsmClientFactory) client_factory, NULL);
  g_signal_connect (session, "initialized",
		    G_CALLBACK (session_initialized), program_name);

  if (program_name)
    g_print ("Removing '%s' from the session\n", program_name);

  if (do_list)
    g_print ("Currently registered clients: \n");

  gtk_main ();

  g_free (program_name);

  return 0;
}
