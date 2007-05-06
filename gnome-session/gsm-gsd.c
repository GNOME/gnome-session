#include <config.h>

#include "gsm-gsd.h"
#include "util.h"

#include <time.h>
#include <glib/gi18n.h>

#include <dbus/dbus-glib-lowlevel.h>

#include <gtk/gtkmessagedialog.h>

#include <gnome-settings-daemon/gnome-settings-client.h>

typedef struct {
  DBusGProxy       *dbus_proxy;
  gboolean     activating;
  time_t       start_time;
  guint        attempts;
  char        *last_error;
} GnomeSettingsData;

static void
gsd_set_error (GnomeSettingsData *gsd, const char *error)
{
  g_free (gsd->last_error);
  gsd->last_error = g_strdup (error);
}

static void
gsd_error_dialog (GnomeSettingsData *gsd, const char *error)
{
  static GtkWidget *dialog = NULL;
  GString *msg;
  
  /*
   * it would be nice to have a dialog which either:
   *
   * 1.  lets you change the message on it
   * 2.  lets you append messages and has a "history"
   *
   * for now, we just kill the old dialog and pop up a new one.
   */

  msg = g_string_new (_("There was an error starting the GNOME Settings Daemon.\n\n"
			"Some things, such as themes, sounds, or background "
			"settings may not work correctly."));

  if (error)
    {
      g_string_append (msg, "\n\n");
      g_string_append (msg, error);
    }

  if (gsd->last_error)
    {
      g_string_append (msg, _("\n\nThe last error message was:\n\n"));
      g_string_append (msg, gsd->last_error);
      gsd_set_error (gsd, NULL);
    }

  g_string_append (msg, _("\n\nGNOME will still try to restart the Settings Daemon next time you log in."));

  /* if there's one from last time destroy it */
  if (dialog)
    gtk_widget_destroy (dialog);

  dialog = gtk_message_dialog_new (NULL, 0,
				   GTK_MESSAGE_ERROR,
				   GTK_BUTTONS_CLOSE,
				   "%s", msg->str);
  g_object_add_weak_pointer (G_OBJECT (dialog), (gpointer *) &dialog);
  g_string_free (msg, TRUE);

  g_signal_connect (dialog, "response",
		    G_CALLBACK (gtk_widget_destroy),
		    NULL);
  gtk_widget_show (dialog);
}

static void
name_owner_changed (DBusGProxy *proxy,
                    const char *name,
                    const char *prev_owner,
                    const char *new_owner,
                    GnomeSettingsData *gsd)
{
  if (!g_ascii_strcasecmp (name, "org.gnome.SettingsDaemon"))
    {
      /* gsd terminated */
      if (!g_ascii_strcasecmp ("", new_owner))
        {
	  if (!gsd->activating)
            {
              gsm_verbose ("gnome-settings-daemon terminated\n");
              g_return_if_fail (gsd != NULL);
	      /* Previous attempts failed */
	      if (gsd->dbus_proxy == NULL)
                return;

              gsd->dbus_proxy = NULL;

              gsm_gsd_start ();
            }
	}
      else
        {
          gsm_verbose ("gnome-settings-daemon started\n");
          gsd->activating = FALSE;
	}
    }
}

void
gsm_gsd_start (void)
{
  static GnomeSettingsData gsd = { NULL };
  time_t now;
  DBusGConnection *connection;
  GError *error = NULL;

  gsm_verbose ("gsm_gsd_start(): starting\n");

  if (gsd.activating)
    return;

  if (gsd.dbus_proxy)
    {
      gsm_warning ("disconnected...\n");
      gsd.dbus_proxy = NULL;
    }

  /* stolen from manager.c:client_clean_up() */
  now = time (NULL);
  gsm_verbose ("%ld secs since last attempt\n", now - gsd.start_time);
  if (now > gsd.start_time + 120)
    {
      gsd.attempts = 0;
      gsd.start_time = now;
    }

  gsm_verbose ("%d/10 attempts done\n", gsd.attempts);
  if (gsd.attempts++ > 10)
    {
      gsd_error_dialog (&gsd, _("The Settings Daemon restarted too many times."));
      return;
    }

  gsd.activating = TRUE;

  connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
  if (connection == NULL)
    {
      gsd_set_error (&gsd, error->message);
      gsd_error_dialog (&gsd, NULL);
      g_error_free (error);
    }
  else
    {
      dbus_connection_set_exit_on_disconnect (dbus_g_connection_get_connection (connection),
                                              FALSE);

      gsd.dbus_proxy = dbus_g_proxy_new_for_name (connection,
                                                  "org.gnome.SettingsDaemon",
                                                  "/org/gnome/SettingsDaemon",
                                                  "org.gnome.SettingsDaemon");

      if (gsd.dbus_proxy == NULL)
        {
          gsd_set_error (&gsd, "Could not obtain DBUS proxy");
          gsd_error_dialog (&gsd, NULL);
        } 
      else
        {
          if (!org_gnome_SettingsDaemon_awake(gsd.dbus_proxy, &error))
            {
              /* Method failed, the GError is set, let's warn everyone */
              gsd_set_error (&gsd, error->message);
              gsd_error_dialog (&gsd, NULL);
              g_error_free (error);
            }
          else
            {
              DBusGProxy *dbusService;
              dbusService = dbus_g_proxy_new_for_name (connection,
                                                       DBUS_SERVICE_DBUS,
                                                       DBUS_PATH_DBUS,
                                                       DBUS_INTERFACE_DBUS);

              dbus_g_proxy_add_signal (dbusService,
                                       "NameOwnerChanged",
                                       G_TYPE_STRING,
                                       G_TYPE_STRING,
                                       G_TYPE_STRING,
                                       G_TYPE_INVALID);

              dbus_g_proxy_connect_signal (dbusService,
                                           "NameOwnerChanged",
                                           G_CALLBACK (name_owner_changed),
                                           &gsd,
                                           NULL);
            }
        }
    }
}
