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

#include <gconf/gconf-client.h>
#include "headers.h"

#include "gsm-protocol.h"
#include "session-properties-capplet.h"
#include "session-properties.h"

/* Indicates whether we have to ask for save on close */
static gboolean state_dirty = FALSE;
/* Apply button (for toggling sensitivity) */
extern GtkWidget *apply_button;

/* Current state */
GSList *deleted_sessions = NULL;

/* Sessions list */
GtkTreeModel *sessions_store;
GtkTreeView *sessions_view;
GtkTreeSelection *sessions_sel;
/* Startup list */
GtkTreeModel *startup_store;
GtkTreeView *startup_view;
GtkTreeSelection *startup_sel;

static GSList *session_list = NULL;
static GSList *session_list_revert = NULL;

static gchar* current_session;
static gchar* current_session_revert;

static GSList *startup_list;
static GSList *startup_list_revert;

static GHashTable *hashed_sessions;

static GsmProtocol *protocol;

/* capplet widgets */
static GtkWidget *capplet;
static GtkWidget *autosave_button;
static GtkWidget *logout_prompt_button;
static GtkWidget *login_splash_button;

static GtkWidget *startup_command_dialog;
static GtkWidget *session_command_dialog;

/* Other callbacks */
void apply (void);
static void help_cb (void);
static void update_gui (void);
static void dirty_cb (void);
static void add_startup_cb (void);
static void edit_startup_cb (void);
static void delete_startup_cb (void);
static void add_session_cb (void);
static void edit_session_cb (void);
static void delete_session_cb (void);

static void protocol_set_current_session (GtkWidget *widget, gchar *name);
static void saved_sessions (GtkWidget *widget, GSList *session_names);

#define SESSION_STOCK_EDIT "session-stock-edit"

static void
register_stock_for_edit (void)
{
  static gboolean registered = FALSE;

  if (!registered)
  {
    GtkIconFactory *factory;
    GtkIconSet     *icons;

    static GtkStockItem edit_item [] = {
	{ SESSION_STOCK_EDIT, N_("_Edit"), 0, 0, GETTEXT_PACKAGE }, 
    	};

    icons = gtk_icon_factory_lookup_default (GTK_STOCK_PREFERENCES);
    factory = gtk_icon_factory_new ();
    gtk_icon_factory_add (factory, SESSION_STOCK_EDIT, icons);
    gtk_icon_factory_add_default (factory);
    gtk_stock_add_static (edit_item, 1);
    registered = TRUE;
  }
}

static GtkWidget *
left_aligned_stock_button (gchar *label)
{
  GtkWidget *button = gtk_button_new_from_stock (label);
  GtkWidget *child  = gtk_bin_get_child (GTK_BIN (button));

  child = gtk_bin_get_child (GTK_BIN (button));

  if (GTK_IS_ALIGNMENT (child))
	        g_object_set (G_OBJECT (child), "xalign", 0.0, NULL);
  else if (GTK_IS_LABEL (child))
		g_object_set (G_OBJECT (child), "xalign", 0.0, NULL);

  return button;
}

static void
spc_value_toggled (GtkToggleButton *tb, gpointer data)
{
	GConfClient *client;
	gboolean gval, bval;
	const gchar *key;

	client = (GConfClient *) data;

	key = g_object_get_data (G_OBJECT (tb), "key");
	gval = gconf_client_get_bool (client, key, NULL);
	bval = gtk_toggle_button_get_active (tb);

	/* FIXME: Does GConf emit signal even if setting to the same value (Lauris) */
	if (gval != bval) {
		gconf_client_set_bool (client, key, bval, NULL);
	}
}

static void
spc_value_notify (GConfClient *client, guint id, GConfEntry *entry, gpointer tb)
{
	gboolean gval, bval;

	gval = gconf_value_get_bool (entry->value);
	bval = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (tb));

	/* FIXME: Here we can probably avoid check and just set_active (lauris) */
	if (bval != gval) {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tb), gval);
	}
}

static void
spc_close (void)
{
	if (state_dirty) {
		GtkWidget *dlg;
		gint response;
		dlg = gtk_message_dialog_new (NULL, 0, GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL,
					      _("Some changes are not saved.\nIs it still OK to exit?"));
		response = gtk_dialog_run (GTK_DIALOG (dlg));
		gtk_widget_destroy (dlg);
		if (response == GTK_RESPONSE_CANCEL) return;
	}

	gtk_main_quit ();
}

