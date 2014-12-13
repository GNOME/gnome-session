/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include "gsm-shell.h"

#define SHELL_NAME      "org.gnome.Shell"
#define SHELL_PATH      "/org/gnome/Shell"
#define SHELL_INTERFACE "org.gnome.Shell"

#define GSM_SHELL_GET_PRIVATE(o)                                   \
        (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSM_TYPE_SHELL, GsmShellPrivate))

struct _GsmShellPrivate
{
        guint32          is_running : 1;

        guint            watch_id;
};

enum {
        PROP_0,
        PROP_IS_RUNNING
};

static void     gsm_shell_class_init   (GsmShellClass *klass);
static void     gsm_shell_init         (GsmShell      *ck);
static void     gsm_shell_finalize     (GObject            *object);

G_DEFINE_TYPE (GsmShell, gsm_shell, G_TYPE_OBJECT);

static void
gsm_shell_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
        GsmShell *shell = GSM_SHELL (object);

        switch (prop_id) {
        case PROP_IS_RUNNING:
                g_value_set_boolean (value,
                                     shell->priv->is_running);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object,
                                                   prop_id,
                                                   pspec);
        }
}

static void
gsm_shell_class_init (GsmShellClass *shell_class)
{
        GObjectClass *object_class;
        GParamSpec   *param_spec;

        object_class = G_OBJECT_CLASS (shell_class);

        object_class->finalize = gsm_shell_finalize;
        object_class->get_property = gsm_shell_get_property;

        param_spec = g_param_spec_boolean ("is-running",
                                           "Is running",
                                           "Whether GNOME Shell is running in the session",
                                           FALSE,
                                           G_PARAM_READABLE);

        g_object_class_install_property (object_class, PROP_IS_RUNNING,
                                         param_spec);

        g_type_class_add_private (shell_class, sizeof (GsmShellPrivate));
}

static void
on_shell_name_vanished (GDBusConnection *connection,
                        const gchar     *name,
                        gpointer         user_data)
{
        GsmShell *shell = user_data;
        shell->priv->is_running = FALSE;
}

static void
on_shell_name_appeared (GDBusConnection *connection,
                        const gchar     *name,
                        const gchar     *name_owner,
                        gpointer         user_data)
{
        GsmShell *shell = user_data;
        shell->priv->is_running = TRUE;
}

static void
gsm_shell_ensure_connection (GsmShell  *shell)
{
        if (shell->priv->watch_id != 0) {
                return;
        }

        shell->priv->watch_id = g_bus_watch_name (G_BUS_TYPE_SESSION,
                                                  SHELL_NAME,
                                                  G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                  on_shell_name_appeared,
                                                  on_shell_name_vanished,
                                                  shell, NULL);
}

static void
gsm_shell_init (GsmShell *shell)
{
        shell->priv = GSM_SHELL_GET_PRIVATE (shell);

        gsm_shell_ensure_connection (shell);
}

static void
gsm_shell_finalize (GObject *object)
{
        GsmShell *shell;
        GObjectClass  *parent_class;

        shell = GSM_SHELL (object);

        parent_class = G_OBJECT_CLASS (gsm_shell_parent_class);

        if (shell->priv->watch_id != 0) {
                g_bus_unwatch_name (shell->priv->watch_id);
                shell->priv->watch_id = 0;
        }

        if (parent_class->finalize != NULL) {
                parent_class->finalize (object);
        }
}

GsmShell *
gsm_shell_new (void)
{
        GsmShell *shell;

        shell = g_object_new (GSM_TYPE_SHELL, NULL);

        return shell;
}

GsmShell *
gsm_get_shell (void)
{
        static GsmShell *shell = NULL;

        if (shell == NULL) {
                shell = gsm_shell_new ();
        }

        return g_object_ref (shell);
}

gboolean
gsm_shell_is_running (GsmShell *shell)
{
        gsm_shell_ensure_connection (shell);

        return shell->priv->is_running;
}
