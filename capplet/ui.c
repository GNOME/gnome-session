/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 * ui.c
 * Copyright (C) 1999 Free Software Foundation, Inc.
 * Copyright (C) 2008 Lucas Rocha.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "config.h"

#include <string.h>

#include <glib/gi18n.h>
#include <gio/gio.h>

#include <gconf/gconf-client.h>
#include <libgnomeui/gnome-help.h>
#include <glade/glade.h>

#include "eggdesktopfile.h"
#include "gsm-util.h"
#include "ui.h"
#include "commands.h"

#define CAPPLET_DIALOG_WIDGET_NAME        "session_properties_capplet"
#define CAPPLET_TREEVIEW_WIDGET_NAME      "session_properties_treeview"
#define CAPPLET_CLOSE_WIDGET_NAME         "session_properties_close_button"
#define CAPPLET_HELP_WIDGET_NAME          "session_properties_help_button"
#define CAPPLET_ADD_WIDGET_NAME           "session_properties_add_button"
#define CAPPLET_DELETE_WIDGET_NAME        "session_properties_delete_button"
#define CAPPLET_EDIT_WIDGET_NAME          "session_properties_edit_button"
#define CAPPLET_SAVE_WIDGET_NAME          "session_properties_save_button"
#define CAPPLET_REMEMBER_WIDGET_NAME      "session_properties_remember_toggle"
#define CAPPLET_EDIT_DIALOG_WIDGET_NAME   "session_properties_edit_dialog"
#define CAPPLET_NAME_ENTRY_WIDGET_NAME    "session_properties_name_entry"
#define CAPPLET_COMMAND_ENTRY_WIDGET_NAME "session_properties_command_entry"
#define CAPPLET_COMMENT_ENTRY_WIDGET_NAME "session_properties_comment_entry"
#define CAPPLET_BROWSE_WIDGET_NAME        "session_properties_browse_button"

static char *
make_exec_uri (const char *exec)
{
        GString    *str;
        const char *c;

        if (!exec) {
                return g_strdup ("");
        }

        if (!strchr (exec, ' ')) {
                return g_strdup (exec);
        }

        str = g_string_new_len (NULL, strlen (exec));

        str = g_string_append_c (str, '"');
        for (c = exec; *c != '\0'; c++) {
                /* FIXME: GKeyFile will add an additional backslach so we'll
                 * end up with toto\\" instead of toto\"
                 * We could use g_key_file_set_value(), but then we don't
                 * benefit from the other escaping that glib is doing...
                 */
                if (*c == '"') {
                        str = g_string_append (str, "\\\"");
                } else {
                        str = g_string_append_c (str, *c);
                }
        }
        str = g_string_append_c (str, '"');

        return g_string_free (str, FALSE);
}

static void
cmd_browse_button_clicked_cb (GladeXML *xml)
{
        GtkWidget *chooser;
        GtkWidget *dlg;
        GtkWidget *cmd_entry;
        int response;

        dlg = glade_xml_get_widget (xml, CAPPLET_EDIT_DIALOG_WIDGET_NAME);

        cmd_entry = glade_xml_get_widget (xml, CAPPLET_COMMAND_ENTRY_WIDGET_NAME);

        chooser = gtk_file_chooser_dialog_new ("", GTK_WINDOW (dlg),
                                               GTK_FILE_CHOOSER_ACTION_OPEN,
                                               GTK_STOCK_CANCEL,
                                               GTK_RESPONSE_CANCEL,
                                               GTK_STOCK_OPEN,
                                               GTK_RESPONSE_ACCEPT,
                                               NULL);

        gtk_window_set_transient_for (GTK_WINDOW (chooser),
                                      GTK_WINDOW (dlg));

        gtk_window_set_destroy_with_parent (GTK_WINDOW (chooser), TRUE);

        gtk_window_set_title (GTK_WINDOW (chooser), _("Select Command"));

        gtk_widget_show (chooser);

        response = gtk_dialog_run (GTK_DIALOG (chooser));

        if (response == GTK_RESPONSE_ACCEPT) {
                char *text;
                char *uri;

                text = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (chooser));

                uri = make_exec_uri (text);

                g_free (text);

                g_assert (cmd_entry != NULL);

                gtk_entry_set_text (GTK_ENTRY (cmd_entry), uri);

                g_free (uri);
        }

        gtk_widget_destroy (chooser);
}

