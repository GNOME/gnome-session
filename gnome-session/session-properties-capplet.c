/* session-properties.c - Edit session properties.

   Copyright 1999 Free Software Foundation, Inc.

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
   02111-1307, USA. 

   Authors: Felix Bellaby, Owen Taylor */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <gnome.h>
#include <libgnomeui/gnome-window-icon.h>
#include <capplet-widget.h>
#include "headers.h"
#include "gsm-client-list.h"
#include "gsm-client-row.h"
#include "gsm-protocol.h"
#include "session-properties-capplet.h"

/* Current state */
GSList *deleted_sessions = NULL;

static gboolean auto_save;
static gboolean auto_save_revert;

static gboolean logout_prompt;
static gboolean logout_prompt_revert;

static gboolean login_splash;
static gboolean login_splash_revert;

static GSList *session_list = NULL;
static GSList *session_list_revert = NULL;

static gchar* current_session;
static gchar* current_session_revert;

static gint selected_row = -1;
static gint selected_row_revert = -1;

static GSList *startup_list;
static GSList *startup_list_revert;

static GHashTable *hashed_sessions;

static GtkObject *protocol;

/* capplet widgets */
static GtkWidget *capplet;
static GtkWidget *autosave_button;
static GtkWidget *logout_prompt_button;
static GtkWidget *login_splash_button;

static GtkWidget *clist_start;
static GtkWidget *clist_session;

static GtkWidget *startup_command_dialog;
static GtkWidget *session_command_dialog;

/* CORBA callbacks and intialization */
static void capplet_build (void);

/* Capplet callback prototypes */
static void try (void);
static void revert (void);
static void ok (void);
static void cancel (void);
static void show_help (void);
static void page_hidden (void);
static void page_shown (void);

/* Other callbacks */

static void update_gui (void);
static void dirty_cb (GtkWidget *widget, GtkWidget *capplet);
static void add_startup_cb (void);
static void edit_startup_cb (void);
static void delete_startup_cb (void);
static void add_session_cb (void);
static void edit_session_cb (void);
static void delete_session_cb (void);
static void browse_session_cb (void);

static void select_row (GtkWidget *widget, gint row);
static void protocol_set_current_session (GtkWidget *widget, gchar *name);
static void saved_sessions (GtkWidget *widget, GSList *session_names);

static GtkWidget *
left_aligned_button (gchar *label)
{
  GtkWidget *button = gtk_button_new_with_label (label);
  gtk_misc_set_alignment (GTK_MISC (GTK_BIN (button)->child),
			  0.0, 0.5);
  gtk_misc_set_padding (GTK_MISC (GTK_BIN (button)->child),
			GNOME_PAD_SMALL, 0);

  return button;
}

