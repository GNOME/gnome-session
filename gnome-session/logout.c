/* logout.c - Ask user useful logout questions.

   Written by Owen Taylor <otaylor@redhat.com>
   Copyright (C) Red Hat

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
#include <unistd.h>
#include <gtk/gtk.h>
#include <gtk/gtkinvisible.h>
#include <gdk/gdkx.h>

#include "libgnome/libgnome.h"
#include "libgnomeui/libgnomeui.h"

#include "ice.h"
#include "logout.h"
#include "command.h"
#include "util.h"
#include "gsm-multiscreen.h"

static gchar *halt_command[] =
{
  HALT_COMMAND, NULL
};

static gchar *reboot_command[] =
{
  REBOOT_COMMAND, NULL
};

/* What action to take upon shutdown */
static enum
{
  LOGOUT,
  HALT,
  REBOOT
}
action = LOGOUT;

typedef struct {
	GdkScreen    *screen;
	int           monitor;
	GdkRectangle  iris_rect;
	GdkWindow    *root_window;
	int           iris_block;
	GdkGC        *iris_gc;
} IrisData;

static int num_iris_timeouts = 0;

static gint
iris_callback (IrisData *data)
{
  gint i;
  gint width_step;
  gint height_step;
  gint width;
  gint height;

  width_step = MIN (data->iris_rect.width / 2, data->iris_block);
  height_step = MIN (data->iris_rect.width / 2, data->iris_block);

  for (i = 0; i < MIN (width_step, height_step); i++)
    {
      width  = (gint)data->iris_rect.width  - 2 * i;
      height = (gint)data->iris_rect.height - 2 * i;

      if (width < 0 || height < 0)
        break;

      gdk_draw_rectangle (data->root_window, data->iris_gc, FALSE,
                          data->iris_rect.x + i,
			  data->iris_rect.y + i,
                          width, height);
    }

  gdk_flush ();

  data->iris_rect.x += width_step;
  data->iris_rect.y += height_step;
  data->iris_rect.width -= MIN (data->iris_rect.width, data->iris_block * 2);
  data->iris_rect.height -= MIN (data->iris_rect.height, data->iris_block * 2);

  if (data->iris_rect.width == 0 || data->iris_rect.height == 0)
    {
      g_object_unref (data->iris_gc);
      g_free (data);

      if (!--num_iris_timeouts)
        gtk_main_quit ();

      return FALSE;
    }
  else
    return TRUE;
}

static void
iris_on_screen (GdkScreen *screen,
		int        monitor)
{
  static char  dash_list [2] = {1, 1};
  GdkGCValues  values;
  IrisData    *data;

  data = g_new (IrisData, 1);

  data->screen = screen;
  data->monitor = monitor;

  data->iris_rect.x = gsm_screen_get_x (screen, monitor);
  data->iris_rect.y = gsm_screen_get_y (screen, monitor);
  data->iris_rect.width = gsm_screen_get_width (screen, monitor);
  data->iris_rect.height = gsm_screen_get_height (screen, monitor);

  data->root_window = gdk_screen_get_root_window (screen);

  values.line_style = GDK_LINE_ON_OFF_DASH;
  values.subwindow_mode = GDK_INCLUDE_INFERIORS;

  data->iris_gc = gdk_gc_new_with_values (data->root_window,
					  &values,
					  GDK_GC_LINE_STYLE | GDK_GC_SUBWINDOW);
  gdk_gc_set_dashes (data->iris_gc, 0, dash_list, 2);

  /* Plan for a time of 0.5 seconds for effect */
  data->iris_block = data->iris_rect.height / (500 / 20);
  if (data->iris_block < 8)
    data->iris_block = 8;

  g_timeout_add (20, (GSourceFunc) iris_callback, data);
  num_iris_timeouts++;
}

static void
iris (void)
{
  num_iris_timeouts = 0;

  gsm_foreach_screen (iris_on_screen);

  gtk_main ();

  g_assert (num_iris_timeouts == 0);
}

static void
refresh_screen (GdkScreen *screen,
		int        monitor)
{
  GdkWindowAttr attributes;
  GdkWindow *window;
  GdkWindow *parent;

  attributes.x = 0;
  attributes.y = 0;
  attributes.width = gsm_screen_get_width (screen, monitor);
  attributes.height = gsm_screen_get_height (screen, monitor);
  attributes.window_type = GDK_WINDOW_TOPLEVEL;
  attributes.wclass = GDK_INPUT_OUTPUT;
  attributes.override_redirect = TRUE;
  attributes.event_mask = 0;

  parent = gdk_screen_get_root_window (screen);

  window = gdk_window_new (parent, &attributes,
			   GDK_WA_X | GDK_WA_Y | GDK_WA_NOREDIR);

  gdk_window_show (window);
  gdk_flush ();
  gdk_window_hide (window);
}

