#include <config.h>

#include "gsm-at-startup.h"

#include <gdk/gdk.h>
#include <libgnome/libgnome.h>
#include <gconf/gconf-client.h>

#define AT_STARTUP_KEY    "/desktop/gnome/accessibility/startup/exec_ats"

static void
gsm_assistive_tech_exec (gchar *exec_string)
{
  GError *error = NULL;
  gchar *s = g_find_program_in_path (exec_string);
  if (s) {
    g_spawn_command_line_async (exec_string, &error);
    g_free (s);
  }
}

void 
gsm_assistive_technologies_start (void)
{
  GError *error = NULL;
  GConfClient *client;
  GSList *list;

  client  = gconf_client_get_default ();
  list = gconf_client_get_list (client, AT_STARTUP_KEY, GCONF_VALUE_STRING, &error);
  if (error)
    {
      g_warning ("Error getting value of " AT_STARTUP_KEY ": %s", error->message);
      g_error_free (error);
    }
  else
    {
      GSList *l;

      for (l = list; l; l = l->next)
	{
	  gsm_assistive_tech_exec ((char *) l->data);
	  g_free (l->data);
	}
      g_slist_free (list);
    }
  g_object_unref (client);
}
