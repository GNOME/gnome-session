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

#define SESSION_STOCK_LOGOUT "session-logout"

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

/* Some globals for rendering the iris.  */
static GdkRectangle iris_rect;
static gint iris_block = 0;
static GdkGC *iris_gc = NULL;

static gint
iris_callback (gpointer data)
{
  gint i;
  gint width_step;
  gint height_step;

  width_step = MIN (iris_rect.width / 2, iris_block);
  height_step = MIN (iris_rect.width / 2, iris_block);

  for (i = 0; i < MIN (width_step, height_step); i++)
    gdk_draw_rectangle (GDK_ROOT_PARENT (), iris_gc, FALSE,
			iris_rect.x + i, iris_rect.y + i,
			(gint)iris_rect.width - 2 * i,
			(gint)iris_rect.height - 2 * i);

  gdk_flush ();

  iris_rect.x += width_step;
  iris_rect.y += height_step;
  iris_rect.width -= MIN (iris_rect.width, iris_block * 2);
  iris_rect.height -= MIN (iris_rect.height, iris_block * 2);

  if (iris_rect.width == 0 || iris_rect.height == 0)
    {
      gtk_main_quit ();
      return FALSE;
    }
  else
    return TRUE;
}

static void
iris (void)
{
  iris_rect.x = 0;
  iris_rect.y = 0;
  iris_rect.width = gdk_screen_width ();
  iris_rect.height = gdk_screen_height ();

  if (!iris_gc)
    {
      GdkGCValues values;
      static gchar dash_list[2] = {1, 1};

      values.line_style = GDK_LINE_ON_OFF_DASH;
      values.subwindow_mode = GDK_INCLUDE_INFERIORS;

      iris_gc = gdk_gc_new_with_values (GDK_ROOT_PARENT (),
					&values,
				      GDK_GC_LINE_STYLE | GDK_GC_SUBWINDOW);

      gdk_gc_set_dashes (iris_gc, 0, dash_list, 2);
    }

  /* Plan for a time of 0.5 seconds for effect */
  iris_block = iris_rect.height / (500 / 20);
  if (iris_block < 8)
    {
      iris_block = 8;
    }

  gtk_timeout_add (20, iris_callback, NULL);

  gtk_main ();
}

static void
refresh_screen (void)
{
  GdkWindowAttr attributes;
  GdkWindow *window;

  attributes.x = 0;
  attributes.y = 0;
  attributes.width = gdk_screen_width ();
  attributes.height = gdk_screen_height ();
  attributes.window_type = GDK_WINDOW_TOPLEVEL;
  attributes.wclass = GDK_INPUT_OUTPUT;
  attributes.override_redirect = TRUE;
  attributes.event_mask = 0;

  window = gdk_window_new (NULL, &attributes,
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

static inline void
register_logout_stock_item (void)
{
	static gboolean registered = FALSE;

	if (!registered) {
		GtkIconFactory      *factory;
		GtkIconSet          *quit_icons;

		static GtkStockItem  logout_item [] = {
			{ SESSION_STOCK_LOGOUT, N_("_Log Out"), 0, 0, GETTEXT_PACKAGE },
		};

		quit_icons = gtk_icon_factory_lookup_default (GTK_STOCK_QUIT);
		factory = gtk_icon_factory_new ();
		gtk_icon_factory_add (factory, SESSION_STOCK_LOGOUT, quit_icons);
		gtk_icon_factory_add_default (factory);
		gtk_stock_add_static (logout_item, 1);

		registered = TRUE;
	}
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

  gsm_verbose ("display_gui: showing logout dialog\n");

  /* It's really bad here if someone else has the pointer
   * grabbed, so we first grab the pointer and keyboard
   * to an offscreen window, and then once we have the
   * server grabbed, move that to our dialog.
   */
  gtk_rc_reparse_all ();

  invisible = gtk_invisible_new ();
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

  XGrabServer (GDK_DISPLAY ());
  iris ();

  register_logout_stock_item ();

  box = gtk_message_dialog_new (NULL, 0,
				GTK_MESSAGE_QUESTION,
				GTK_BUTTONS_NONE,
				_("Really log out?"));

  gtk_dialog_add_button (GTK_DIALOG (box), GTK_STOCK_HELP, GTK_RESPONSE_HELP);
  gtk_dialog_add_button (GTK_DIALOG (box), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
  gtk_dialog_add_button (GTK_DIALOG (box), SESSION_STOCK_LOGOUT, GTK_RESPONSE_OK);

  g_object_set (G_OBJECT (box), "type", GTK_WINDOW_POPUP, NULL);

  gtk_dialog_set_default_response (GTK_DIALOG (box), GTK_RESPONSE_OK);
  gtk_window_set_position (GTK_WINDOW (box), GTK_WIN_POS_CENTER);
  gtk_window_set_policy (GTK_WINDOW (box), FALSE, FALSE, TRUE);

  gtk_container_set_border_width (
		GTK_CONTAINER (GTK_DIALOG (box)->vbox), GNOME_PAD);

  if (!autosave)
    {
      toggle_button = gtk_check_button_new_with_label (_("Save current setup"));
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

      r = gtk_radio_button_new_with_label (NULL, _("Logout"));
      gtk_box_pack_start (GTK_BOX (action_vbox), r, FALSE, FALSE, 0);

      r = halt = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (r), _("Shut Down"));
      gtk_box_pack_start (GTK_BOX (action_vbox), r, FALSE, FALSE, 0);

      r = reboot = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (r), _("Reboot"));
      gtk_box_pack_start (GTK_BOX (action_vbox), r, FALSE, FALSE, 0);
    }
  g_free (s);
  g_free (t);

  gtk_widget_show_all (box);

  /* Move the grabs to our message box */
  gdk_pointer_grab (box->window, TRUE, 0,
		    NULL, NULL, GDK_CURRENT_TIME);
  gdk_keyboard_grab (box->window, FALSE, GDK_CURRENT_TIME);

  response = gtk_dialog_run (GTK_DIALOG (box));

  if (halt)
    halt_active = GTK_TOGGLE_BUTTON (halt)->active;

  if (reboot)
    reboot_active = GTK_TOGGLE_BUTTON (reboot)->active;

  if (toggle_button)
    save_active = GTK_TOGGLE_BUTTON (toggle_button)->active;

  gtk_widget_destroy (box);

  refresh_screen ();
  XUngrabServer (GDK_DISPLAY ());

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
      gnome_help_display("panelbasics.html#LOGGINGOUT", NULL, NULL);
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