static void
entry_activate_cb (GtkEntry *entry,
                   void     *data)
{
        gtk_dialog_response (GTK_DIALOG (data), GTK_RESPONSE_OK);
}

static gboolean
edit_app_dialog (const char   *title,
                 GtkListStore *store,
                 GtkTreeIter  *iter,
                 GtkWidget    *parent)
{
        GladeXML *xml;
        GtkWidget *dlg;
        GtkWidget *browse_button;
        GtkWidget *name_entry;
        GtkWidget *cmd_entry;
        GtkWidget *comment_entry;
        char *c_name, *c_command, *c_comment;

        gtk_tree_model_get (GTK_TREE_MODEL (store), iter,
                            STORE_COL_NAME, &c_name,
                            STORE_COL_COMMAND, &c_command,
                            STORE_COL_COMMENT, &c_comment,
                            -1);

        /* FIXME: ugly to read same xml file twice */
        xml = glade_xml_new (DATA_DIR "/session-properties.glade",
                             CAPPLET_EDIT_DIALOG_WIDGET_NAME, NULL);

        dlg = glade_xml_get_widget (xml, CAPPLET_EDIT_DIALOG_WIDGET_NAME);

        name_entry = glade_xml_get_widget (xml, CAPPLET_NAME_ENTRY_WIDGET_NAME);

        g_signal_connect (name_entry, "activate",
                          G_CALLBACK (entry_activate_cb),
                          dlg);

        if (c_name) {
                gtk_entry_set_text (GTK_ENTRY (name_entry), c_name);
        }

        browse_button = glade_xml_get_widget (xml, CAPPLET_BROWSE_WIDGET_NAME);

        g_signal_connect_swapped (browse_button, "clicked",
                                  G_CALLBACK (cmd_browse_button_clicked_cb),
                                  xml);

        cmd_entry = glade_xml_get_widget (xml, CAPPLET_COMMAND_ENTRY_WIDGET_NAME);

        g_signal_connect (cmd_entry, "activate",
                          G_CALLBACK (entry_activate_cb),
                          dlg);

        if (c_command) {
                gtk_entry_set_text (GTK_ENTRY (cmd_entry), c_command);
        }

        comment_entry = glade_xml_get_widget (xml, CAPPLET_COMMENT_ENTRY_WIDGET_NAME);

        g_signal_connect (comment_entry, "activate",
                          G_CALLBACK (entry_activate_cb),
                          dlg);

        if (c_comment) {
                gtk_entry_set_text (GTK_ENTRY (comment_entry), c_comment);
        }

        gtk_window_set_title (GTK_WINDOW (dlg), title);

        gtk_window_set_transient_for (GTK_WINDOW (dlg), GTK_WINDOW (parent));

        g_free (c_name);
        g_free (c_command);
        g_free (c_comment);

        while (gtk_dialog_run (GTK_DIALOG (dlg)) == GTK_RESPONSE_OK) {
                const char *name = gtk_entry_get_text (GTK_ENTRY (name_entry));
                const char *command = gtk_entry_get_text (GTK_ENTRY (cmd_entry));
                const char *comment = gtk_entry_get_text (GTK_ENTRY (comment_entry));
                const char *error_msg = NULL;
                char **argv;
                char *description;
                int argc;
                GError *error = NULL;

                if (gsm_util_text_is_blank (name)) {
                        error_msg = _("The name of the startup program cannot be empty");
                }

                if (gsm_util_text_is_blank (command)) {
                        error_msg = _("The startup command cannot be empty");
                }

                if (!g_shell_parse_argv (command, &argc, &argv, &error)) {
                        if (error) {
                                error_msg = error->message;
                        } else {
                                error_msg = _("The startup command is not valid");
                        }
                }

                if (error_msg != NULL) {
                        GtkWidget *msgbox;

                        gtk_widget_show (dlg);

                        msgbox = gtk_message_dialog_new (GTK_WINDOW (dlg),
                                                         GTK_DIALOG_MODAL,
                                                         GTK_MESSAGE_ERROR,
                                                         GTK_BUTTONS_OK,
                                                         error_msg);

                        if (error) {
                                g_error_free (error);
                        }

                        gtk_dialog_run (GTK_DIALOG (msgbox));

                        gtk_widget_destroy (msgbox);
                } else {
                        g_strfreev (argv);

                        description = spc_command_get_app_description (name, comment);

                        gtk_list_store_set (GTK_LIST_STORE (store), iter,
                                            STORE_COL_DESCRIPTION, description,
                                            STORE_COL_NAME, name,
                                            STORE_COL_COMMAND, command,
                                            STORE_COL_COMMENT, comment,
                                            -1);

                        g_free (description);

                        gtk_widget_destroy (dlg);

                        return TRUE;
                }
        }

        gtk_widget_destroy (dlg);

        return FALSE;
}

