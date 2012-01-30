/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Author: Matthias Clasen
 */

#include "config.h"
#include "gsm-systemd.h"

#ifdef HAVE_SYSTEMD

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>

#include <polkit/polkit.h>
#include <systemd/sd-login.h>
#include <systemd/sd-daemon.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include "gsm-marshal.h"
#include "gsm-system.h"

#define SD_NAME              "org.freedesktop.login1"
#define SD_PATH              "/org/freedesktop/login1"
#define SD_INTERFACE         "org.freedesktop.login1.Manager"
#define SD_SEAT_INTERFACE    "org.freedesktop.login1.Seat"
#define SD_SESSION_INTERFACE "org.freedesktop.login1.Session"

struct _GsmSystemdPrivate
{
        GDBusProxy      *sd_proxy;
        gchar           *session_id;
        gchar           *session_path;
        PolkitAuthority *authority;
        PolkitSubject   *subject;
};

static void gsm_systemd_system_init (GsmSystemInterface *iface);

G_DEFINE_TYPE_WITH_CODE (GsmSystemd, gsm_systemd, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GSM_TYPE_SYSTEM,
                                                gsm_systemd_system_init))


static void
gsm_systemd_finalize (GObject *object)
{
        GsmSystemd *systemd = GSM_SYSTEMD (object);

        g_clear_object (&systemd->priv->sd_proxy);
        g_clear_object (&systemd->priv->authority);
        g_clear_object (&systemd->priv->subject);
        g_free (systemd->priv->session_id);
        g_free (systemd->priv->session_path);

        G_OBJECT_CLASS (gsm_systemd_parent_class)->finalize (object);
}

static void
gsm_systemd_class_init (GsmSystemdClass *manager_class)
{
        GObjectClass *object_class;

        object_class = G_OBJECT_CLASS (manager_class);

        object_class->finalize = gsm_systemd_finalize;

        g_type_class_add_private (manager_class, sizeof (GsmSystemdPrivate));
}

static void
gsm_systemd_init (GsmSystemd *manager)
{
        GError *error;
        GDBusConnection *bus;
        GVariant *res;

        manager->priv = G_TYPE_INSTANCE_GET_PRIVATE (manager,
                                                     GSM_TYPE_SYSTEMD,
                                                     GsmSystemdPrivate);

        error = NULL;

        bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
        if (bus == NULL) {
                g_warning ("Failed to connect to system bus: %s",
                           error->message);
                g_error_free (error);
        } else {
                manager->priv->sd_proxy =
                        g_dbus_proxy_new_sync (bus,
                                               0,
                                               NULL,
                                               SD_NAME,
                                               SD_PATH,
                                               SD_INTERFACE,
                                               NULL,
                                               &error);

                if (manager->priv->sd_proxy == NULL) {
                        g_warning ("Failed to connect to systemd: %s",
                                   error->message);
                        g_error_free (error);
                }

                g_object_unref (bus);
        }

        manager->priv->authority = polkit_authority_get_sync (NULL, NULL);
        manager->priv->subject = polkit_unix_session_new_for_process_sync (getpid (), NULL, NULL);

        sd_pid_get_session (getpid (), &manager->priv->session_id);

        res = g_dbus_proxy_call_sync (manager->priv->sd_proxy,
                                      "GetSession",
                                      g_variant_new ("(s)", manager->priv->session_id),
                                      0,
                                      G_MAXINT,
                                      NULL,
                                      NULL);
        g_variant_get (res, "(o)", &manager->priv->session_path);
        g_variant_unref (res);
}

static void
emit_restart_complete (GsmSystemd *manager,
                       GError     *error)
{
        GError *call_error;

        call_error = NULL;

        if (error != NULL) {
                call_error = g_error_new_literal (GSM_SYSTEM_ERROR,
                                                  GSM_SYSTEM_ERROR_RESTARTING,
                                                  error->message);
        }

        g_signal_emit_by_name (G_OBJECT (manager),
                               "request_completed", call_error);

        if (call_error != NULL) {
                g_error_free (call_error);
        }
}

static void
emit_stop_complete (GsmSystemd *manager,
                    GError     *error)
{
        GError *call_error;

        call_error = NULL;

        if (error != NULL) {
                call_error = g_error_new_literal (GSM_SYSTEM_ERROR,
                                                  GSM_SYSTEM_ERROR_STOPPING,
                                                  error->message);
        }

        g_signal_emit_by_name (G_OBJECT (manager),
                               "request_completed", call_error);

        if (call_error != NULL) {
                g_error_free (call_error);
        }
}

static void
restart_done (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
        GDBusProxy *proxy = G_DBUS_PROXY (source);
        GsmSystemd *manager = user_data;
        GError *error = NULL;
        GVariant *res;

        res = g_dbus_proxy_call_finish (proxy, result, &error);

        if (!res) {
                g_warning ("Unable to restart system: %s", error->message);
                emit_restart_complete (manager, error);
                g_error_free (error);
        } else {
                emit_restart_complete (manager, NULL);
        }

        g_variant_unref (res);
}

