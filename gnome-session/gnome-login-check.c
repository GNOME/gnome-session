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

#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-program.h>
#include <libgnome/gnome-util.h>
#include <libgnome/gnome-init.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#include <unistd.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/wait.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <sys/stat.h>

/* for performance checking */
#if 1
static struct stat dummy_sb;
#define DUMMY_STAT(s) (stat ((s), &dummy_sb))
#else /* !DO_DUMMY_STATS */
#define DUMMY_STAT(s)
#endif /* !DO_DUMMY_STATS */

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
	
	DUMMY_STAT ("get_hostname");

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
	char *hostname;

	DUMMY_STAT ("check_for_dns");

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

  DUMMY_STAT ("check_orbit_dir");

  g_snprintf(buf, sizeof(buf), "/tmp/orbit-%s", g_get_user_name());
  if(stat(buf, &sbuf))
    return TRUE; /* Doesn't exist - things are fine */

  if(sbuf.st_uid != getuid()
     && sbuf.st_uid != geteuid())
    return FALSE;

  return TRUE;
}

enum {
	RESPONSE_LOG_IN,
	RESPONSE_TRY_AGAIN,
	RESPONSE_RESET
};

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
	int response;

	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
 	textdomain (GETTEXT_PACKAGE);

	gtk_init (&argc, &argv);

	gdk_window_get_pointer (GDK_ROOT_PARENT(), NULL, NULL, &state);

	if ((state & (GDK_CONTROL_MASK|GDK_SHIFT_MASK)) == (GDK_CONTROL_MASK|GDK_SHIFT_MASK)) {
		dialog = gtk_dialog_new_with_buttons (_("GNOME Login"), 
						      NULL, GTK_DIALOG_NO_SEPARATOR,
						      _("Log In"), RESPONSE_LOG_IN,
						      NULL);
		
		gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);

		hbox = gtk_hbox_new (FALSE, 5);
		gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
				    hbox, FALSE, FALSE, 0);

		gnome_program_init ("gnome-login-check", VERSION,
				    LIBGNOME_MODULE, argc, argv,
				    NULL);
		s = gnome_program_locate_file (NULL, GNOME_FILE_DOMAIN_PIXMAP, "gnome-logo-large.png", FALSE, NULL);
		if (s) {
			pixmap = gtk_image_new_from_file (s);
                        g_free(s);
			
			frame = gtk_frame_new (NULL);
			gtk_container_set_border_width (GTK_CONTAINER (frame), 5);
			gtk_frame_set_shadow_type (GTK_FRAME (frame),
						   GTK_SHADOW_IN);
			gtk_container_add (GTK_CONTAINER (frame), pixmap);
			
			gtk_box_pack_start (GTK_BOX (hbox),
					    frame, FALSE, FALSE, 0);
                }

		vbox = gtk_vbox_new (FALSE, 5);
		gtk_box_pack_start (GTK_BOX (hbox),
				    vbox, FALSE, FALSE, 0);

		session_toggle = gtk_check_button_new_with_label (_("Start with default programs"));
		gtk_box_pack_start (GTK_BOX (vbox), session_toggle,
				    FALSE, FALSE, 0);

		settings_toggle = gtk_check_button_new_with_label (_("Reset all user settings"));
		gtk_box_pack_start (GTK_BOX (vbox), settings_toggle,
				    FALSE, FALSE, 0);
	    
		while (1) {
		        gtk_widget_show_all (dialog);
			response = gtk_dialog_run (GTK_DIALOG (dialog));

			if (GTK_TOGGLE_BUTTON (settings_toggle)->active) {
				msg = g_strdup_printf (_("Really reset all GNOME user settings for %s?"), g_get_user_name());
				msgbox = gtk_message_dialog_new (GTK_WINDOW (dialog), 0,
								 GTK_MESSAGE_QUESTION,
								 GTK_BUTTONS_NONE,
								 _("Really reset all GNOME user settings for %s?"),
								 g_get_user_name ());
				gtk_dialog_add_buttons (GTK_DIALOG (msgbox),
							_("No"), GTK_RESPONSE_NO,
							_("Reset all Settings"), RESPONSE_RESET,
							NULL);
				gtk_window_set_position (GTK_WINDOW (msgbox),
							 GTK_WIN_POS_CENTER);

				response = gtk_dialog_run (GTK_DIALOG (msgbox));
				gtk_widget_destroy (msgbox);
				if (response != RESPONSE_RESET)
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

		tmp_msgbox = gtk_message_dialog_new (NULL, 0,
						     GTK_MESSAGE_WARNING,
						     GTK_BUTTONS_NONE,
						     _("The directory /tmp/orbit-%s is not owned\nby the current user, %s.\n"
						       "Please correct the ownership of this directory."),
						     g_get_user_name(), g_get_user_name());
		
		gtk_dialog_add_buttons (GTK_DIALOG (tmp_msgbox),
					_("Log in Anyway"), RESPONSE_LOG_IN,
					_("Try Again"), RESPONSE_TRY_AGAIN,
					NULL);
	  
		gtk_window_set_position (GTK_WINDOW (tmp_msgbox), GTK_WIN_POS_CENTER);
		
		while ((RESPONSE_TRY_AGAIN == gtk_dialog_run (GTK_DIALOG (tmp_msgbox))) &&
		       !check_orbit_dir ())
			/* Nothing */;
	}
	
	if (!check_for_dns ()) {
		GtkWidget *tmp_msgbox;

		tmp_msgbox = gtk_message_dialog_new (NULL, 0,
						     GTK_MESSAGE_WARNING,
						     GTK_BUTTONS_NONE,
						     _("Could not look up internet address for %s.\n"
						       "This will prevent GNOME from operating correctly.\n"
						       "It may be possible to correct the problem by adding\n"
						       "%s to the file /etc/hosts."),
						     get_hostname(TRUE), get_hostname(TRUE));
		
		gtk_dialog_add_buttons (GTK_DIALOG (tmp_msgbox),
					_("Log in Anyway"), RESPONSE_LOG_IN,
					_("Try Again"), RESPONSE_TRY_AGAIN,
					NULL);
	  
		gtk_window_set_position (GTK_WINDOW (tmp_msgbox), GTK_WIN_POS_CENTER);
		
		while ((RESPONSE_TRY_AGAIN == gtk_dialog_run (GTK_DIALOG (tmp_msgbox))) &&
		       !check_for_dns ())
			/* Nothing */;
			
	}

	return 0;
}