static GtkWidget *
capplet_build (void)
{
  GtkWidget *vbox;
  GtkWidget *util_vbox;
  GtkWidget *button;
  GtkWidget *notebook;
  GtkWidget *label;

  GtkWidget *dlg, *b, *a, *hb, *sw, *help_button;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;

  GConfClient *client;

  client = gconf_client_get_default ();

  /* Create toplevel dialog */
  dlg = gtk_dialog_new ();
  gtk_window_set_resizable (GTK_WINDOW (dlg), TRUE);
  gtk_window_set_title (GTK_WINDOW (dlg), _("Sessions"));
  help_button = gtk_dialog_add_button (GTK_DIALOG (dlg), GTK_STOCK_HELP, GTK_RESPONSE_HELP);
  g_signal_connect (G_OBJECT (help_button), "clicked", help_cb, NULL);
  b = gtk_dialog_add_button (GTK_DIALOG (dlg), GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE);
  g_signal_connect (G_OBJECT (b), "clicked", spc_close, NULL);
  g_signal_connect (dlg, "delete-event", spc_close, NULL);

  /* FIXME: Get rid of global capplet variable, or use only it, not both (Lauris) */
  capplet = dlg;

  /* Retrieve options */

  gnome_config_push_prefix (GSM_OPTION_CONFIG_PREFIX);
  
  /* Set up the notebook */
  notebook = gtk_notebook_new();
  gtk_notebook_set_tab_pos (GTK_NOTEBOOK (notebook), GTK_POS_TOP);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), notebook, TRUE, TRUE, 0);

  /* Options page - Set Current Session, Logout Prompt, Autosave, Splash */
  vbox = gtk_vbox_new (FALSE, GNOME_PAD);
  
  util_vbox = gtk_vbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), util_vbox, FALSE, FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (vbox) , GNOME_PAD);
  /* Splash screen */
  a = gtk_alignment_new (0.0, 0.5, 0.0, 0.0);
  gtk_box_pack_start (GTK_BOX (util_vbox), a, FALSE, FALSE, 0);
  login_splash_button = gtk_check_button_new_with_mnemonic (_("Show splash screen on _login"));
  gtk_container_add (GTK_CONTAINER (a), login_splash_button);
  g_object_set_data (G_OBJECT (login_splash_button), "key", SPLASH_SCREEN_KEY);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (login_splash_button),
				gconf_client_get_bool (client, SPLASH_SCREEN_KEY, NULL));
  gconf_client_notify_add (client, SPLASH_SCREEN_KEY, spc_value_notify, login_splash_button, NULL, NULL);
  g_signal_connect (login_splash_button, "toggled", G_CALLBACK (spc_value_toggled), client);

  /* Logout prompt */
  a = gtk_alignment_new (0.0, 0.5, 0.0, 0.0);
  gtk_box_pack_start (GTK_BOX (util_vbox), a, FALSE, FALSE, 0);
  logout_prompt_button = gtk_check_button_new_with_mnemonic (_("_Prompt on logout"));
  gtk_container_add (GTK_CONTAINER (a), logout_prompt_button);
  g_object_set_data (G_OBJECT (logout_prompt_button), "key", LOGOUT_PROMPT_KEY);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (logout_prompt_button),
				gconf_client_get_bool (client, LOGOUT_PROMPT_KEY, NULL));
  gconf_client_notify_add (client, LOGOUT_PROMPT_KEY, spc_value_notify, logout_prompt_button, NULL, NULL);
  g_signal_connect (G_OBJECT (logout_prompt_button), "toggled", G_CALLBACK (spc_value_toggled), client);

  /* Autosave */
  a = gtk_alignment_new (0.0, 0.5, 0.0, 0.0);
  gtk_box_pack_start (GTK_BOX (util_vbox), a, FALSE, FALSE, 0);
  autosave_button = gtk_check_button_new_with_mnemonic (_("Automatically save chan_ges to session"));
  gtk_container_add (GTK_CONTAINER (a), autosave_button);
  g_object_set_data (G_OBJECT (autosave_button), "key", AUTOSAVE_MODE_KEY);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (autosave_button),
				gconf_client_get_bool (client, AUTOSAVE_MODE_KEY, NULL));
  gconf_client_notify_add (client, AUTOSAVE_MODE_KEY, spc_value_notify, autosave_button, NULL, NULL);
  g_signal_connect (G_OBJECT (autosave_button), "toggled", G_CALLBACK (spc_value_toggled), client);

  /* Session names list */
  a = gtk_alignment_new (0.0, 0.5, 0.0, 0.0);
  gtk_box_pack_start (GTK_BOX (vbox), a, FALSE, FALSE, 0);
  label = gtk_label_new_with_mnemonic (_("_Sessions:"));
  gtk_container_add (GTK_CONTAINER (a), label);
  hb = gtk_hbox_new (FALSE, GNOME_PAD);
  gtk_box_pack_start (GTK_BOX (vbox), hb, TRUE, TRUE, 0);

  sw = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw), GTK_SHADOW_IN);
  gtk_box_pack_start (GTK_BOX (hb), sw, TRUE, TRUE, 0);

  sessions_store = (GtkTreeModel *) gtk_list_store_new (1, G_TYPE_STRING);
  sessions_view = (GtkTreeView *) gtk_tree_view_new_with_model (sessions_store);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), 
		  		 GTK_WIDGET (sessions_view));
  sessions_sel = gtk_tree_view_get_selection (sessions_view);
  gtk_tree_selection_set_mode (sessions_sel, GTK_SELECTION_SINGLE);
  renderer = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes (_("Session Name"), renderer, "text", 0, NULL);
  gtk_tree_view_append_column (sessions_view, column);

  gtk_container_add (GTK_CONTAINER (sw), GTK_WIDGET (sessions_view));

  /* Add/Edit/Delete buttons */
  util_vbox = gtk_vbox_new (FALSE, GNOME_PAD_SMALL);
  gtk_box_pack_start (GTK_BOX (hb), util_vbox, FALSE, FALSE, 0);

  button = left_aligned_stock_button (GTK_STOCK_ADD);
  gtk_box_pack_start (GTK_BOX (util_vbox), button, FALSE, FALSE, 0);
  g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (add_session_cb), NULL);

  button = left_aligned_stock_button (SESSION_STOCK_EDIT);
  gtk_box_pack_start (GTK_BOX (util_vbox), button, FALSE, FALSE, 0);
  g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (edit_session_cb), NULL);

  button = left_aligned_stock_button (GTK_STOCK_DELETE);
  gtk_box_pack_start (GTK_BOX (util_vbox), button, FALSE, FALSE, 0);
  g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (delete_session_cb), NULL);
 
  /* Set up the hash table to keep tracked of edited session names */

  hashed_sessions = g_hash_table_new(g_str_hash,g_str_equal);

  /* FIXME - don't really want this here...the protocol should set current_session
     but I'm a little unsure if it actually does though */ 
  current_session = gnome_config_get_string (CURRENT_SESSION_KEY "=" DEFAULT_SESSION);
  /* if key was specified but is blank, just use the default */
  if (!*current_session) {
	  g_free (current_session);
	  current_session = g_strdup (DEFAULT_SESSION);
  }

  gnome_config_pop_prefix ();
  
  startup_list = NULL;


  g_signal_connect (G_OBJECT (protocol), "current_session",
		    G_CALLBACK (protocol_set_current_session), NULL);
  
  g_signal_connect (G_OBJECT (protocol), "saved_sessions",
		    G_CALLBACK (saved_sessions), NULL);

  gsm_protocol_get_saved_sessions (GSM_PROTOCOL (protocol));
  
  label = gtk_label_new (_("Session Options"));
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), vbox, label);

  /* Frame for manually started programs */
  startup_list = startup_list_read ("Default");
  startup_list_revert = startup_list_duplicate (startup_list);

  vbox = session_properties_create_page (&dirty_cb);

  label = gtk_label_new (_("Current Session"));
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), vbox, label);
 
  vbox = gtk_vbox_new (FALSE, GNOME_PAD);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), GNOME_PAD);
 
  /*non-session managed startup programs */
  a = gtk_alignment_new (0.0, 0.5, 0.0, 0.0);
  gtk_box_pack_start (GTK_BOX (vbox), a, FALSE, FALSE, 0);
  label = gtk_label_new_with_mnemonic (_("Additional startup _programs:"));
  gtk_container_add (GTK_CONTAINER (a), label);

  hb = gtk_hbox_new (FALSE, GNOME_PAD);
  gtk_box_pack_start (GTK_BOX (vbox), hb, TRUE, TRUE,0);

  sw = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw), GTK_SHADOW_IN);
  gtk_box_pack_start (GTK_BOX (hb), sw, TRUE, TRUE, 0);
  
  startup_store = (GtkTreeModel *) gtk_list_store_new (3, G_TYPE_POINTER, G_TYPE_STRING, G_TYPE_STRING);
  startup_view = (GtkTreeView *) gtk_tree_view_new_with_model (startup_store);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label),
		  		 GTK_WIDGET (startup_view));
  startup_sel = gtk_tree_view_get_selection (startup_view);
  gtk_tree_selection_set_mode (startup_sel, GTK_SELECTION_SINGLE);
  renderer = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes (_("Order"), renderer, "text", 1, NULL);
  gtk_tree_view_append_column (startup_view, column);
  column = gtk_tree_view_column_new_with_attributes (_("Command"), renderer, "text", 2, NULL);
  gtk_tree_view_append_column (startup_view, column);

  gtk_container_add (GTK_CONTAINER (sw), GTK_WIDGET (startup_view));

  util_vbox = gtk_vbox_new (FALSE, GNOME_PAD_SMALL);
  gtk_box_pack_start (GTK_BOX (hb), util_vbox, FALSE, FALSE, 0);

  /* Add/Edit/Delete buttons */
  button = left_aligned_stock_button (GTK_STOCK_ADD);
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
		      GTK_SIGNAL_FUNC (add_startup_cb), NULL);
  gtk_box_pack_start (GTK_BOX (util_vbox), button, FALSE, FALSE, 0);
  
  button = left_aligned_stock_button (SESSION_STOCK_EDIT);
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
		      GTK_SIGNAL_FUNC (edit_startup_cb), NULL);
  gtk_box_pack_start (GTK_BOX (util_vbox), button, FALSE, FALSE, 0);
  
  button = left_aligned_stock_button (GTK_STOCK_DELETE);
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
		      GTK_SIGNAL_FUNC (delete_startup_cb), NULL);
  gtk_box_pack_start (GTK_BOX (util_vbox), button, FALSE, FALSE, 0);

  /* Button for running session-properties */
  a = gtk_alignment_new (0.0, 0.5, 0.0, 0.0);
  gtk_box_pack_start (GTK_BOX (vbox), a, FALSE, FALSE, 0);
  
  label = gtk_label_new (_("Startup Programs"));
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), vbox, label);
  
  update_gui ();

  gtk_widget_show_all (dlg);
  gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), 0);

  return dlg;
}

