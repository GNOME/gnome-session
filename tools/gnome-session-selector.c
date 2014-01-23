/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright 2010, 2013  Red Hat, Inc,
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Written by: Matthias Clasen <mclasen@redhat.com>
 */

#include "config.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>
#include <gtk/gtk.h>
#include <gio/gio.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>

#define GSM_MANAGER_SCHEMA        "org.gnome.SessionManager"
#define KEY_AUTOSAVE_ONE_SHOT     "auto-save-session-one-shot"

static GtkBuilder *builder;
static GtkWidget *session_list;
static GtkListStore *store;
static GtkTreeModelSort *sort_model;

static void select_session (const char *name);

static char *
get_session_path (const char *name)
{
        return g_build_filename (g_get_user_config_dir (), "gnome-session", name, NULL);
}

static char *
find_new_session_name (void)
{
        char *name;
        char *path;
        int i;

        for (i = 1; i < 20; i++) {
                name = g_strdup_printf (_("Session %d"), i);
                path = get_session_path (name);
                if (!g_file_test (path, G_FILE_TEST_EXISTS)) {
                        g_free (path);
                        return name;
                }
                g_free (path);
                g_free (name);
        }

        return NULL;
}

static gboolean
is_valid_session_name (const char *name)
{
        GtkTreeIter iter;
        char *n;
        const char *info_text;
        char *warning_text;
        gboolean user_tried_dot;
        gboolean user_tried_slash;
        GtkWidget *info_bar;
        GtkWidget *label;

        if (name[0] == 0) {
                return FALSE;
        }

        if (name[0] == '.') {
            user_tried_dot = TRUE;
        } else {
            user_tried_dot = FALSE;
        }

        if (strchr (name, '/') != NULL) {
            user_tried_slash = TRUE;
        } else {
            user_tried_slash = FALSE;
        }

        info_text = _("Please select a custom session to run");
        warning_text = NULL;
        if (user_tried_dot && user_tried_slash) {
            warning_text = g_strdup_printf ("%s\n<small><b>Note:</b> <i>%s</i></small>",
                                            info_text,
                                            _("Session names are not allowed to start with ‘.’ or contain ‘/’ characters"));
        } else if (user_tried_dot) {
            warning_text = g_strdup_printf ("%s\n<small><b>Note:</b> <i>%s</i></small>",
                                            info_text,
                                            _("Session names are not allowed to start with ‘.’"));
        } else if (user_tried_slash) {
            warning_text = g_strdup_printf ("%s\n<small><b>Note:</b> <i>%s</i></small>",
                                            info_text,
                                            _("Session names are not allowed to contain ‘/’ characters"));
        }

        gtk_tree_model_get_iter_first (GTK_TREE_MODEL (store), &iter);
        do {
                gtk_tree_model_get (GTK_TREE_MODEL (store), &iter, 0, &n, -1);
                if (strcmp (n, name) == 0) {
                        char *message;
                        message = g_strdup_printf (_("A session named ‘%s’ already exists"), name);
                        warning_text = g_strdup_printf ("%s\n<small><b>Note:</b> <i>%s</i></small>", info_text, message);
                        g_free (message);
                        g_free (n);
                        break;
                }
                g_free (n);
        } while (gtk_tree_model_iter_next (GTK_TREE_MODEL (store), &iter));

        info_bar = (GtkWidget *) gtk_builder_get_object (builder, "info-bar");
        label = (GtkWidget*) gtk_builder_get_object (builder, "info-label");

        if (warning_text != NULL) {
            gtk_info_bar_set_message_type (GTK_INFO_BAR (info_bar), GTK_MESSAGE_WARNING);
            gtk_label_set_markup (GTK_LABEL (label), warning_text);
            g_free (warning_text);
            return FALSE;
        }

        gtk_info_bar_set_message_type (GTK_INFO_BAR (info_bar), GTK_MESSAGE_OTHER);
        gtk_label_set_markup (GTK_LABEL (label), info_text);

        return TRUE;
}

