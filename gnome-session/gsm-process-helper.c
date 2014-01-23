/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Novell, Inc.
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <gio/gio.h>
#include <glib/gi18n.h>

#include "gsm-process-helper.h"

typedef struct {
        gboolean        done;
        GSubprocess    *process;
        gboolean        caught_error;
        GError        **error;
        GMainContext   *maincontext;
        GSource        *timeout_source;
} GsmProcessHelper;

static void
on_child_exited (GObject        *source,
                 GAsyncResult   *result,
                 gpointer        data)
{
        GsmProcessHelper *helper = data;

        helper->done = TRUE;

        if (!g_subprocess_wait_check_finish ((GSubprocess*)source, result,
                                             helper->caught_error ? NULL : helper->error))
                helper->caught_error = TRUE;

        g_clear_pointer (&helper->timeout_source, g_source_destroy);

        g_main_context_wakeup (helper->maincontext);
}

static gboolean
on_child_timeout (gpointer data)
{
        GsmProcessHelper *helper = data;
        
        g_assert (!helper->done);
        
        g_subprocess_force_exit (helper->process);
        
        g_set_error_literal (helper->error,
                             G_IO_CHANNEL_ERROR,
                             G_IO_CHANNEL_ERROR_FAILED,
                             "Timed out");

        helper->timeout_source = NULL;
        
        return FALSE;
}

gboolean
gsm_process_helper (const char   *command_line,
                    unsigned int  timeout,
                    GError      **error)
{
        gboolean ret = FALSE;
        GsmProcessHelper helper = { 0, };
        gchar **argv = NULL;
        GMainContext *subcontext = NULL;

        if (!g_shell_parse_argv (command_line, NULL, &argv, error))
                goto out;

        helper.error = error;

        subcontext = g_main_context_new ();
        g_main_context_push_thread_default (subcontext);

        helper.process = g_subprocess_newv ((const char*const*)argv, 0, error);
        if (!helper.process)
                goto out;

        g_subprocess_wait_async (helper.process, NULL, on_child_exited, &helper);

        helper.timeout_source = g_timeout_source_new (timeout);

        g_source_set_callback (helper.timeout_source, on_child_timeout, &helper, NULL);
        g_source_attach (helper.timeout_source, subcontext);

        while (!helper.done)
                g_main_context_iteration (subcontext, TRUE);

        ret = helper.caught_error;
 out:
        g_strfreev (argv);
        if (subcontext) {
                g_main_context_pop_thread_default (subcontext);
                g_main_context_unref (subcontext);
        }
        g_clear_object (&helper.process);
        return ret;
}
