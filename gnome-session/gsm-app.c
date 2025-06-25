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

#include <glib.h>
#include <string.h>

#include "gsm-app.h"

typedef struct
{
        char            *id;
        char            *app_id;
        int              phase;
        char            *startup_id;
} GsmAppPrivate;

static guint32 app_serial = 1;

enum {
        PROP_0,
        PROP_ID,
        PROP_STARTUP_ID,
        PROP_PHASE,
        LAST_PROP
};

G_DEFINE_TYPE_WITH_PRIVATE (GsmApp, gsm_app, G_TYPE_OBJECT)

GQuark
gsm_app_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("gsm_app_error");
        }

        return ret;

}

static guint32
get_next_app_serial (void)
{
        guint32 serial;

        serial = app_serial++;

        if ((gint32)app_serial < 0) {
                app_serial = 1;
        }

        return serial;
}

static GObject *
gsm_app_constructor (GType                  type,
                     guint                  n_construct_properties,
                     GObjectConstructParam *construct_properties)
{
        GsmApp    *app;
        GsmAppPrivate *priv;

        app = GSM_APP (G_OBJECT_CLASS (gsm_app_parent_class)->constructor (type,
                                                                           n_construct_properties,
                                                                           construct_properties));
        priv = gsm_app_get_instance_private (app);

        g_free (priv->id);
        priv->id = g_strdup_printf ("/org/gnome/SessionManager/App%u", get_next_app_serial ());

        return G_OBJECT (app);
}

static void
gsm_app_init (GsmApp *app)
{
}

static void
gsm_app_set_phase (GsmApp *app,
                   int     phase)
{
        GsmAppPrivate *priv = gsm_app_get_instance_private (app);

        g_return_if_fail (GSM_IS_APP (app));

        priv->phase = phase;
}

static void
gsm_app_set_id (GsmApp     *app,
                const char *id)
{
        GsmAppPrivate *priv = gsm_app_get_instance_private (app);

        g_return_if_fail (GSM_IS_APP (app));

        g_free (priv->id);

        priv->id = g_strdup (id);
        g_object_notify (G_OBJECT (app), "id");

}
static void
gsm_app_set_startup_id (GsmApp     *app,
                        const char *startup_id)
{
        GsmAppPrivate *priv = gsm_app_get_instance_private (app);

        g_return_if_fail (GSM_IS_APP (app));

        g_free (priv->startup_id);

        priv->startup_id = g_strdup (startup_id);
        g_object_notify (G_OBJECT (app), "startup-id");

}

static void
gsm_app_set_property (GObject      *object,
                      guint         prop_id,
                      const GValue *value,
                      GParamSpec   *pspec)
{
        GsmApp *app = GSM_APP (object);

        switch (prop_id) {
        case PROP_STARTUP_ID:
                gsm_app_set_startup_id (app, g_value_get_string (value));
                break;
        case PROP_ID:
                gsm_app_set_id (app, g_value_get_string (value));
                break;
        case PROP_PHASE:
                gsm_app_set_phase (app, g_value_get_int (value));
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
        GsmAppPrivate *priv = gsm_app_get_instance_private (app);

        switch (prop_id) {
        case PROP_STARTUP_ID:
                g_value_set_string (value, priv->startup_id);
                break;
        case PROP_ID:
                g_value_set_string (value, priv->id);
                break;
        case PROP_PHASE:
                g_value_set_int (value, priv->phase);
                break;
        default:
                break;
        }
}

static void
gsm_app_dispose (GObject *object)
{
        GsmApp *app = GSM_APP (object);
        GsmAppPrivate *priv = gsm_app_get_instance_private (app);

        g_free (priv->startup_id);
        priv->startup_id = NULL;

        g_free (priv->id);
        priv->id = NULL;

        G_OBJECT_CLASS (gsm_app_parent_class)->dispose (object);
}

static void
gsm_app_class_init (GsmAppClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->set_property = gsm_app_set_property;
        object_class->get_property = gsm_app_get_property;
        object_class->dispose = gsm_app_dispose;
        object_class->constructor = gsm_app_constructor;

        klass->impl_start = NULL;
        klass->impl_get_app_id = NULL;

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
                                         PROP_STARTUP_ID,
                                         g_param_spec_string ("startup-id",
                                                              "startup ID",
                                                              "Session management startup ID",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

const char *
gsm_app_peek_id (GsmApp *app)
{
        GsmAppPrivate *priv = gsm_app_get_instance_private (app);

        return priv->id;
}

const char *
gsm_app_peek_app_id (GsmApp *app)
{
        return GSM_APP_GET_CLASS (app)->impl_get_app_id (app);
}

const char *
gsm_app_peek_startup_id (GsmApp *app)
{
        GsmAppPrivate *priv = gsm_app_get_instance_private (app);

        return priv->startup_id;
}

/**
 * gsm_app_peek_phase:
 * @app: a %GsmApp
 *
 * Returns @app's startup phase.
 *
 * Return value: @app's startup phase
 **/
GsmManagerPhase
gsm_app_peek_phase (GsmApp *app)
{
        GsmAppPrivate *priv = gsm_app_get_instance_private (app);

        g_return_val_if_fail (GSM_IS_APP (app), GSM_MANAGER_PHASE_APPLICATION);

        return priv->phase;
}

gboolean
gsm_app_peek_is_disabled (GsmApp *app)
{
        g_return_val_if_fail (GSM_IS_APP (app), FALSE);

        if (GSM_APP_GET_CLASS (app)->impl_is_disabled) {
                return GSM_APP_GET_CLASS (app)->impl_is_disabled (app);
        } else {
                return FALSE;
        }
}

gboolean
gsm_app_start (GsmApp  *app,
               GError **error)
{
        GsmAppPrivate *priv = gsm_app_get_instance_private (app);

        g_debug ("Starting app: %s", priv->id);
        return GSM_APP_GET_CLASS (app)->impl_start (app, error);
}