static void
capplet_build (void)
{
  GtkWidget *hbox, *vbox;
  GtkWidget *util_vbox;
  GtkWidget *frame;
  GtkWidget *button;
  GtkWidget *scrolled_window;
  GtkWidget *alignment;
  GtkWidget *notebook;
  GtkWidget *label;

  /* Set up the hash table to keep tracked of edited session names */

  hashed_sessions = g_hash_table_new(g_str_hash,g_str_equal);

  /* Retrieve options */

  gnome_config_push_prefix (GSM_OPTION_CONFIG_PREFIX);
  auto_save = gnome_config_get_bool (AUTOSAVE_MODE_KEY "=" AUTOSAVE_MODE_DEFAULT);
  auto_save_revert = auto_save;

  logout_prompt = gnome_config_get_bool (LOGOUT_SCREEN_KEY "=" LOGOUT_SCREEN_DEFAULT);
  logout_prompt_revert = logout_prompt;

  login_splash = gnome_config_get_bool (SPLASH_SCREEN_KEY "=" SPLASH_SCREEN_DEFAULT);
  login_splash_revert = login_splash;
  
  /* FIXME - don't really want this here...the protocol should set current_session
     but I'm a little unsure if it actually does though */ 
  current_session = gnome_config_get_string (CURRENT_SESSION_KEY "=" DEFAULT_SESSION);
  
  gnome_config_pop_prefix ();
  
  startup_list = NULL;

  /* capplet callbacks */
  capplet = capplet_widget_new ();
  gtk_signal_connect (GTK_OBJECT (capplet), "try",
		      GTK_SIGNAL_FUNC (try), NULL);
  gtk_signal_connect (GTK_OBJECT (capplet), "revert",
		      GTK_SIGNAL_FUNC (revert), NULL);
  gtk_signal_connect (GTK_OBJECT (capplet), "cancel",
		      GTK_SIGNAL_FUNC (cancel), NULL);
  gtk_signal_connect (GTK_OBJECT (capplet), "ok",
		      GTK_SIGNAL_FUNC (ok), NULL);
  gtk_signal_connect (GTK_OBJECT (capplet), "page_hidden",
		      GTK_SIGNAL_FUNC (page_hidden), NULL);
  gtk_signal_connect (GTK_OBJECT (capplet), "page_shown",
		      GTK_SIGNAL_FUNC (page_shown), NULL);
  gtk_signal_connect (GTK_OBJECT (capplet), "help",
		      GTK_SIGNAL_FUNC (show_help), NULL);

  /**** GUI ****/

  /* Set up the notebook */
  notebook = gtk_notebook_new();
  gtk_notebook_set_tab_pos (GTK_NOTEBOOK (notebook), GTK_POS_TOP);

  /* Options page - Set Current Session, Logout Prompt, Autosave, Splash */
  vbox = gtk_vbox_new (FALSE, GNOME_PAD);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), GNOME_PAD_SMALL);
  
  /* frame for options */

  frame = gtk_frame_new (_("Options"));
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 0);

  util_vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (frame), util_vbox);
  gtk_container_set_border_width (GTK_CONTAINER (util_vbox), GNOME_PAD_SMALL);
  
  alignment = gtk_alignment_new (0.0, 0.5, 0.0, 0.0);
  gtk_box_pack_start (GTK_BOX (util_vbox), alignment, FALSE, FALSE, 0);

  login_splash_button = gtk_check_button_new_with_label
    (_("Show splash screen on login"));
  gtk_container_add (GTK_CONTAINER (alignment), login_splash_button);

  alignment = gtk_alignment_new (0.0, 0.5, 0.0, 0.0);
  gtk_box_pack_start (GTK_BOX (util_vbox), alignment, FALSE, FALSE, 0);

  logout_prompt_button = gtk_check_button_new_with_label
    (_("Prompt on logout"));
  gtk_container_add (GTK_CONTAINER (alignment), logout_prompt_button);

  alignment = gtk_alignment_new (0.0, 0.5, 0.0, 0.0);
  gtk_box_pack_start (GTK_BOX (util_vbox), alignment, FALSE, FALSE, 0);

  autosave_button = gtk_check_button_new_with_label
    (_("Automatically save changes to session"));
  gtk_container_add (GTK_CONTAINER (alignment), autosave_button);
 
  frame = gtk_frame_new (_("Choose Current Session"));
  gtk_box_pack_start (GTK_BOX (vbox), frame, TRUE, TRUE, 0);
 
  hbox = gtk_hbox_new (FALSE, GNOME_PAD);
  gtk_container_set_border_width (GTK_CONTAINER (hbox), GNOME_PAD);
  gtk_container_add (GTK_CONTAINER (frame), hbox);

  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
				  GTK_POLICY_AUTOMATIC,
				  GTK_POLICY_AUTOMATIC);

  clist_session = gtk_clist_new(1);

  gtk_clist_column_titles_show (GTK_CLIST (clist_session));
  gtk_clist_set_column_auto_resize (GTK_CLIST (clist_session), 0, TRUE);
  gtk_clist_set_selection_mode (GTK_CLIST (clist_session), GTK_SELECTION_BROWSE);
  gtk_clist_set_column_title (GTK_CLIST (clist_session), 0, _("Session Name"));

  gtk_container_add (GTK_CONTAINER (scrolled_window), clist_session);

  gtk_signal_connect (GTK_OBJECT (clist_session), "select_row",
			GTK_SIGNAL_FUNC (select_row), NULL); 
  
  gtk_signal_connect (GTK_OBJECT (protocol), "current_session",
  			GTK_SIGNAL_FUNC (protocol_set_current_session), NULL);
  
  gtk_signal_connect (GTK_OBJECT (protocol), "saved_sessions",
			GTK_SIGNAL_FUNC (saved_sessions), NULL);

  gsm_protocol_get_saved_sessions (GSM_PROTOCOL (protocol));

  
  gtk_box_pack_start (GTK_BOX (hbox), scrolled_window, TRUE, TRUE, 0);

  util_vbox = gtk_vbox_new (FALSE, GNOME_PAD_SMALL);
  gtk_box_pack_start (GTK_BOX (hbox), util_vbox, FALSE, FALSE, 0);

  button = left_aligned_button (_("Add..."));
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
			GTK_SIGNAL_FUNC (add_session_cb), NULL);
  gtk_box_pack_start (GTK_BOX (util_vbox), button, FALSE, FALSE, 0);

  button = left_aligned_button (_("Edit..."));
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
			GTK_SIGNAL_FUNC (edit_session_cb), NULL);
  gtk_box_pack_start (GTK_BOX (util_vbox), button, FALSE, FALSE, 0);

  button = left_aligned_button (_("Delete"));
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
			GTK_SIGNAL_FUNC (delete_session_cb), NULL);
  gtk_box_pack_start (GTK_BOX (util_vbox), button, FALSE, FALSE, 0);
 
  label = gtk_label_new (_("Session Options"));
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), vbox, label);

  /* frame for manually started programs */
  startup_list = startup_list_read ("Default");
  startup_list_revert = startup_list_duplicate (startup_list);
 
  vbox = gtk_vbox_new (FALSE, GNOME_PAD);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), GNOME_PAD_SMALL);
 
  /* Frame for non-session managed startup programs */
 
  frame = gtk_frame_new (_("Non-session-managed Startup Programs"));
  gtk_box_pack_start (GTK_BOX (vbox), frame, TRUE, TRUE, 0);

  hbox = gtk_hbox_new (FALSE, GNOME_PAD);
  gtk_container_set_border_width (GTK_CONTAINER (hbox), GNOME_PAD);
  gtk_container_add (GTK_CONTAINER (frame), hbox);

  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
				  GTK_POLICY_AUTOMATIC,
				  GTK_POLICY_AUTOMATIC);
   
  clist_start = gtk_clist_new (2);

  gtk_clist_column_titles_show (GTK_CLIST (clist_start));
  gtk_clist_set_column_auto_resize (GTK_CLIST (clist_start), 0, TRUE);
  gtk_clist_set_selection_mode (GTK_CLIST (clist_start), GTK_SELECTION_BROWSE);
  gtk_clist_set_column_title (GTK_CLIST (clist_start), 0, _("Priority"));
  gtk_clist_set_column_title (GTK_CLIST (clist_start), 1, _("Command"));

  gtk_container_add (GTK_CONTAINER (scrolled_window), clist_start);
  
  gtk_box_pack_start (GTK_BOX (hbox), scrolled_window, TRUE, TRUE, 0);
  
  util_vbox = gtk_vbox_new (FALSE, GNOME_PAD_SMALL);
  gtk_box_pack_start (GTK_BOX (hbox), util_vbox, FALSE, FALSE, 0);

  button = left_aligned_button (_("Add..."));
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
		      GTK_SIGNAL_FUNC (add_startup_cb), NULL);
  gtk_box_pack_start (GTK_BOX (util_vbox), button, FALSE, FALSE, 0);
  
  button = left_aligned_button (_("Edit..."));
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
		      GTK_SIGNAL_FUNC (edit_startup_cb), NULL);
  gtk_box_pack_start (GTK_BOX (util_vbox), button, FALSE, FALSE, 0);
  
  button = left_aligned_button (_("Delete"));
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
		      GTK_SIGNAL_FUNC (delete_startup_cb), NULL);
  gtk_box_pack_start (GTK_BOX (util_vbox), button, FALSE, FALSE, 0);

  /* Button for running session-properties */

  alignment = gtk_alignment_new (0.0, 0.5, 0.0, 0.0);
  gtk_box_pack_start (GTK_BOX (vbox), alignment, FALSE, FALSE, 0);
  
  button = gtk_button_new_with_label (_("Browse Currently Running Programs..."));
  gtk_misc_set_padding (GTK_MISC (GTK_BIN (button)->child),
			GNOME_PAD, 0);
  
  gtk_container_add (GTK_CONTAINER (alignment), button);

  gtk_signal_connect (GTK_OBJECT (button), "clicked",
  		      GTK_SIGNAL_FUNC (browse_session_cb), NULL);

  label = gtk_label_new (_("Startup Programs"));
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), vbox, label);
  gtk_container_add (GTK_CONTAINER (capplet), notebook);
  
  
  update_gui ();

  gtk_signal_connect (GTK_OBJECT (autosave_button), "toggled",
		      GTK_SIGNAL_FUNC (dirty_cb), capplet);
  gtk_signal_connect (GTK_OBJECT (logout_prompt_button), "toggled",
		      GTK_SIGNAL_FUNC (dirty_cb), capplet);
  gtk_signal_connect (GTK_OBJECT (login_splash_button), "toggled",
		      GTK_SIGNAL_FUNC (dirty_cb), capplet);
  
  update_gui();
  gtk_widget_show_all (capplet);
}

