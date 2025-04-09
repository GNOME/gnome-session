/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011,2025 Red Hat, Inc.
 * Copyright (C) 2019 Canonical Ltd.
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *        Colin Walters <walters@verbum.org>
 *        Marco Trevisan <marco@ubuntu.com>
 *        Adrian Vovk <adrianvovk@gmail.com>
 */

#include <config.h>

#include <stdlib.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "gsm-icon-names.h"

typedef struct GsmFailContext {
        GMainLoop *loop;
        GList *monitors;

        gboolean debug_mode;
        gboolean allow_logout;
        gboolean extensions;
} GsmFailContext;

static gboolean
on_close_req (void)
{
        /* Suppress any further handling, including destruction of the window. */
        return TRUE;
}

static void
on_logout_clicked (GtkWidget      *button,
                   GsmFailContext *ctx)
{
        if (!ctx->debug_mode) {
                g_spawn_command_line_async ("gnome-session-quit --force", NULL);
        }
        g_main_loop_quit (ctx->loop);
}

static void
create_fail_dialog (GdkMonitor     *monitor,
                    GsmFailContext *ctx)
{
        GtkWindow *dialog;
        GtkBox *box;
        GtkImage *icon;
        gchar *markup;
        GtkLabel *title;
        gchar *message_text;
        GtkLabel *message;

        dialog = GTK_WINDOW (gtk_window_new ());

        gtk_window_set_title (dialog, "");
        gtk_window_set_icon_name (dialog, GSM_ICON_COMPUTER_FAIL);

        /* we only allow the window to be closed via on_monitors_changed */
        gtk_window_set_deletable (dialog, FALSE);
        g_signal_connect(dialog, "close-request", G_CALLBACK (on_close_req), NULL);

        box = GTK_BOX (gtk_box_new (GTK_ORIENTATION_VERTICAL, 12));
        gtk_widget_set_valign (GTK_WIDGET (box), GTK_ALIGN_CENTER);
        gtk_window_set_child (dialog, GTK_WIDGET (box));

        icon = GTK_IMAGE (gtk_image_new_from_icon_name (GSM_ICON_COMPUTER_FAIL));
        gtk_image_set_pixel_size (icon, 128);
        gtk_box_append (box, GTK_WIDGET (icon));


        markup = g_strdup_printf ("<b><big>%s</big></b>", _("Oh no! Something has gone wrong."));
        title = GTK_LABEL (gtk_label_new (markup));
        gtk_label_set_use_markup (title, TRUE);
        gtk_box_append (box, GTK_WIDGET (title));
        g_free (markup);

        if (!ctx->allow_logout)
                message_text = _("A problem has occurred and the system can’t recover. Please contact a system administrator");
        else if (ctx->extensions)
                message_text = _("A problem has occurred and the system can’t recover. All extensions have been disabled as a precaution.");
        else
                message_text = _("A problem has occurred and the system can’t recover.\nPlease log out and try again.");
        message = GTK_LABEL (gtk_label_new (message_text));
        gtk_label_set_justify (message, GTK_JUSTIFY_CENTER);
        gtk_label_set_wrap (message, TRUE);
        gtk_box_append (box, GTK_WIDGET (message));

        if (ctx->allow_logout) {
                GtkButton *btn;
                btn = GTK_BUTTON (gtk_button_new_with_mnemonic (_("_Log Out")));
                gtk_widget_set_halign (GTK_WIDGET (btn), GTK_ALIGN_CENTER);
                g_signal_connect (btn, "clicked",
                                  G_CALLBACK (on_logout_clicked), ctx);
                gtk_box_append (box, GTK_WIDGET (btn));
        }

        gtk_window_present (dialog);
        gtk_window_fullscreen_on_monitor (dialog, monitor);

        g_object_set_data_full (G_OBJECT (monitor), "gsm_fail_whale",
                                dialog, (GDestroyNotify) gtk_window_destroy);
}

static void
on_monitors_changed (GListModel     *monitors,
                     guint           position,
                     guint           removed,
                     guint           added,
                     GsmFailContext *ctx)
{
        GList *cursor;
        guint i;

        cursor = g_list_nth (ctx->monitors, position);

        for (i = position; i < position + added; i++) {
                GdkMonitor *monitor = GDK_MONITOR (g_list_model_get_item (monitors, i));
                create_fail_dialog(monitor, ctx);
                ctx->monitors = g_list_insert_before (ctx->monitors, cursor, monitor);
        }

        for (i = 0; i < removed; i++) {
                GList *next = cursor->next;
                g_object_unref (G_OBJECT (cursor->data));
                ctx->monitors = g_list_delete_link (ctx->monitors, cursor);
                cursor = next;
        }
}

int main (int argc, char *argv[])
{
        GsmFailContext ctx = {};
        GOptionEntry entries[] = {
                { "debug", 0, 0, G_OPTION_ARG_NONE, &ctx.debug_mode, N_("Enable debugging code"), NULL },
                { "allow-logout", 0, 0, G_OPTION_ARG_NONE, &ctx.allow_logout, N_("Allow logout"), NULL },
                { "extensions", 0, 0, G_OPTION_ARG_NONE, &ctx.extensions, N_("Show extension warning"), NULL },
                { NULL, 0, 0, 0, NULL, NULL, NULL }
        };

        GError *error = NULL;
        GOptionContext *opts;
        GListModel *monitors;

        bindtextdomain (GETTEXT_PACKAGE, LOCALE_DIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        opts = g_option_context_new (" - fail whale");
        g_option_context_add_main_entries (opts, entries, GETTEXT_PACKAGE);
        if (!g_option_context_parse (opts, &argc, &argv, &error)) {
                g_warning ("%s", error->message);
                exit (1);
        }
        g_option_context_free (opts);

        /* Force-off allow_logout when running inside GDM, this is needed
         * because the systemd service always passes --allow-logout
         */
        if (g_strcmp0 (g_getenv ("RUNNING_UNDER_GDM"), "true") == 0)
                ctx.allow_logout = FALSE;

        gtk_init ();
        ctx.loop = g_main_loop_new (NULL, TRUE);

        monitors = gdk_display_get_monitors (gdk_display_get_default ());
        on_monitors_changed(monitors, 0, 0, g_list_model_get_n_items (monitors), &ctx);
        g_signal_connect (monitors, "items-changed", G_CALLBACK (on_monitors_changed), &ctx);

        g_main_loop_run (ctx.loop);

        g_main_loop_unref (ctx.loop);
        g_list_free_full (ctx.monitors, g_object_unref);

        return 0;
}