static void
selection_changed_cb (GtkTreeSelection *selection,
                      GladeXML         *xml)
{
        GtkWidget *edit_button;
        GtkWidget *delete_button;
        gboolean   sel;

        edit_button = glade_xml_get_widget (xml, CAPPLET_EDIT_WIDGET_NAME);
        delete_button = glade_xml_get_widget (xml, CAPPLET_DELETE_WIDGET_NAME);

        sel = gtk_tree_selection_get_selected (selection, NULL, NULL);

        if (edit_button) {
                gtk_widget_set_sensitive (edit_button, sel);
        }

        if (delete_button) {
                gtk_widget_set_sensitive (delete_button, sel);
        }
}

static void
startup_enabled_toggled_cb (GtkCellRendererToggle *cell_renderer,
                            gchar                 *path,
                            GtkListStore          *store)
{
        GtkTreeIter iter;
        gchar *desktop_file_path;
        gboolean active;

        if (!gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (store),
                                                  &iter, path)) {
                return;
        }

        gtk_tree_model_get (GTK_TREE_MODEL (store), &iter,
                            STORE_COL_DESKTOP_FILE, &desktop_file_path,
                            -1);

        active = gtk_cell_renderer_toggle_get_active (cell_renderer);
        active = !active;
        gtk_cell_renderer_toggle_set_active (cell_renderer, active);

        if (active) {
                if (spc_command_enable_app (store, &iter)) {
                        gtk_list_store_set (store, &iter,
                                            STORE_COL_ENABLED, TRUE,
                                            -1);
                }
        } else {
                if (spc_command_disable_app (store, &iter)) {
                        gtk_list_store_set (store, &iter,
                                            STORE_COL_ENABLED, FALSE,
                                            -1);
                }
        }
}

static gboolean
add_from_desktop_file (GtkTreeView *treeview,
                       char        *filename)
{
        EggDesktopFile *desktop_file;
        gboolean        success = FALSE;

        /* Assume that the file is local */
        GFile *file = g_file_new_for_uri (filename);
        gchar *path = g_file_get_path (file);

        if (path != NULL) {
                desktop_file = egg_desktop_file_new (path, NULL);

                if (desktop_file != NULL) {
                        GtkTreeIter   iter;
                        GtkTreeModel *model;
                        const char   *name;
                        char         *comment;
                        char         *description;
                        char         *command;
                        char         *icon;

                        model = gtk_tree_view_get_model (treeview);

                        gtk_list_store_append (GTK_LIST_STORE (model), &iter);

                        name = egg_desktop_file_get_name (desktop_file);

                        comment = egg_desktop_file_get_locale_string (desktop_file,
                                                                      EGG_DESKTOP_FILE_KEY_COMMENT,
                                                                      NULL, NULL);
                        if (comment == NULL) {
                                comment = egg_desktop_file_get_string (desktop_file,
                                                                       EGG_DESKTOP_FILE_KEY_COMMENT,
                                                                       NULL);
                        }

                        description = spc_command_get_app_description (name, comment);

                        command = egg_desktop_file_get_string (desktop_file,
                                                               EGG_DESKTOP_FILE_KEY_EXEC,
                                                               NULL);

                        icon = egg_desktop_file_get_string (desktop_file,
                                                            EGG_DESKTOP_FILE_KEY_ICON,
                                                            NULL);

                        if (name && comment && description && command) {
                                gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                                                    STORE_COL_DESCRIPTION, description,
                                                    STORE_COL_NAME, name,
                                                    STORE_COL_COMMAND, command,
                                                    STORE_COL_COMMENT, comment,
                                                    STORE_COL_ICON_NAME, icon,
                                                    STORE_COL_DESKTOP_FILE, desktop_file,
                                                    -1);

                                spc_command_add_app (GTK_LIST_STORE (model), &iter);
                                success = TRUE;
                        }

                        g_free (comment);
                        g_free (description);
                        g_free (command);
                        g_free (icon);
                        egg_desktop_file_free (desktop_file);
                }
        }

        g_free (path);
        return success;
}

