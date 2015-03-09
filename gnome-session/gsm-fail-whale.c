/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 * gsm-fail-whale.c
 * Copyright (C) 2012 Red Hat, Inc
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

#include <signal.h>
#include <stdlib.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "gsm-fail-whale.h"

static void
on_fail_whale_failed (void)
{
        raise (SIGTERM);
}

void
gsm_fail_whale_dialog_we_failed  (gboolean            debug_mode,
                                  gboolean            allow_logout,
                                  GsmShellExtensions *extensions)
{
        gint i;
        gchar *argv[5];
        GPid  pid;

        i = 0;
        argv[i++] = LIBEXECDIR "/gnome-session-failed";
        if (debug_mode)
                argv[i++] = "--debug";
        if (allow_logout)
                argv[i++] = "--allow-logout";
        if (extensions != NULL && gsm_shell_extensions_n_extensions (extensions) > 0)
                argv[i++] = "--extensions";
        argv[i++] = NULL;

        if (!g_spawn_async (NULL, argv, NULL, 0, NULL, NULL, &pid, NULL)) {
                exit (1);
        }

        g_child_watch_add (pid,
                           (GChildWatchFunc)on_fail_whale_failed,
                           NULL);
}
