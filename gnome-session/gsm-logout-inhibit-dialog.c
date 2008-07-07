/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 William Jon McCann <mccann@jhu.edu>
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
#include <glib-object.h>
#include <gtk/gtk.h>

#include <glade/glade-xml.h>
#include <gconf/gconf-client.h>

#include "gsm-logout-inhibit-dialog.h"
#include "eggdesktopfile.h"
#include "util.h"

#define GSM_LOGOUT_INHIBIT_DIALOG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSM_TYPE_LOGOUT_INHIBIT_DIALOG, GsmLogoutInhibitDialogPrivate))

#define GLADE_XML_FILE "gsm-logout-inhibit-dialog.glade"

#ifndef DEFAULT_ICON_SIZE
#define DEFAULT_ICON_SIZE 64
#endif

#define DIALOG_RESPONSE_LOCK_SCREEN 1

struct GsmLogoutInhibitDialogPrivate
{
        GladeXML          *xml;
        int                action;
        GsmInhibitorStore *inhibitors;
        GtkListStore      *list_store;
};

enum {
        PROP_0,
        PROP_ACTION,
        PROP_INHIBITOR_STORE,
};

enum {
        INHIBIT_IMAGE_COLUMN = 0,
        INHIBIT_NAME_COLUMN,
        INHIBIT_REASON_COLUMN,
        INHIBIT_COOKIE_COLUMN,
        NUMBER_OF_COLUMNS
};

static void     gsm_logout_inhibit_dialog_class_init  (GsmLogoutInhibitDialogClass *klass);
static void     gsm_logout_inhibit_dialog_init        (GsmLogoutInhibitDialog      *logout_inhibit_dialog);
static void     gsm_logout_inhibit_dialog_finalize    (GObject                     *object);

G_DEFINE_TYPE (GsmLogoutInhibitDialog, gsm_logout_inhibit_dialog, GTK_TYPE_DIALOG)

static void
lock_screen (GsmLogoutInhibitDialog *dialog)
{
        GError *error;
        error = NULL;
        g_spawn_command_line_async ("gnome-screensaver-command --lock", &error);
        if (error != NULL) {
                g_warning ("Couldn't lock screen: %s", error->message);
                g_error_free (error);
        }
}

static void
on_response (GsmLogoutInhibitDialog *dialog,
             gint                    response_id)
{
        switch (response_id) {
        case DIALOG_RESPONSE_LOCK_SCREEN:
                g_signal_stop_emission_by_name (dialog, "response");
                lock_screen (dialog);
                break;
        default:
                break;
        }
}

static void
gsm_logout_inhibit_dialog_set_action (GsmLogoutInhibitDialog *dialog,
                                      int                     action)
{
        dialog->priv->action = action;
}

static gboolean
find_inhibitor (GsmLogoutInhibitDialog *dialog,
                guint                   cookie,
                GtkTreeIter            *iter)
{
        GtkTreeModel *model;
        gboolean      found_item;

        g_assert (GSM_IS_LOGOUT_INHIBIT_DIALOG (dialog));

        found_item = FALSE;
        model = GTK_TREE_MODEL (dialog->priv->list_store);

        if (!gtk_tree_model_get_iter_first (model, iter)) {
                return FALSE;
        }

        do {
                guint item_cookie;

                gtk_tree_model_get (model,
                                    iter,
                                    INHIBIT_COOKIE_COLUMN, &item_cookie,
                                    -1);
                if (cookie == item_cookie) {
                        found_item = TRUE;
                }
        } while (!found_item && gtk_tree_model_iter_next (model, iter));

        return found_item;
}

/* copied from gnome-panel panel-util.c */
static char *
_util_icon_remove_extension (const char *icon)
{
        char *icon_no_extension;
        char *p;

        icon_no_extension = g_strdup (icon);
        p = strrchr (icon_no_extension, '.');
        if (p &&
            (strcmp (p, ".png") == 0 ||
             strcmp (p, ".xpm") == 0 ||
             strcmp (p, ".svg") == 0)) {
            *p = 0;
        }

        return icon_no_extension;
}

