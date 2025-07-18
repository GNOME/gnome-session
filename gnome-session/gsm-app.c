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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <glib.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-systemd.h>

#include "gsm-app.h"
#include "gsm-util.h"

#define GSM_APP_SYSTEMD_SKIP_KEY   "X-systemd-skip"
#define GSM_APP_SYSTEMD_HIDDEN_KEY "X-GNOME-HiddenUnderSystemd"
#define GSM_APP_ENABLED_KEY        "X-GNOME-Autostart-enabled"
#define GSM_APP_PHASE_KEY          "X-GNOME-Autostart-Phase"

/* This comment is a record of keys that were previously used but are not used
 * anymore. We keep this so that we don't accidentally redefine these keys in
 * the future, to be used in some incompatible way.
 *
 * X-GNOME-AutoRestart
 * X-GNOME-Autostart-discard-exec
 * AutostartCondition
 * X-GNOME-DBus-Name
 * X-GNOME-DBus-Path
 * X-GNOME-DBus-Start-Arguments
 * X-GNOME-Autostart-startup-id
 * */

struct _GsmApp
{
        GObject          parent;
        GDesktopAppInfo *inner;
};

enum {
        PROP_0,
        PROP_INNER,
        LAST_PROP
};

G_DEFINE_TYPE (GsmApp, gsm_app, G_TYPE_OBJECT)

static void
gsm_app_init (GsmApp *app)
{
}

static void
gsm_app_set_inner (GsmApp          *app,
                   GDesktopAppInfo *app_info)
{
        g_return_if_fail (GSM_IS_APP (app));
        g_return_if_fail (app_info == NULL || G_IS_DESKTOP_APP_INFO (app_info));

        g_set_object (&app->inner, app_info);
}

static void
gsm_app_set_property (GObject      *object,
                      guint         prop_id,
                      const GValue *value,
                      GParamSpec   *pspec)
{
        GsmApp *app = GSM_APP (object);

        switch (prop_id) {
        case PROP_INNER:
                gsm_app_set_inner (app, g_value_get_object (value));
                break;
        default:
                break;
        }
}

static void
gsm_app_get_property (GObject    *object,
                      guint       prop_id,
                      GValue     *value,
                      GParamSpec *pspec)
{
        GsmApp *app = GSM_APP (object);

        switch (prop_id) {
        case PROP_INNER:
                g_value_set_object (value, app->inner);
                break;
        default:
                break;
        }
}

static void
gsm_app_dispose (GObject *object)
{
        GsmApp *app = GSM_APP (object);

        g_clear_object (&app->inner);

        G_OBJECT_CLASS (gsm_app_parent_class)->dispose (object);
}