/* CAPPLET CALLBACKS */
static void
write_state (void)
{
  gchar *current_session_tmp = NULL;
  auto_save = GTK_TOGGLE_BUTTON (autosave_button)->active;
  logout_prompt = GTK_TOGGLE_BUTTON (logout_prompt_button)->active;
  login_splash  = GTK_TOGGLE_BUTTON (login_splash_button)->active;
 
  if(clist_session) { 
    gtk_clist_get_text (GTK_CLIST(clist_session), selected_row, 0, &current_session_tmp);
  }
  if(current_session_tmp) {
  /* If the user removes all the session listing we don't want to set 
   * the current session */
    current_session = NULL;
    current_session = g_strdup(current_session_tmp);
  } 
  gnome_config_push_prefix (GSM_OPTION_CONFIG_PREFIX);
  gnome_config_set_bool (LOGOUT_SCREEN_KEY, logout_prompt);
  gnome_config_set_bool (SPLASH_SCREEN_KEY, login_splash);
  gnome_config_pop_prefix ();
  gnome_config_sync ();

  /* Set the autosave mode in the session manager so it takes 
   * effect immediately.  */
  
  gsm_protocol_set_autosave_mode (GSM_PROTOCOL(protocol), auto_save);
  gsm_protocol_set_session_name (GSM_PROTOCOL(protocol), current_session);  
  
  session_list_write (session_list, session_list_revert, hashed_sessions);
  startup_list_write (startup_list, current_session);
}

