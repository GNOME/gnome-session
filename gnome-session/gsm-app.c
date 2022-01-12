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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <string.h>

#include "gsm-app.h"
#include "org.gnome.SessionManager.App.h"

/* If a component crashes twice within a minute, we count that as a fatal error */
#define _GSM_APP_RESPAWN_RATELIMIT_SECONDS 60

typedef struct
{
        char            *id;
        char            *app_id;
        int              phase;
        char            *startup_id;
        gboolean         registered;
        GTimeVal         last_restart_time;
        GDBusConnection *connection;
        GsmExportedApp  *skeleton;
} GsmAppPrivate;


enum {
        EXITED,
        DIED,
        LAST_SIGNAL
};

static guint32 app_serial = 1;

static guint signals[LAST_SIGNAL] = { 0 };

enum {
        PROP_0,
        PROP_ID,
        PROP_STARTUP_ID,
        PROP_PHASE,
        PROP_REGISTERED,
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

static gboolean
gsm_app_get_app_id (GsmExportedApp        *skeleton,
                    GDBusMethodInvocation *invocation,
                    GsmApp                *app)
{
        const gchar *id;

        id = GSM_APP_GET_CLASS (app)->impl_get_app_id (app);
        gsm_exported_app_complete_get_app_id (skeleton, invocation, id);

        return TRUE;
}

static gboolean
gsm_app_get_startup_id (GsmExportedApp        *skeleton,
                        GDBusMethodInvocation *invocation,
                        GsmApp                *app)
{
        GsmAppPrivate *priv = gsm_app_get_instance_private (app);
        const gchar *id;

        id = g_strdup (priv->startup_id);
        gsm_exported_app_complete_get_startup_id (skeleton, invocation, id);

        return TRUE;
}

static gboolean
gsm_app_get_phase (GsmExportedApp        *skeleton,
                   GDBusMethodInvocation *invocation,
                   GsmApp                *app)
{
        GsmAppPrivate *priv = gsm_app_get_instance_private (app);

        gsm_exported_app_complete_get_phase (skeleton, invocation, priv->phase);
        return TRUE;
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

static gboolean
register_app (GsmApp *app)
{
        GsmAppPrivate *priv = gsm_app_get_instance_private (app);
        GError *error;
        GsmExportedApp *skeleton;

        error = NULL;
        priv->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
        if (error != NULL) {
                g_critical ("error getting session bus: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        skeleton = gsm_exported_app_skeleton_new ();
        priv->skeleton = skeleton;
        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (skeleton),
                                          priv->connection, priv->id,
                                          &error);

        if (error != NULL) {
                g_critical ("error registering app on session bus: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        g_signal_connect (skeleton, "handle-get-app-id",
                          G_CALLBACK (gsm_app_get_app_id), app);
        g_signal_connect (skeleton, "handle-get-phase",
                          G_CALLBACK (gsm_app_get_phase), app);
        g_signal_connect (skeleton, "handle-get-startup-id",
                          G_CALLBACK (gsm_app_get_startup_id), app);

        return TRUE;
}

static GObject *
gsm_app_constructor (GType                  type,
                     guint                  n_construct_properties,
                     GObjectConstructParam *construct_properties)
{
        GsmApp    *app;
        GsmAppPrivate *priv;
        gboolean   res;

        app = GSM_APP (G_OBJECT_CLASS (gsm_app_parent_class)->constructor (type,
                                                                           n_construct_properties,
                                                                           construct_properties));
        priv = gsm_app_get_instance_private (app);

        g_free (priv->id);
        priv->id = g_strdup_printf ("/org/gnome/SessionManager/App%u", get_next_app_serial ());

        res = register_app (app);
        if (! res) {
                g_warning ("Unable to register app with session bus");
        }

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
        case PROP_REGISTERED:
                gsm_app_set_registered (app, g_value_get_boolean (value));
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
        case PROP_REGISTERED:
                g_value_set_boolean (value, priv->registered);
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

        if (priv->skeleton != NULL) {
                g_dbus_interface_skeleton_unexport_from_connection (G_DBUS_INTERFACE_SKELETON (priv->skeleton),
                                                                    priv->connection);
                g_clear_object (&priv->skeleton);
        }

        g_clear_object (&priv->connection);

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
        klass->impl_get_autorestart = NULL;
        klass->impl_provides = NULL;
        klass->impl_get_provides = NULL;
        klass->impl_is_running = NULL;

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

        g_object_class_install_property (object_class,
                                         PROP_REGISTERED,
                                         g_param_spec_boolean ("registered",
                                                               "Registered",
                                                               "Registered",
                                                               FALSE,
                                                               G_PARAM_READWRITE));

        signals[EXITED] =
                g_signal_new ("exited",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmAppClass, exited),
                              NULL, NULL, NULL,
                              G_TYPE_NONE,
                              1, G_TYPE_UCHAR);
        signals[DIED] =
                g_signal_new ("died",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmAppClass, died),
                              NULL, NULL, NULL,
                              G_TYPE_NONE,
                              1, G_TYPE_INT);
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
gsm_app_peek_is_conditionally_disabled (GsmApp *app)
{
        g_return_val_if_fail (GSM_IS_APP (app), FALSE);

        if (GSM_APP_GET_CLASS (app)->impl_is_conditionally_disabled) {
                return GSM_APP_GET_CLASS (app)->impl_is_conditionally_disabled (app);
        } else {
                return FALSE;
        }
}

gboolean
gsm_app_is_running (GsmApp *app)
{
        g_return_val_if_fail (GSM_IS_APP (app), FALSE);

        if (GSM_APP_GET_CLASS (app)->impl_is_running) {
                return GSM_APP_GET_CLASS (app)->impl_is_running (app);
        } else {
                return FALSE;
        }
}

gboolean
gsm_app_peek_autorestart (GsmApp *app)
{
        g_return_val_if_fail (GSM_IS_APP (app), FALSE);

        if (GSM_APP_GET_CLASS (app)->impl_get_autorestart) {
                return GSM_APP_GET_CLASS (app)->impl_get_autorestart (app);
        } else {
                return FALSE;
        }
}

gboolean
gsm_app_provides (GsmApp *app, const char *service)
{
        if (GSM_APP_GET_CLASS (app)->impl_provides) {
                return GSM_APP_GET_CLASS (app)->impl_provides (app, service);
        } else {
                return FALSE;
        }
}

char **
gsm_app_get_provides (GsmApp *app)
{
        if (GSM_APP_GET_CLASS (app)->impl_get_provides) {
                return GSM_APP_GET_CLASS (app)->impl_get_provides (app);
        } else {
                return NULL;
        }
}

gboolean
gsm_app_has_autostart_condition (GsmApp     *app,
                                 const char *condition)
{

        if (GSM_APP_GET_CLASS (app)->impl_has_autostart_condition) {
                return GSM_APP_GET_CLASS (app)->impl_has_autostart_condition (app, condition);
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

gboolean
gsm_app_restart (GsmApp  *app,
                 GError **error)
{
        GsmAppPrivate *priv = gsm_app_get_instance_private (app);
        GTimeVal current_time;
        g_debug ("Re-starting app: %s", priv->id);

        g_get_current_time (&current_time);
        if (priv->last_restart_time.tv_sec > 0
            && (current_time.tv_sec - priv->last_restart_time.tv_sec) < _GSM_APP_RESPAWN_RATELIMIT_SECONDS) {
                g_warning ("App '%s' respawning too quickly", gsm_app_peek_app_id (app));
                g_set_error (error,
                             GSM_APP_ERROR,
                             GSM_APP_ERROR_RESTART_LIMIT,
                             "Component '%s' crashing too quickly",
                             gsm_app_peek_app_id (app));
                return FALSE;
        }
        priv->last_restart_time = current_time;

        return GSM_APP_GET_CLASS (app)->impl_restart (app, error);
}

gboolean
gsm_app_stop (GsmApp  *app,
              GError **error)
{
        return GSM_APP_GET_CLASS (app)->impl_stop (app, error);
}

void
gsm_app_exited (GsmApp *app,
                guchar  exit_code)
{
        g_return_if_fail (GSM_IS_APP (app));

        g_signal_emit (app, signals[EXITED], 0, exit_code);
}

void
gsm_app_died (GsmApp *app,
              int     signal)
{
        g_return_if_fail (GSM_IS_APP (app));

        g_signal_emit (app, signals[DIED], 0, signal);
}

gboolean
gsm_app_get_registered (GsmApp *app)
{
        GsmAppPrivate *priv = gsm_app_get_instance_private (app);

        g_return_val_if_fail (GSM_IS_APP (app), FALSE);

        return priv->registered;
}

void
gsm_app_set_registered (GsmApp   *app,
                        gboolean  registered)
{
        GsmAppPrivate *priv = gsm_app_get_instance_private (app);

        g_return_if_fail (GSM_IS_APP (app));

        if (priv->registered != registered) {
                priv->registered = registered;
                g_object_notify (G_OBJECT (app), "registered");
        }
}

gboolean
gsm_app_save_to_keyfile (GsmApp    *app,
                         GKeyFile  *keyfile,
                         GError   **error)
{
        GsmAppPrivate *priv = gsm_app_get_instance_private (app);

        g_debug ("Saving app: %s", priv->id);
        return GSM_APP_GET_CLASS (app)->impl_save_to_keyfile (app, keyfile, error);
}
