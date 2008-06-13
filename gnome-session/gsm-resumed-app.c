/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Novell, Inc.
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

#include <X11/SM/SMlib.h>

#include "gsm-resumed-app.h"

#define GSM_RESUMED_APP_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSM_TYPE_RESUMED_APP, GsmResumedAppPrivate))

struct _GsmResumedAppPrivate
{
        char    *program;
        char    *restart_command;
        char    *discard_command;
        gboolean discard_on_resume;
};

G_DEFINE_TYPE (GsmResumedApp, gsm_resumed_app, GSM_TYPE_APP)

static void
gsm_resumed_app_init (GsmResumedApp *app)
{
        app->priv = GSM_RESUMED_APP_GET_PRIVATE (app);
}

static gboolean
launch (GsmApp  *app,
        GError **err)
{
        const char *restart_command;
        int         argc;
        char      **argv;
        gboolean    success;
        gboolean    res;
        pid_t       pid;

        restart_command = GSM_RESUMED_APP (app)->priv->restart_command;

        if (restart_command == NULL) {
                return FALSE;
        }

        res = g_shell_parse_argv (restart_command, &argc, &argv, err);
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
                                 err);
        g_strfreev (argv);

        return success;
}

static const char *
get_basename (GsmApp *app)
{
        return GSM_RESUMED_APP (app)->priv->program;
}

static void
gsm_resumed_app_class_init (GsmResumedAppClass *klass)
{
        GsmAppClass *app_class = GSM_APP_CLASS (klass);

        app_class->get_basename = get_basename;
        app_class->start = launch;

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
                            "client-id", id,
                            NULL);

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
