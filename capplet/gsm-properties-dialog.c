/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 1999 Free Software Foundation, Inc.
 * Copyright (C) 2007 Vincent Untz.
 * Copyright (C) 2008 Lucas Rocha.
 * Copyright (C) 2008 William Jon McCann <jmccann@redhat.com>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <glade/glade-xml.h>
#include <gconf/gconf-client.h>

#include "gsm-properties-dialog.h"
#include "gsm-app-dialog.h"
#include "eggdesktopfile.h"
#include "gsm-util.h"

#define GSM_PROPERTIES_DIALOG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSM_TYPE_PROPERTIES_DIALOG, GsmPropertiesDialogPrivate))

#define IS_STRING_EMPTY(x) ((x)==NULL||(x)[0]=='\0')

#define REALLY_IDENTICAL_STRING(a, b)                   \
        ((a && b && !strcmp (a, b)) || (!a && !b))

#define GLADE_XML_FILE "session-properties.glade"

#define CAPPLET_DIALOG_WIDGET_NAME        "session_properties_capplet"
#define CAPPLET_TREEVIEW_WIDGET_NAME      "session_properties_treeview"
#define CAPPLET_CLOSE_WIDGET_NAME         "session_properties_close_button"
#define CAPPLET_HELP_WIDGET_NAME          "session_properties_help_button"
#define CAPPLET_ADD_WIDGET_NAME           "session_properties_add_button"
#define CAPPLET_DELETE_WIDGET_NAME        "session_properties_delete_button"
#define CAPPLET_EDIT_WIDGET_NAME          "session_properties_edit_button"
#define CAPPLET_SAVE_WIDGET_NAME          "session_properties_save_button"
#define CAPPLET_REMEMBER_WIDGET_NAME      "session_properties_remember_toggle"

#define STARTUP_APP_ICON     "system-run"

#define SPC_GCONF_CONFIG_PREFIX   "/apps/gnome-session/options"
#define SPC_GCONF_AUTOSAVE_KEY    SPC_GCONF_CONFIG_PREFIX "/auto_save_session"

struct GsmPropertiesDialogPrivate
{
        GladeXML          *xml;
        GtkListStore      *list_store;
};

enum {
        STORE_COL_ENABLED = 0,
        STORE_COL_ICON_NAME,
        STORE_COL_DESCRIPTION,
        STORE_COL_NAME,
        STORE_COL_COMMAND,
        STORE_COL_COMMENT,
        STORE_COL_DESKTOP_FILE,
        STORE_COL_ID,
        STORE_COL_ACTIVATABLE,
        NUMBER_OF_COLUMNS
};

static void     gsm_properties_dialog_class_init  (GsmPropertiesDialogClass *klass);
static void     gsm_properties_dialog_init        (GsmPropertiesDialog      *properties_dialog);
static void     gsm_properties_dialog_finalize    (GObject                  *object);

G_DEFINE_TYPE (GsmPropertiesDialog, gsm_properties_dialog, GTK_TYPE_DIALOG)

static void
on_response (GsmPropertiesDialog *dialog,
             gint                 response_id)

{
}

static gboolean
find_by_id (GtkListStore *store,
            const char   *id)
{
        GtkTreeIter iter;
        char       *iter_id = NULL;

        if (!gtk_tree_model_get_iter_first (GTK_TREE_MODEL (store), &iter)) {
                return FALSE;
        }

        do {
                gtk_tree_model_get (GTK_TREE_MODEL (store), &iter,
                                    STORE_COL_ID, &iter_id,
                                    -1);

                if (!strcmp (iter_id, id)) {
                        return TRUE;
                }
        } while (gtk_tree_model_iter_next (GTK_TREE_MODEL (store), &iter));

        return FALSE;
}

static char *
get_app_description (const char *name,
                     const char *comment)
{
        return g_markup_printf_escaped ("<b>%s</b>\n%s", name,
                                        (!gsm_util_text_is_blank (comment) ?
                                         comment : _("No description")));
}