static gboolean
drag_data_cb (GtkWidget        *widget,
              GdkDragContext   *drag_context,
              gint              x,
              gint              y,
              GtkSelectionData *data,
              guint             info,
              guint             time,
              gpointer          user_data)
{
        gboolean dnd_success = FALSE;

        if ((data != NULL) && (data->length >= 0)) {
                gchar **filenames = g_strsplit ((gchar *)data->data, "\r\n", 0);
                int i;

                for (i = 0; filenames[i] && filenames[i][0]; i++) {
                        /* Return success if at least one file succeeded */
                        gboolean file_success =
                                add_from_desktop_file (GTK_TREE_VIEW (widget), filenames[i]);
                        dnd_success = dnd_success || file_success;
                }

                g_strfreev (filenames);
        }

        gtk_drag_finish (drag_context, dnd_success, FALSE, time);
        return TRUE;
}

static void
setup_treeview (GtkTreeView *treeview,
                GladeXML    *xml)
{
        GtkTreeModel      *store;
        GtkCellRenderer   *renderer;
        GtkTreeSelection  *selection;
        GtkTreeViewColumn *column;

        static const GtkTargetEntry drag_targets[] = { { "text/uri-list", 0, 0 } };

        store = spc_command_get_store ();
        gtk_tree_view_set_model (treeview, store);
        gtk_tree_view_set_headers_visible (treeview, FALSE);

        selection = gtk_tree_view_get_selection (treeview);

        gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);

        g_signal_connect (selection,
                          "changed",
                          G_CALLBACK (selection_changed_cb),
                          xml);

        renderer = gtk_cell_renderer_toggle_new ();

        column = gtk_tree_view_column_new_with_attributes (_("Enabled"), renderer,
                                                           "active", STORE_COL_ENABLED,
                                                           "activatable", STORE_COL_ACTIVATABLE,
                                                           NULL);

        gtk_tree_view_append_column (treeview, column);

        g_signal_connect (renderer,
                          "toggled",
                          G_CALLBACK (startup_enabled_toggled_cb),
                          store);

        renderer = gtk_cell_renderer_pixbuf_new ();

        column = gtk_tree_view_column_new_with_attributes (_("Icon"), renderer,
                                                           "icon-name", STORE_COL_ICON_NAME,
                                                           NULL);

        g_object_set (renderer,
                      "stock-size", GTK_ICON_SIZE_LARGE_TOOLBAR,
                      NULL);

        gtk_tree_view_append_column (treeview, column);

        renderer = gtk_cell_renderer_text_new ();

        column = gtk_tree_view_column_new_with_attributes (_("Program"), renderer,
                                                           "markup", STORE_COL_DESCRIPTION,
                                                           NULL);

        g_object_set (renderer,
                      "ellipsize", PANGO_ELLIPSIZE_END,
                      NULL);

        gtk_tree_view_append_column (treeview, column);

        gtk_tree_view_column_set_sort_column_id (column, STORE_COL_NAME);
        gtk_tree_view_set_search_column (treeview, STORE_COL_NAME);
        gtk_tree_view_set_rules_hint (treeview, TRUE);

        gtk_drag_dest_set (GTK_WIDGET (treeview),
                           GTK_DEST_DEFAULT_ALL,
                           drag_targets,
                           G_N_ELEMENTS (drag_targets),
                           GDK_ACTION_COPY);

        g_signal_connect (G_OBJECT (treeview),
                          "drag-data-received",
                          G_CALLBACK (drag_data_cb),
                          NULL);
}

