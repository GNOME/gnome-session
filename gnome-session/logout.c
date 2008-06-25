/* logout-dialog.c
 * Copyright (C) 2006 Vincent Untz
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Authors:
 *      Vincent Untz <vuntz@gnome.org>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include <gtk/gtkimage.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkstock.h>
#include <gdk/gdkx.h>

#include "gsm.h"
#include "session.h"
#include "logout.h"
#include "power-manager.h"
#include "consolekit.h"
#include "gdm.h"

#define GSM_LOGOUT_DIALOG_GET_PRIVATE(o) \
        (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSM_TYPE_LOGOUT_DIALOG, GsmLogoutDialogPrivate))

#define AUTOMATIC_ACTION_TIMEOUT 60

#define GSM_ICON_LOGOUT   "gnome-logout"
#define GSM_ICON_SHUTDOWN "gnome-shutdown"

struct _GsmLogoutDialogPrivate 
{
  GsmSessionLogoutType type;
  
  GsmPowerManager     *power_manager;
  GsmConsolekit       *consolekit;
  
  int                  timeout;
  unsigned int         timeout_id;
  
  unsigned int         default_response;
};

static GsmLogoutDialog *current_dialog = NULL;

static void gsm_logout_dialog_set_timeout  (GsmLogoutDialog *logout_dialog);

static void gsm_logout_dialog_destroy  (GsmLogoutDialog *logout_dialog,
                                        gpointer         data);

static void gsm_logout_dialog_show     (GsmLogoutDialog *logout_dialog,
                                        gpointer         data);

enum {
  PROP_0,
  PROP_MESSAGE_TYPE
};

G_DEFINE_TYPE (GsmLogoutDialog, gsm_logout_dialog, GTK_TYPE_MESSAGE_DIALOG);

static void 
gsm_logout_dialog_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  switch (prop_id) 
    {
    case PROP_MESSAGE_TYPE:
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void 
gsm_logout_dialog_get_property (GObject     *object,
                                guint        prop_id,
                                GValue      *value,
                                GParamSpec  *pspec)
{
  switch (prop_id) 
    {
    case PROP_MESSAGE_TYPE:
       g_value_set_enum (value, GTK_MESSAGE_WARNING);
       break;
    default:
       G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
       break;
    }
}

static void
gsm_logout_dialog_class_init (GsmLogoutDialogClass *klass)
{
  GObjectClass *gobject_class;
  
  gobject_class = G_OBJECT_CLASS (klass);
  
  /* This is a workaround to avoid a stupid crash: libgnomeui
   * listens for the "show" signal on all GtkMessageDialog and
   * gets the "message-type" of the dialogs. We will crash when
   * it accesses this property if we don't override it since we
   * didn't define it. */
  gobject_class->set_property = gsm_logout_dialog_set_property;
  gobject_class->get_property = gsm_logout_dialog_get_property;
  
  g_object_class_override_property (gobject_class,
                                    PROP_MESSAGE_TYPE,
                                    "message-type");
  
  g_type_class_add_private (klass, sizeof (GsmLogoutDialogPrivate));
}

static void
on_ck_request_completed (GsmConsolekit *gsm_consolekit,
                         GError          *error)
{
  if (error == NULL) 
    {
      /* request was successful */
      return;
    }
}

static void
gsm_logout_dialog_init (GsmLogoutDialog *logout_dialog)
{
  logout_dialog->priv = GSM_LOGOUT_DIALOG_GET_PRIVATE (logout_dialog);
  
  logout_dialog->priv->timeout_id = 0;
  logout_dialog->priv->timeout = 0;
  logout_dialog->priv->default_response = GTK_RESPONSE_CANCEL;
  
  gtk_window_set_skip_taskbar_hint (GTK_WINDOW (logout_dialog), TRUE);
  gtk_window_set_keep_above (GTK_WINDOW (logout_dialog), TRUE);
  gtk_window_stick (GTK_WINDOW (logout_dialog));
  
  logout_dialog->priv->power_manager = gsm_get_power_manager ();

  logout_dialog->priv->consolekit = gsm_get_consolekit ();

  g_signal_connect (logout_dialog->priv->consolekit,
                    "request-completed",
                    G_CALLBACK (on_ck_request_completed), 
                    NULL);
  
  g_signal_connect (logout_dialog, 
                    "destroy",
                    G_CALLBACK (gsm_logout_dialog_destroy), 
                    NULL);

  g_signal_connect (logout_dialog, 
                    "show",
                      G_CALLBACK (gsm_logout_dialog_show), 
                    NULL);
}

