/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Authors:
 *	Colin Walters <walters@verbum.org>
 */

#include <config.h>

#include "gsm-fail-whale-dialog.h"
#include "gsm-manager.h"

#include <glib/gi18n.h>

#define GSM_FAIL_WHALE_DIALOG_GET_PRIVATE(o)                                \
        (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSM_TYPE_FAIL_WHALE_DIALOG, GsmFailWhaleDialogPrivate))

/* Shared with gsm-logout-dialog.c */
#define AUTOMATIC_ACTION_TIMEOUT 60

enum
{
        GSM_FAIL_WHALE_DIALOG_RESPONSE_LOG_OUT,
        GSM_FAIL_WHALE_DIALOG_RESPONSE_TRY_RECOVERY
};

struct _GsmFailWhaleDialogPrivate
{
        GsmFailWhaleDialogFailType type;

        int                  timeout;
        unsigned int         timeout_id;

        unsigned int         default_response;
};

static GsmFailWhaleDialog *current_dialog = NULL;

static void gsm_fail_whale_dialog_destroy  (GsmFailWhaleDialog *fail_dialog,
                                            gpointer         data);

enum {
        PROP_0,
        PROP_FAIL_TYPE
};

G_DEFINE_TYPE (GsmFailWhaleDialog, gsm_fail_whale_dialog, GTK_TYPE_MESSAGE_DIALOG);

