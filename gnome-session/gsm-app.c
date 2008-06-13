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
#include "config.h"
#endif

#include <glib.h>
#include <string.h>

#include "gsm-app.h"

#define GSM_APP_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSM_TYPE_APP, GsmAppPrivate))

struct _GsmAppPrivate
{
        char   *id;
        int     phase;
        char   *client_id;
};


enum {
        EXITED,
        DIED,
        REGISTERED,
        LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

enum {
        PROP_0,
        PROP_ID,
        PROP_CLIENT_ID,
        PROP_PHASE,
        LAST_PROP
};

G_DEFINE_TYPE (GsmApp, gsm_app, G_TYPE_OBJECT)

static void
gsm_app_init (GsmApp *app)
{
        app->priv = GSM_APP_GET_PRIVATE (app);
}

static void
gsm_app_set_phase (GsmApp *app,
                   int     phase)
{
        g_return_if_fail (GSM_IS_APP (app));

        app->priv->phase = phase;
}

static void
set_property (GObject      *object,
              guint         prop_id,
              const GValue *value,
              GParamSpec   *pspec)
{
        GsmApp *app = GSM_APP (object);

        switch (prop_id) {
        case PROP_CLIENT_ID:
                g_free (app->priv->client_id);
                app->priv->client_id = g_value_dup_string (value);
                break;
        case PROP_ID:
                g_free (app->priv->id);
                app->priv->id = g_value_dup_string (value);
                break;
        case PROP_PHASE:
                gsm_app_set_phase (app, g_value_get_int (value));
                break;
        default:
                break;
        }
}

static void
get_property (GObject    *object,
              guint       prop_id,
              GValue     *value,
              GParamSpec *pspec)
{
        GsmApp *app = GSM_APP (object);

        switch (prop_id) {
        case PROP_CLIENT_ID:
                g_value_set_string (value, app->priv->client_id);
                break;
        case PROP_ID:
                g_value_set_string (value, app->priv->id);
                break;
        case PROP_PHASE:
                g_value_set_int (value, app->priv->phase);
                break;
        default:
                break;
        }
}

static void
dispose (GObject *object)
{
        GsmApp *app = GSM_APP (object);

        if (app->priv->client_id) {
                g_free (app->priv->client_id);
                app->priv->client_id = NULL;
        }
}

static void
gsm_app_class_init (GsmAppClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->set_property = set_property;
        object_class->get_property = get_property;
        object_class->dispose = dispose;

        klass->get_basename = NULL;
        klass->start = NULL;
        klass->provides = NULL;
        klass->is_running = NULL;

        g_object_class_install_property (object_class,
                                         PROP_PHASE,
                                         g_param_spec_int ("phase",
                                                           "Phase",
                                                           "Phase",
                                                           -1,
                                                           G_MAXINT,
                                                           -1,
                                                           G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_ID,
                                         g_param_spec_string ("id",
                                                              "ID",
                                                              "ID",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_CLIENT_ID,
                                         g_param_spec_string ("client-id",
                                                              "Client ID",
                                                              "Session management client ID",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        signals[EXITED] =
                g_signal_new ("exited",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmAppClass, exited),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals[DIED] =
                g_signal_new ("died",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmAppClass, died),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

        signals[REGISTERED] =
                g_signal_new ("registered",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmAppClass, registered),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

        g_type_class_add_private (klass, sizeof (GsmAppPrivate));
}

const char *
gsm_app_get_basename (GsmApp *app)
{
        return GSM_APP_GET_CLASS (app)->get_basename (app);
}

const char *
gsm_app_get_id (GsmApp *app)
{
        return app->priv->id;
}

const char *
gsm_app_get_client_id (GsmApp *app)
{
        return app->priv->client_id;
}

/**
 * gsm_app_get_phase:
 * @app: a %GsmApp
 *
 * Returns @app's startup phase.
 *
 * Return value: @app's startup phase
 **/
GsmManagerPhase
gsm_app_get_phase (GsmApp *app)
{
        g_return_val_if_fail (GSM_IS_APP (app), GSM_MANAGER_PHASE_APPLICATION);

        return app->priv->phase;
}

gboolean
gsm_app_is_disabled (GsmApp *app)
{
        g_return_val_if_fail (GSM_IS_APP (app), FALSE);

        if (GSM_APP_GET_CLASS (app)->is_disabled) {
                return GSM_APP_GET_CLASS (app)->is_disabled (app);
        } else {
                return FALSE;
        }
}

gboolean
gsm_app_is_running (GsmApp *app)
{
        g_return_val_if_fail (GSM_IS_APP (app), FALSE);

        if (GSM_APP_GET_CLASS (app)->is_running) {
                return GSM_APP_GET_CLASS (app)->is_running (app);
        } else {
                return FALSE;
        }
}

gboolean
gsm_app_provides (GsmApp *app, const char *service)
{

        if (GSM_APP_GET_CLASS (app)->provides) {
                return GSM_APP_GET_CLASS (app)->provides (app, service);
        } else {
                return FALSE;
        }
}

gboolean
gsm_app_start (GsmApp  *app,
               GError **error)
{
        g_debug ("Starting app: %s", app->priv->id);

        return GSM_APP_GET_CLASS (app)->start (app, error);
}

gboolean
gsm_app_stop (GsmApp  *app,
              GError **error)
{
        return GSM_APP_GET_CLASS (app)->stop (app, error);
}

void
gsm_app_registered (GsmApp *app)
{
        g_return_if_fail (GSM_IS_APP (app));

        g_signal_emit (app, signals[REGISTERED], 0);
}

void
gsm_app_exited (GsmApp *app)
{
        g_return_if_fail (GSM_IS_APP (app));

        g_signal_emit (app, signals[EXITED], 0);
}

void
gsm_app_died (GsmApp *app)
{
        g_return_if_fail (GSM_IS_APP (app));

        g_signal_emit (app, signals[DIED], 0);
}