static void
gsm_logout_dialog_destroy (GsmLogoutDialog *logout_dialog,
                           gpointer         data)
{
  if (logout_dialog->priv->timeout_id != 0)
    {
      g_source_remove (logout_dialog->priv->timeout_id);
      logout_dialog->priv->timeout_id = 0;
    }  

  if (logout_dialog->priv->power_manager)
    {
      g_object_unref (logout_dialog->priv->power_manager);
      logout_dialog->priv->power_manager = NULL;
    }  

  if (logout_dialog->priv->consolekit)
    {
      g_object_unref (logout_dialog->priv->consolekit);
      logout_dialog->priv->consolekit = NULL;
    }  

  current_dialog = NULL;
}

static void
gsm_logout_dialog_show (GsmLogoutDialog *logout_dialog, gpointer user_data)
{
  gsm_logout_dialog_set_timeout (logout_dialog);
}

static gboolean
gsm_logout_dialog_timeout (gpointer data)
{
  GsmLogoutDialog *logout_dialog;
  char *secondary_text;
  const char *name;
  int seconds_to_show;
  
  logout_dialog = (GsmLogoutDialog *) data;
  
  if (!logout_dialog->priv->timeout) 
  {
    gtk_dialog_response (GTK_DIALOG (logout_dialog),
                         logout_dialog->priv->default_response);
    
    return FALSE;
  }
  
  if (logout_dialog->priv->timeout <= 30)
    {
      seconds_to_show = logout_dialog->priv->timeout;
    }
  else 
    {
      seconds_to_show = (logout_dialog->priv->timeout/10) * 10;

      if (logout_dialog->priv->timeout % 10)
        seconds_to_show += 10;
    }
  
  switch (logout_dialog->priv->type) 
    {
    case GSM_SESSION_LOGOUT_TYPE_LOGOUT:
      secondary_text = ngettext ("You are currently logged in as "
                                 "\"%s\".\n"
                                 "You will be automatically logged "
                                 "out in %d second.",
                                 "You are currently logged in as "
                                 "\"%s\".\n"
                                 "You will be automatically logged "
                                 "out in %d seconds.",
                                 seconds_to_show);
      break;

    case GSM_SESSION_LOGOUT_TYPE_SHUTDOWN:
      secondary_text = ngettext ("You are currently logged in as "
                                 "\"%s\".\n"
                                 "This system will be automatically "
                                 "shut down in %d second.",
                                 "You are currently logged in as "
                                 "\"%s\".\n"
                                 "This system will be automatically "
                                 "shut down in %d seconds.",
                                 seconds_to_show);
      break;

    default:
      g_assert_not_reached ();
    }

  name = g_get_real_name ();

  if (!name || name[0] == '\0')
    name = g_get_user_name ();
  
  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (logout_dialog),
                                            secondary_text,
                                            name,
                                            seconds_to_show,
                                            NULL);
  
  logout_dialog->priv->timeout--;
  
  return TRUE;
}

static void
gsm_logout_dialog_set_timeout (GsmLogoutDialog *logout_dialog)
{
  logout_dialog->priv->timeout = AUTOMATIC_ACTION_TIMEOUT;
  
  /* Sets the secondary text */
  gsm_logout_dialog_timeout (logout_dialog);
  
  if (logout_dialog->priv->timeout_id != 0)
    g_source_remove (logout_dialog->priv->timeout_id);
  
  logout_dialog->priv->timeout_id = g_timeout_add (1000,
                                                   gsm_logout_dialog_timeout,
                                                   logout_dialog);
}

static gboolean
vt_is_available (void)
{
  Display *xdisplay;
  GdkDisplay *gdisplay;
  Atom prop;

  gdisplay = gdk_display_get_default ();
  xdisplay = gdk_x11_display_get_xdisplay (gdisplay);

  prop = XInternAtom (xdisplay, "XFree86_VT", TRUE);

  return (prop == None ? FALSE : TRUE);
}

