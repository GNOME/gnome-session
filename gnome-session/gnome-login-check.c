/* gnome-login-check.c - Do some checks during login

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
#include <gnome.h>
#include <gdk/gdkx.h>
#include <stdio.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <sys/stat.h>

GtkWidget *dns_dialog = NULL;
GtkWidget *progress = NULL;
gint input_tag = FALSE;
gint timeout_tag = FALSE;

/* returns the hostname */
static gchar *
get_hostname (gboolean readable)
{
	static gboolean init = FALSE;
	static gchar *result = NULL;
	
	if (!init){
		char hostname[256];
		
		if (gethostname (hostname, sizeof (hostname)) == 0)
			result = g_strdup (hostname);
		
		init = TRUE;
	}
	
	if (result)
		return result;
	else
		return readable ? "(Unknown)" : NULL;
}

/* Check if a DNS lookup on `hostname` can be done */
static gboolean
check_for_dns (void)
{
	gchar          *hostname;
	struct hostent *host;
	struct in_addr  addr;

	hostname = get_hostname (FALSE);
	
	if (!hostname)
		return FALSE;

	/*
	 * FIXME:
	 *  we should probably be a lot more robust here
	 */
	if (!gethostbyname (hostname))
		return FALSE;

	return TRUE;
}

static gboolean
check_orbit_dir(void)
{
  char buf[PATH_MAX];
  struct stat sbuf;

  g_snprintf(buf, sizeof(buf), "/tmp/orbit-%s", g_get_user_name());
  if(stat(buf, &sbuf))
    return TRUE; /* Doesn't exist - things are fine */

  if(sbuf.st_uid != getuid()
     && sbuf.st_uid != geteuid())
    return FALSE;

  return TRUE;
}

#ifdef DO_LIBICE_CHECK
static gboolean
check_for_libice_bug (void)
{
	int retval = system ("gnome-libice-check");
	return retval == -1 || retval == 0 ||  retval == 127;
}
#endif