static void
populate_session_list (GtkWidget *session_list)
{
        GtkTreeIter iter;
        char *path;
        const char *name;
        GDir *dir;
        GError *error;
        char *saved_session;
        char *default_session;
        char *default_name;
        char last_session[PATH_MAX] = "";

        saved_session = get_session_path ("saved-session");

        if (!g_file_test (saved_session, G_FILE_TEST_IS_SYMLINK)) {
                default_name = find_new_session_name ();
                default_session = get_session_path (default_name);
                rename (saved_session, default_session);
                if (symlink (default_name, saved_session) < 0)
                        g_warning ("Failed to convert saved-session to symlink");
                g_free (default_name);
                g_free (default_session);
        }

        path = g_build_filename (g_get_user_config_dir (), "gnome-session", NULL);
        error = NULL;
        dir = g_dir_open (path, 0, &error);
        if (dir == NULL) {
                g_warning ("Failed to open %s: %s", path, error->message);
                g_error_free (error);
                goto out;
        }

        default_name = NULL;
        if (readlink (saved_session, last_session, PATH_MAX - 1) > 0) {
                default_name = g_path_get_basename (last_session);
        }

        while ((name = g_dir_read_name (dir)) != NULL) {
                if (strcmp (name, "saved-session") == 0)
                        continue;

                gtk_list_store_insert_with_values (store, &iter, 100, 0, name, -1);

                if (g_strcmp0 (default_name, name) == 0) {
                        GtkTreeSelection *selection;
                        GtkTreeIter child_iter;

                        gtk_tree_model_sort_convert_child_iter_to_iter (GTK_TREE_MODEL_SORT (sort_model), &child_iter, &iter);
                        selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (session_list));
                        gtk_tree_selection_select_iter (selection, &child_iter);
                }
        }

        g_free (default_name);
        g_dir_close (dir);

 out:
        g_free (saved_session);
        g_free (path);
}

static char *
get_selected_session (void)
{
        GtkTreeSelection *selection;
        GtkTreeModel *model;
        GtkTreeIter iter;
        gchar *name;

        selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (session_list));
        if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
                gtk_tree_model_get (model, &iter, 0, &name, -1);
                return name;
        }

        return NULL;
}

static void
remove_session (const char *name)
{
        char *path1, *path2;
        char *n, *path;
        const char *d;
        GDir *dir;
        GError *error;

        path1 = get_session_path ("saved-session");
        path2 = get_session_path (name);

        error = NULL;
        n = g_file_read_link (path1, &error);
        if (n == NULL) {
                g_warning ("Failed to read link: %s", error->message);
                g_error_free (error);
        }
        else if (strcmp (n, name) == 0) {
                unlink (path1);
        }
        g_free (n);

        dir = g_dir_open (path2, 0, NULL);
        while ((d = g_dir_read_name (dir)) != NULL) {
                path = g_build_filename (path2, d, NULL);
                unlink (path);
                g_free (path);
        }
        g_dir_close (dir);

        remove (path2);

        g_free (path1);
        g_free (path2);
}

static void
on_remove_session_clicked (GtkButton *button,
                           gpointer   data)
{
        GtkTreeSelection *selection;
        GtkTreeModel *model;
        GtkTreeIter iter;
        char *name;

        selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (session_list));
        if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
                GtkTreeIter child_iter;
                gtk_tree_model_get (model, &iter, 0, &name, -1);
                remove_session (name);
                g_free (name);

                gtk_tree_model_sort_convert_iter_to_child_iter (GTK_TREE_MODEL_SORT (model), &child_iter, &iter);
                gtk_list_store_remove (GTK_LIST_STORE (store), &child_iter);

                if (!gtk_tree_selection_get_selected (selection, NULL, NULL)) {
                        gtk_tree_model_get_iter_first (model, &iter);
                        gtk_tree_model_get (model, &iter, 0, &name, -1);
                        select_session (name);
                        g_free (name);
                }
        }
}

static void
begin_rename (void)
{
        GtkTreePath *path;
        GtkTreeViewColumn *column;
        GList *cells;

        gtk_widget_grab_focus (session_list);

        gtk_tree_view_get_cursor (GTK_TREE_VIEW (session_list),
                                  &path, &column);

        cells = gtk_cell_layout_get_cells (GTK_CELL_LAYOUT (column));

        if (cells != NULL) {
            GtkCellRenderer *cell;

            cell = (GtkCellRenderer *) cells->data;
            g_list_free (cells);

            g_object_set (cell, "editable", TRUE, NULL);
            gtk_tree_view_set_cursor_on_cell (GTK_TREE_VIEW (session_list), path,
                                              column, cell, TRUE);
        }
        gtk_tree_path_free (path);
}

static void
on_rename_session_clicked (GtkButton *button,
                           gpointer   data)
{
    begin_rename ();
}

static void
on_continue_clicked (GtkButton *button,
                     gpointer    data)
{
        char *name;

        name = get_selected_session ();
        g_free (name);

        gtk_main_quit ();
}

static void
create_session (const char *name)
{
        char *path;
        GtkTreeIter iter;

        path = get_session_path (name);

        if (mkdir (path, 0755) < 0) {
                g_warning ("Failed to create directory %s", path);
        }
        else {
                char *marker;

                gtk_list_store_insert_with_values (store, &iter, 100, 0, name, -1);

                marker = g_build_filename (path, ".new-session", NULL);
                creat (marker, 0600);
                g_free (marker);
        }

        g_free (path);
}