void
apply (void)
{
  session_properties_apply ();

  gtk_widget_set_sensitive (apply_button, FALSE);
  state_dirty = FALSE;
}


static void
help_cb (void)
{
  GError *error = NULL;
  gnome_help_display_desktop (NULL, "user-guide", "wgoscustlookandfeel.xml",
                              "goscustsession-5", &error);
  if (error) {
    GtkWidget *dialog;

    dialog = gtk_message_dialog_new (NULL,
                                     GTK_DIALOG_MODAL,
                                     GTK_MESSAGE_ERROR,
                                     GTK_BUTTONS_CLOSE,
                                     ("There was an error displaying help: \n%s"),
                                     error->message);

    g_signal_connect (G_OBJECT (dialog), "response",
                      G_CALLBACK (gtk_widget_destroy),
                      NULL);

    gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
    gtk_widget_show (dialog);
    g_error_free (error);

  }
}

void
spc_write_state (void)
{
  GtkTreeIter iter;

  if (gtk_tree_selection_get_selected (sessions_sel, NULL, &iter)) {
    gchar *str;
    gtk_tree_model_get (sessions_store, &iter, 0, &str, -1);
    if (str) {
      /* If the user removes all the session listing we don't want to set
       * the current session */
      if (current_session) g_free (current_session);
      current_session = g_strdup (str);
    }
  }
  
  session_list_write (session_list, session_list_revert, hashed_sessions);
  startup_list_write (startup_list, current_session);

  state_dirty = FALSE;
}