/* copied from gnome-panel panel-util.c */
static char *
_find_icon (GtkIconTheme  *icon_theme,
            const char    *icon_name,
            gint           size)
{
        GtkIconInfo *info;
        char        *retval;
        char        *icon_no_extension;

        if (icon_name == NULL || strcmp (icon_name, "") == 0)
                return NULL;

        if (g_path_is_absolute (icon_name)) {
                if (g_file_test (icon_name, G_FILE_TEST_EXISTS)) {
                        return g_strdup (icon_name);
                } else {
                        char *basename;

                        basename = g_path_get_basename (icon_name);
                        retval = _find_icon (icon_theme, basename,
                                             size);
                        g_free (basename);

                        return retval;
                }
        }

        /* This is needed because some .desktop files have an icon name *and*
         * an extension as icon */
        icon_no_extension = _util_icon_remove_extension (icon_name);

        info = gtk_icon_theme_lookup_icon (icon_theme, icon_no_extension,
                                           size, 0);

        g_free (icon_no_extension);

        if (info) {
                retval = g_strdup (gtk_icon_info_get_filename (info));
                gtk_icon_info_free (info);
        } else
                retval = NULL;

        return retval;
}

/* copied from gnome-panel panel-util.c */
static GdkPixbuf *
_load_icon (GtkIconTheme  *icon_theme,
            const char    *icon_name,
            int            size,
            int            desired_width,
            int            desired_height,
            char         **error_msg)
{
        GdkPixbuf *retval;
        char      *file;
        GError    *error;

        g_return_val_if_fail (error_msg == NULL || *error_msg == NULL, NULL);

        file = _find_icon (icon_theme, icon_name, size);
        if (!file) {
                if (error_msg)
                        *error_msg = g_strdup_printf (_("Icon '%s' not found"),
                                                      icon_name);

                return NULL;
        }

        error = NULL;
        retval = gdk_pixbuf_new_from_file_at_size (file,
                                                   desired_width,
                                                   desired_height,
                                                   &error);
        if (error) {
                if (error_msg)
                        *error_msg = g_strdup (error->message);
                g_error_free (error);
        }

        g_free (file);

        return retval;
}

static void
add_inhibitor (GsmLogoutInhibitDialog *dialog,
               GsmInhibitor           *inhibitor)
{
        const char     *name;
        const char     *icon_name;
        const char     *app_id;
        char           *desktop_filename;
        GdkPixbuf      *pixbuf;
        EggDesktopFile *desktop_file;
        GError         *error;
        char          **search_dirs;

        /* FIXME: get info from xid */

        pixbuf = NULL;
        app_id = gsm_inhibitor_get_app_id (inhibitor);

        if (! g_str_has_suffix (app_id, ".desktop")) {
                desktop_filename = g_strdup_printf ("%s.desktop", app_id);
        } else {
                desktop_filename = g_strdup (app_id);
        }

        /* FIXME: maybe also append the autostart dirs ? */
        search_dirs = gsm_util_get_app_dirs ();

        error = NULL;
        desktop_file = egg_desktop_file_new_from_dirs (desktop_filename,
                                                       (const char **)search_dirs,
                                                       &error);
        g_strfreev (search_dirs);

        if (desktop_file == NULL) {
                g_warning ("Unable to find desktop file '%s': %s", desktop_filename, error->message);
                g_error_free (error);
                name = app_id;
                pixbuf = _load_icon (gtk_icon_theme_get_default (),
                                     "gnome-windows",
                                     DEFAULT_ICON_SIZE,
                                     DEFAULT_ICON_SIZE,
                                     DEFAULT_ICON_SIZE,
                                     NULL);
        } else {
                name = egg_desktop_file_get_name (desktop_file);
                icon_name = egg_desktop_file_get_icon (desktop_file);

                pixbuf = _load_icon (gtk_icon_theme_get_default (),
                                     icon_name,
                                     DEFAULT_ICON_SIZE,
                                     DEFAULT_ICON_SIZE,
                                     DEFAULT_ICON_SIZE,
                                     NULL);
        }

        gtk_list_store_insert_with_values (dialog->priv->list_store,
                                           NULL, 0,
                                           INHIBIT_IMAGE_COLUMN, pixbuf,
                                           INHIBIT_NAME_COLUMN, name,
                                           INHIBIT_REASON_COLUMN, gsm_inhibitor_get_reason (inhibitor),
                                           INHIBIT_COOKIE_COLUMN, gsm_inhibitor_get_cookie (inhibitor),
                                           -1);

        g_free (desktop_filename);
        if (pixbuf != NULL) {
                g_object_unref (pixbuf);
        }
}

static gboolean
model_has_one_entry (GtkTreeModel *model)
{
        guint n_rows;

        n_rows = gtk_tree_model_iter_n_children (model, NULL);
        g_debug ("Model has %d rows", n_rows);

        return (n_rows < 2);
}