static gboolean
append_app (GsmPropertiesDialog *dialog,
            EggDesktopFile      *desktop_file)
{
        GtkIconTheme *theme;
        GtkTreeIter   iter;
        GFile        *source;
        char         *basename;
        char         *description;
        char         *name;
        char         *comment;
        char         *command;
        char         *icon_name;
        gboolean      enabled = TRUE;

        source = g_file_new_for_uri (egg_desktop_file_get_source (desktop_file));

        basename = g_file_get_basename (source);

        if (egg_desktop_file_has_key (desktop_file,
                                      G_KEY_FILE_DESKTOP_KEY_HIDDEN, NULL)) {
                if (egg_desktop_file_get_boolean (desktop_file,
                                                  G_KEY_FILE_DESKTOP_KEY_HIDDEN,
                                                  NULL))
                        return FALSE;
        }

        /* Check for duplicate apps */
        if (find_by_id (dialog->priv->list_store, basename)) {
                return TRUE;
        }

        name = egg_desktop_file_get_locale_string (desktop_file,
                                                   G_KEY_FILE_DESKTOP_KEY_NAME,
                                                   NULL, NULL);

        comment = NULL;

        if (egg_desktop_file_has_key (desktop_file,
                                      "Comment", NULL)) {
                comment =
                        egg_desktop_file_get_locale_string (desktop_file,
                                                            G_KEY_FILE_DESKTOP_KEY_COMMENT,
                                                            NULL, NULL);
        }

        description = get_app_description (name, comment);

        command = egg_desktop_file_get_string (desktop_file,
                                               G_KEY_FILE_DESKTOP_KEY_EXEC,
                                               NULL);

        icon_name = NULL;

        if (egg_desktop_file_has_key (desktop_file,
                                      G_KEY_FILE_DESKTOP_KEY_ICON, NULL)) {
                icon_name =
                        egg_desktop_file_get_string (desktop_file,
                                                     G_KEY_FILE_DESKTOP_KEY_ICON,
                                                     NULL);
        }

        theme = gtk_icon_theme_get_default ();

        if (icon_name == NULL || *icon_name == '\0' ||
            !gtk_icon_theme_has_icon (theme, icon_name)) {
                if (icon_name) {
                        g_free (icon_name);
                }

                icon_name = g_strdup (STARTUP_APP_ICON);
        }

        if (egg_desktop_file_has_key (desktop_file,
                                      "X-GNOME-Autostart-enabled", NULL)) {
                enabled = egg_desktop_file_get_boolean (desktop_file,
                                                        "X-GNOME-Autostart-enabled",
                                                        NULL);
        }

        gtk_list_store_append (dialog->priv->list_store, &iter);

        gtk_list_store_set (dialog->priv->list_store,
                            &iter,
                            STORE_COL_ENABLED, enabled,
                            STORE_COL_ICON_NAME, icon_name,
                            STORE_COL_DESCRIPTION, description,
                            STORE_COL_NAME, name,
                            STORE_COL_COMMAND, command,
                            STORE_COL_COMMENT, comment,
                            STORE_COL_DESKTOP_FILE, desktop_file,
                            STORE_COL_ID, basename,
                            STORE_COL_ACTIVATABLE, TRUE,
                            -1);

        g_object_unref (source);
        g_free (basename);
        g_free (name);
        g_free (comment);
        g_free (description);
        g_free (icon_name);

        return TRUE;
}

static int
compare_app (gconstpointer a,
             gconstpointer b)
{
        if (!strcmp (a, b)) {
                return 0;
        }

        return 1;
}

static void
append_autostart_apps (GsmPropertiesDialog *dialog,
                       const char          *path,
                       GList              **removed_apps)
{
        GDir       *dir;
        const char *name;

        dir = g_dir_open (path, 0, NULL);
        if (!dir) {
                return;
        }

        while ((name = g_dir_read_name (dir))) {
                EggDesktopFile *desktop_file;
                GError         *error;
                char           *desktop_file_path;

                if (!g_str_has_suffix (name, ".desktop")) {
                        continue;
                }

                if (removed_apps &&
                    g_list_find_custom (*removed_apps, name, compare_app)) {
                        continue;
                }

                desktop_file_path = g_build_filename (path, name, NULL);

                error = NULL;
                desktop_file = egg_desktop_file_new (desktop_file_path, &error);
                if (error == NULL) {
                        if (!append_app (dialog, desktop_file)) {
                                if (removed_apps) {
                                        *removed_apps = g_list_prepend (*removed_apps, g_strdup (name));
                                }
                        }
                } else {
                        g_warning ("could not read %s: %s\n", desktop_file_path, error->message);

                        g_error_free (error);
                        error = NULL;
                }

                g_free (desktop_file_path);
        }

        g_dir_close (dir);
}

static void
populate_model (GsmPropertiesDialog *dialog)
{
        GList        *removed_apps;
        char        **autostart_dirs;
        int           i;

        autostart_dirs = gsm_util_get_autostart_dirs ();

        removed_apps = NULL;
        for (i = 0; autostart_dirs[i]; i++) {
                append_autostart_apps (dialog, autostart_dirs[i], &removed_apps);
        }

        g_strfreev (autostart_dirs);
        g_list_foreach (removed_apps, (GFunc) g_free, NULL);
        g_list_free (removed_apps);
}

static void
on_selection_changed (GtkTreeSelection    *selection,
                      GsmPropertiesDialog *dialog)
{
        GtkWidget *edit_button;
        GtkWidget *delete_button;
        gboolean   sel;

        edit_button = glade_xml_get_widget (dialog->priv->xml, CAPPLET_EDIT_WIDGET_NAME);
        delete_button = glade_xml_get_widget (dialog->priv->xml, CAPPLET_DELETE_WIDGET_NAME);

        sel = gtk_tree_selection_get_selected (selection, NULL, NULL);

        if (edit_button) {
                gtk_widget_set_sensitive (edit_button, sel);
        }

        if (delete_button) {
                gtk_widget_set_sensitive (delete_button, sel);
        }
}


static gboolean
system_desktop_entry_exists (const char  *basename,
                             char       **system_path)
{
        char *path;
        char **autostart_dirs;
        int i;

        autostart_dirs = gsm_util_get_autostart_dirs ();

        for (i = 0; autostart_dirs[i]; i++) {
                path = g_build_filename (autostart_dirs[i], basename, NULL);

                if (g_str_has_prefix (autostart_dirs[i], g_get_user_config_dir ())) {
                        continue;
                }

                if (g_file_test (path, G_FILE_TEST_EXISTS)) {
                        if (system_path) {
                                *system_path = path;
                        } else {
                                g_free (path);
                        }
                        g_strfreev (autostart_dirs);

                        return TRUE;
                }

                g_free (path);
        }

        g_strfreev (autostart_dirs);

        return FALSE;
}