/* Called to make the contents of the GUI reflect the current settings */
static void
update_gui (void)
{ 
  /* We do not have to set checkbuttons here (Lauris) */
  startup_list_update_gui (startup_list, startup_store, startup_sel);
  session_list_update_gui (session_list, sessions_store, sessions_sel, current_session);
}

/* This is called when an change is made in the client list.  */
static void
dirty_cb (void)
{
  state_dirty = TRUE;
  gtk_widget_set_sensitive (apply_button, TRUE);
}

/* Add a startup program to the list */
static void
add_startup_cb (void)
{
  /* This is bad, bad, bad. We mark the capplet as changed at
   * this point so our dialog doesn't die if the user switches
   * away to a different capplet
   */
  dirty_cb ();
  startup_list_add_dialog (&startup_list, &startup_command_dialog, capplet);
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
  dirty_cb ();
  startup_list_edit_dialog (&startup_list, startup_store, startup_sel, &startup_command_dialog, capplet);
  update_gui ();
}

/* Remove a startup program from the list */
static void
delete_startup_cb (void)
{
  dirty_cb ();
  startup_list_delete (&startup_list, startup_store, startup_sel);
  update_gui ();
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
  GtkTreeIter iter;
  gboolean current_found = FALSE; 
  
  tmplist = session_list_duplicate (session_names);
  
  for(tmp = tmplist; tmp; tmp = tmp->next) {
    name = (gchar*) tmp->data;
    gtk_list_store_append (GTK_LIST_STORE (sessions_store), &iter);
    gtk_list_store_set (GTK_LIST_STORE (sessions_store), &iter, 0, name, -1);
    if(!strcmp (current_session, name)) {
       current_session_revert = g_strdup (current_session);
       gtk_tree_selection_select_iter (sessions_sel, &iter);
       current_found = TRUE;
    }
  }
  if(!current_found) {
    name = g_strdup(current_session);
    gtk_list_store_append (GTK_LIST_STORE (sessions_store), &iter);
    gtk_list_store_set (GTK_LIST_STORE (sessions_store), &iter, 0, name, -1);
    gtk_tree_selection_select_iter (sessions_sel, &iter);
    tmplist = g_slist_append (tmplist, g_strdup (current_session));
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
  dirty_cb ();
  session_list_add_dialog (&session_list, &session_command_dialog, capplet);
  update_gui ();
}

/* Edit a session name in the list */
static void
edit_session_cb (void)
{
  GtkTreeIter iter;
  gchar *str;
  /* This is bad, bad, bad. We mark the capplet as changed at
   * this point so our dialog doesn't die if the user switches
   * away to a different capplet
   */
  dirty_cb ();
  if (!gtk_tree_selection_get_selected (sessions_sel, NULL, &iter)) return;
  gtk_tree_model_get (sessions_store, &iter, 0, &str, -1);
  session_list_edit_dialog (&session_list, str, &hashed_sessions, &session_command_dialog, capplet);
  update_gui ();
}

/* Remove a session name from the list */
static void
delete_session_cb (void)
{
  GtkTreeIter iter;
  gchar *str;
  dirty_cb ();
  if (!gtk_tree_selection_get_selected (sessions_sel, NULL, &iter)) return;
  gtk_tree_model_get (sessions_store, &iter, 0, &str, -1);
  session_list_delete (&session_list, str, &session_command_dialog);
  update_gui ();
}

int
main (int argc, char *argv[])
{
  GConfClient *client;
  GtkWidget *dlg;

  bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  gnome_program_init ("gnome-session-properties", VERSION, 
		      LIBGNOMEUI_MODULE, argc, argv, 
		      GNOME_PROGRAM_STANDARD_PROPERTIES,
		      NULL);

  gnome_window_icon_set_default_from_file (GNOME_ICONDIR "/gnome-session.png");

  client = gconf_client_get_default ();
  gconf_client_add_dir (client, GSM_GCONF_CONFIG_PREFIX, GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);

  protocol = gsm_protocol_new (gnome_master_client());
  if (!protocol) {
	  g_warning ("Could not connect to gnome-session.");
	  exit (1);
  }

  g_signal_connect (gnome_master_client (), "die", G_CALLBACK (gtk_main_quit), NULL);

  /* We make this call immediately, as a convenient way
   * of putting ourselves into command mode; if we
   * don't do this, then the "event loop" that
   * GsmProtocol creates will leak memory all over the
   * place.
   * We ignore the resulting "current_session" signal.
   */
  gsm_protocol_get_current_session (GSM_PROTOCOL (protocol));

  register_stock_for_edit ();

  dlg = capplet_build ();

  gtk_main ();

  return 0;
}