static void
gsm_fail_whale_dialog_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
        GsmFailWhaleDialog *dialog = GSM_FAIL_WHALE_DIALOG (object);
        (void) dialog;

        switch (prop_id) {
        case PROP_FAIL_TYPE:
                dialog->priv->type = g_value_get_enum (value);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_fail_whale_dialog_get_property (GObject     *object,
                                    guint        prop_id,
                                    GValue      *value,
                                    GParamSpec  *pspec)
{
        GsmFailWhaleDialog *dialog = GSM_FAIL_WHALE_DIALOG (object);
        (void) dialog;

        switch (prop_id) {
        case PROP_FAIL_TYPE:
                g_value_set_enum (value, dialog->priv->type);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_fail_whale_dialog_class_init (GsmFailWhaleDialogClass *klass)
{
        GObjectClass *gobject_class;

        gobject_class = G_OBJECT_CLASS (klass);
        gobject_class->set_property = gsm_fail_whale_dialog_set_property;
        gobject_class->get_property = gsm_fail_whale_dialog_get_property;

        g_type_class_add_private (klass, sizeof (GsmFailWhaleDialogPrivate));
}

static void
gsm_fail_whale_dialog_init (GsmFailWhaleDialog *fail_dialog)
{
        fail_dialog->priv = GSM_FAIL_WHALE_DIALOG_GET_PRIVATE (fail_dialog);

        fail_dialog->priv->timeout_id = 0;
        fail_dialog->priv->timeout = 0;
        fail_dialog->priv->default_response = GTK_RESPONSE_CANCEL;

        gtk_window_set_skip_taskbar_hint (GTK_WINDOW (fail_dialog), TRUE);
        gtk_window_set_keep_above (GTK_WINDOW (fail_dialog), TRUE);
        gtk_window_stick (GTK_WINDOW (fail_dialog));

        g_signal_connect (fail_dialog,
                          "destroy",
                          G_CALLBACK (gsm_fail_whale_dialog_destroy),
                          NULL);
}

static void
gsm_fail_whale_dialog_destroy (GsmFailWhaleDialog *fail_dialog,
                               gpointer         data)
{
        if (fail_dialog->priv->timeout_id != 0) {
                g_source_remove (fail_dialog->priv->timeout_id);
                fail_dialog->priv->timeout_id = 0;
        }

        g_assert (current_dialog != NULL);
        current_dialog = NULL;
}

static gboolean
gsm_fail_whale_dialog_timeout (gpointer data)
{
        GsmFailWhaleDialog *fail_dialog;
        char            *seconds_warning;
        int              seconds_to_show;

        fail_dialog = GSM_FAIL_WHALE_DIALOG (data);

        if (fail_dialog->priv->timeout == 0) {
                gtk_dialog_response (GTK_DIALOG (fail_dialog), GSM_FAIL_WHALE_DIALOG_RESPONSE_LOG_OUT);
                return FALSE;
        }

        if (fail_dialog->priv->timeout <= 30) {
                seconds_to_show = fail_dialog->priv->timeout;
        } else {
                seconds_to_show = (fail_dialog->priv->timeout/10) * 10;

                if (fail_dialog->priv->timeout % 10)
                        seconds_to_show += 10;
        }

        /* This string is shared with gsm-logout-dialog.c */
        seconds_warning = ngettext ("You will be automatically logged "
                                    "out in %d second.",
                                    "You will be automatically logged "
                                    "out in %d seconds.",
                                    seconds_to_show);
        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (fail_dialog),
                                                  seconds_warning,
                                                  seconds_to_show,
                                                  NULL);
        fail_dialog->priv->timeout--;

        return TRUE;
}

static void
gsm_fail_whale_dialog_on_response (GsmFailWhaleDialog *fail_dialog,
                                   int                 response_id,
                                   gpointer            user_data)
{
        switch (response_id) {
        case GSM_FAIL_WHALE_DIALOG_RESPONSE_LOG_OUT:
                gsm_manager_logout (gsm_manager_get (),
                                    GSM_MANAGER_LOGOUT_MODE_FORCE,
                                    NULL);
                break;
        }
}

void
gsm_fail_whale_dialog_we_failed (GsmFailWhaleDialogFailType fail_type,
                                 const char *               additional_text)
{
        GsmFailWhaleDialog *fail_dialog;
        char *primary_text;

        g_printerr ("gnome-session: %s failure: %s\n",
                    fail_type == GSM_FAIL_WHALE_DIALOG_FAIL_TYPE_FATAL ? "fatal" : "recoverable",
                    additional_text ? additional_text : "");

        if (current_dialog != NULL) {
                /* Only allow replacing non-fatal dialogs */
                if (fail_type == GSM_FAIL_WHALE_DIALOG_FAIL_TYPE_FATAL)
                        gtk_widget_destroy (GTK_WIDGET (current_dialog));
                else
                        return;
        }

        fail_dialog = g_object_new (GSM_TYPE_FAIL_WHALE_DIALOG,
                                    "fail-type", fail_type,
                                    NULL);
        
        current_dialog = fail_dialog;

        gtk_window_set_title (GTK_WINDOW (fail_dialog), "");

        gtk_dialog_add_button (GTK_DIALOG (fail_dialog),
                               _("_Log Out"),
                               GSM_FAIL_WHALE_DIALOG_RESPONSE_LOG_OUT);

        gtk_window_set_icon_name (GTK_WINDOW (fail_dialog), "gtk-error");
        gtk_window_set_position (GTK_WINDOW (fail_dialog), GTK_WIN_POS_CENTER_ALWAYS);
        
        switch (fail_type) {
        case GSM_FAIL_WHALE_DIALOG_FAIL_TYPE_RECOVERABLE:
                gtk_dialog_add_button (GTK_DIALOG (fail_dialog),
                                       _("Try _Recovery"),
                                       GSM_FAIL_WHALE_DIALOG_RESPONSE_TRY_RECOVERY);
                primary_text = g_strdup_printf (_("A non-fatal system error has occurred: %s"), additional_text);
                break;
        case GSM_FAIL_WHALE_DIALOG_FAIL_TYPE_FATAL:
                primary_text = g_strdup_printf (_("A fatal system error has occurred: %s"), additional_text);
                break;
        default:
                g_assert_not_reached ();
                return;
        }
        gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG (fail_dialog),
                                       primary_text);
        gtk_dialog_set_default_response (GTK_DIALOG (fail_dialog),
                                         GSM_FAIL_WHALE_DIALOG_RESPONSE_LOG_OUT);

        fail_dialog->priv->timeout = AUTOMATIC_ACTION_TIMEOUT;

        gsm_fail_whale_dialog_timeout (fail_dialog);

        if (fail_dialog->priv->timeout_id != 0) {
                g_source_remove (fail_dialog->priv->timeout_id);
        }
        fail_dialog->priv->timeout_id = g_timeout_add (1000,
                                                       gsm_fail_whale_dialog_timeout,
                                                       fail_dialog);
        g_signal_connect (fail_dialog, "response",
                          G_CALLBACK (gsm_fail_whale_dialog_on_response),
                          fail_dialog);
        gtk_widget_show (GTK_WIDGET (fail_dialog));
}