static gboolean
rename_session (const char *old_name,
                const char *new_name)
{
        char *old_path, *new_path;
        int result;

        if (g_strcmp0 (old_name, new_name) == 0) {
                return TRUE;
        }

        if (!is_valid_session_name (new_name)) {
               return FALSE;
        }

        old_path = get_session_path (old_name);
        new_path = get_session_path (new_name);

        result = g_rename (old_path, new_path);

        if (result < 0) {
                g_warning ("Failed to rename session from '%s' to '%s': %m", old_name, new_name);
        }

        g_free (old_path);
        g_free (new_path);

        return result == 0;
}

static gboolean
make_session_current (const char *name)
{
        char *path1;
        gboolean ret = TRUE;

        path1 = g_build_filename (g_get_user_config_dir (), "gnome-session", "saved-session", NULL);

        unlink (path1);
        if (symlink (name, path1) < 0) {
                g_warning ("Failed to make session '%s' current", name);
                ret = FALSE;
        }

        g_free (path1);

        return ret;
}

static gboolean
create_and_select_session (const char *name)
{
        gchar *path;

        if (name[0] == 0 || name[0] == '.' || strchr (name, '/')) {
                g_warning ("Invalid session name");
                return FALSE;
        }

        path = g_build_filename (g_get_user_config_dir (), "gnome-session", name, NULL);
        if (!g_file_test (path, G_FILE_TEST_IS_DIR)) {
                if (mkdir (path, 0755) < 0) {
                        g_warning ("Failed to create directory %s", path);
                        g_free (path);
                        return FALSE;
                }
        }

        g_free (path);

        return make_session_current (name);
}

static void
select_session (const char *name)
{
        GtkTreeIter iter;
        char *n;

        make_session_current (name);

        /* now select it in the list */
        gtk_tree_model_get_iter_first (GTK_TREE_MODEL (sort_model), &iter);
        do {
                gtk_tree_model_get (GTK_TREE_MODEL (sort_model), &iter, 0, &n, -1);
                if (strcmp (n, name) == 0) {
                        GtkTreePath *path;

                        path = gtk_tree_model_get_path (GTK_TREE_MODEL (sort_model), &iter);
                        gtk_tree_view_set_cursor (GTK_TREE_VIEW (session_list), path, NULL, FALSE);
                        gtk_tree_path_free (path);
                        g_free (n);
                        break;
                }
                g_free (n);
        } while (gtk_tree_model_iter_next (GTK_TREE_MODEL (sort_model), &iter));
}

static void
on_new_session_clicked (GtkButton *button,
                        gpointer   data)
{
        gchar *name;

        name = find_new_session_name ();
        create_session (name);
        select_session (name);

        begin_rename ();
}

static void
on_selection_changed (GtkTreeSelection *selection,
                      gpointer          data)
{
        char *name;

        name = get_selected_session ();

        if (name == NULL) {
                return;
        }

        make_session_current (name);

        g_free (name);
}

static void
update_remove_button (void)
{
        GtkWidget *button;

        button = (GtkWidget *)gtk_builder_get_object (builder, "remove-session");
        if (gtk_tree_model_iter_n_children (GTK_TREE_MODEL (store), NULL) > 1) {
                gtk_widget_set_sensitive (button, TRUE);
        } else {
                gtk_widget_set_sensitive (button, FALSE);
        }
}

static void
on_row_edited (GtkCellRendererText *cell,
               const char          *path_string,
               const char          *new_name,
               gpointer             data)
{
        GtkTreePath *path;
        GtkTreeIter  sort_iter, items_iter;
        char        *old_name;
        gboolean     was_renamed;

        path = gtk_tree_path_new_from_string (path_string);
        gtk_tree_model_get_iter (GTK_TREE_MODEL (sort_model), &sort_iter, path);

        gtk_tree_model_get (GTK_TREE_MODEL (sort_model), &sort_iter, 0, &old_name, -1);

        was_renamed = rename_session (old_name, new_name);

        if (was_renamed) {
                gtk_tree_model_sort_convert_iter_to_child_iter (sort_model, &items_iter, &sort_iter);

                gtk_list_store_set (store, &items_iter, 0, g_strdup (new_name), -1);
                g_free (old_name);
                make_session_current (new_name);
        } else {
                begin_rename ();
        }

        gtk_tree_path_free (path);

        g_object_set (cell, "editable", FALSE, NULL);
}

static void
on_row_deleted (GtkTreeModel *model,
                GtkTreePath  *path,
                gpointer      data)
{
        update_remove_button ();
}

static void
on_row_inserted (GtkTreeModel *model,
                 GtkTreePath  *path,
                 GtkTreeIter  *iter,
                 gpointer      data)
{
        update_remove_button ();
}

