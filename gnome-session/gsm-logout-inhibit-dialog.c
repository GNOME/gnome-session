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

#define GSM_LOGOUT_INHIBIT_DIALOG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSM_TYPE_LOGOUT_INHIBIT_DIALOG, GsmLogoutInhibitDialogPrivate))

#define GLADE_XML_FILE "gsm-logout-inhibit-dialog.glade"

struct GsmLogoutInhibitDialogPrivate
{
        GladeXML *xml;
        int       action;
};

enum {
        PROP_0,
        PROP_ACTION,
};

static void     gsm_logout_inhibit_dialog_class_init  (GsmLogoutInhibitDialogClass *klass);
static void     gsm_logout_inhibit_dialog_init        (GsmLogoutInhibitDialog      *logout_inhibit_dialog);
static void     gsm_logout_inhibit_dialog_finalize    (GObject                     *object);

G_DEFINE_TYPE (GsmLogoutInhibitDialog, gsm_logout_inhibit_dialog, GTK_TYPE_DIALOG)

static void
gsm_logout_inhibit_dialog_set_action (GsmLogoutInhibitDialog *dialog,
                                      int                     action)
{
        dialog->priv->action = action;
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
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
on_response (GsmLogoutInhibitDialog *dialog,
             gint                      response_id)
{
        switch (response_id) {
        default:
                break;
        }
}

static void
setup_dialog (GsmLogoutInhibitDialog *dialog)
{
        const char *button_text;

        switch (dialog->priv->action) {
        case GSM_LOGOUT_ACTION_SWITCH_USER:
                button_text = _("Switch User Now");
                break;
        case GSM_LOGOUT_ACTION_LOGOUT:
                button_text = _("Logout Now");
                break;
        case GSM_LOGOUT_ACTION_SLEEP:
                button_text = _("Suspend Now");
                break;
        case GSM_LOGOUT_ACTION_HIBERNATE:
                button_text = _("Hibernate Now");
                break;
        case GSM_LOGOUT_ACTION_SHUTDOWN:
                button_text = _("Shutdown Now");
                break;
        case GSM_LOGOUT_ACTION_REBOOT:
                button_text = _("Reboot Now");
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        gtk_dialog_add_button (GTK_DIALOG (dialog),
                               button_text,
                               GTK_RESPONSE_ACCEPT);
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
                                                           G_PARAM_READWRITE));

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

        g_signal_connect (dialog,
                          "response",
                          G_CALLBACK (on_response),
                          dialog);
}

static void
gsm_logout_inhibit_dialog_finalize (GObject *object)
{
        GsmLogoutInhibitDialog *dialog;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSM_IS_LOGOUT_INHIBIT_DIALOG (object));

        dialog = GSM_LOGOUT_INHIBIT_DIALOG (object);

        g_return_if_fail (dialog->priv != NULL);

        G_OBJECT_CLASS (gsm_logout_inhibit_dialog_parent_class)->finalize (object);
}

GtkWidget *
gsm_logout_inhibit_dialog_new (int action)
{
        GObject *object;

        object = g_object_new (GSM_TYPE_LOGOUT_INHIBIT_DIALOG,
                               "action", action,
                               NULL);

        return GTK_WIDGET (object);
}
