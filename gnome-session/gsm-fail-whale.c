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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <config.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "gsm-fail-whale.h"

void
gsm_fail_whale_dialog_we_failed  (gboolean            debug_mode,
                                  gboolean            allow_logout,
                                  GsmShellExtensions *extensions)
{
        gint i;
        gchar *argv[5];

        i = 0;
        argv[i++] = LIBEXECDIR "/gnome-session-failed";
        if (debug_mode)
                argv[i++] = "--debug";
        if (allow_logout)
                argv[i++] = "--allow-logout";
        if (extensions != NULL && gsm_shell_extensions_n_extensions (extensions) > 0)
                argv[i++] = "--extensions";
        argv[i++] = NULL;

        g_spawn_async (NULL, argv, NULL, 0, NULL, NULL, NULL, NULL);

        return 0;
}