static void
gsm_app_class_init (GsmAppClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->set_property = gsm_app_set_property;
        object_class->get_property = gsm_app_get_property;
        object_class->dispose = gsm_app_dispose;

        g_object_class_install_property (object_class,
                                         PROP_INNER,
                                         g_param_spec_object ("inner",
                                                              "Inner",
                                                              "Inner",
                                                              G_TYPE_DESKTOP_APP_INFO,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

const char *
gsm_app_peek_app_id (GsmApp *app)
{
        g_return_val_if_fail (GSM_IS_APP (app), NULL);

        return g_app_info_get_id (G_APP_INFO (app->inner));
}

gboolean
gsm_app_peek_is_disabled (GsmApp *app)
{
        g_return_val_if_fail (GSM_IS_APP (app), FALSE);

        /* GSM_AUTOSTART_APP_ENABLED_KEY key, used by old gnome-session */
        if (g_desktop_app_info_has_key (app->inner, GSM_APP_ENABLED_KEY) &&
            !g_desktop_app_info_get_boolean (app->inner, GSM_APP_ENABLED_KEY)) {
                g_debug ("App %s is disabled by " GSM_APP_ENABLED_KEY,
                         gsm_app_peek_app_id (app));
                return TRUE;
        }

        /* Hidden key, used by fdo Desktop Entry spec */
        if (g_desktop_app_info_get_is_hidden (app->inner)) {
                g_debug ("App %s is disabled by Hidden",
                         gsm_app_peek_app_id (app));
                return TRUE;
        }

        /* Check OnlyShowIn/NotShowIn/TryExec */
        if (!g_desktop_app_info_get_show_in (app->inner, NULL)) {
                g_debug ("App %s is not for the current desktop",
                         gsm_app_peek_app_id (app));
                return TRUE;
        }

        /* Check if app is systemd enabled */
        if (g_desktop_app_info_get_boolean (app->inner, GSM_APP_SYSTEMD_HIDDEN_KEY)) {
                g_debug ("App %s is disabled by " GSM_APP_SYSTEMD_HIDDEN_KEY,
                         gsm_app_peek_app_id (app));
                return TRUE;
        }
        if (g_desktop_app_info_get_boolean (app->inner, GSM_APP_SYSTEMD_SKIP_KEY)) {
                g_debug ("App %s is disabled by " GSM_APP_SYSTEMD_SKIP_KEY,
                         gsm_app_peek_app_id (app));
                return TRUE;
        }

        return FALSE;
}

static void
app_launched (GAppLaunchContext *ctx,
              GAppInfo          *appinfo,
              GVariant          *platform_data,
              gpointer           data)
{
        GsmApp *app = data;

        gint pid = 0;
        g_variant_lookup (platform_data, "pid", "i", &pid);

        /* If pid == 0 the application was launched through D-Bus
         * activation, therefore it's already in its own unit */
        if (pid == 0)
                return;

        /* We are not interested in the result. */
        gnome_start_systemd_scope (gsm_app_peek_app_id (app),
                                   pid,
                                   NULL,
                                   NULL,
                                   NULL, NULL, NULL);
}

gboolean
gsm_app_start (GsmApp  *app,
               GError **error)
{
        g_autoptr (GAppLaunchContext) ctx = NULL;
        GError *local_error = NULL;
        const char * const *variable_denylist;
        const char * const *child_environment;
        guint handler;
        gboolean success;

        g_debug ("GsmApp: starting %s (command=%s)",
                 gsm_app_peek_app_id (app),
                 g_app_info_get_commandline (G_APP_INFO (app->inner)));

        ctx = g_app_launch_context_new ();

        variable_denylist = gsm_util_get_variable_blacklist ();
        for (size_t i = 0; variable_denylist[i] != NULL; i++)
                g_app_launch_context_unsetenv (ctx, variable_denylist[i]);

        child_environment = gsm_util_listenv ();
        for (size_t i = 0; child_environment[i] != NULL; i++) {
                g_auto (GStrv) split = g_strsplit (child_environment[i], "=", 2);
                if (split[1] != NULL)
                        g_app_launch_context_setenv (ctx, split[0], split[1]);
        }

        handler = g_signal_connect (ctx, "launched", G_CALLBACK (app_launched), app);
        success = g_desktop_app_info_launch_uris_as_manager (app->inner,
                                                             NULL,
                                                             ctx,
                                                             G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
                                                             NULL, NULL, NULL, NULL,
                                                             &local_error);
        if (!success)
                g_propagate_prefixed_error (error, local_error,
                                            "Unable to start app (%s): ",
                                            gsm_app_peek_app_id (app));

        g_signal_handler_disconnect (ctx, handler);

        return success;
}

GsmApp *
gsm_app_new (GDesktopAppInfo  *info,
             GError          **error)
{
        g_autofree char *app_phase = NULL;

        g_return_val_if_fail (info != NULL, NULL);

        app_phase = g_desktop_app_info_get_string (info, GSM_APP_PHASE_KEY);
        if (app_phase && strcmp (app_phase, "Application") != 0) {
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                             "App %s sets " GSM_APP_PHASE_KEY ", but gnome-session no longer manages session services",
                             g_app_info_get_id (G_APP_INFO(info)));
                return NULL;
        }

        return g_object_new (GSM_TYPE_APP, "inner", info, NULL);
}

GsmApp *
gsm_app_new_for_path (const char  *path,
                      GError     **error)
{
        g_autoptr (GDesktopAppInfo) info = NULL;

        g_return_val_if_fail (path != NULL, NULL);

        info = g_desktop_app_info_new_from_filename (path);
        if (info == NULL) {
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Desktop file %s couldn't be parsed, or references a missing TryExec binary",
                             path);
                return NULL;
        }

        return gsm_app_new (info, error);
}