static void
update_desktop_file (GtkListStore   *store,
                     GtkTreeIter    *iter,
                     EggDesktopFile *new_desktop_file)
{
        EggDesktopFile *old_desktop_file;

        gtk_tree_model_get (GTK_TREE_MODEL (store), iter,
                            STORE_COL_DESKTOP_FILE, &old_desktop_file,
                            -1);

        egg_desktop_file_free (old_desktop_file);

        gtk_list_store_set (store, iter,
                            STORE_COL_DESKTOP_FILE, new_desktop_file,
                            -1);
}

static void
ensure_user_autostart_dir ()
{
        char *dir;

        dir = g_build_filename (g_get_user_config_dir (), "autostart", NULL);
        g_mkdir_with_parents (dir, S_IRWXU);

        g_free (dir);
}

static void
key_file_set_locale_string (GKeyFile   *keyfile,
                            const char *group,
                            const char *key,
                            const char *value)
{
        const char         *locale;
        const char * const *langs_pointer;
        int                 i;

        g_assert (key != NULL);

        if (value == NULL) {
                value = "";
        }

        locale = NULL;
        langs_pointer = g_get_language_names ();

        for (i = 0; langs_pointer[i] != NULL; i++) {
                /* Find first without encoding  */
                if (strchr (langs_pointer[i], '.') == NULL) {
                        locale = langs_pointer[i];
                        break;
                }
        }

        if (locale != NULL) {
                g_key_file_set_locale_string (keyfile,
                                              group,
                                              key,
                                              locale,
                                              value);
        } else {
                g_key_file_set_string (keyfile,
                                       G_KEY_FILE_DESKTOP_GROUP,
                                       key, value);
        }
}

static gboolean
key_file_to_file (GKeyFile   *keyfile,
                  const char *file,
                  GError    **error)
{
        GError  *write_error;
        char    *filename;
        char    *data;
        gsize    length;
        gboolean res;

        g_return_val_if_fail (keyfile != NULL, FALSE);
        g_return_val_if_fail (file != NULL, FALSE);

        write_error = NULL;

        data = g_key_file_to_data (keyfile, &length, &write_error);

        if (write_error) {
                g_propagate_error (error, write_error);
                return FALSE;
        }

        if (!g_path_is_absolute (file)) {
                filename = g_filename_from_uri (file, NULL, &write_error);
        } else {
                filename = g_filename_from_utf8 (file, -1, NULL, NULL, &write_error);
        }

        if (write_error) {
                g_propagate_error (error, write_error);
                g_free (data);
                return FALSE;
        }

        res = g_file_set_contents (filename, data, length, &write_error);

        g_free (filename);

        if (write_error) {
                g_propagate_error (error, write_error);
                g_free (data);
                return FALSE;
        }

        g_free (data);

        return res;
}

static void
write_desktop_file (EggDesktopFile *desktop_file,
                    GtkListStore   *store,
                    GtkTreeIter    *iter,
                    gboolean        enabled)
{
        GKeyFile *keyfile;
        GFile    *source;
        GError   *error;
        char     *path;
        char     *name;
        char     *command;
        char     *comment;
        gboolean  path_changed = FALSE;

        ensure_user_autostart_dir ();

        keyfile = g_key_file_new ();

        source = g_file_new_for_uri (egg_desktop_file_get_source (desktop_file));

        path = g_file_get_path (source);

        error = NULL;
        g_key_file_load_from_file (keyfile,
                                   path,
                                   G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS,
                                   &error);
        if (error != NULL) {
                goto out;
        }

        if (!g_str_has_prefix (path, g_get_user_config_dir ())) {
                /* It's a system-wide file, save it to the user's home */
                char *basename;

                basename = g_file_get_basename (source);

                g_free (path);

                path = g_build_filename (g_get_user_config_dir (),
                                         "autostart", basename, NULL);

                g_free (basename);

                path_changed = TRUE;
        }

        gtk_tree_model_get (GTK_TREE_MODEL (store),
                            iter,
                            STORE_COL_NAME, &name,
                            STORE_COL_COMMAND, &command,
                            STORE_COL_COMMENT, &comment,
                            -1);

        key_file_set_locale_string (keyfile,
                                    G_KEY_FILE_DESKTOP_GROUP,
                                    G_KEY_FILE_DESKTOP_KEY_NAME,
                                    name);

        key_file_set_locale_string (keyfile,
                                    G_KEY_FILE_DESKTOP_GROUP,
                                    G_KEY_FILE_DESKTOP_KEY_COMMENT,
                                    comment);

        g_key_file_set_string (keyfile,
                               G_KEY_FILE_DESKTOP_GROUP,
                               G_KEY_FILE_DESKTOP_KEY_EXEC,
                               command);

        g_key_file_set_boolean (keyfile,
                                G_KEY_FILE_DESKTOP_GROUP,
                                "X-GNOME-Autostart-enabled",
                                enabled);

        if (!key_file_to_file (keyfile, path, &error)) {
                goto out;
        }

        if (path_changed) {
                EggDesktopFile *new_desktop_file;

                new_desktop_file = egg_desktop_file_new (path, &error);

                if (error) {
                        goto out;
                }

                update_desktop_file (store, iter, new_desktop_file);
        }

 out:
        if (error != NULL) {
                g_warning ("Error when writing desktop file %s: %s",
                           path, error->message);

                g_error_free (error);
        }

        g_free (path);
        g_free (name);
        g_free (comment);
        g_free (command);
        g_object_unref (source);
        g_key_file_free (keyfile);
}

