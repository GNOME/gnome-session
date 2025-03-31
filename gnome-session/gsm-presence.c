/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Red Hat, Inc.
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

#include "config.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-idle-monitor.h>

#include "gsm-presence.h"
#include "org.gnome.SessionManager.Presence.h"

#define GSM_PRESENCE_DBUS_IFACE "org.gnome.SessionManager.Presence"
#define GSM_PRESENCE_DBUS_PATH "/org/gnome/SessionManager/Presence"

#define GS_NAME      "org.gnome.ScreenSaver"
#define GS_PATH      "/org/gnome/ScreenSaver"
#define GS_INTERFACE "org.gnome.ScreenSaver"

#define MAX_STATUS_TEXT 140

struct GsmPresencePrivate
{
        guint             status;
        guint             saved_status;
        char             *status_text;
        gboolean          idle_enabled;
        GnomeIdleMonitor *idle_monitor;
        guint             idle_watch_id;
        guint             idle_timeout;
        gboolean          screensaver_active;
        GDBusConnection  *connection;
        GDBusProxy       *screensaver_proxy;

        GsmExportedPresence *skeleton;
};

enum {
        PROP_0,
        PROP_IDLE_ENABLED,
        PROP_IDLE_TIMEOUT,
};

enum {
        STATUS_CHANGED,
        LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE_WITH_CODE (GsmPresence, gsm_presence, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (GsmPresence))

static const GDBusErrorEntry gsm_presence_error_entries[] = {
        { GSM_PRESENCE_ERROR_GENERAL, GSM_PRESENCE_DBUS_IFACE ".GeneralError" }
};

GQuark
gsm_presence_error_quark (void)
{
        static volatile gsize quark_volatile = 0;

        g_dbus_error_register_error_domain ("gsm_presence_error",
                                            &quark_volatile,
                                            gsm_presence_error_entries,
                                            G_N_ELEMENTS (gsm_presence_error_entries));
        return quark_volatile;
}

static void idle_became_active_cb (GnomeIdleMonitor *idle_monitor,
                                   guint             id,
                                   gpointer          user_data);

static void
gsm_presence_set_status (GsmPresence  *presence,
                         guint         status)
{
        if (status != presence->priv->status) {
                presence->priv->status = status;
                gsm_exported_presence_set_status (presence->priv->skeleton, status);
                gsm_exported_presence_emit_status_changed (presence->priv->skeleton, presence->priv->status);
                g_signal_emit (presence, signals[STATUS_CHANGED], 0, presence->priv->status);
        }
}

static gboolean
gsm_presence_set_status_text (GsmPresence  *presence,
                              const char   *status_text,
                              GError      **error)
{
        g_return_val_if_fail (GSM_IS_PRESENCE (presence), FALSE);

        g_free (presence->priv->status_text);
        presence->priv->status_text = NULL;

        /* check length */
        if (status_text != NULL && strlen (status_text) > MAX_STATUS_TEXT) {
                g_set_error (error,
                             GSM_PRESENCE_ERROR,
                             GSM_PRESENCE_ERROR_GENERAL,
                             "Status text too long");
                return FALSE;
        }

        if (status_text != NULL) {
                presence->priv->status_text = g_strdup (status_text);
        } else {
                presence->priv->status_text = g_strdup ("");
        }

        gsm_exported_presence_set_status_text (presence->priv->skeleton, presence->priv->status_text);
        gsm_exported_presence_emit_status_text_changed (presence->priv->skeleton, presence->priv->status_text);
        return TRUE;
}

static void
set_session_idle (GsmPresence   *presence,
                  gboolean       is_idle)
{
        g_debug ("GsmPresence: setting idle: %d", is_idle);

        if (is_idle) {
                if (presence->priv->status == GSM_PRESENCE_STATUS_IDLE) {
                        g_debug ("GsmPresence: already idle, ignoring");
                        return;
                }

                /* save current status */
                presence->priv->saved_status = presence->priv->status;
                gsm_presence_set_status (presence, GSM_PRESENCE_STATUS_IDLE);

                gnome_idle_monitor_add_user_active_watch (presence->priv->idle_monitor,
                                                          idle_became_active_cb,
                                                          presence,
                                                          NULL);
        } else {
                if (presence->priv->status != GSM_PRESENCE_STATUS_IDLE) {
                        g_debug ("GsmPresence: already not idle, ignoring");
                        return;
                }

                /* restore saved status */
                gsm_presence_set_status (presence, presence->priv->saved_status);
                g_debug ("GsmPresence: setting non-idle status %d", presence->priv->saved_status);
                presence->priv->saved_status = GSM_PRESENCE_STATUS_AVAILABLE;
        }
}

static void
idle_became_idle_cb (GnomeIdleMonitor *idle_monitor,
                     guint             id,
                     gpointer          user_data)
{
        GsmPresence *presence = user_data;
        set_session_idle (presence, TRUE);
}

static void
idle_became_active_cb (GnomeIdleMonitor *idle_monitor,
                       guint             id,
                       gpointer          user_data)
{
        GsmPresence *presence = user_data;
        set_session_idle (presence, FALSE);
}

static void
reset_idle_watch (GsmPresence  *presence)
{
        if (presence->priv->idle_watch_id > 0) {
                g_debug ("GsmPresence: removing idle watch (%i)", presence->priv->idle_watch_id);
                gnome_idle_monitor_remove_watch (presence->priv->idle_monitor,
                                                 presence->priv->idle_watch_id);
                presence->priv->idle_watch_id = 0;
        }

        if (presence->priv->idle_enabled
            && presence->priv->idle_timeout > 0) {
                presence->priv->idle_watch_id = gnome_idle_monitor_add_idle_watch (presence->priv->idle_monitor,
                                                                                   presence->priv->idle_timeout,
                                                                                   idle_became_idle_cb,
                                                                                   presence,
                                                                                   NULL);
                g_debug ("GsmPresence: adding idle watch (%i) for %d secs",
                         presence->priv->idle_watch_id,
                         presence->priv->idle_timeout / 1000);
        }
}

static void
on_screensaver_dbus_signal (GDBusProxy  *proxy,
                            gchar       *sender_name,
                            gchar       *signal_name,
                            GVariant    *parameters,
                            GsmPresence *presence)
{
        gboolean is_active;

        if (g_strcmp0 (signal_name, "ActiveChanged") != 0) {
                return;
        }

        g_variant_get (parameters, "(b)", &is_active);

        if (presence->priv->screensaver_active != is_active) {
                presence->priv->screensaver_active = is_active;
                set_session_idle (presence, is_active);
        }
}

static void
screensaver_get_active_cb (GDBusProxy  *screensaver_proxy,
                           GAsyncResult *res,
                           GsmPresence *presence)
{
        g_autoptr(GVariant) data = NULL;
        g_autoptr(GError) error = NULL;
        gboolean is_active;

        data = g_dbus_proxy_call_finish (screensaver_proxy, res, &error);
        if (!data) {
                if (error) {
                        g_warning ("Could not retrieve current screensaver active state: %s",
                                   error->message);
                } else {
                        g_warning ("Could not retrieve current screensaver active state!");
                }

                return;
        }

        g_variant_get (data, "(b)", &is_active);
        if (presence->priv->screensaver_active != is_active) {
                presence->priv->screensaver_active = is_active;
                set_session_idle (presence, is_active);
        }
}

static void
on_screensaver_name_owner_changed (GDBusProxy  *screensaver_proxy,
                                   GParamSpec  *pspec,
                                   GsmPresence *presence)
{
        gchar *name_owner;

        name_owner = g_dbus_proxy_get_name_owner (screensaver_proxy);
        if (name_owner == NULL) {
                g_debug ("Detected that screensaver has left the bus");

                presence->priv->screensaver_active = FALSE;
                set_session_idle (presence, FALSE);
        } else {
                g_debug ("Detected that screensaver has aquired the bus");

                g_dbus_proxy_call (presence->priv->screensaver_proxy,
                                   "GetActive",
                                   NULL,
                                   G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                   1000,
                                   NULL,
                                   (GAsyncReadyCallback) screensaver_get_active_cb,
                                   presence);
        }

        g_free (name_owner);
}

static gboolean
gsm_presence_set_status_text_dbus (GsmExportedPresence   *skeleton,
                                   GDBusMethodInvocation *invocation,
                                   gchar                 *status_text,
                                   GsmPresence           *presence)
{
        GError *error = NULL;

        if (gsm_presence_set_status_text (presence, status_text, &error)) {
                gsm_exported_presence_complete_set_status_text (skeleton, invocation);
        } else {
                g_dbus_method_invocation_take_error (invocation, error);
        }

        return TRUE;
}

static gboolean
gsm_presence_set_status_dbus (GsmExportedPresence   *skeleton,
                              GDBusMethodInvocation *invocation,
                              guint                  status,
                              GsmPresence           *presence)
{
        gsm_presence_set_status (presence, status);
        gsm_exported_presence_complete_set_status (skeleton, invocation);
        return TRUE;
}

static gboolean
register_presence (GsmPresence *presence)
{
        GError *error;
        GsmExportedPresence *skeleton;

        error = NULL;
        presence->priv->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
        if (error != NULL) {
                g_critical ("error getting session bus: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        skeleton = gsm_exported_presence_skeleton_new ();
        presence->priv->skeleton = skeleton;
        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (skeleton),
                                          presence->priv->connection,
                                          GSM_PRESENCE_DBUS_PATH, &error);
        if (error != NULL) {
                g_critical ("error registering presence object on session bus: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        g_signal_connect (skeleton, "handle-set-status",
                          G_CALLBACK (gsm_presence_set_status_dbus), presence);
        g_signal_connect (skeleton, "handle-set-status-text",
                          G_CALLBACK (gsm_presence_set_status_text_dbus), presence);

        return TRUE;
}

static GObject *
gsm_presence_constructor (GType                  type,
                          guint                  n_construct_properties,
                          GObjectConstructParam *construct_properties)
{
        GsmPresence *presence;
        gboolean     res;
        GError      *error = NULL;

        presence = GSM_PRESENCE (G_OBJECT_CLASS (gsm_presence_parent_class)->constructor (type,
                                                                                             n_construct_properties,
                                                                                             construct_properties));

        res = register_presence (presence);
        if (! res) {
                g_warning ("Unable to register presence with session bus");
        }

        /* This only connects to signals and resolves the current name owner
         * synchronously. It is important to not auto-start the service!
         */
        presence->priv->screensaver_proxy = g_dbus_proxy_new_sync (presence->priv->connection,
                                                                   G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START |
                                                                   G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                                                                   NULL,
                                                                   GS_NAME,
                                                                   GS_PATH,
                                                                   GS_INTERFACE,
                                                                   NULL, &error);
        if (error != NULL) {
                g_critical ("Unable to create a DBus proxy for GnomeScreensaver: %s",
                            error->message);
                g_error_free (error);
        } else {
                g_signal_connect (presence->priv->screensaver_proxy, "notify::g-name-owner",
                                  G_CALLBACK (on_screensaver_name_owner_changed), presence);
                g_signal_connect (presence->priv->screensaver_proxy, "g-signal",
                                  G_CALLBACK (on_screensaver_dbus_signal), presence);
        }

        return G_OBJECT (presence);
}

static void
gsm_presence_init (GsmPresence *presence)
{
        presence->priv = gsm_presence_get_instance_private (presence);

        presence->priv->idle_monitor = gnome_idle_monitor_new ();
}

void
gsm_presence_set_idle_enabled (GsmPresence  *presence,
                               gboolean      enabled)
{
        g_return_if_fail (GSM_IS_PRESENCE (presence));

        if (presence->priv->idle_enabled != enabled) {
                presence->priv->idle_enabled = enabled;
                reset_idle_watch (presence);
                g_object_notify (G_OBJECT (presence), "idle-enabled");

        }
}

void
gsm_presence_set_idle_timeout (GsmPresence  *presence,
                               guint         timeout)
{
        g_return_if_fail (GSM_IS_PRESENCE (presence));

        if (timeout != presence->priv->idle_timeout) {
                presence->priv->idle_timeout = timeout;
                reset_idle_watch (presence);
                g_object_notify (G_OBJECT (presence), "idle-timeout");
        }
}

static void
gsm_presence_set_property (GObject       *object,
                           guint          prop_id,
                           const GValue  *value,
                           GParamSpec    *pspec)
{
        GsmPresence *self;

        self = GSM_PRESENCE (object);

        switch (prop_id) {
        case PROP_IDLE_ENABLED:
                gsm_presence_set_idle_enabled (self, g_value_get_boolean (value));
                break;
        case PROP_IDLE_TIMEOUT:
                gsm_presence_set_idle_timeout (self, g_value_get_uint (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_presence_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
        GsmPresence *self;

        self = GSM_PRESENCE (object);

        switch (prop_id) {
        case PROP_IDLE_ENABLED:
                g_value_set_boolean (value, self->priv->idle_enabled);
                break;
        case PROP_IDLE_TIMEOUT:
                g_value_set_uint (value, self->priv->idle_timeout);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_presence_finalize (GObject *object)
{
        GsmPresence *presence = (GsmPresence *) object;

        if (presence->priv->idle_watch_id > 0) {
                gnome_idle_monitor_remove_watch (presence->priv->idle_monitor,
                                                 presence->priv->idle_watch_id);
                presence->priv->idle_watch_id = 0;
        }

        g_clear_pointer (&presence->priv->status_text, g_free);
        g_clear_object (&presence->priv->idle_monitor);
        g_clear_object (&presence->priv->screensaver_proxy);

        G_OBJECT_CLASS (gsm_presence_parent_class)->finalize (object);
}

static void
gsm_presence_class_init (GsmPresenceClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize             = gsm_presence_finalize;
        object_class->constructor          = gsm_presence_constructor;
        object_class->get_property         = gsm_presence_get_property;
        object_class->set_property         = gsm_presence_set_property;

        signals [STATUS_CHANGED] =
                g_signal_new ("status-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmPresenceClass, status_changed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__UINT,
                              G_TYPE_NONE,
                              1, G_TYPE_UINT);

        g_object_class_install_property (object_class,
                                         PROP_IDLE_ENABLED,
                                         g_param_spec_boolean ("idle-enabled",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_IDLE_TIMEOUT,
                                         g_param_spec_uint ("idle-timeout",
                                                            "idle timeout",
                                                            "idle timeout",
                                                            0,
                                                            G_MAXINT,
                                                            120000,
                                                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

GsmPresence *
gsm_presence_new (void)
{
        GsmPresence *presence;

        presence = g_object_new (GSM_TYPE_PRESENCE,
                                 NULL);

        return presence;
}