static void
update_dialog_text (GsmLogoutInhibitDialog *dialog)
{
        const char *description_text;
        GtkWidget  *widget;

        if (model_has_one_entry (GTK_TREE_MODEL (dialog->priv->list_store))) {
                g_debug ("Found one entry in model");
                description_text = _("Waiting for program to finish.  Interrupting program may cause you to lose work.");
        } else {
                g_debug ("Found multiple entries in model");
                description_text = _("Waiting for programs to finish.  Interrupting these programs may cause you to lose work.");
        }

        widget = glade_xml_get_widget (dialog->priv->xml, "description-label");
        if (widget != NULL) {
                gtk_label_set_text (GTK_LABEL (widget), description_text);
        }
}

static void
on_store_inhibitor_added (GsmInhibitorStore      *store,
                          guint                   cookie,
                          GsmLogoutInhibitDialog *dialog)
{
        GsmInhibitor *inhibitor;
        GtkTreeIter   iter;

        g_debug ("GsmLogoutInhibitDialog: inhibitor added: %u", cookie);

        inhibitor = gsm_inhibitor_store_lookup (store, cookie);

        /* Add to model */
        if (! find_inhibitor (dialog, cookie, &iter)) {
                add_inhibitor (dialog, inhibitor);
                update_dialog_text (dialog);
        }

}

static void
on_store_inhibitor_removed (GsmInhibitorStore      *store,
                            guint                   cookie,
                            GsmLogoutInhibitDialog *dialog)
{
        GtkTreeIter   iter;

        g_debug ("GsmLogoutInhibitDialog: inhibitor removed: %u", cookie);

        /* Remove from model */
        if (find_inhibitor (dialog, cookie, &iter)) {
                gtk_list_store_remove (dialog->priv->list_store, &iter);
                update_dialog_text (dialog);
        }

        /* if there are no inhibitors left then trigger response */
        if (! gtk_tree_model_get_iter_first (GTK_TREE_MODEL (dialog->priv->list_store), &iter)) {
                gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
        }
}

static void
gsm_logout_inhibit_dialog_set_inhibitor_store (GsmLogoutInhibitDialog *dialog,
                                               GsmInhibitorStore      *store)
{
        g_return_if_fail (GSM_IS_LOGOUT_INHIBIT_DIALOG (dialog));

        if (store != NULL) {
                g_object_ref (store);
        }

        if (dialog->priv->inhibitors != NULL) {
                g_signal_handlers_disconnect_by_func (dialog->priv->inhibitors,
                                                      on_store_inhibitor_added,
                                                      dialog);
                g_signal_handlers_disconnect_by_func (dialog->priv->inhibitors,
                                                      on_store_inhibitor_removed,
                                                      dialog);

                g_object_unref (dialog->priv->inhibitors);
        }


        g_debug ("GsmLogoutInhibitDialog: setting store %p", store);

        dialog->priv->inhibitors = store;

        if (dialog->priv->inhibitors != NULL) {
                g_signal_connect (dialog->priv->inhibitors,
                                  "inhibitor-added",
                                  G_CALLBACK (on_store_inhibitor_added),
                                  dialog);
                g_signal_connect (dialog->priv->inhibitors,
                                  "inhibitor-removed",
                                  G_CALLBACK (on_store_inhibitor_removed),
                                  dialog);
        }
}

