#include <config.h>

#include "gsm-gsd.h"
#include "util.h"

#include <time.h>

#include <gtk/gtkmessagedialog.h>
#include <libbonobo.h>

typedef struct {
  CORBA_Object gsd_object;
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
  g_object_add_weak_pointer (G_OBJECT (dialog), &dialog);
  g_string_free (msg, TRUE);

  g_signal_connect (dialog, "response",
		    G_CALLBACK (gtk_widget_destroy),
		    NULL);
  gtk_widget_show (dialog);
}

static void
broken_cb (LINCConnection *cnx, GnomeSettingsData *gsd)
{
  g_return_if_fail (gsd != NULL);
  g_return_if_fail (gsd->gsd_object != CORBA_OBJECT_NIL);
  g_return_if_fail (gsd->activating == FALSE);

  gsm_verbose ("broken_cb()");

  gsd->gsd_object = CORBA_OBJECT_NIL;

  gsm_gsd_start ();
}

static void
activate_cb (Bonobo_Unknown     object,
	     CORBA_Environment *ev,
	     gpointer           user_data)
{
  GnomeSettingsData *gsd = user_data;
  
  gsm_verbose ("activate_cb(): object: %p", object);

  g_return_if_fail (gsd != NULL);
  g_return_if_fail (gsd->gsd_object == CORBA_OBJECT_NIL);
  g_return_if_fail (gsd->activating == TRUE);

  gsd->activating = FALSE;
  gsd->gsd_object = object;

  if (BONOBO_EX (ev))
    {
      gsd_set_error (gsd, bonobo_exception_general_error_get (ev));
      gsm_gsd_start ();
      return;
    }
  else if (object == CORBA_OBJECT_NIL)
    {
      gsd_set_error (gsd, _("There was an unknown activation error."));
      gsm_gsd_start ();
      return;
    }

  gsd_set_error (gsd, NULL);
  ORBit_small_listen_for_broken (object, G_CALLBACK (broken_cb), gsd);
}

void
gsm_gsd_start (void)
{
  static GnomeSettingsData gsd = { CORBA_OBJECT_NIL };
  CORBA_Environment ev;
  time_t now;

  gsm_verbose ("gsm_gsd_start(): starting");

  if (gsd.activating)
    return;
  
  if (gsd.gsd_object)
    {
      /* this shouldn't happen */
      if (ORBIT_CONNECTION_CONNECTED == 
	  ORBit_small_get_connection_status (gsd.gsd_object))
	return;
      
      gsm_warning ("disconnected...");
      gsd.gsd_object = CORBA_OBJECT_NIL;
    }

  /* stolen from manager.c:client_clean_up() */
  now = time (NULL);
  if (now > gsd.start_time + 120)
    {
      gsd.attempts = 0;
      gsd.start_time = now;
    }

  if (gsd.attempts++ > 10)
    {
      gsd_error_dialog (&gsd, _("The Settings Daemon restarted too many times."));
      return;
    }

  gsd.activating = TRUE;

  CORBA_exception_init (&ev);

  bonobo_get_object_async ("OAFIID:GNOME_SettingsDaemon",
			   "IDL:Bonobo/Unknown:1.0",
			   &ev,
			   activate_cb,
			   &gsd);

  if (BONOBO_EX (&ev))
    {
      gsd_set_error (&gsd, bonobo_exception_general_error_get (&ev));
      gsd_error_dialog (&gsd, NULL);
    }

  CORBA_exception_free (&ev);
}
