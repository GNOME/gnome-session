#include <config.h>
#include <string.h>

#include "gsm-at-startup.h"
#include "util.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gconf/gconf-client.h>

static Atom AT_SPI_IOR;

static GdkFilterReturn 
gsm_assistive_filter_watch (GdkXEvent *xevent, GdkEvent *event, gpointer data){
     XEvent *xev = (XEvent *)xevent;

     if (xev->xany.type == PropertyNotify &&
	 xev->xproperty.atom == AT_SPI_IOR)
       {
          gtk_main_quit ();
	  
          return GDK_FILTER_REMOVE;
       }

     return GDK_FILTER_CONTINUE;
}

static void
gsm_assistive_error_dialog (void)
{
     GtkWidget *dialog = gtk_message_dialog_new (NULL,
						 GTK_DIALOG_MODAL,
						 GTK_MESSAGE_ERROR,
						 GTK_BUTTONS_OK,
						 _("Assistive technology support has been requested for this session, but the accessibility registry was not found. Please ensure that the AT-SPI package is installed. Your session has been started without assistive technology support."));
     gtk_dialog_run (GTK_DIALOG (dialog));
     gtk_widget_destroy (dialog);     
}

void
gsm_assistive_registry_start (void)
{
     GdkWindow *w = gdk_get_default_root_window (); 
     gchar *command;
 
     if (!AT_SPI_IOR)
       AT_SPI_IOR = XInternAtom (GDK_DISPLAY (), "AT_SPI_IOR", False); 

     command = g_strdup (AT_SPI_REGISTRYD_DIR "/at-spi-registryd");

     gdk_window_set_events (w, GDK_PROPERTY_CHANGE_MASK);
     gsm_exec_command_line_async (command, NULL);
     gdk_window_add_filter (w, gsm_assistive_filter_watch, NULL);

     gtk_main ();

     gdk_window_remove_filter (w, gsm_assistive_filter_watch, NULL);

     g_free (command);
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