static void
revert_state (void)
{ 
  GSList *temp;
  auto_save = auto_save_revert;
  logout_prompt = logout_prompt_revert;
  login_splash  = login_splash_revert;
  current_session = NULL;
  current_session = g_strdup(current_session_revert);
  
  selected_row_revert = selected_row; 
  startup_list_free (startup_list);
  startup_list = startup_list_duplicate (startup_list_revert);
  
  temp = session_list_duplicate (session_list);
  session_list_free (session_list);
  session_list = session_list_duplicate (session_list_revert);
  session_list_free (session_list_revert);
  session_list_revert = session_list_duplicate (temp);
  g_slist_free(temp);
}

static void
try (void)
{
  write_state ();
}

static void
revert (void)
{
  revert_state ();
  write_state ();
  deleted_session_list_free();
  update_gui ();
}

static void
ok (void)
{
  write_state ();
  deleted_session_list_free();
  gtk_main_quit();
}

static void
cancel (void)
{
  revert_state ();
  write_state ();
  deleted_session_list_free(); 
  gtk_main_quit();  
}

static void
show_help (void)
{

  GnomeHelpMenuEntry help_entry = { "control-center" };
  help_entry.path = 	"session.html#STARTUP-PROGS";
  gnome_help_display (NULL, &help_entry);

}

static void
page_shown (void)
{
  if (startup_command_dialog)
    gtk_widget_hide (startup_command_dialog);
  if (session_command_dialog)
    gtk_widget_hide (session_command_dialog);
}

static void
page_hidden (void)
{
  if (startup_command_dialog)
    gtk_widget_show (startup_command_dialog);
  if (session_command_dialog)
    gtk_widget_show (session_command_dialog);
}

/* Called to make the contents of the GUI reflect the current settings */
static void
update_gui (void)
{ 
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (autosave_button),
				auto_save);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (logout_prompt_button),
				logout_prompt);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (login_splash_button),
				login_splash);
  startup_list_update_gui (startup_list, clist_start);
  session_list_update_gui (session_list, clist_session, current_session);
}

/* This is called when an change is made in the client list.  */
static void
dirty_cb (GtkWidget *widget, GtkWidget *capplet)
{
  capplet_widget_state_changed (CAPPLET_WIDGET (capplet), TRUE);
}

/* Add a startup program to the list */
static void
add_startup_cb (void)
{
  /* This is bad, bad, bad. We mark the capplet as changed at
   * this point so our dialog doesn't die if the user switches
   * away to a different capplet
   */
  dirty_cb (NULL, capplet);
  startup_list_add_dialog (&startup_list, clist_start, &startup_command_dialog);
  update_gui ();
}

/* Edit a startup program in the list */
static void
edit_startup_cb (void)
{
  /* This is bad, bad, bad. We mark the capplet as changed at
   * this point so our dialog doesn't die if the user switches
   * away to a different capplet
   */
  dirty_cb (NULL, capplet);
  startup_list_edit_dialog (&startup_list, clist_start, &startup_command_dialog);
  update_gui ();
}