GtkWidget *
gsm_logout_get_dialog (GsmSessionLogoutType  type,
                       GdkScreen            *screen,
                       guint32               activate_time)
{
  GsmLogoutDialog *logout_dialog;
  const char *primary_text;
  const char *icon_name;
  
  if (current_dialog != NULL) 
    {
      gtk_widget_destroy (GTK_WIDGET (current_dialog));
    }
  
  logout_dialog = g_object_new (GSM_TYPE_LOGOUT_DIALOG, NULL);

  current_dialog = logout_dialog;
  
  gtk_window_set_title (GTK_WINDOW (logout_dialog), "");
  
  logout_dialog->priv->type = type;
  
  icon_name = NULL;
  primary_text = NULL;
  
  switch (type) 
    {
    case GSM_SESSION_LOGOUT_TYPE_LOGOUT:
      icon_name    = GSM_ICON_LOGOUT;
      primary_text = _("Log out of this system now?");

      logout_dialog->priv->default_response = GSM_LOGOUT_RESPONSE_LOGOUT;

      if (gdm_is_available () && vt_is_available ())
        gtk_dialog_add_button (GTK_DIALOG (logout_dialog),
                               _("_Switch User"),
                               GSM_LOGOUT_RESPONSE_SWITCH_USER);

      gtk_dialog_add_button (GTK_DIALOG (logout_dialog),
                             GTK_STOCK_CANCEL,
                             GTK_RESPONSE_CANCEL);

      gtk_dialog_add_button (GTK_DIALOG (logout_dialog),
                             _("_Log Out"),
                             GSM_LOGOUT_RESPONSE_LOGOUT);

      break;
    case GSM_SESSION_LOGOUT_TYPE_SHUTDOWN:
      icon_name    = GSM_ICON_SHUTDOWN;
      primary_text = _("Shut down this system now?");
 
      logout_dialog->priv->default_response = GSM_LOGOUT_RESPONSE_SHUTDOWN;

      if (gsm_power_manager_can_suspend (logout_dialog->priv->power_manager))
        gtk_dialog_add_button (GTK_DIALOG (logout_dialog),
                               _("S_uspend"),
                               GSM_LOGOUT_RESPONSE_STR);
 
      if (gsm_power_manager_can_hibernate (logout_dialog->priv->power_manager))
        gtk_dialog_add_button (GTK_DIALOG (logout_dialog),
                               _("_Hibernate"),
                               GSM_LOGOUT_RESPONSE_STD);
      
      if (gsm_logout_can_reboot ())
        gtk_dialog_add_button (GTK_DIALOG (logout_dialog),
                               _("_Restart"),
                               GSM_LOGOUT_RESPONSE_REBOOT);
      
      gtk_dialog_add_button (GTK_DIALOG (logout_dialog),
                             GTK_STOCK_CANCEL,
                             GTK_RESPONSE_CANCEL);
  
      if (gsm_logout_can_shutdown ())
        gtk_dialog_add_button (GTK_DIALOG (logout_dialog),
                               _("_Shut Down"),
                               GSM_LOGOUT_RESPONSE_SHUTDOWN);
      break;
    default:
      g_assert_not_reached ();
    }
  
  gtk_image_set_from_icon_name (GTK_IMAGE (GTK_MESSAGE_DIALOG (logout_dialog)->image),
                                icon_name, GTK_ICON_SIZE_DIALOG);
  
  gtk_label_set_text (GTK_LABEL (GTK_MESSAGE_DIALOG (logout_dialog)->label),
                      primary_text);
  
  gtk_dialog_set_default_response (GTK_DIALOG (logout_dialog),
                                   logout_dialog->priv->default_response);
  
  gtk_window_set_screen (GTK_WINDOW (logout_dialog), screen);

  return GTK_WIDGET (logout_dialog);
}

gboolean
gsm_logout_can_reboot ()
{
  GsmConsolekit *consolekit;
  gboolean ret;

  consolekit = gsm_get_consolekit ();

  ret = gsm_consolekit_can_restart (consolekit);

  if (!ret) 
    {
      ret = gdm_supports_logout_action (GDM_LOGOUT_ACTION_REBOOT);
    }

  g_object_unref (consolekit);

  return ret;
}

gboolean
gsm_logout_can_shutdown ()
{
  GsmConsolekit *consolekit;
  gboolean ret;

  consolekit = gsm_get_consolekit ();

  ret = gsm_consolekit_can_stop (consolekit);

  if (!ret) 
    {
      ret = gdm_supports_logout_action (GDM_LOGOUT_ACTION_SHUTDOWN);
    }

  g_object_unref (consolekit);

  return ret;
}