static void
add_app_cb (GtkWidget *widget,
            GladeXML  *xml)
{
        GtkWidget        *parent;
        GtkTreeView      *view;
        GtkTreeSelection *selection;
        GtkTreeModel     *model;
        GtkTreeIter       iter;

        parent = glade_xml_get_widget (xml, CAPPLET_DIALOG_WIDGET_NAME);

        view = GTK_TREE_VIEW (glade_xml_get_widget (xml,
                                                    CAPPLET_TREEVIEW_WIDGET_NAME));

        selection = gtk_tree_view_get_selection (view);
        model = gtk_tree_view_get_model (view);

        gtk_list_store_append (GTK_LIST_STORE (model), &iter);

        if (edit_app_dialog (_("Add Startup Program"), GTK_LIST_STORE (model), &iter, parent)) {
                spc_command_add_app (GTK_LIST_STORE (model), &iter);
        } else {
                gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
        }
}

static void
delete_app_cb (GtkWidget *widget,
               GladeXML  *xml)
{
        GtkTreeView *view;
        GtkTreeSelection *selection;
        GtkTreeModel *model;
        GtkTreeIter iter;

        view = GTK_TREE_VIEW (glade_xml_get_widget (xml,
                                                    CAPPLET_TREEVIEW_WIDGET_NAME));

        selection = gtk_tree_view_get_selection (view);
        model = gtk_tree_view_get_model (view);

        if (!gtk_tree_selection_get_selected (selection, NULL, &iter)) {
                return;
        }

        spc_command_delete_app (GTK_LIST_STORE (model), &iter);

        gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
}

static void
edit_app_cb (GtkWidget *widget,
             GladeXML  *xml)
{
        GtkWidget        *parent;
        GtkTreeView      *view;
        GtkTreeSelection *selection;
        GtkTreeModel     *model;
        GtkTreeIter       iter;

        parent = glade_xml_get_widget (xml, CAPPLET_DIALOG_WIDGET_NAME);

        view = GTK_TREE_VIEW (glade_xml_get_widget (xml,
                                                    CAPPLET_TREEVIEW_WIDGET_NAME));

        selection = gtk_tree_view_get_selection (view);
        model = gtk_tree_view_get_model (view);

        if (!gtk_tree_selection_get_selected (selection, NULL, &iter)) {
                return;
        }

        if (edit_app_dialog (_("Edit Startup Program"), GTK_LIST_STORE (model), &iter, parent)) {
                spc_command_update_app (GTK_LIST_STORE (model), &iter);
        }
}

static void
autosave_value_notify (GConfClient *client,
                       guint        id,
                       GConfEntry  *entry,
                       gpointer     tb)
{
        gboolean gval, bval;

        gval = gconf_value_get_bool (entry->value);
        bval = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (tb));

        if (bval != gval) {
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tb), gval);
        }
}

static void
autosave_value_toggled (GtkToggleButton *tb,
                        gpointer         data)
{
        GConfClient *client;
        gboolean     gval;
        gboolean     bval;
        const gchar *key;

        client = (GConfClient *) data;

        key = g_object_get_data (G_OBJECT (tb), "key");

        gval = gconf_client_get_bool (client, key, NULL);
        bval = gtk_toggle_button_get_active (tb);

        if (gval != bval) {
                gconf_client_set_bool (client, key, bval, NULL);
        }
}

static void
save_session_cb (GtkWidget *widget,
                 GladeXML *xml)
{
        g_debug ("Session saving is not implemented yet!");
}

