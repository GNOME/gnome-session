#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>

#include <libgnomeui/gnome-client.h>
#include <libgnomeui/gnome-help.h>
#include <libgnomeui/gnome-ui-init.h>

#include <gconf/gconf-client.h>

#include "ui.h"

int
main (int argc, char *argv[])
{
  GOptionContext *context;
  GConfClient *client;
  GnomeClient *master_client;

  bindtextdomain (GETTEXT_PACKAGE, LOCALE_DIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  context = g_option_context_new (" - GNOME Session Preferences");

  gnome_program_init ("gnome-session-properties", VERSION, 
		      LIBGNOMEUI_MODULE, argc, argv, 
		      GNOME_PARAM_GOPTION_CONTEXT, context,
		      NULL);

  /* Don't restart the capplet, it's the place where the session is saved. */
  gnome_client_set_restart_style (gnome_master_client (), GNOME_RESTART_NEVER);

  gtk_window_set_default_icon_name ("session-properties");

  client = gconf_client_get_default ();

  gconf_client_add_dir (client, 
                        SPC_GCONF_CONFIG_PREFIX, 
                        GCONF_CLIENT_PRELOAD_ONELEVEL, 
                        NULL);

  master_client = gnome_master_client ();

  if (!GNOME_CLIENT_CONNECTED (master_client)) 
    {
      g_printerr (_("Could not connect to the session manager\n"));
      return 1;
    }

  g_signal_connect (master_client, 
                    "die", 
                    G_CALLBACK (gtk_main_quit), 
                    NULL);

  spc_ui_build (client);

  gtk_main ();

  return 0;
}