static gboolean
toggle_app (GsmPropertiesDialog *dialog,
            GtkTreeIter         *iter,
            gboolean             enabled)
{
        EggDesktopFile *desktop_file;
        GFile          *source;
        char           *system_path;
        char           *basename;
        char           *path;
        char           *name;
        char           *comment;

        gtk_tree_model_get (GTK_TREE_MODEL (dialog->priv->list_store),
                            iter,
                            STORE_COL_NAME, &name,
                            STORE_COL_COMMENT, &comment,
                            STORE_COL_DESKTOP_FILE, &desktop_file,
                            -1);

        source = g_file_new_for_uri (egg_desktop_file_get_source (desktop_file));

        path = g_file_get_path (source);

        basename = g_file_get_basename (source);

        if (system_desktop_entry_exists (basename, &system_path)) {
                EggDesktopFile *system_desktop_file;
                char           *original_name;
                char           *original_comment;
                gboolean        original_enabled;

                system_desktop_file = egg_desktop_file_new (system_path, NULL);

                original_name = egg_desktop_file_get_locale_string (system_desktop_file,
                                                                    "Name", NULL, NULL);

                original_comment = egg_desktop_file_get_locale_string (system_desktop_file,
                                                                       "Comment", NULL, NULL);

                if (egg_desktop_file_has_key (system_desktop_file,
                                              "X-GNOME-Autostart-enabled", NULL)) {
                        original_enabled = egg_desktop_file_get_boolean (system_desktop_file,
                                                                         "X-GNOME-Autostart-enabled", NULL);
                } else {
                        original_enabled = TRUE;
                }

                if (REALLY_IDENTICAL_STRING (name, original_name) &&
                    REALLY_IDENTICAL_STRING (comment, original_comment) &&
                    (enabled == original_enabled)) {
                        char *user_file =
                                g_build_filename (g_get_user_config_dir (),
                                                  "autostart", basename, NULL);

                        if (g_file_test (user_file, G_FILE_TEST_EXISTS)) {
                                g_remove (user_file);
                        }

                        g_free (user_file);

                        update_desktop_file (dialog->priv->list_store, iter, system_desktop_file);
                } else {
                        write_desktop_file (desktop_file, dialog->priv->list_store, iter, enabled);
                        egg_desktop_file_free (system_desktop_file);
                }

                g_free (original_name);
                g_free (original_comment);
        } else {
                write_desktop_file (desktop_file, dialog->priv->list_store, iter, enabled);
        }

        g_free (name);
        g_free (comment);
        g_free (basename);

        return TRUE;
}

static void
on_startup_enabled_toggled (GtkCellRendererToggle *cell_renderer,
                            char                  *path,
                            GsmPropertiesDialog   *dialog)
{
        GtkTreeIter iter;
        char       *desktop_file_path;
        gboolean    active;

        if (!gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (dialog->priv->list_store),
                                                  &iter, path)) {
                return;
        }

        gtk_tree_model_get (GTK_TREE_MODEL (dialog->priv->list_store),
                            &iter,
                            STORE_COL_DESKTOP_FILE, &desktop_file_path,
                            -1);

        active = gtk_cell_renderer_toggle_get_active (cell_renderer);
        active = !active;
        gtk_cell_renderer_toggle_set_active (cell_renderer, active);

        if (toggle_app (dialog, &iter, active)) {
                gtk_list_store_set (dialog->priv->list_store,
                                    &iter,
                                    STORE_COL_ENABLED, active,
                                    -1);
        }
}