static void
help_cb (GtkWidget *widget,
         GtkWidget *dialog)
{
        GError *error = NULL;

        gnome_help_display_desktop (NULL,
                                    "user-guide",
                                    "user-guide.xml",
                                    "goscustsession-5",
                                    &error);

        if (error) {
                GtkWidget *error_dlg;

                error_dlg = gtk_message_dialog_new (GTK_WINDOW (dialog),
                                                    GTK_DIALOG_MODAL,
                                                    GTK_MESSAGE_ERROR,
                                                    GTK_BUTTONS_CLOSE,
                                                    ("There was an error displaying help: \n%s"),
                                                    error->message);

                g_signal_connect (G_OBJECT (error_dlg), "response",
                                  G_CALLBACK (gtk_widget_destroy),
                                  NULL);

                gtk_window_set_resizable (GTK_WINDOW (error_dlg), FALSE);
                gtk_widget_show (error_dlg);

                g_error_free (error);
        }
}

static gboolean
delete_cb (GtkWidget *dialog)
{
        gtk_main_quit ();
        return FALSE;
}

static void
close_cb (GtkWidget *widget,
          GtkWidget *dialog)
{
        delete_cb (dialog);
}

void
spc_ui_build (GConfClient *client)
{
        GladeXML  *xml;
        GtkWidget *dlg;
        GtkWidget *treeview;
        GtkWidget *button;

        xml = glade_xml_new (DATA_DIR "/session-properties.glade",
                             CAPPLET_DIALOG_WIDGET_NAME, NULL);

        dlg = glade_xml_get_widget (xml, CAPPLET_DIALOG_WIDGET_NAME);

        g_signal_connect (dlg,
                          "delete-event",
                          G_CALLBACK (delete_cb),
                          NULL);

        treeview = glade_xml_get_widget (xml, CAPPLET_TREEVIEW_WIDGET_NAME);

        setup_treeview (GTK_TREE_VIEW (treeview), xml);

        button = glade_xml_get_widget (xml, CAPPLET_CLOSE_WIDGET_NAME);

        gtk_dialog_set_default_response (GTK_DIALOG (dlg), GTK_RESPONSE_CLOSE);

        g_signal_connect (G_OBJECT (button),
                          "clicked",
                          G_CALLBACK (close_cb),
                          dlg);

        button = glade_xml_get_widget (xml, CAPPLET_HELP_WIDGET_NAME);

        g_signal_connect (G_OBJECT (button),
                          "clicked",
                          G_CALLBACK (help_cb),
                          dlg);

        button = glade_xml_get_widget (xml, CAPPLET_ADD_WIDGET_NAME);

        g_signal_connect (G_OBJECT (button),
                          "clicked",
                          G_CALLBACK (add_app_cb),
                          xml);

        button = glade_xml_get_widget (xml, CAPPLET_DELETE_WIDGET_NAME);

        g_signal_connect (G_OBJECT (button),
                          "clicked",
                          G_CALLBACK (delete_app_cb),
                          xml);

        button = glade_xml_get_widget (xml, CAPPLET_EDIT_WIDGET_NAME);

        g_signal_connect (G_OBJECT (button),
                          "clicked",
                          G_CALLBACK (edit_app_cb),
                          xml);

        button = glade_xml_get_widget (xml, CAPPLET_REMEMBER_WIDGET_NAME);

        g_object_set_data (G_OBJECT (button), "key", SPC_GCONF_AUTOSAVE_KEY);

        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button),
                                      gconf_client_get_bool (client, SPC_GCONF_AUTOSAVE_KEY, NULL));

        gconf_client_notify_add (client, SPC_GCONF_AUTOSAVE_KEY,
                                 autosave_value_notify,
                                 button, NULL, NULL);

        g_signal_connect (G_OBJECT (button),
                          "toggled",
                          G_CALLBACK (autosave_value_toggled),
                          client);

        button = glade_xml_get_widget (xml, CAPPLET_SAVE_WIDGET_NAME);

        g_signal_connect (G_OBJECT (button),
                          "clicked",
                          G_CALLBACK (save_session_cb),
                          xml);

        gtk_widget_show (dlg);
}