/* Remove a startup program from the list */
static void
delete_startup_cb (void)
{
  dirty_cb (NULL, capplet);
  startup_list_delete (&startup_list, clist_start);
  update_gui ();
}

static void
select_row (GtkWidget *widget, gint row) {
        if(selected_row != row && selected_row > -1) {
		dirty_cb (NULL, capplet);
		selected_row_revert = selected_row;
		selected_row = row;		
	}
}

static void 
protocol_set_current_session (GtkWidget *widget, gchar* name)
{
  if(!name) {
    current_session = NULL; 
    current_session = g_strdup(name); 
  }
}

static void
saved_sessions (GtkWidget *widget, GSList* session_names)
{
  GSList *tmplist;
  GSList *tmp;
  gchar *name;
  gint row;
  gboolean current_found = FALSE; 
  
  tmplist = session_list_duplicate (session_names);
  
  for(tmp = tmplist; tmp; tmp = tmp->next) {
    name = (gchar*)tmp->data;
    row = gtk_clist_append (GTK_CLIST (clist_session), &name);
    if(!strcmp (current_session, name)) {
       current_session_revert = g_strdup(current_session);
       gtk_clist_select_row (GTK_CLIST (clist_session), row, 0);
       selected_row = row;
       selected_row_revert = row;
       current_found = TRUE;
    }
  }
  if(!current_found) {
    name = g_strdup(current_session);
    row = gtk_clist_append (GTK_CLIST (clist_session), &name);
    gtk_clist_select_row (GTK_CLIST (clist_session), row, 0);
    selected_row = row;
    selected_row_revert = row;
    tmplist = APPEND (tmplist, g_strdup(current_session));
  } 
  
  session_list = session_list_duplicate (tmplist);
  session_list_revert = session_list_duplicate (tmplist);
}

/* Add a new session name to the list */
static void
add_session_cb(void)
{
  /* This is bad, bad, bad. We mark the capplet as changed at
   * this point so our dialog doesn't die if the user switches
   * away to a different capplet
   */
  dirty_cb (NULL, capplet);
  session_list_add_dialog (&session_list, clist_session, &session_command_dialog);
  update_gui ();
}

/* Edit a session name in the list */
static void
edit_session_cb (void)
{
  /* This is bad, bad, bad. We mark the capplet as changed at
   * this point so our dialog doesn't die if the user switches
   * away to a different capplet
   */
  dirty_cb (NULL, capplet);
  session_list_edit_dialog (&session_list, clist_session, &hashed_sessions, selected_row, &session_command_dialog);
  update_gui ();
}

/* Remove a session name from the list */
static void
delete_session_cb (void)
{
  dirty_cb (NULL, capplet);
  session_list_delete (&session_list, clist_session, selected_row, &session_command_dialog);
  update_gui ();
}

/* Run a browser for the currently running session managed clients */
static void
browse_session_cb (void)
{
  static char *const command[] = {
    "session-properties"
  };

  gnome_execute_async (NULL, 1, command);
}


int
main (int argc, char *argv[])
{
  gint init_result;

  bindtextdomain (PACKAGE, GNOMELOCALEDIR);
  textdomain (PACKAGE);

  init_result = gnome_capplet_init("session-properties", VERSION, argc, argv,
				   NULL, 0, NULL);
  gnome_window_icon_set_default_from_file (GNOME_ICONDIR"/gnome-session.png");
  gtk_signal_connect (GTK_OBJECT (gnome_master_client ()), "die",
		      GTK_SIGNAL_FUNC (gtk_main_quit), NULL);
  gnome_client_set_restart_style (gnome_master_client(), GNOME_RESTART_NEVER);
  switch(init_result) 
    {
    case 0:
    case 1:
      break;

    default:
	    /* We need better error handling.  This prolly just means
	     * one already exists.*/
	    exit (0);
    }

  protocol = gsm_protocol_new (gnome_master_client());
  if (!protocol)
    {
      g_warning ("Could not connect to gnome-session.");
      exit (1);
    }

  /* We make this call immediately, as a convenient way
   * of putting ourselves into command mode; if we
   * don't do this, then the "event loop" that
   * GsmProtocol creates will leak memory all over the
   * place.
   
   * We ignore the resulting "current_session" signal.
   */
  gsm_protocol_get_current_session (GSM_PROTOCOL (protocol));
  switch(init_result) 
    {
    case 0:
      capplet_build ();
      capplet_gtk_main ();
      break;

    case 1:
        gtk_main ();
      break;
    }
  return 0;
}