static void
gsm_logout_inhibit_dialog_set_property (GObject        *object,
                                        guint           prop_id,
                                        const GValue   *value,
                                        GParamSpec     *pspec)
{
        GsmLogoutInhibitDialog *dialog = GSM_LOGOUT_INHIBIT_DIALOG (object);

        switch (prop_id) {
        case PROP_ACTION:
                gsm_logout_inhibit_dialog_set_action (dialog, g_value_get_int (value));
                break;
        case PROP_INHIBITOR_STORE:
                gsm_logout_inhibit_dialog_set_inhibitor_store (dialog, g_value_get_object (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_logout_inhibit_dialog_get_property (GObject        *object,
                                        guint           prop_id,
                                        GValue         *value,
                                        GParamSpec     *pspec)
{
        GsmLogoutInhibitDialog *dialog = GSM_LOGOUT_INHIBIT_DIALOG (object);

        switch (prop_id) {
        case PROP_ACTION:
                g_value_set_int (value, dialog->priv->action);
                break;
        case PROP_INHIBITOR_STORE:
                g_value_set_object (value, dialog->priv->inhibitors);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
name_cell_data_func (GtkTreeViewColumn      *tree_column,
                     GtkCellRenderer        *cell,
                     GtkTreeModel           *model,
                     GtkTreeIter            *iter,
                     GsmLogoutInhibitDialog *dialog)
{
        char    *name;
        char    *reason;
        char    *markup;

        name = NULL;
        reason = NULL;
        gtk_tree_model_get (model,
                            iter,
                            INHIBIT_NAME_COLUMN, &name,
                            INHIBIT_REASON_COLUMN, &reason,
                            -1);

        markup = g_strdup_printf ("<b>%s</b> is busy\n"
                                  "<i><span size=\"x-small\">%s</span></i>",
                                  name ? name : "(null)",
                                  reason ? reason : "(null)");

        g_free (name);
        g_free (reason);

        g_object_set (cell, "markup", markup, NULL);
        g_free (markup);
}

static gboolean
add_to_model (guint                   cookie,
              GsmInhibitor           *inhibitor,
              GsmLogoutInhibitDialog *dialog)
{
        add_inhibitor (dialog, inhibitor);
        return FALSE;
}

static void
populate_model (GsmLogoutInhibitDialog *dialog)
{
        gsm_inhibitor_store_foreach_remove (dialog->priv->inhibitors,
                                            (GsmInhibitorStoreFunc)add_to_model,
                                            dialog);
        update_dialog_text (dialog);
}

static void
setup_dialog (GsmLogoutInhibitDialog *dialog)
{
        const char        *button_text;
        GtkWidget         *treeview;
        GtkTreeViewColumn *column;
        GtkCellRenderer   *renderer;

        switch (dialog->priv->action) {
        case GSM_LOGOUT_ACTION_SWITCH_USER:
                button_text = _("Switch User Anyway");
                break;
        case GSM_LOGOUT_ACTION_LOGOUT:
                button_text = _("Logout Anyway");
                break;
        case GSM_LOGOUT_ACTION_SLEEP:
                button_text = _("Suspend Anyway");
                break;
        case GSM_LOGOUT_ACTION_HIBERNATE:
                button_text = _("Hibernate Anyway");
                break;
        case GSM_LOGOUT_ACTION_SHUTDOWN:
                button_text = _("Shutdown Anyway");
                break;
        case GSM_LOGOUT_ACTION_REBOOT:
                button_text = _("Reboot Anyway");
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        gtk_dialog_add_button (GTK_DIALOG (dialog),
                               _("Lock Screen"),
                               DIALOG_RESPONSE_LOCK_SCREEN);
        gtk_dialog_add_button (GTK_DIALOG (dialog),
                               _("Cancel"),
                               GTK_RESPONSE_CANCEL);
        gtk_dialog_add_button (GTK_DIALOG (dialog),
                               button_text,
                               GTK_RESPONSE_ACCEPT);
        g_signal_connect (dialog,
                          "response",
                          G_CALLBACK (on_response),
                          dialog);

        dialog->priv->list_store = gtk_list_store_new (NUMBER_OF_COLUMNS,
                                                       GDK_TYPE_PIXBUF,
                                                       G_TYPE_STRING,
                                                       G_TYPE_STRING,
                                                       G_TYPE_UINT);

        treeview = glade_xml_get_widget (dialog->priv->xml, "inhibitors-treeview");
        gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (treeview), FALSE);
        gtk_tree_view_set_model (GTK_TREE_VIEW (treeview),
                                 GTK_TREE_MODEL (dialog->priv->list_store));

        /* IMAGE COLUMN */
        renderer = gtk_cell_renderer_pixbuf_new ();
        gtk_cell_renderer_set_fixed_size (renderer,
                                          DEFAULT_ICON_SIZE,
                                          DEFAULT_ICON_SIZE);
        column = gtk_tree_view_column_new ();
        gtk_tree_view_column_pack_start (column, renderer, FALSE);
        gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

        gtk_tree_view_column_set_attributes (column,
                                             renderer,
                                             "pixbuf", INHIBIT_IMAGE_COLUMN,
                                             NULL);

        g_object_set (renderer, "xalign", 1.0, NULL);

        /* NAME COLUMN */
        renderer = gtk_cell_renderer_text_new ();
        column = gtk_tree_view_column_new ();
        gtk_tree_view_column_pack_start (column, renderer, FALSE);
        gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);
        gtk_tree_view_column_set_cell_data_func (column,
                                                 renderer,
                                                 (GtkTreeCellDataFunc) name_cell_data_func,
                                                 dialog,
                                                 NULL);

        gtk_tree_view_set_tooltip_column (GTK_TREE_VIEW (treeview),
                                          INHIBIT_REASON_COLUMN);

        populate_model (dialog);
}

static GObject *
gsm_logout_inhibit_dialog_constructor (GType                  type,
                                       guint                  n_construct_properties,
                                       GObjectConstructParam *construct_properties)
{
        GsmLogoutInhibitDialog *dialog;

        dialog = GSM_LOGOUT_INHIBIT_DIALOG (G_OBJECT_CLASS (gsm_logout_inhibit_dialog_parent_class)->constructor (type,
                                                                                                                  n_construct_properties,
                                                                                                                  construct_properties));

        setup_dialog (dialog);
        gtk_widget_show_all (GTK_WIDGET (dialog));

        return G_OBJECT (dialog);
}

static void
gsm_logout_inhibit_dialog_dispose (GObject *object)
{
        G_OBJECT_CLASS (gsm_logout_inhibit_dialog_parent_class)->dispose (object);
}

static void
gsm_logout_inhibit_dialog_class_init (GsmLogoutInhibitDialogClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsm_logout_inhibit_dialog_get_property;
        object_class->set_property = gsm_logout_inhibit_dialog_set_property;
        object_class->constructor = gsm_logout_inhibit_dialog_constructor;
        object_class->dispose = gsm_logout_inhibit_dialog_dispose;
        object_class->finalize = gsm_logout_inhibit_dialog_finalize;

        g_object_class_install_property (object_class,
                                         PROP_ACTION,
                                         g_param_spec_int ("action",
                                                           "action",
                                                           "action",
                                                           -1,
                                                           G_MAXINT,
                                                           -1,
                                                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_INHIBITOR_STORE,
                                         g_param_spec_object ("inhibitor-store",
                                                              NULL,
                                                              NULL,
                                                              GSM_TYPE_INHIBITOR_STORE,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_type_class_add_private (klass, sizeof (GsmLogoutInhibitDialogPrivate));
}

static void
gsm_logout_inhibit_dialog_init (GsmLogoutInhibitDialog *dialog)
{
        GtkWidget *widget;

        dialog->priv = GSM_LOGOUT_INHIBIT_DIALOG_GET_PRIVATE (dialog);

        dialog->priv->xml = glade_xml_new (GLADEDIR "/" GLADE_XML_FILE,
                                           "main-box",
                                           PACKAGE);
        g_assert (dialog->priv->xml != NULL);

        widget = glade_xml_get_widget (dialog->priv->xml, "main-box");
        gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), widget);

        gtk_container_set_border_width (GTK_CONTAINER (dialog), 12);
        gtk_container_set_border_width (GTK_CONTAINER (widget), 12);
        gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
        gtk_window_set_icon_name (GTK_WINDOW (dialog), "gnome-logout");
        gtk_window_set_title (GTK_WINDOW (dialog), "");
        g_object_set (dialog,
                      "allow-shrink", FALSE,
                      "allow-grow", FALSE,
                      NULL);
}

static void
gsm_logout_inhibit_dialog_finalize (GObject *object)
{
        GsmLogoutInhibitDialog *dialog;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSM_IS_LOGOUT_INHIBIT_DIALOG (object));

        dialog = GSM_LOGOUT_INHIBIT_DIALOG (object);

        g_return_if_fail (dialog->priv != NULL);

        g_debug ("GsmLogoutInhibitDialog: finalizing");

        g_signal_handlers_disconnect_by_func (dialog->priv->inhibitors,
                                              on_store_inhibitor_added,
                                              dialog);
        g_signal_handlers_disconnect_by_func (dialog->priv->inhibitors,
                                              on_store_inhibitor_removed,
                                              dialog);
        if (dialog->priv->inhibitors != NULL) {
                g_object_unref (dialog->priv->inhibitors);
        }

        G_OBJECT_CLASS (gsm_logout_inhibit_dialog_parent_class)->finalize (object);
}

GtkWidget *
gsm_logout_inhibit_dialog_new (GsmInhibitorStore *inhibitors,
                               int                action)
{
        GObject *object;

        object = g_object_new (GSM_TYPE_LOGOUT_INHIBIT_DIALOG,
                               "action", action,
                               "inhibitor-store", inhibitors,
                               NULL);

        return GTK_WIDGET (object);
}
