/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Novell, Inc.
 * Copyright (C) 2008 Red Hat, Inc.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/wait.h>
#include <glib.h>

#include <X11/SM/SMlib.h>

#include "gsm-resumed-app.h"

#define GSM_RESUMED_APP_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSM_TYPE_RESUMED_APP, GsmResumedAppPrivate))

struct _GsmResumedAppPrivate
{
        char    *program;
        char    *restart_command;
        char    *discard_command;
        gboolean discard_on_resume;
        GPid     pid;
        guint    child_watch_id;
};

G_DEFINE_TYPE (GsmResumedApp, gsm_resumed_app, GSM_TYPE_APP)

static void
gsm_resumed_app_init (GsmResumedApp *app)
{
        app->priv = GSM_RESUMED_APP_GET_PRIVATE (app);
}

static void
app_exited (GPid           pid,
            int            status,
            GsmResumedApp *app)
{
        g_debug ("GsmResumedApp: (pid:%d) done (%s:%d)",
                 (int) pid,
                 WIFEXITED (status) ? "status"
                 : WIFSIGNALED (status) ? "signal"
                 : "unknown",
                 WIFEXITED (status) ? WEXITSTATUS (status)
                 : WIFSIGNALED (status) ? WTERMSIG (status)
                 : -1);

        g_spawn_close_pid (app->priv->pid);
        app->priv->pid = -1;
        app->priv->child_watch_id = 0;

        if (WIFEXITED (status)) {
                gsm_app_exited (GSM_APP (app));
        } else if (WIFSIGNALED (status)) {
                gsm_app_died (GSM_APP (app));
        }
}

static gboolean
gsm_resumed_app_start (GsmApp  *app,
                       GError **error)
{
        const char *restart_command;
        int         argc;
        char      **argv;
        gboolean    success;
        gboolean    res;
        pid_t       pid;
        GsmResumedApp *rapp;

        rapp = GSM_RESUMED_APP (app);

        restart_command = rapp->priv->restart_command;

        if (restart_command == NULL) {
                return FALSE;
        }

        res = g_shell_parse_argv (restart_command, &argc, &argv, error);
        if (!res) {
                return FALSE;
        }

        /* In theory, we should set up the environment according to
         * SmEnvironment, and the current directory according to
         * SmCurrentDirectory. However, ksmserver doesn't support either of
         * those properties, so apps that want to be portable can't depend
         * on them working anyway. Also, no one ever uses them. So we just
         * ignore them.
         */

        success = g_spawn_async (NULL,
                                 argv,
                                 NULL,
                                 G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                 NULL,
                                 NULL,
                                 &pid,
                                 error);
        g_strfreev (argv);

        if (success) {
                g_debug ("GsmResumedApp: started pid:%d",
                         rapp->priv->pid);
                rapp->priv->child_watch_id = g_child_watch_add (rapp->priv->pid,
                                                                (GChildWatchFunc)app_exited,
                                                                app);
        }

        return success;
}

static gboolean
gsm_resumed_app_restart (GsmApp  *app,
                         GError **error)
{
        GError  *local_error;
        gboolean res;

        local_error = NULL;
        res = gsm_app_stop (app, &local_error);
        if (! res) {
                g_propagate_error (error, local_error);
                return FALSE;
        }

        res = gsm_app_start (app, &local_error);
        if (! res) {
                g_propagate_error (error, local_error);
                return FALSE;
        }

        return TRUE;
}

static const char *
gsm_resumed_app_get_app_id (GsmApp *app)
{
        return GSM_RESUMED_APP (app)->priv->program;
}

static void
gsm_resumed_app_dispose (GObject *object)
{
        GsmResumedAppPrivate *priv;

        priv = GSM_RESUMED_APP (object)->priv;

        g_free (priv->program);
        priv->program = NULL;
        g_free (priv->restart_command);
        priv->restart_command = NULL;
        g_free (priv->discard_command);
        priv->discard_command = NULL;

        if (priv->child_watch_id > 0) {
                g_source_remove (priv->child_watch_id);
                priv->child_watch_id = 0;
        }

        G_OBJECT_CLASS (gsm_resumed_app_parent_class)->dispose (object);
}

static void
gsm_resumed_app_class_init (GsmResumedAppClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        GsmAppClass  *app_class = GSM_APP_CLASS (klass);

        object_class->dispose = gsm_resumed_app_dispose;

        app_class->impl_get_app_id = gsm_resumed_app_get_app_id;
        app_class->impl_start = gsm_resumed_app_start;
        app_class->impl_restart = gsm_resumed_app_restart;

        g_type_class_add_private (klass, sizeof (GsmResumedAppPrivate));
}

/**
 * gsm_resumed_app_new_from_legacy_session:
 * @session_file: a session file (eg, ~/.gnome2/session) from the old
 * gnome-session
 * @n: the number of the client in @session_file to read
 *
 * Creates a new #GsmApp corresponding to the indicated client in
 * a legacy session file.
 *
 * Return value: the new #GsmApp, or %NULL if the @n'th entry in
 * @session_file is not actually a saved XSMP client.
 **/
GsmApp *
gsm_resumed_app_new_from_legacy_session (GKeyFile *session_file,
                                         int       n)
{
        GsmResumedApp *app;
        char          *key;
        char          *id;
        char          *val;

        key = g_strdup_printf ("%d,id", n);
        id = g_key_file_get_string (session_file, "Default", key, NULL);
        g_free (key);

        if (!id) {
                /* Not actually a saved app, just a redundantly-specified
                 * autostart app; ignore.
                 */
                return NULL;
        }

        app = g_object_new (GSM_TYPE_RESUMED_APP,
                            "startup-id", id,
                            NULL);
        g_free (id);

        key = g_strdup_printf ("%d," SmProgram, n);
        val = g_key_file_get_string (session_file, "Default", key, NULL);
        g_free (key);

        if (val) {
                app->priv->program = val;
        }

        key = g_strdup_printf ("%d," SmRestartCommand, n);
        val = g_key_file_get_string (session_file, "Default", key, NULL);
        g_free (key);

        if (val) {
                app->priv->restart_command = val;
        }

        /* We ignore the discard_command on apps resumed from the legacy
         * session, so that the legacy session will still work if the user
         * reverts back to the old gnome-session.
         */
        app->priv->discard_on_resume = FALSE;

        return (GsmApp *) app;
}