static void
add_app (GtkListStore *store,
         GtkTreeIter  *iter)
{
        EggDesktopFile *desktop_file;
        GKeyFile       *keyfile;
        char          **argv;
        char           *basename;
        char           *orig_filename;
        char           *filename;
        char           *name;
        char           *command;
        char           *comment;
        char           *description;
        char           *icon;
        int             argc;
        int             i = 2;

        gtk_tree_model_get (GTK_TREE_MODEL (store), iter,
                            STORE_COL_NAME, &name,
                            STORE_COL_COMMAND, &command,
                            STORE_COL_COMMENT, &comment,
                            STORE_COL_ICON_NAME, &icon,
                            -1);

        g_assert (command != NULL);
        g_shell_parse_argv (command, &argc, &argv, NULL);

        basename = g_path_get_basename (argv[0]);

        orig_filename = g_build_filename (g_get_user_config_dir (),
                                          "autostart", basename, NULL);

        filename = g_strdup_printf ("%s.desktop", orig_filename);

        while (g_file_test (filename, G_FILE_TEST_EXISTS)) {
                char *tmp = g_strdup_printf ("%s-%d.desktop", orig_filename, i);

                g_free (filename);
                filename = tmp;

                i++;
        }

        g_free (orig_filename);

        ensure_user_autostart_dir ();

        keyfile = g_key_file_new ();

        g_key_file_set_string (keyfile,
                               G_KEY_FILE_DESKTOP_GROUP,
                               G_KEY_FILE_DESKTOP_KEY_TYPE,
                               "Application");

        g_key_file_set_string (keyfile,
                               G_KEY_FILE_DESKTOP_GROUP,
                               G_KEY_FILE_DESKTOP_KEY_NAME,
                               name);

        g_key_file_set_string (keyfile,
                               G_KEY_FILE_DESKTOP_GROUP,
                               G_KEY_FILE_DESKTOP_KEY_EXEC,
                               command);

        if (icon == NULL) {
                icon = g_strdup (STARTUP_APP_ICON);
        }

        g_key_file_set_string (keyfile,
                               G_KEY_FILE_DESKTOP_GROUP,
                               G_KEY_FILE_DESKTOP_KEY_ICON,
                               icon);

        if (comment) {
                g_key_file_set_string (keyfile,
                                       G_KEY_FILE_DESKTOP_GROUP,
                                       G_KEY_FILE_DESKTOP_KEY_COMMENT,
                                       comment);
        }

        description = get_app_description (name, comment);

        if (!key_file_to_file (keyfile, filename, NULL)) {
                g_warning ("Could not save %s file", filename);
        }

        desktop_file = egg_desktop_file_new_from_key_file (keyfile,
                                                           filename,
                                                           NULL);

        g_free (basename);

        basename = g_path_get_basename (filename);

        gtk_list_store_set (store, iter,
                            STORE_COL_ENABLED, TRUE,
                            STORE_COL_ICON_NAME, icon,
                            STORE_COL_DESKTOP_FILE, desktop_file,
                            STORE_COL_ID, basename,
                            STORE_COL_ACTIVATABLE, TRUE,
                            -1);

        g_key_file_free (keyfile);
        g_strfreev (argv);
        g_free (name);
        g_free (command);
        g_free (comment);
        g_free (description);
        g_free (basename);
        g_free (icon);
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

                        description = get_app_description (name, comment);

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

                                add_app (GTK_LIST_STORE (model), &iter);
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
on_drag_data (GtkWidget           *widget,
              GdkDragContext      *drag_context,
              gint                 x,
              gint                 y,
              GtkSelectionData    *data,
              guint                info,
              guint                time,
              GsmPropertiesDialog *dialog)
{
        gboolean dnd_success;

        dnd_success = FALSE;

        if ((data != NULL) && (data->length >= 0)) {
                char **filenames;
                int    i;

                filenames = g_strsplit ((char *)data->data, "\r\n", 0);
                for (i = 0; filenames[i] && filenames[i][0]; i++) {
                        /* Return success if at least one file succeeded */
                        gboolean file_success;
                        file_success = add_from_desktop_file (GTK_TREE_VIEW (widget), filenames[i]);
                        dnd_success = dnd_success || file_success;
                }

                g_strfreev (filenames);
        }

        gtk_drag_finish (drag_context, dnd_success, FALSE, time);
        return TRUE;
}

static gboolean
edit_app_dialog (GsmPropertiesDialog *dialog,
                 GtkTreeIter         *iter)
{
        GtkWidget *dlg;
        char      *c_name;
        char      *c_command;
        char      *c_comment;

        gtk_tree_model_get (GTK_TREE_MODEL (dialog->priv->list_store),
                            iter,
                            STORE_COL_NAME, &c_name,
                            STORE_COL_COMMAND, &c_command,
                            STORE_COL_COMMENT, &c_comment,
                            -1);

        dlg = gsm_app_dialog_new (c_name, c_command, c_comment);
        gtk_window_set_transient_for (GTK_WINDOW (dlg), GTK_WINDOW (dialog));
        g_free (c_name);
        g_free (c_command);
        g_free (c_comment);

        while (gtk_dialog_run (GTK_DIALOG (dlg)) == GTK_RESPONSE_OK) {
                const char *name;
                const char *command;
                const char *comment;
                const char *error_msg;
                char      **argv;
                char       *description;
                int         argc;
                GError     *error;

                name = gsm_app_dialog_get_name (GSM_APP_DIALOG (dlg));
                command = gsm_app_dialog_get_command (GSM_APP_DIALOG (dlg));
                comment = gsm_app_dialog_get_comment (GSM_APP_DIALOG (dlg));
                error = NULL;

                error_msg = NULL;
                if (gsm_util_text_is_blank (name)) {
                        error_msg = _("The name of the startup program cannot be empty");
                }

                if (gsm_util_text_is_blank (command)) {
                        error_msg = _("The startup command cannot be empty");
                } else {
                        if (!g_shell_parse_argv (command, &argc, &argv, &error)) {
                                if (error != NULL) {
                                        error_msg = error->message;
                                } else {
                                        error_msg = _("The startup command is not valid");
                                }
                        }
                }

                if (error_msg != NULL) {
                        GtkWidget *msgbox;

                        gtk_widget_show (dlg);

                        msgbox = gtk_message_dialog_new (GTK_WINDOW (dlg),
                                                         GTK_DIALOG_MODAL,
                                                         GTK_MESSAGE_ERROR,
                                                         GTK_BUTTONS_CLOSE,
                                                         "%s", error_msg);

                        if (error != NULL) {
                                g_error_free (error);
                        }

                        gtk_dialog_run (GTK_DIALOG (msgbox));

                        gtk_widget_destroy (msgbox);
                } else {
                        g_strfreev (argv);

                        description = get_app_description (name, comment);

                        gtk_list_store_set (GTK_LIST_STORE (dialog->priv->list_store), iter,
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
on_add_app_clicked (GtkWidget           *widget,
                    GsmPropertiesDialog *dialog)
{
        GtkTreeView      *view;
        GtkTreeSelection *selection;
        GtkTreeModel     *model;
        GtkTreeIter       iter;

        view = GTK_TREE_VIEW (glade_xml_get_widget (dialog->priv->xml,
                                                    CAPPLET_TREEVIEW_WIDGET_NAME));

        selection = gtk_tree_view_get_selection (view);
        model = gtk_tree_view_get_model (view);

        gtk_list_store_append (GTK_LIST_STORE (model), &iter);

        if (edit_app_dialog (dialog, &iter)) {
                add_app (GTK_LIST_STORE (model), &iter);
        } else {
                gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
        }
}

static void
delete_desktop_file (GtkListStore *store,
                     GtkTreeIter  *iter)
{
        EggDesktopFile *desktop_file;
        GFile          *source;
        char           *basename;
        char           *path;

        gtk_tree_model_get (GTK_TREE_MODEL (store), iter,
                            STORE_COL_DESKTOP_FILE, &desktop_file,
                            -1);

        source = g_file_new_for_uri (egg_desktop_file_get_source (desktop_file));

        path = g_file_get_path (source);
        basename = g_file_get_basename (source);

        if (g_str_has_prefix (path, g_get_user_config_dir ()) &&
            !system_desktop_entry_exists (basename, NULL)) {
                if (g_file_test (path, G_FILE_TEST_EXISTS))
                        g_remove (path);
        } else {
                /* Two possible cases:
                 * a) We want to remove a system wide startup desktop file.
                 *    We can't do that, so we will create a user desktop file
                 *    with the hidden flag set.
                 * b) We want to remove a startup desktop file that is both
                 *    system and user. So we have to mark it as hidden.
                 */
                GKeyFile *keyfile;
                GError   *error;
                char     *user_path;

                ensure_user_autostart_dir ();

                keyfile = g_key_file_new ();

                error = NULL;

                g_key_file_load_from_file (keyfile, path,
                                           G_KEY_FILE_KEEP_COMMENTS|G_KEY_FILE_KEEP_TRANSLATIONS,
                                           &error);

                if (error) {
                        g_error_free (error);
                        g_key_file_free (keyfile);
                }

                g_key_file_set_boolean (keyfile,
                                        G_KEY_FILE_DESKTOP_GROUP,
                                        G_KEY_FILE_DESKTOP_KEY_HIDDEN,
                                        TRUE);

                user_path = g_build_filename (g_get_user_config_dir (),
                                              "autostart", basename, NULL);

                if (!key_file_to_file (keyfile, user_path, NULL)) {
                        g_warning ("Could not save %s file", user_path);
                }

                g_key_file_free (keyfile);

                g_free (user_path);
        }

        g_object_unref (source);
        g_free (path);
        g_free (basename);
}

static void
delete_app (GtkListStore *store,
            GtkTreeIter  *iter)
{
        delete_desktop_file (store, iter);
}

static void
on_delete_app_clicked (GtkWidget           *widget,
                       GsmPropertiesDialog *dialog)
{
        GtkTreeView      *view;
        GtkTreeSelection *selection;
        GtkTreeModel     *model;
        GtkTreeIter       iter;

        view = GTK_TREE_VIEW (glade_xml_get_widget (dialog->priv->xml,
                                                    CAPPLET_TREEVIEW_WIDGET_NAME));

        selection = gtk_tree_view_get_selection (view);
        model = gtk_tree_view_get_model (view);

        if (!gtk_tree_selection_get_selected (selection, NULL, &iter)) {
                return;
        }

        delete_app (GTK_LIST_STORE (model), &iter);

        gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
}

static void
update_app (GtkListStore *store,
            GtkTreeIter *iter)
{
        EggDesktopFile *desktop_file;
        gboolean        enabled;

        gtk_tree_model_get (GTK_TREE_MODEL (store), iter,
                            STORE_COL_ENABLED, &enabled,
                            STORE_COL_DESKTOP_FILE, &desktop_file,
                            -1);

        write_desktop_file (desktop_file, store, iter, enabled);
}

static void
on_edit_app_clicked (GtkWidget           *widget,
                     GsmPropertiesDialog *dialog)
{
        GtkTreeView      *view;
        GtkTreeSelection *selection;
        GtkTreeModel     *model;
        GtkTreeIter       iter;

        view = GTK_TREE_VIEW (glade_xml_get_widget (dialog->priv->xml,
                                                    CAPPLET_TREEVIEW_WIDGET_NAME));

        selection = gtk_tree_view_get_selection (view);
        model = gtk_tree_view_get_model (view);

        if (!gtk_tree_selection_get_selected (selection, NULL, &iter)) {
                return;
        }

        if (edit_app_dialog (dialog, &iter)) {
                update_app (GTK_LIST_STORE (model), &iter);
        }
}

static void
on_row_activated (GtkTreeView         *tree_view,
                  GtkTreePath         *path,
                  GtkTreeViewColumn   *column,
                  GsmPropertiesDialog *dialog)  
{
        on_edit_app_clicked (NULL, dialog);
}

static void
on_autosave_value_notify (GConfClient         *client,
                          guint                id,
                          GConfEntry          *entry,
                          GsmPropertiesDialog *dialog)
{
        gboolean   gval;
        gboolean   bval;
        GtkWidget *button;

        button = glade_xml_get_widget (dialog->priv->xml, CAPPLET_REMEMBER_WIDGET_NAME);
        if (button == NULL) {
                return;
        }

        gval = gconf_value_get_bool (entry->value);
        bval = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));

        if (bval != gval) {
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), gval);
        }
}

static void
on_autosave_value_toggled (GtkToggleButton     *button,
                           GsmPropertiesDialog *dialog)
{
        GConfClient *client;
        gboolean     gval;
        gboolean     bval;

        client = gconf_client_get_default ();
        gval = gconf_client_get_bool (client, SPC_GCONF_AUTOSAVE_KEY, NULL);
        bval = gtk_toggle_button_get_active (button);

        if (gval != bval) {
                gconf_client_set_bool (client, SPC_GCONF_AUTOSAVE_KEY, bval, NULL);
        }
        g_object_unref (client);
}

static void
on_save_session_clicked (GtkWidget           *widget,
                         GsmPropertiesDialog *dialog)
{
        g_debug ("Session saving is not implemented yet!");
}

static void
setup_dialog (GsmPropertiesDialog *dialog)
{
        GtkWidget         *treeview;
        GtkWidget         *button;
        GtkTreeViewColumn *column;
        GtkCellRenderer   *renderer;
        GtkTreeSelection  *selection;
        GConfClient       *client;
        static const GtkTargetEntry drag_targets[] = { { "text/uri-list", 0, 0 } };

        gtk_dialog_add_buttons (GTK_DIALOG (dialog),
                                GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                NULL);
        g_signal_connect (dialog,
                          "response",
                          G_CALLBACK (on_response),
                          dialog);

        dialog->priv->list_store = gtk_list_store_new (NUMBER_OF_COLUMNS,
                                                       G_TYPE_BOOLEAN,
                                                       G_TYPE_STRING,
                                                       G_TYPE_STRING,
                                                       G_TYPE_STRING,
                                                       G_TYPE_STRING,
                                                       G_TYPE_STRING,
                                                       G_TYPE_POINTER,
                                                       G_TYPE_STRING,
                                                       G_TYPE_BOOLEAN);

        treeview = glade_xml_get_widget (dialog->priv->xml, CAPPLET_TREEVIEW_WIDGET_NAME);
        gtk_tree_view_set_model (GTK_TREE_VIEW (treeview),
                                 GTK_TREE_MODEL (dialog->priv->list_store));
        gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (treeview), FALSE);
        g_signal_connect (treeview,
                          "row-activated",
                          G_CALLBACK (on_row_activated),
                          dialog);

        selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
        gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
        g_signal_connect (selection,
                          "changed",
                          G_CALLBACK (on_selection_changed),
                          dialog);

        /* CHECKBOX COLUMN */
        renderer = gtk_cell_renderer_toggle_new ();
        column = gtk_tree_view_column_new_with_attributes (_("Enabled"),
                                                           renderer,
                                                           "active", STORE_COL_ENABLED,
                                                           "activatable", STORE_COL_ACTIVATABLE,
                                                           NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);
        g_signal_connect (renderer,
                          "toggled",
                          G_CALLBACK (on_startup_enabled_toggled),
                          dialog);

        /* ICON COLUMN */
        renderer = gtk_cell_renderer_pixbuf_new ();
        column = gtk_tree_view_column_new_with_attributes (_("Icon"),
                                                           renderer,
                                                           "icon-name", STORE_COL_ICON_NAME,
                                                           NULL);
        g_object_set (renderer,
                      "stock-size", GTK_ICON_SIZE_LARGE_TOOLBAR,
                      NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

        /* NAME COLUMN */
        renderer = gtk_cell_renderer_text_new ();
        column = gtk_tree_view_column_new_with_attributes (_("Program"),
                                                           renderer,
                                                           "markup", STORE_COL_DESCRIPTION,
                                                           NULL);
        g_object_set (renderer,
                      "ellipsize", PANGO_ELLIPSIZE_END,
                      NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);


        gtk_tree_view_column_set_sort_column_id (column, STORE_COL_NAME);
        gtk_tree_view_set_search_column (GTK_TREE_VIEW (treeview), STORE_COL_NAME);
        gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (treeview), TRUE);

        gtk_drag_dest_set (treeview,
                           GTK_DEST_DEFAULT_ALL,
                           drag_targets,
                           G_N_ELEMENTS (drag_targets),
                           GDK_ACTION_COPY);

        g_signal_connect (treeview,
                          "drag-data-received",
                          G_CALLBACK (on_drag_data),
                          dialog);

        gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (dialog->priv->list_store),
                                              STORE_COL_NAME,
                                              GTK_SORT_ASCENDING);


        button = glade_xml_get_widget (dialog->priv->xml, CAPPLET_ADD_WIDGET_NAME);
        g_signal_connect (button,
                          "clicked",
                          G_CALLBACK (on_add_app_clicked),
                          dialog);

        button = glade_xml_get_widget (dialog->priv->xml, CAPPLET_DELETE_WIDGET_NAME);
        g_signal_connect (button,
                          "clicked",
                          G_CALLBACK (on_delete_app_clicked),
                          dialog);

        button = glade_xml_get_widget (dialog->priv->xml, CAPPLET_EDIT_WIDGET_NAME);
        g_signal_connect (button,
                          "clicked",
                          G_CALLBACK (on_edit_app_clicked),
                          dialog);

        client = gconf_client_get_default ();
        button = glade_xml_get_widget (dialog->priv->xml, CAPPLET_REMEMBER_WIDGET_NAME);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button),
                                      gconf_client_get_bool (client, SPC_GCONF_AUTOSAVE_KEY, NULL));
        gconf_client_notify_add (client,
                                 SPC_GCONF_AUTOSAVE_KEY,
                                 (GConfClientNotifyFunc)on_autosave_value_notify,
                                 dialog,
                                 NULL,
                                 NULL);
        g_object_unref (client);

        g_signal_connect (button,
                          "toggled",
                          G_CALLBACK (on_autosave_value_toggled),
                          dialog);

        button = glade_xml_get_widget (dialog->priv->xml, CAPPLET_SAVE_WIDGET_NAME);
        g_signal_connect (button,
                          "clicked",
                          G_CALLBACK (on_save_session_clicked),
                          dialog);


        populate_model (dialog);
}

