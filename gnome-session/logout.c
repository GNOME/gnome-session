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

#include <libgnome/libgnome.h>
#include <libgnomeui/libgnomeui.h>

#include "ice.h"
#include "logout.h"
#include "command.h"
#include "util.h"
#include "gsm-multiscreen.h"
#include "egg-screen-help.h"

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
  GdkRectangle  area;
  int           rowstride;
  GdkWindow    *root_window;
  GdkWindow    *draw_window;
  GdkPixbuf    *start_pb, *end_pb, *frame;
  guchar       *start_p, *end_p, *frame_p;
  GTimeVal      start_time;
  GdkGC        *gc;
} FadeoutData;

static GList *fadeout_windows = NULL;

/* Go for five seconds */
#define FADE_DURATION 1500.0

static void
get_current_frame (FadeoutData *fadeout,
		   double    sat)
{
  guchar *sp, *ep, *fp;
  int i, j, width, offset;

  width = fadeout->area.width * 3;
  offset = 0;
  
  for (i = 0; i < fadeout->area.height; i++)
    {
      sp = fadeout->start_p + offset;
      ep = fadeout->end_p   + offset;
      fp = fadeout->frame_p + offset;

      for (j = 0; j < width; j += 3)
	{
	  guchar r = abs (*(sp++) - ep[0]);
	  guchar g = abs (*(sp++) - ep[1]);
	  guchar b = abs (*(sp++) - ep[2]);

	  *(fp++) = *(ep++) + r * sat;
	  *(fp++) = *(ep++) + g * sat;
	  *(fp++) = *(ep++) + b * sat;
	}

      offset += fadeout->rowstride;
    }
}

static void
darken_pixbuf (GdkPixbuf *pb)
{
  int width, height, rowstride;
  int i, j;
  guchar *p, *pixels;
  
  width     = gdk_pixbuf_get_width (pb) * 3;
  height    = gdk_pixbuf_get_height (pb);
  rowstride = gdk_pixbuf_get_rowstride (pb);
  pixels    = gdk_pixbuf_get_pixels (pb);
  
  for (i = 0; i < height; i++)
    {
      p = pixels + (i * rowstride);
      for (j = 0; j < width; j++)
	p [j] >>= 1;
    }
}

static gboolean
fadeout_callback (FadeoutData *fadeout)
{
  GTimeVal current_time;
  double elapsed, percent;

  g_get_current_time (&current_time);
  elapsed = ((((double)current_time.tv_sec - fadeout->start_time.tv_sec) * G_USEC_PER_SEC +
	      (current_time.tv_usec - fadeout->start_time.tv_usec))) / 1000.0;

  if (elapsed < 0)
    {
      g_warning ("System clock seemed to go backwards?");
      elapsed = G_MAXDOUBLE;
    }

  if (elapsed > FADE_DURATION)
    {
      gdk_draw_pixbuf (fadeout->draw_window,
		       fadeout->gc,
		       fadeout->end_pb,
		       0, 0,
		       fadeout->area.x,
		       fadeout->area.y,
		       fadeout->area.width,
		       fadeout->area.height,
		       GDK_RGB_DITHER_NONE,
		       0, 0);

      g_object_unref (fadeout->gc);
      g_object_unref (fadeout->start_pb);
      g_object_unref (fadeout->end_pb);
      g_object_unref (fadeout->frame);

      g_free (fadeout);
    
      return FALSE;
    }

  percent = elapsed / FADE_DURATION;

  get_current_frame (fadeout, 1.0 - percent);
  gdk_draw_pixbuf (fadeout->draw_window,
		   fadeout->gc,
		   fadeout->frame,
		   0, 0,
		   fadeout->area.x,
		   fadeout->area.y,
		   fadeout->area.width,
		   fadeout->area.height,
		   GDK_RGB_DITHER_NONE,
		   0, 0);

  gdk_flush ();
  
  return TRUE;
}
  