static void
gsm_systemd_attempt_restart (GsmSystem *system)
{
        GsmSystemd *manager = GSM_SYSTEMD (system);

        g_dbus_proxy_call (manager->priv->sd_proxy,
                           "Reboot",
                           g_variant_new ("(b)", TRUE),
                           0,
                           G_MAXINT,
                           NULL,
                           restart_done,
                           NULL);
}

static void
stop_done (GObject      *source,
           GAsyncResult *result,
           gpointer      user_data)
{
        GDBusProxy *proxy = G_DBUS_PROXY (source);
        GsmSystemd *manager = user_data;
        GError *error = NULL;
        GVariant *res;

        res = g_dbus_proxy_call_finish (proxy, result, &error);

        if (!res) {
                g_warning ("Unable to stop system: %s", error->message);
                emit_stop_complete (manager, error);
                g_error_free (error);
        } else {
                emit_stop_complete (manager, NULL);
        }

        g_variant_unref (res);
}

static void
gsm_systemd_attempt_stop (GsmSystem *system)
{
        GsmSystemd *manager = GSM_SYSTEMD (system);

        g_dbus_proxy_call (manager->priv->sd_proxy,
                           "PowerOff",
                           g_variant_new ("(b)", TRUE),
                           0,
                           G_MAXINT,
                           NULL,
                           stop_done,
                           NULL);
}

static void
gsm_systemd_set_session_idle (GsmSystem *system,
                              gboolean   is_idle)
{
        GsmSystemd *manager = GSM_SYSTEMD (system);
        GDBusConnection *bus;

        g_debug ("Updating systemd idle status: %d", is_idle);
        bus = g_dbus_proxy_get_connection (manager->priv->sd_proxy);
        g_dbus_connection_call (bus,
                                SD_NAME,
                                manager->priv->session_path,
                                SD_SESSION_INTERFACE,
                                "SetIdleHint",
                                g_variant_new ("(b)", is_idle),
                                G_VARIANT_TYPE_BOOLEAN,
                                0,
                                G_MAXINT,
                                NULL, NULL, NULL);
}

static gboolean
gsm_systemd_can_switch_user (GsmSystem *system)
{
        GsmSystemd *manager = GSM_SYSTEMD (system);
        gchar *seat;
        gint ret;

        sd_session_get_seat (manager->priv->session_id, &seat);
        ret = sd_seat_can_multi_session (seat);
        free (seat);

        return ret > 0;
}

static gboolean
gsm_systemd_can_restart (GsmSystem *system)
{
        GsmSystemd *manager = GSM_SYSTEMD (system);
        PolkitAuthorizationResult *res;
        gboolean can_restart;

        res = polkit_authority_check_authorization_sync (manager->priv->authority,
                                                         manager->priv->subject,
                                                         "org.freedesktop.login1.reboot",
                                                         NULL,
                                                         POLKIT_CHECK_AUTHORIZATION_FLAGS_NONE,
                                                         NULL,
                                                         NULL);
        if (res == NULL) {
                return FALSE;
        }

        can_restart = polkit_authorization_result_get_is_authorized (res) ||
                      polkit_authorization_result_get_is_challenge (res);

        g_object_unref (res);

        return can_restart;
}

static gboolean
gsm_systemd_can_stop (GsmSystem *system)
{
        GsmSystemd *manager = GSM_SYSTEMD (system);
        PolkitAuthorizationResult *res;
        gboolean can_stop;

        res = polkit_authority_check_authorization_sync (manager->priv->authority,
                                                         manager->priv->subject,
                                                         "org.freedesktop.login1.power-off",
                                                         NULL,
                                                         POLKIT_CHECK_AUTHORIZATION_FLAGS_NONE,
                                                         NULL,
                                                         NULL);
        if (res == NULL) {
                return FALSE;
        }

        can_stop = polkit_authorization_result_get_is_authorized (res) ||
                   polkit_authorization_result_get_is_challenge (res);

        g_object_unref (res);

        return can_stop;
}

static gboolean
gsm_systemd_is_login_session (GsmSystem *system)
{
        GsmSystemd *manager = GSM_SYSTEMD (system);
        int res;
        gboolean ret;
        gchar *service = NULL;

        ret = FALSE;

        res = sd_session_get_service (manager->priv->session_id, &service);
        if (res < 0) {
                g_warning ("could not get pam service: %s", strerror (-ret));
                return FALSE;
        }
        ret = (g_strcmp0 (service, "gdm-welcome") == 0);
        free (service);

        return ret;
}

static void
gsm_systemd_system_init (GsmSystemInterface *iface)
{
        iface->can_switch_user = gsm_systemd_can_switch_user;
        iface->can_stop = gsm_systemd_can_stop;
        iface->can_restart = gsm_systemd_can_restart;
        iface->attempt_stop = gsm_systemd_attempt_stop;
        iface->attempt_restart = gsm_systemd_attempt_restart;
        iface->set_session_idle = gsm_systemd_set_session_idle;
        iface->is_login_session = gsm_systemd_is_login_session;
}

GsmSystemd *
gsm_systemd_new (void)
{
        GsmSystemd *manager;

        if (sd_booted () <= 0)
                return NULL;

        manager = g_object_new (GSM_TYPE_SYSTEMD, NULL);

        return manager;
}

#else

GsmSystemd *
gsm_systemd_new (void)
{
        return NULL;
}

#endif