int
main (int argc, char **argv)
{
        GdkModifierType state;
	GtkWidget *dialog;
	GtkWidget *msgbox;
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *session_toggle;
	GtkWidget *settings_toggle;
	GtkWidget *frame;
	GtkWidget *pixmap = NULL;
	gchar *msg, *s;

	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
 	textdomain (GETTEXT_PACKAGE);

	gnome_init ("gnome-login-check", "0.1", argc, argv);

	gdk_window_get_pointer (GDK_ROOT_PARENT(), NULL, NULL, &state);

	if ((state & (GDK_CONTROL_MASK|GDK_SHIFT_MASK)) == (GDK_CONTROL_MASK|GDK_SHIFT_MASK)) {
		dialog = gnome_dialog_new (_("GNOME Login"), _("Login"), NULL);
		gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);

		hbox = gtk_hbox_new (FALSE, GNOME_PAD);
		gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox),
				    hbox, FALSE, FALSE, 0);

		s = gnome_unconditional_pixmap_file("gnome-logo-large.png");
		if (s) {
                        pixmap = gnome_pixmap_new_from_file(s);
                        g_free(s);
			
			frame = gtk_frame_new (NULL);
			gtk_container_set_border_width (GTK_CONTAINER (frame),
							GNOME_PAD_SMALL);
			gtk_frame_set_shadow_type (GTK_FRAME (frame),
						   GTK_SHADOW_IN);
			gtk_container_add (GTK_CONTAINER (frame), pixmap);
			
			gtk_box_pack_start (GTK_BOX (hbox),
					    frame, FALSE, FALSE, 0);
                }

		vbox = gtk_vbox_new (FALSE, GNOME_PAD_SMALL);
		gtk_box_pack_start (GTK_BOX (hbox),
				    vbox, FALSE, FALSE, 0);

		session_toggle = gtk_check_button_new_with_label (_("Start with default programs"));
		gtk_box_pack_start (GTK_BOX (vbox), session_toggle,
				    FALSE, FALSE, 0);

		settings_toggle = gtk_check_button_new_with_label (_("Reset all user settings"));
		gtk_box_pack_start (GTK_BOX (vbox), settings_toggle,
				    FALSE, FALSE, 0);
	    
		gnome_dialog_close_hides (GNOME_DIALOG (dialog), TRUE);

		while (1) {
		        gtk_widget_show_all (dialog);
			gnome_dialog_run (GNOME_DIALOG (dialog));

			if (GTK_TOGGLE_BUTTON (settings_toggle)->active) {
				msg = g_strdup_printf (_("Really reset all GNOME user settings for %s?"), g_get_user_name());
				msgbox = gnome_message_box_new (msg,
								GNOME_MESSAGE_BOX_QUESTION,
								GNOME_STOCK_BUTTON_OK,
								GNOME_STOCK_BUTTON_CANCEL,
								NULL);
				gtk_window_set_position (GTK_WINDOW (msgbox),
							 GTK_WIN_POS_CENTER);

				if (gnome_dialog_run (GNOME_DIALOG (msgbox)) != 0)
					continue;
			}

			break;
		}
		
		if (GTK_TOGGLE_BUTTON (settings_toggle)->active) {
			gchar *filename = gnome_util_home_file ("");
			gchar *command = g_strconcat ("rm -rf ", filename, NULL);
			system (command);
			g_print("%s\n", command);
		} else if (GTK_TOGGLE_BUTTON (session_toggle)->active) {
			gchar *filename = gnome_util_home_file ("session");
			gchar *command = g_strconcat ("rm ", filename, NULL);
			system (command);
		}
	}

	if (!check_orbit_dir ()) {
		GtkWidget *tmp_msgbox;
		gchar *tmp_msg;
		tmp_msg = g_strdup_printf (
			_("The directory /tmp/orbit-%s is not owned\nby the current user, %s.\n"
			  "Please correct the ownership of this directory."),
			g_get_user_name(), g_get_user_name());
		
		tmp_msgbox = gnome_message_box_new (
			tmp_msg,
			GNOME_MESSAGE_BOX_WARNING,
			_("Try again"),
			_("Continue"),
			NULL);
	  
		gtk_window_set_position (GTK_WINDOW (tmp_msgbox), GTK_WIN_POS_CENTER);
		gnome_dialog_close_hides (GNOME_DIALOG (tmp_msgbox), TRUE);
		
		while ((gnome_dialog_run (GNOME_DIALOG (tmp_msgbox)) == 0) &&
		       !check_orbit_dir ())
			/* Nothing */;
	}
	
	if (!check_for_dns ()) {
		GtkWidget *tmp_msgbox;
		gchar *tmp_msg;
		
		tmp_msg = g_strdup_printf (
		  _("Could not look up internet address for %s.\n"
		    "This will prevent GNOME from operating correctly.\n"
		    "It may be possible to correct the problem by adding\n"
		    "%s to the file /etc/hosts."),
		  get_hostname(TRUE), get_hostname(TRUE));
		
		tmp_msgbox = gnome_message_box_new (tmp_msg,
						GNOME_MESSAGE_BOX_WARNING,
						_("Try again"),
						_("Continue"),
						NULL);

		gtk_window_set_position (GTK_WINDOW (tmp_msgbox), GTK_WIN_POS_CENTER);
		gnome_dialog_close_hides (GNOME_DIALOG (tmp_msgbox), TRUE);

		while ((gnome_dialog_run (GNOME_DIALOG (tmp_msgbox)) == 0) &&
		       !check_for_dns ())
			/* Nothing */;
			
	}

#ifdef DO_LIBICE_CHECK
	if (!check_for_libice_bug ()) {
		GtkWidget *tmp_msgbox;
		
		tmp_msgbox = gnome_message_box_new (
			_("Your version of libICE has a bug which causes gnome-session\n"
			  "to not function correctly.\n\n"
			  "If you are on Solaris, you should either upgrade to Solaris patch\n"
			  "#108376-16 or use the libICE.so.6 from the original Solaris 7.\n"
			  "Copy the file into /usr/openwin/lib.  (Thanks go to Andy Reitz\n"
			  "for information on this bug).\n\n"
			  "Your GNOME session will terminate after closing this dialog."),
			GNOME_MESSAGE_BOX_ERROR,
			GNOME_STOCK_BUTTON_OK,
			NULL);
		
		gtk_window_set_position (GTK_WINDOW (tmp_msgbox), GTK_WIN_POS_CENTER);
		gnome_dialog_close_hides (GNOME_DIALOG (tmp_msgbox), TRUE);

		gnome_dialog_run (GNOME_DIALOG (tmp_msgbox));
	}	
#endif

	return 0;
}
