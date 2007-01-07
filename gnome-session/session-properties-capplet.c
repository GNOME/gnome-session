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
#include <libgnomeui/gnome-client.h>

#include <gconf/gconf-client.h>
#include "headers.h"

#include "gsm-protocol.h"
#include "session-properties-capplet.h"
#include "session-properties.h"

/* Indicates whether we have to ask for save on close */
static gboolean state_dirty = FALSE;
/* Apply button (for toggling sensitivity) */
extern GtkWidget *apply_button;

/* Startup list */
GtkTreeModel *startup_store;
GtkTreeView *startup_view;
GtkTreeSelection *startup_sel;

static GSList *startup_list;

static GsmProtocol *protocol;

/* capplet widgets */
static GtkWidget *autosave_button;
static GtkWidget *saved_session_label;

/* Other callbacks */
void apply (void);
static void help_cb (void);
static void update_gui (void);
static void add_startup_cb (GtkWidget *button, GtkWidget *parent);
static void edit_startup_cb (GtkWidget *button, GtkWidget *parent);
static void delete_startup_cb (void);
static void save_session_cb (void);

static void
startup_enabled_toggled (GtkCellRendererToggle *cell_renderer,
                         gchar                 *path,
                         gpointer               userdata)
{
  gboolean    active;
  GtkTreeIter iter;

  if (!gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (startup_store),
                                            &iter, path))
      return;

  active = gtk_cell_renderer_toggle_get_active (cell_renderer);
  active = !active;
  gtk_cell_renderer_toggle_set_active (cell_renderer, active);

  if (active)
    startup_list_enable (&startup_list, startup_store, &iter);
  else
    startup_list_disable (&startup_list, startup_store, &iter);

  update_gui ();
}