static void
fadeout_screen (GdkScreen *screen,
		int        monitor)
{
  GdkWindowAttr attr;
  int attr_mask;
  GdkGCValues values;
  FadeoutData *fadeout;

  fadeout = g_new (FadeoutData, 1);

  fadeout->screen = screen;
  fadeout->monitor = monitor;

  fadeout->area.x = gsm_screen_get_x (screen, monitor);
  fadeout->area.y = gsm_screen_get_y (screen, monitor);
  fadeout->area.width = gsm_screen_get_width (screen, monitor);
  fadeout->area.height = gsm_screen_get_height (screen, monitor);

  fadeout->root_window = gdk_screen_get_root_window (screen);
  attr.window_type = GDK_WINDOW_CHILD;
  attr.x = fadeout->area.x;
  attr.y = fadeout->area.y;
  attr.width = fadeout->area.width;
  attr.height = fadeout->area.height;
  attr.wclass = GDK_INPUT_OUTPUT;
  attr.visual = gdk_screen_get_system_visual (fadeout->screen);
  attr.colormap = gdk_screen_get_default_colormap (fadeout->screen);
  attr.override_redirect = TRUE;
  attr_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP | GDK_WA_NOREDIR;

  fadeout->draw_window = gdk_window_new (NULL, &attr, attr_mask);
  fadeout_windows = g_list_prepend (fadeout_windows, fadeout->draw_window);
  
  fadeout->start_pb = gdk_pixbuf_get_from_drawable (NULL,
						    fadeout->root_window,
						    NULL,
						    fadeout->area.x,
						    fadeout->area.y,
						    0, 0,
						    fadeout->area.width,
						    fadeout->area.height);
  
  fadeout->end_pb = gdk_pixbuf_copy (fadeout->start_pb);
  darken_pixbuf (fadeout->end_pb);
  
  fadeout->frame = gdk_pixbuf_copy (fadeout->start_pb);
  fadeout->rowstride = gdk_pixbuf_get_rowstride (fadeout->start_pb);

  fadeout->start_p = gdk_pixbuf_get_pixels (fadeout->start_pb);
  fadeout->end_p   = gdk_pixbuf_get_pixels (fadeout->end_pb);
  fadeout->frame_p = gdk_pixbuf_get_pixels (fadeout->frame);
  
  values.subwindow_mode = GDK_INCLUDE_INFERIORS;

  fadeout->gc = gdk_gc_new_with_values (fadeout->root_window, &values, GDK_GC_SUBWINDOW);

  gdk_window_show (fadeout->draw_window);
  gdk_draw_pixbuf (fadeout->draw_window,
		   fadeout->gc,
		   fadeout->frame,
		   0, 0,
		   fadeout->area.x,
		   fadeout->area.y,
		   fadeout->area.width,
		   fadeout->area.height,
		   GDK_RGB_DITHER_NONE,
		   0, 0);
  
  g_get_current_time (&fadeout->start_time);
  g_idle_add ((GSourceFunc) fadeout_callback, fadeout);
}

static void
hide_fadeout_windows (void)
{
  GList *l;

  for (l = fadeout_windows; l; l = l->next)
    {
      gdk_window_hide (GDK_WINDOW (l->data));
      g_object_unref (l->data);
    }
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

static GtkWidget *
make_title_label (const char *text)
{
  GtkWidget *label;
  char *full;

  full = g_strdup_printf ("<span weight=\"bold\">%s</span>", text);
  label = gtk_label_new (full);
  g_free (full);

  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_label_set_use_markup (GTK_LABEL (label), TRUE);

  return label;
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
      gsm_foreach_screen (fadeout_screen);
    }

  gtk_dialog_add_button (GTK_DIALOG (box), GTK_STOCK_HELP, GTK_RESPONSE_HELP);
  gtk_dialog_add_button (GTK_DIALOG (box), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
  gtk_dialog_add_button (GTK_DIALOG (box), GTK_STOCK_OK, GTK_RESPONSE_OK);

  g_object_set (G_OBJECT (box), "type", GTK_WINDOW_POPUP, NULL);

  gtk_dialog_set_default_response (GTK_DIALOG (box), GTK_RESPONSE_OK);
  gtk_window_set_screen (GTK_WINDOW (box), screen);
  gtk_window_set_policy (GTK_WINDOW (box), FALSE, FALSE, TRUE);

  gtk_container_set_border_width (GTK_CONTAINER (box), 6);
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
      GtkWidget *title, *spacer;
      GtkWidget *action_vbox, *hbox;
      GtkWidget *r;
      
      title = make_title_label (_("Action"));
      gtk_box_pack_start (GTK_BOX (GTK_DIALOG (box)->vbox),
			  title, FALSE, FALSE, GNOME_PAD_SMALL);

      hbox = gtk_hbox_new (FALSE, 6);
      gtk_box_pack_start (GTK_BOX (GTK_DIALOG (box)->vbox), hbox, TRUE, TRUE, 0);

      spacer = gtk_label_new ("    ");
      gtk_box_pack_start (GTK_BOX (hbox), spacer, FALSE, FALSE, 0);

      action_vbox = gtk_vbox_new (FALSE, 0);
      gtk_box_pack_start (GTK_BOX (hbox), action_vbox, TRUE, TRUE, 0);
      
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
      hide_fadeout_windows ();
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
      retval = FALSE;
      break;
    case GTK_RESPONSE_HELP:
      egg_help_display_desktop_on_screen (NULL, "user-guide",
					  "wgosstartsession.xml",
					  "gosgetstarted-73",
					  screen,
					  &error);

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