/* FIXME:
 * 	Hackaround for Pango opening a separate display
 * connection and doing a server grab while we have a grab
 * on the primary display connection. This forces Pango
 * to go ahead and do its font cache before we try to
 * grab the server.
 *
 * c/f metacity/src/ui.c:meta_ui_init()
 */
static void
force_pango_cache_init ()
{
	PangoFontMetrics     *metrics;
	PangoLanguage        *lang;
	PangoContext         *context;
	PangoFontDescription *font_desc;

	context = gdk_pango_context_get ();
	lang = gtk_get_default_language ();
	font_desc = pango_font_description_from_string ("Sans 12");
	metrics = pango_context_get_metrics (context, font_desc, lang);

	pango_font_metrics_unref (metrics);
	pango_font_description_free (font_desc);
}

static gboolean
display_gui (void)
{
  GtkWidget *box;
  GtkWidget *toggle_button = NULL;
  gint response;
  gchar *s, *t;
  GtkWidget *halt = NULL;
  GtkWidget *reboot = NULL;
  GtkWidget *invisible;
  gboolean   retval = FALSE;
  gboolean save_active = FALSE;
  gboolean halt_active = FALSE;
  gboolean reboot_active = FALSE;
  gboolean a11y_enabled;
  GError *error = NULL;
  GdkScreen *screen;
  int monitor;

  gsm_verbose ("display_gui: showing logout dialog\n");

  /* It's really bad here if someone else has the pointer
   * grabbed, so we first grab the pointer and keyboard
   * to an offscreen window, and then once we have the
   * server grabbed, move that to our dialog.
   */
  gtk_rc_reparse_all ();

  screen = gsm_locate_screen_with_pointer (&monitor);
  if (!screen)
    screen = gdk_screen_get_default ();

  invisible = gtk_invisible_new_for_screen (screen);

  gtk_widget_show (invisible);

  while (1)
    {
      if (gdk_pointer_grab (invisible->window, FALSE, 0,
			    NULL, NULL, GDK_CURRENT_TIME) == Success)
	{
	  if (gdk_keyboard_grab (invisible->window, FALSE, GDK_CURRENT_TIME)
	      == Success)
	    break;
	  gdk_pointer_ungrab (GDK_CURRENT_TIME);
	}
      sleep (1);
    }

  force_pango_cache_init ();

  box = gtk_message_dialog_new (NULL, 0,
				GTK_MESSAGE_QUESTION,
				GTK_BUTTONS_NONE,
				_("Are you sure you want to log out?"));

  a11y_enabled = GTK_IS_ACCESSIBLE (gtk_widget_get_accessible (box));

  /* Grabbing the Xserver when accessibility is enabled will cause
   * a hang. See #93103 for details.
   */
  if (!a11y_enabled)
    {
      XGrabServer (GDK_DISPLAY ());
      iris ();
    }

  gtk_dialog_add_button (GTK_DIALOG (box), GTK_STOCK_HELP, GTK_RESPONSE_HELP);
  gtk_dialog_add_button (GTK_DIALOG (box), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
  gtk_dialog_add_button (GTK_DIALOG (box), GTK_STOCK_OK, GTK_RESPONSE_OK);

  g_object_set (G_OBJECT (box), "type", GTK_WINDOW_POPUP, NULL);

  gtk_dialog_set_default_response (GTK_DIALOG (box), GTK_RESPONSE_OK);
  gtk_window_set_screen (GTK_WINDOW (box), screen);
  gtk_window_set_policy (GTK_WINDOW (box), FALSE, FALSE, TRUE);

  gtk_container_set_border_width (
		GTK_CONTAINER (GTK_DIALOG (box)->vbox), GNOME_PAD);

  if (!autosave)
    {
      toggle_button = gtk_check_button_new_with_mnemonic (_("_Save current setup"));
      gtk_widget_show (toggle_button);
      gtk_box_pack_start (GTK_BOX (GTK_DIALOG (box)->vbox),
			  toggle_button,
			  FALSE, TRUE, 0);
    }

  /* Red Hat specific code to check if the user has a
   * good chance of being able to shutdown the system,
   * and if so, give them that option
   */
  s = g_strconcat ("/var/lock/console/", g_get_user_name (), NULL);
  t = g_strconcat ("/var/run/console/", g_get_user_name (), NULL);
  if (((geteuid () == 0) || g_file_exists (t) || g_file_exists(s)) &&
      access (halt_command[0], X_OK) == 0)
    {
      GtkWidget *frame;
      GtkWidget *action_vbox;
      GtkWidget *r;

      frame = gtk_frame_new (_("Action"));
      gtk_box_pack_start (GTK_BOX (GTK_DIALOG (box)->vbox),
			  frame,
			  FALSE, TRUE, GNOME_PAD_SMALL);

      action_vbox = gtk_vbox_new (FALSE, 0);
      gtk_container_add (GTK_CONTAINER (frame), action_vbox);

      r = gtk_radio_button_new_with_mnemonic (NULL, _("_Log Out"));
      gtk_box_pack_start (GTK_BOX (action_vbox), r, FALSE, FALSE, 0);

      r = halt = gtk_radio_button_new_with_mnemonic_from_widget (GTK_RADIO_BUTTON (r), _("Sh_ut Down"));
      gtk_box_pack_start (GTK_BOX (action_vbox), r, FALSE, FALSE, 0);

      r = reboot = gtk_radio_button_new_with_mnemonic_from_widget (GTK_RADIO_BUTTON (r), _("_Restart the computer"));
      gtk_box_pack_start (GTK_BOX (action_vbox), r, FALSE, FALSE, 0);
    }
  g_free (s);
  g_free (t);

  gsm_center_window_on_screen (GTK_WINDOW (box), screen, monitor);

  gtk_widget_show_all (box);

  /* Move the grabs to our message box */
  gdk_pointer_grab (box->window, TRUE, 0,
		    NULL, NULL, GDK_CURRENT_TIME);
  gdk_keyboard_grab (box->window, FALSE, GDK_CURRENT_TIME);
  XSetInputFocus (GDK_DISPLAY (),
		  GDK_WINDOW_XWINDOW (box->window),
		  RevertToParent,
		  CurrentTime);

  response = gtk_dialog_run (GTK_DIALOG (box));

  if (halt)
    halt_active = GTK_TOGGLE_BUTTON (halt)->active;

  if (reboot)
    reboot_active = GTK_TOGGLE_BUTTON (reboot)->active;

  if (toggle_button)
    save_active = GTK_TOGGLE_BUTTON (toggle_button)->active;

  gtk_widget_destroy (box);

  if (!a11y_enabled)
    {
      gsm_foreach_screen (refresh_screen);
      XUngrabServer (GDK_DISPLAY ());
    }

  gdk_pointer_ungrab (GDK_CURRENT_TIME);
  gdk_keyboard_ungrab (GDK_CURRENT_TIME);

  gdk_flush ();

  switch (response) {
    case GTK_RESPONSE_OK:
      /* We want to know if we should trash changes (and lose forever)
       * or save them */
      if(save_active)
	save_selected = save_active;
      if (halt_active)
	action = HALT;
      else if (reboot_active)
	action = REBOOT;
      retval = TRUE;
      break;
    default:
    case GTK_RESPONSE_CANCEL:
      retval= FALSE;
      break;
    case GTK_RESPONSE_HELP:
      gnome_help_display_desktop (NULL, "user-guide", "wgosstartsession.xml",
				  "gosgetstarted-73", &error);

      if (error) 
        {
          GtkWidget *dialog;

          dialog = gtk_message_dialog_new (NULL,
        				   GTK_DIALOG_DESTROY_WITH_PARENT,
        				   GTK_MESSAGE_ERROR,
        				   GTK_BUTTONS_CLOSE,
        				   ("There was an error displaying help: \n%s"),
        				   error->message);

          g_signal_connect (G_OBJECT (dialog), "response",
	   		    G_CALLBACK (gtk_widget_destroy),
			    NULL);

	  gtk_window_set_screen (GTK_WINDOW (dialog), screen);

          gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
          gtk_widget_show (dialog);
          g_error_free (error);
        }
   
      retval = FALSE;
      break;
    }

  return retval;
}

/* Display GUI if user wants it.  Returns TRUE if save should
   continue, FALSE otherwise.  */
gboolean
maybe_display_gui (void)
{
  gboolean result;

  gsm_verbose ("maybe_display_gui(): showing dialog %d\n", logout_prompt);

  if (!logout_prompt)
    return TRUE;

  /*
   *	Don't let ICE run during the dialog, otherwise we may free clients
   *	during the dialog and rather suprise and offend the caller.
   */
  ice_frozen();
  result = display_gui ();
  if (action == HALT)
    set_logout_command (halt_command);
  else if (action == REBOOT)
    set_logout_command (reboot_command);
  ice_thawed();
  return result;
}
