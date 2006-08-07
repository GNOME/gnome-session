#include <config.h>
#include <string.h>

#include "gsm-at-startup.h"
#include "util.h"

#include <gdk/gdk.h>
#include <libgnome/libgnome.h>
#include <gconf/gconf-client.h>

#define AT_STARTUP_KEY    "/desktop/gnome/accessibility/startup/exec_ats"

static void
gsm_assistive_tech_exec (gchar *exec_string)
{
  gchar    *s;
  gboolean  success;
  
  success = FALSE;
  s = g_find_program_in_path (exec_string);

  if (s) {
    success = g_spawn_command_line_async (exec_string, NULL);
    g_free (s);
  }
  
  if (!success && !strcmp (exec_string, "gnopernicus")) {
    /* backwards compatibility for 2.14 */
    gsm_assistive_tech_exec ("orca");
  }
}

void 
gsm_assistive_technologies_start (void)
{
  GError *error = NULL;
  GConfClient *client;
  GSList *list;

  client  = gsm_get_conf_client ();
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
}

void
gsm_at_set_gtk_modules (void)
{
  GSList *modules_list;
  GSList *l;
  const char *old;
  char **modules;
  gboolean found_gail;
  gboolean found_atk_bridge;
  int n;

  n = 0;
  modules_list = NULL;
  found_gail = FALSE;
  found_atk_bridge = FALSE;

  if ((old = g_getenv ("GTK_MODULES")) != NULL)
    {
      modules = g_strsplit (old, ":", -1);
      for (n = 0; modules[n]; n++)
        {
          if (!strcmp (modules[n], "gail"))
            found_gail = TRUE;
          else if (!strcmp (modules[n], "atk-bridge"))
            found_atk_bridge = TRUE;

          modules_list = g_slist_prepend (modules_list, modules[n]);
          modules[n] = NULL;
        }
      g_free (modules);
    }

  if (!found_gail)
    {
      modules_list = g_slist_prepend (modules_list, "gail");
      ++n;
    }

  if (!found_atk_bridge)
    {
      modules_list = g_slist_prepend (modules_list, "atk-bridge");
      ++n;
    }

  modules = g_new (char *, n + 1);
  modules[n--] = NULL;
  for (l = modules_list; l; l = l->next)
    modules[n--] = g_strdup (l->data);

  g_setenv ("GTK_MODULES", g_strjoinv (":", modules), TRUE);
  g_strfreev (modules);
  g_slist_free (modules_list);
}