static void
selection_changed_cb (GtkTreeSelection *selection, GtkTreeView *view)
{
  gboolean sel;
  GtkWidget *edit_button;
  GtkWidget *delete_button;

  edit_button = g_object_get_data (G_OBJECT (view), "edit");
  delete_button = g_object_get_data (G_OBJECT (view), "delete");

  sel = gtk_tree_selection_get_selected (selection, NULL, NULL);

  if (edit_button)
    gtk_widget_set_sensitive (edit_button, sel);

  if (delete_button)
    gtk_widget_set_sensitive (delete_button, sel);
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

static gboolean
show_message_dialog (void)
{
	GtkWidget *dlg;
	gint response;

	dlg = gtk_message_dialog_new (NULL, 0, GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL,
				      _("Some changes are not saved.\nIs it still OK to exit?"));
	response = gtk_dialog_run (GTK_DIALOG (dlg));
	gtk_widget_destroy (dlg);
	if (response != GTK_RESPONSE_OK) 
		return TRUE;
	else
		return FALSE;
}

static gboolean
spc_delete (void)
{
	gint ret = FALSE;
	if (state_dirty)
		ret = show_message_dialog ();

	if (ret == TRUE) 
		return TRUE;
	else {
		gtk_main_quit ();
		return FALSE;
	}
}

static void
spc_close (void)
{
	spc_delete ();
}

static GtkWidget *
capplet_build (void)
{
  GtkWidget *vbox;
  GtkWidget *util_vbox;
  GtkWidget *hbox;
  GtkWidget *button;
  GtkWidget *notebook;
  GtkWidget *label;

  GtkWidget *dlg, *b, *a, *hb, *sw, *help_button;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;

  int font_size;

  GConfClient *client;

  client = gconf_client_get_default ();

  /* Create toplevel dialog */
  dlg = gtk_dialog_new ();
  gtk_window_set_resizable (GTK_WINDOW (dlg), TRUE);
  gtk_window_set_title (GTK_WINDOW (dlg), _("Sessions"));
  gtk_dialog_set_has_separator (GTK_DIALOG (dlg), FALSE);
  gtk_container_set_border_width (GTK_CONTAINER (dlg), 5);
  gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dlg)->vbox), 2);
  help_button = gtk_dialog_add_button (GTK_DIALOG (dlg), GTK_STOCK_HELP, GTK_RESPONSE_HELP);
  g_signal_connect (G_OBJECT (help_button), "clicked", help_cb, NULL);
  b = gtk_dialog_add_button (GTK_DIALOG (dlg), GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE);
  gtk_dialog_set_default_response (GTK_DIALOG (dlg), GTK_RESPONSE_CLOSE);
  g_signal_connect (G_OBJECT (b), "clicked", spc_close, NULL);
  g_signal_connect (dlg, "delete-event", G_CALLBACK(spc_delete), NULL);

  /* Set up the notebook */
  notebook = gtk_notebook_new();
  gtk_container_set_border_width (GTK_CONTAINER (notebook), 5);
  gtk_notebook_set_tab_pos (GTK_NOTEBOOK (notebook), GTK_POS_TOP);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), notebook, TRUE, TRUE, 0);

  /* Frame for manually started programs */
  startup_list = startup_list_read ();

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
  
  startup_store = (GtkTreeModel *) gtk_list_store_new (4,
                                                       G_TYPE_POINTER,
                                                       G_TYPE_BOOLEAN,
                                                       G_TYPE_STRING,
                                                       G_TYPE_BOOLEAN);
  startup_view = (GtkTreeView *) gtk_tree_view_new_with_model (startup_store);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label),
		  		 GTK_WIDGET (startup_view));
  startup_sel = gtk_tree_view_get_selection (startup_view);
  gtk_tree_selection_set_mode (startup_sel, GTK_SELECTION_SINGLE);
  g_signal_connect (G_OBJECT (startup_sel), "changed",
                    (GCallback) selection_changed_cb, startup_view);
  renderer = gtk_cell_renderer_toggle_new ();
  column = gtk_tree_view_column_new_with_attributes (_("Enabled"), renderer,
                                                     "active", 1,
                                                     "activatable", 3,
                                                     NULL);
  gtk_tree_view_append_column (startup_view, column);
  g_signal_connect (G_OBJECT (renderer), "toggled",
                    (GCallback) startup_enabled_toggled, NULL);
  renderer = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes (_("Program"), renderer,
                                                     "text", 2, NULL);
  gtk_tree_view_append_column (startup_view, column);

  gtk_tree_view_set_search_column (startup_view, 2);

  gtk_container_add (GTK_CONTAINER (sw), GTK_WIDGET (startup_view));

  util_vbox = gtk_vbox_new (FALSE, GNOME_PAD_SMALL);
  gtk_box_pack_start (GTK_BOX (hb), util_vbox, FALSE, FALSE, 0);

  /* Add/Edit/Delete buttons */
  button = gtk_button_new_from_stock (GTK_STOCK_NEW);
  g_signal_connect (button, "clicked",
		    G_CALLBACK (add_startup_cb), dlg);
  gtk_box_pack_start (GTK_BOX (util_vbox), button, FALSE, FALSE, 0);
  
  button = gtk_button_new_from_stock (GTK_STOCK_EDIT);
  g_signal_connect (button, "clicked",
		    G_CALLBACK (edit_startup_cb), dlg);
  gtk_box_pack_start (GTK_BOX (util_vbox), button, FALSE, FALSE, 0);
  gtk_widget_set_sensitive (button, FALSE);
  g_object_set_data (G_OBJECT (startup_view), "edit", button);
  
  button = gtk_button_new_from_stock (GTK_STOCK_DELETE);
  g_signal_connect (button, "clicked",
		    G_CALLBACK (delete_startup_cb), NULL);
  gtk_box_pack_start (GTK_BOX (util_vbox), button, FALSE, FALSE, 0);
  gtk_widget_set_sensitive (button, FALSE);
  g_object_set_data (G_OBJECT (startup_view), "delete", button);

  /* Button for running session-properties */
  a = gtk_alignment_new (0.0, 0.5, 0.0, 0.0);
  gtk_box_pack_start (GTK_BOX (vbox), a, FALSE, FALSE, 0);
  
  label = gtk_label_new (_("Startup Programs"));
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), vbox, label);
  

  vbox = session_properties_create_page (&mark_dirty);

  label = gtk_label_new (_("Current Session"));
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), vbox, label);
 
  /* Options page - Autosave, Save session */
  vbox = gtk_vbox_new (FALSE, GNOME_PAD);
  
  util_vbox = gtk_vbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), util_vbox, FALSE, FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (vbox) , GNOME_PAD);

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

  /* Save session now */
  util_vbox = gtk_vbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), util_vbox, FALSE, FALSE, 0);

  hbox = gtk_hbutton_box_new ();
  gtk_box_pack_start (GTK_BOX (util_vbox), hbox, FALSE, FALSE, 0);

  button = gtk_button_new_with_label (_("Save the current session"));
  gtk_button_set_image (GTK_BUTTON (button), gtk_image_new_from_stock (GTK_STOCK_SAVE, GTK_ICON_SIZE_BUTTON));
  gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, FALSE, 0);
  g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (save_session_cb), NULL);

  saved_session_label = gtk_label_new("");
  gtk_box_pack_start (GTK_BOX (util_vbox), saved_session_label, FALSE, FALSE, 0);

  label = gtk_label_new (_("Session Options"));
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), vbox, label);

  update_gui ();

  font_size = pango_font_description_get_size (dlg->style->font_desc);
  font_size = PANGO_PIXELS (font_size);
  gtk_window_set_default_size (dlg, -1, font_size * 40);

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
  gnome_help_display_desktop (NULL, "user-guide", "user-guide.xml",
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

/* Called to make the contents of the GUI reflect the current settings */
static void
update_gui (void)
{ 
  /* We do not have to set checkbuttons here (Lauris) */
  startup_list_update_gui (&startup_list, startup_store, startup_sel);
}

/* This is called when an change is made in the client list.  */
void
mark_dirty (void)
{
  state_dirty = TRUE;
  gtk_widget_set_sensitive (apply_button, TRUE);
}

/* Add a startup program to the list */
static void
add_startup_cb (GtkWidget *button, GtkWidget *parent)
{
  startup_list_add_dialog (&startup_list, parent);
  update_gui ();
}

/* Edit a startup program in the list */
static void
edit_startup_cb (GtkWidget *button, GtkWidget *parent)
{
  startup_list_edit_dialog (&startup_list, startup_store, startup_sel, parent);
  update_gui ();
}

/* Remove a startup program from the list */
static void
delete_startup_cb (void)
{
  startup_list_delete (&startup_list, startup_store, startup_sel);
  update_gui ();
}

static void
update_saved_session_label_cb (void)
{
  gtk_label_set_text (GTK_LABEL (saved_session_label),
                      _("Your session has been saved."));
}

/* Save the current session */
static void
save_session_cb (void)
{
  g_signal_connect (gnome_master_client (), "save_complete",
                    G_CALLBACK (update_saved_session_label_cb), NULL);
  gnome_client_request_save (gnome_master_client (), GNOME_SAVE_BOTH, 0,
                             GNOME_INTERACT_NONE, 0, 1);
}

int
main (int argc, char *argv[])
{
  GConfClient *client;
  GOptionContext *context;
  GtkWidget *dlg;
  GnomeClient *master_client;

  bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  context = g_option_context_new ("");

  gnome_program_init ("gnome-session-properties", VERSION, 
		      LIBGNOMEUI_MODULE, argc, argv, 
		      GNOME_PARAM_GOPTION_CONTEXT, context,
		      GNOME_PROGRAM_STANDARD_PROPERTIES,
		      NULL);

  /* Don't restart the capplet, it's the place where the session is saved. */
  gnome_client_set_restart_style (gnome_master_client (), GNOME_RESTART_NEVER);

  gtk_window_set_default_icon_name ("gnome-session");

  client = gconf_client_get_default ();
  gconf_client_add_dir (client, GSM_GCONF_CONFIG_PREFIX, GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);

  master_client = gnome_master_client ();
  if (! GNOME_CLIENT_CONNECTED (master_client)) {
    g_printerr (_("could not connect to the session manager\n"));
    exit (1);
  }

  protocol = gsm_protocol_new (master_client);
  if (!protocol) {
    g_printerr (_("session manager does not support GNOME extensions\n"));
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

  dlg = capplet_build ();

  gtk_main ();

  startup_list_free (startup_list);

  return 0;
}