static GObject *
gsm_properties_dialog_constructor (GType                  type,
                                guint                  n_construct_properties,
                                GObjectConstructParam *construct_properties)
{
        GsmPropertiesDialog *dialog;

        dialog = GSM_PROPERTIES_DIALOG (G_OBJECT_CLASS (gsm_properties_dialog_parent_class)->constructor (type,
                                                                                                                  n_construct_properties,
                                                                                                                  construct_properties));

        setup_dialog (dialog);

        gtk_widget_show_all (GTK_WIDGET (dialog));

        return G_OBJECT (dialog);
}

static void
gsm_properties_dialog_dispose (GObject *object)
{
        GsmPropertiesDialog *dialog;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSM_IS_PROPERTIES_DIALOG (object));

        dialog = GSM_PROPERTIES_DIALOG (object);

        if (dialog->priv->xml != NULL) {
                g_object_unref (dialog->priv->xml);
                dialog->priv->xml = NULL;
        }

        G_OBJECT_CLASS (gsm_properties_dialog_parent_class)->dispose (object);
}

static void
gsm_properties_dialog_class_init (GsmPropertiesDialogClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->constructor = gsm_properties_dialog_constructor;
        object_class->dispose = gsm_properties_dialog_dispose;
        object_class->finalize = gsm_properties_dialog_finalize;

        g_type_class_add_private (klass, sizeof (GsmPropertiesDialogPrivate));
}

