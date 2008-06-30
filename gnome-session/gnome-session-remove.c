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

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libgnome/gnome-program.h>
#include <libgnomeui/gnome-client.h>
#include <libgnomeui/gnome-ui-init.h>

#include "gsm-protocol.h"

static GSList   *clients;
static gboolean  do_list;

/* The number of names of programs we've seen.  */
static int program_count;

/* The names of the programs we're looking to remove.  */
static char **program_names;

/* A count of the number of client programs we are trying to
   remove.  */
static int remove_count;

static const GOptionEntry gsm_remove_opts[] = {
  { "list", 0, 0, G_OPTION_ARG_NONE, &do_list,
    N_("List registered clients, then exit"), NULL },
  { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY,
    &program_names, NULL, N_("PROGRAM...") },
  {NULL}
};

static GsmClient *
client_factory (gpointer user_data)
{
  GsmClient *client;

  client = g_object_new (GSM_TYPE_CLIENT, NULL);

  clients = g_slist_prepend (clients, client);

  return client;
}

static void
client_removed (void)
{
  if (--remove_count == 0)
    gtk_main_quit ();
}

static int
find_program (char *name)
{
  if (name != NULL)
    {
      int i;
      for (i = 0; i < program_count; ++i)
	if (program_names[i] && ! strcmp (name, program_names[i]))
	  return i;
    }
  return -1;
}

static void
session_initialized (GsmSession *session, void *ignore)
{
  GSList *l;
  int should_stop = 1;

  for (l = clients; l; l = l->next)
    {
      GsmClient *client = l->data;
      char *client_program = NULL;

      if (client->program != NULL)
        client_program = g_strdup (client->program);
      else if (client->command != NULL)
        {
          char **argv = NULL;
          int argc;

          if (g_shell_parse_argv (client->command, &argc, &argv, NULL))
            {
              client_program = g_strdup (argv[0]);
            }

          if (argv)
            g_strfreev (argv);
        }

      if (do_list)
	{
	  if (client_program
	      && strcmp (client_program, "gnome-session-remove"))
	    g_print ("  %s\n", client_program);
	}
      else
	{
	  int index = find_program (client_program);
	  if (index > -1)
	    {
	      should_stop = 0;
	      ++remove_count;
	      program_names[index] = NULL;
	      g_print ("Removing '%s' from the session\n", client_program);
	      gsm_client_commit_remove (client);
	      g_signal_connect (client, "remove",
				G_CALLBACK (client_removed), NULL);
	    }
	}

      if (client_program)
        g_free (client_program);
    }

  if (should_stop)
    gtk_main_quit ();
}

int
main (int argc, char **argv)
{
  GnomeClient *client;
  GsmProtocol *protocol;
  GsmSession  *session;
  GOptionContext *option_context;

  bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  option_context = g_option_context_new ("");
  g_option_context_add_main_entries (option_context, gsm_remove_opts,
				     GETTEXT_PACKAGE);

  gnome_program_init ("gnome-session-remove",
		      VERSION, LIBGNOMEUI_MODULE,
                      argc, argv,
		      GNOME_PARAM_GOPTION_CONTEXT, option_context,
		      GNOME_PROGRAM_STANDARD_PROPERTIES,
                      GNOME_PARAM_NONE);

  if (program_names)
    program_names = g_strdupv (program_names);
  for (program_count = 0;
       program_names && program_names[program_count];
       ++program_count)
    ;

  if (!do_list && program_count < 1)
    {
      g_printerr (_("You must specify at least one program to remove. You can list the programs with --list.\n"));
      return 1;
    }

  client = gnome_master_client ();
  if (!GNOME_CLIENT_CONNECTED (client))
    {
      g_printerr (_("Error: could not connect to the session manager\n"));
      return 1;
    }

  gnome_client_set_restart_style (client, GNOME_RESTART_NEVER);

  protocol = gsm_protocol_new (client);
  gsm_protocol_get_current_session (GSM_PROTOCOL (protocol));

  session = gsm_session_live ((GsmClientFactory) client_factory, NULL);
  g_signal_connect (session, "initialized",
		    G_CALLBACK (session_initialized), NULL);

  if (do_list)
    g_print (_("Currently registered clients:\n"));

  gtk_main ();

  if (! do_list)
    {
      int i;
      for (i = 0; i < program_count; ++i)
	{
	  if (program_names[i])
	    g_print (_("Couldn't find program %s in session\n"),
		     program_names[i]);
	}
    }

  return 0;
}
