/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006, 2010 Novell, Inc.
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

#include <config.h>

#include "gsm-consolekit.h"
#include "gsm-gconf.h"
#include "gsm-util.h"
#include "gsm-manager.h"
#include "gsm-session-fill.h"

#define GSM_GCONF_DEFAULT_SESSION_KEY           "/desktop/gnome/session/default_session"
#define GSM_GCONF_REQUIRED_COMPONENTS_DIRECTORY "/desktop/gnome/session/required_components"
#define GSM_GCONF_REQUIRED_COMPONENTS_LIST_KEY  "/desktop/gnome/session/required_components_list"

#define IS_STRING_EMPTY(x) ((x)==NULL||(x)[0]=='\0')

/* This doesn't contain the required components, so we need to always
 * call append_required_apps() after a call to append_default_apps(). */
static void
append_default_apps (GsmManager *manager,
                     const char *default_session_key,
                     char      **autostart_dirs)
{
        GSList      *default_apps;
        GSList      *a;
        GConfClient *client;

        g_debug ("main: *** Adding default apps");

        g_assert (default_session_key != NULL);
        g_assert (autostart_dirs != NULL);

        client = gconf_client_get_default ();
        default_apps = gconf_client_get_list (client,
                                              default_session_key,
                                              GCONF_VALUE_STRING,
                                              NULL);
        g_object_unref (client);

        for (a = default_apps; a; a = a->next) {
                char *app_path;

                if (IS_STRING_EMPTY ((char *)a->data)) {
                        continue;
                }

                app_path = gsm_util_find_desktop_file_for_app_name (a->data, autostart_dirs);
                if (app_path != NULL) {
                        gsm_manager_add_autostart_app (manager, app_path, NULL);
                        g_free (app_path);
                }
        }

        g_slist_foreach (default_apps, (GFunc) g_free, NULL);
        g_slist_free (default_apps);
}

static void
append_required_apps (GsmManager *manager)
{
        GSList      *required_components;
        GSList      *r;
        GConfClient *client;

        g_debug ("main: *** Adding required apps");

        client = gconf_client_get_default ();
        required_components = gconf_client_get_list (client,
                                                     GSM_GCONF_REQUIRED_COMPONENTS_LIST_KEY,
                                                     GCONF_VALUE_STRING,
                                                     NULL);
        if (required_components == NULL) {
                g_warning ("No required applications specified");
        }

        for (r = required_components; r != NULL; r = r->next) {
                char       *path;
                char       *default_provider;
                const char *component;

                if (IS_STRING_EMPTY ((char *)r->data)) {
                        continue;
                }

                component = r->data;

                path = g_strdup_printf ("%s/%s",
                                        GSM_GCONF_REQUIRED_COMPONENTS_DIRECTORY,
                                        component);

                default_provider = gconf_client_get_string (client, path, NULL);
                g_debug ("main: %s looking for component: '%s'", path, default_provider);
                if (default_provider != NULL) {
                        char *app_path;

                        app_path = gsm_util_find_desktop_file_for_app_name (default_provider, NULL);
                        if (app_path != NULL) {
                                gsm_manager_add_autostart_app (manager, app_path, component);
                        } else {
                                g_warning ("Unable to find provider '%s' of required component '%s'",
                                           default_provider,
                                           component);
                        }
                        g_free (app_path);
                }

                g_free (default_provider);
                g_free (path);
        }

        g_debug ("main: *** Done adding required apps");

        g_slist_foreach (required_components, (GFunc)g_free, NULL);
        g_slist_free (required_components);

        g_object_unref (client);
}

static void
maybe_load_saved_session_apps (GsmManager *manager)
{
        GsmConsolekit *consolekit;
        char *session_type;

        consolekit = gsm_get_consolekit ();
        session_type = gsm_consolekit_get_current_session_type (consolekit);

        if (g_strcmp0 (session_type, GSM_CONSOLEKIT_SESSION_TYPE_LOGIN_WINDOW) != 0) {
                gsm_manager_add_autostart_apps_from_dir (manager, gsm_util_get_saved_session_dir ());
        }

        g_object_unref (consolekit);
        g_free (session_type);
}

static void
load_standard_apps (GsmManager *manager,
                    const char *default_session_key)
{
        char **autostart_dirs;
        int    i;

        autostart_dirs = gsm_util_get_autostart_dirs ();

        if (!gsm_manager_get_failsafe (manager)) {
                maybe_load_saved_session_apps (manager);

                for (i = 0; autostart_dirs[i]; i++) {
                        gsm_manager_add_autostart_apps_from_dir (manager,
                                                                 autostart_dirs[i]);
                }
        }

        /* We do this at the end in case a saved session contains an
         * application that already provides one of the components. */
        append_default_apps (manager, default_session_key, autostart_dirs);
        append_required_apps (manager);

        g_strfreev (autostart_dirs);
}

static void
load_override_apps (GsmManager *manager,
                    char      **override_autostart_dirs)
{
        int i;
        for (i = 0; override_autostart_dirs[i]; i++) {
                gsm_manager_add_autostart_apps_from_dir (manager, override_autostart_dirs[i]);
        }
}

void
gsm_session_fill (GsmManager  *manager,
                  char       **override_autostart_dirs,
                  char        *default_session_key)
{
        if (override_autostart_dirs != NULL) {
                load_override_apps (manager, override_autostart_dirs);
        } else {
                if (! IS_STRING_EMPTY (default_session_key)) {
                        load_standard_apps (manager, default_session_key);
                } else {
                        load_standard_apps (manager, GSM_GCONF_DEFAULT_SESSION_KEY);
                }
        }
}