static void
on_row_activated (GtkTreeView       *tree_view,
                  GtkTreePath       *path,
                  GtkTreeViewColumn *column,
                  gpointer           data)
{
        char *name;

        name = get_selected_session ();
        g_free (name);

        gtk_main_quit ();
}

static void
auto_save_next_session (void)
{
        GSettings *settings;

        settings = g_settings_new (GSM_MANAGER_SCHEMA);
        g_settings_set_boolean (settings, KEY_AUTOSAVE_ONE_SHOT, TRUE);
        g_object_unref (settings);
}

static void
auto_save_next_session_if_needed (void)
{
        char *marker;

        marker = g_build_filename (g_get_user_config_dir (),
                                   "gnome-session", "saved-session",
                                   ".new-session", NULL);

        if (g_file_test (marker, G_FILE_TEST_EXISTS)) {
                auto_save_next_session ();
                unlink (marker);
        }
        g_free (marker);
}

static int
compare_sessions (GtkTreeModel *model,
                  GtkTreeIter  *a,
                  GtkTreeIter  *b,
                  gpointer      data)
{
    char *name_a, *name_b;
    int result;

    gtk_tree_model_get (model, a, 0, &name_a, -1);
    gtk_tree_model_get (model, b, 0, &name_b, -1);

    result = g_utf8_collate (name_a, name_b);

    g_free (name_a);
    g_free (name_b);

    return result;
}

static void
on_map (GtkWidget *widget,
        gpointer   data)
{
        gdk_window_focus (gtk_widget_get_window (widget), GDK_CURRENT_TIME);
}

int
main (int argc, char *argv[])
{
        GtkWidget *window;
        GtkWidget *widget;
        GtkCellRenderer *cell;
        GtkTreeViewColumn *column;
        GtkTreeSelection *selection;
        GError *error;

        if (getenv ("SESSION_MANAGER") != NULL)
            return 1;

        gtk_init (&argc, &argv);
        if (argc > 1) {
                g_print ("create and select session\n");
                if (!create_and_select_session (argv[1]))
                        return 1;
                else
                        return 0;
        }

        builder = gtk_builder_new ();
        gtk_builder_set_translation_domain (builder, GETTEXT_PACKAGE);

        error = NULL;
        if (!gtk_builder_add_from_file (builder, GTKBUILDER_DIR "/" "session-selector.ui",  &error)) {
                g_warning ("Could not load file 'session-selector.ui': %s", error->message);
                exit (1);
        }

        window = (GtkWidget *) gtk_builder_get_object (builder, "main-window");

        store = (GtkListStore *) gtk_builder_get_object (builder, "session-store");
        sort_model = (GtkTreeModelSort *) gtk_builder_get_object (builder, "sort-model");

        gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (sort_model),
                                         0, compare_sessions, NULL, NULL);
        gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (sort_model),
                                              0, GTK_SORT_ASCENDING);
        g_signal_connect (store, "row-deleted", G_CALLBACK (on_row_deleted), NULL);
        g_signal_connect (store, "row-inserted", G_CALLBACK (on_row_inserted), NULL);
        session_list = (GtkWidget *) gtk_builder_get_object (builder, "session-list");

        selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (session_list));
        gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);

        populate_session_list (session_list);

        cell = gtk_cell_renderer_text_new ();
        g_signal_connect (cell, "edited", G_CALLBACK (on_row_edited), NULL);

        column = gtk_tree_view_column_new_with_attributes ("", cell, "text", 0, NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (session_list), GTK_TREE_VIEW_COLUMN (column));

        g_signal_connect (session_list, "row-activated", G_CALLBACK (on_row_activated), NULL);

        g_signal_connect (selection, "changed",
                          G_CALLBACK (on_selection_changed), NULL);

        widget = (GtkWidget *) gtk_builder_get_object (builder, "new-session");
        g_signal_connect (widget, "clicked", G_CALLBACK (on_new_session_clicked), NULL);
        widget = (GtkWidget *) gtk_builder_get_object (builder, "remove-session");
        g_signal_connect (widget, "clicked", G_CALLBACK (on_remove_session_clicked), NULL);
        widget = (GtkWidget *) gtk_builder_get_object (builder, "rename-session");
        g_signal_connect (widget, "clicked", G_CALLBACK (on_rename_session_clicked), NULL);
        widget = (GtkWidget *) gtk_builder_get_object (builder, "continue-button");
        g_signal_connect (widget, "clicked", G_CALLBACK (on_continue_clicked), NULL);

        g_signal_connect (window, "map", G_CALLBACK (on_map), NULL);
        gtk_widget_show (window);

        gtk_main ();

        auto_save_next_session_if_needed ();

        return 0;
}