static void
gsm_properties_dialog_init (GsmPropertiesDialog *dialog)
{
        GtkWidget   *widget;
        GConfClient *client;

        dialog->priv = GSM_PROPERTIES_DIALOG_GET_PRIVATE (dialog);

        client = gconf_client_get_default ();
        gconf_client_add_dir (client,
                              SPC_GCONF_CONFIG_PREFIX,
                              GCONF_CLIENT_PRELOAD_ONELEVEL,
                              NULL);

        dialog->priv->xml = glade_xml_new (GLADEDIR "/" GLADE_XML_FILE,
                                           "main-notebook",
                                           GETTEXT_PACKAGE);
        g_assert (dialog->priv->xml != NULL);

        widget = glade_xml_get_widget (dialog->priv->xml, "main-notebook");
        gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), widget);

        gtk_window_set_resizable (GTK_WINDOW (dialog), TRUE);
        gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
        gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 2);
        gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
        gtk_window_set_icon_name (GTK_WINDOW (dialog), "session-properties");
        gtk_window_set_title (GTK_WINDOW (dialog), _("Startup Applications Preferences"));
}

static void
gsm_properties_dialog_finalize (GObject *object)
{
        GsmPropertiesDialog *dialog;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSM_IS_PROPERTIES_DIALOG (object));

        dialog = GSM_PROPERTIES_DIALOG (object);

        g_return_if_fail (dialog->priv != NULL);

        G_OBJECT_CLASS (gsm_properties_dialog_parent_class)->finalize (object);
}

GtkWidget *
gsm_properties_dialog_new (void)
{
        GObject *object;

        object = g_object_new (GSM_TYPE_PROPERTIES_DIALOG,
                               NULL);

        return GTK_WIDGET (object);
}
