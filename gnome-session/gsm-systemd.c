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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
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

#include <systemd/sd-login.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include "gsm-system.h"

#define SD_NAME              "org.freedesktop.login1"
#define SD_PATH              "/org/freedesktop/login1"
#define SD_INTERFACE         "org.freedesktop.login1.Manager"
#define SD_SEAT_INTERFACE    "org.freedesktop.login1.Seat"
#define SD_SESSION_INTERFACE "org.freedesktop.login1.Session"

struct _GsmSystemdPrivate
{
        GSource         *sd_source;
        GDBusProxy      *sd_proxy;
        char            *session_id;
        gchar           *session_path;

        GSList          *inhibitors;
        gint             inhibit_fd;

        gboolean         is_active;

        gint             delay_inhibit_fd;
        gboolean         prepare_for_shutdown_expected;
};

enum {
        PROP_0,
        PROP_ACTIVE
};

static void gsm_systemd_system_init (GsmSystemInterface *iface);

G_DEFINE_TYPE_WITH_CODE (GsmSystemd, gsm_systemd, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GSM_TYPE_SYSTEM,
                                                gsm_systemd_system_init))

static void
drop_system_inhibitor (GsmSystemd *manager)
{
        if (manager->priv->inhibit_fd != -1) {
                g_debug ("GsmSystemd: Dropping system inhibitor");
                close (manager->priv->inhibit_fd);
                manager->priv->inhibit_fd = -1;
        }
}

static void
drop_delay_inhibitor (GsmSystemd *manager)
{
        if (manager->priv->delay_inhibit_fd != -1) {
                g_debug ("GsmSystemd: Dropping delay inhibitor");
                close (manager->priv->delay_inhibit_fd);
                manager->priv->delay_inhibit_fd = -1;
        }
}

static void
gsm_systemd_finalize (GObject *object)
{
        GsmSystemd *systemd = GSM_SYSTEMD (object);

        g_clear_object (&systemd->priv->sd_proxy);
        free (systemd->priv->session_id);
        g_free (systemd->priv->session_path);

        if (systemd->priv->sd_source) {
                g_source_destroy (systemd->priv->sd_source);
        }

        if (systemd->priv->inhibitors != NULL) {
                g_slist_free_full (systemd->priv->inhibitors, g_free);
        }
        drop_system_inhibitor (systemd);
        drop_delay_inhibitor (systemd);

        G_OBJECT_CLASS (gsm_systemd_parent_class)->finalize (object);
}

static void
gsm_systemd_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
        GsmSystemd *self = GSM_SYSTEMD (object);

        switch (prop_id) {
        case PROP_ACTIVE:
                self->priv->is_active = g_value_get_boolean (value);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
}

static void
gsm_systemd_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
        GsmSystemd *self = GSM_SYSTEMD (object);

        switch (prop_id) {
        case PROP_ACTIVE:
                g_value_set_boolean (value, self->priv->is_active);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_systemd_class_init (GsmSystemdClass *manager_class)
{
        GObjectClass *object_class;

        object_class = G_OBJECT_CLASS (manager_class);

        object_class->get_property = gsm_systemd_get_property;
        object_class->set_property = gsm_systemd_set_property;
        object_class->finalize = gsm_systemd_finalize;

        g_object_class_override_property (object_class, PROP_ACTIVE, "active");

        g_type_class_add_private (manager_class, sizeof (GsmSystemdPrivate));
}

typedef struct
{
        GSource source;
        GPollFD pollfd;
        sd_login_monitor *monitor;
} SdSource;

static gboolean
sd_source_prepare (GSource *source,
                   gint    *timeout)
{
        *timeout = -1;
        return FALSE;
}

static gboolean
sd_source_check (GSource *source)
{
        SdSource *sd_source = (SdSource *)source;

        return sd_source->pollfd.revents != 0;
}

static gboolean
sd_source_dispatch (GSource     *source,
                    GSourceFunc  callback,
                    gpointer     user_data)

{
        SdSource *sd_source = (SdSource *)source;
        gboolean ret;

        g_warn_if_fail (callback != NULL);

        ret = (*callback) (user_data);

        sd_login_monitor_flush (sd_source->monitor);
        return ret;
}

static void
sd_source_finalize (GSource *source)
{
        SdSource *sd_source = (SdSource*)source;

        sd_login_monitor_unref (sd_source->monitor);
}

static GSourceFuncs sd_source_funcs = {
        sd_source_prepare,
        sd_source_check,
        sd_source_dispatch,
        sd_source_finalize
};

static GSource *
sd_source_new (void)
{
        GSource *source;
        SdSource *sd_source;
        int ret;

        source = g_source_new (&sd_source_funcs, sizeof (SdSource));
        sd_source = (SdSource *)source;

        if ((ret = sd_login_monitor_new ("session", &sd_source->monitor)) < 0) {
                g_warning ("Error getting login monitor: %d", ret);
        } else {
                sd_source->pollfd.fd = sd_login_monitor_get_fd (sd_source->monitor);
                sd_source->pollfd.events = G_IO_IN;
                g_source_add_poll (source, &sd_source->pollfd);
        }

        return source;
}

static gboolean
on_sd_source_changed (gpointer user_data)
{
        GsmSystemd *self = user_data;
        int active_r;
        gboolean active;

        active_r = sd_session_is_active (self->priv->session_id);
        if (active_r < 0)
                active = FALSE;
        else
                active = active_r;
        if (active != self->priv->is_active) {
                self->priv->is_active = active;
                g_object_notify (G_OBJECT (self), "active");
        }

        return TRUE;
}

static void sd_proxy_signal_cb (GDBusProxy  *proxy,
                                const gchar *sender_name,
                                const gchar *signal_name,
                                GVariant    *parameters,
                                gpointer     user_data);

static void
gsm_systemd_init (GsmSystemd *manager)
{
        GError *error = NULL;
        GDBusConnection *bus;
        GVariant *res;

        manager->priv = G_TYPE_INSTANCE_GET_PRIVATE (manager,
                                                     GSM_TYPE_SYSTEMD,
                                                     GsmSystemdPrivate);

        manager->priv->inhibit_fd = -1;
        manager->priv->delay_inhibit_fd = -1;

        bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
        if (bus == NULL)
                g_error ("Failed to connect to system bus: %s",
                         error->message);
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
                g_clear_error (&error);
        }

        g_signal_connect (manager->priv->sd_proxy, "g-signal",
                          G_CALLBACK (sd_proxy_signal_cb), manager);

        sd_pid_get_session (getpid (), &manager->priv->session_id);

        if (manager->priv->session_id == NULL) {
                g_warning ("Could not get session id for session. Check that logind is "
                           "properly installed and pam_systemd is getting used at login.");
                return;
        }

        res = g_dbus_proxy_call_sync (manager->priv->sd_proxy,
                                      "GetSession",
                                      g_variant_new ("(s)", manager->priv->session_id),
                                      0,
                                      G_MAXINT,
                                      NULL,
                                      NULL);
        if (res == NULL) {
                g_warning ("Could not get session id for session. Check that logind is "
                           "properly installed and pam_systemd is getting used at login.");
                return;
        }

        g_variant_get (res, "(o)", &manager->priv->session_path);
        g_variant_unref (res);

        manager->priv->sd_source = sd_source_new ();
        g_source_set_callback (manager->priv->sd_source, on_sd_source_changed, manager, NULL);
        g_source_attach (manager->priv->sd_source, NULL);

        on_sd_source_changed (manager);

        g_object_unref (bus);
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
                g_variant_unref (res);
        }
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
                           manager);
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
                g_variant_unref (res);
        }
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
                           manager);
}

static void
gsm_systemd_set_session_idle (GsmSystem *system,
                              gboolean   is_idle)
{
        GsmSystemd *manager = GSM_SYSTEMD (system);
        GDBusConnection *bus;

        if (manager->priv->session_path == NULL) {
                g_warning ("Could not get session path for session. Check that logind is "
                           "properly installed and pam_systemd is getting used at login.");
                return;
        }

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
        gchar *rv;
        GVariant *res;
        gboolean can_restart;

        res = g_dbus_proxy_call_sync (manager->priv->sd_proxy,
                                      "CanReboot",
                                      NULL,
                                      0,
                                      G_MAXINT,
                                      NULL,
                                      NULL);
        if (!res) {
                g_warning ("Calling CanReboot failed. Check that logind is "
                           "properly installed and pam_systemd is getting used at login.");
                return FALSE;
        }

        g_variant_get (res, "(s)", &rv);
        g_variant_unref (res);

        can_restart = g_strcmp0 (rv, "yes") == 0 ||
                      g_strcmp0 (rv, "challenge") == 0;

        g_free (rv);

        return can_restart;
}

static gboolean
gsm_systemd_can_stop (GsmSystem *system)
{
        GsmSystemd *manager = GSM_SYSTEMD (system);
        gchar *rv;
        GVariant *res;
        gboolean can_stop;

        res = g_dbus_proxy_call_sync (manager->priv->sd_proxy,
                                      "CanPowerOff",
                                      NULL,
                                      0,
                                      G_MAXINT,
                                      NULL,
                                      NULL);
        if (!res) {
                g_warning ("Calling CanPowerOff failed. Check that logind is "
                           "properly installed and pam_systemd is getting used at login.");
                return FALSE;
        }

        g_variant_get (res, "(s)", &rv);
        g_variant_unref (res);

        can_stop = g_strcmp0 (rv, "yes") == 0 ||
                   g_strcmp0 (rv, "challenge") == 0;

        g_free (rv);

        return can_stop;
}

static gboolean
gsm_systemd_is_login_session (GsmSystem *system)
{
        GsmSystemd *manager = GSM_SYSTEMD (system);
        int res;
        gboolean ret;
        gchar *session_class = NULL;

        ret = FALSE;

        if (manager->priv->session_id == NULL) {
                return ret;
        }

        res = sd_session_get_class (manager->priv->session_id, &session_class);
        if (res < 0) {
                g_warning ("Could not get session class: %s", strerror (-res));
                return FALSE;
        }
        ret = (g_strcmp0 (session_class, "greeter") == 0);
        free (session_class);

        return ret;
}

static gboolean
gsm_systemd_can_suspend (GsmSystem *system)
{
        GsmSystemd *manager = GSM_SYSTEMD (system);
        gchar *rv;
        GVariant *res;
        gboolean can_suspend;

        res = g_dbus_proxy_call_sync (manager->priv->sd_proxy,
                                      "CanSuspend",
                                      NULL,
                                      0,
                                      G_MAXINT,
                                      NULL,
                                      NULL);
        if (!res) {
                g_warning ("Calling CanSuspend failed. Check that logind is "
                           "properly installed and pam_systemd is getting used at login.");
                return FALSE;
        }

        g_variant_get (res, "(s)", &rv);
        g_variant_unref (res);

        can_suspend = g_strcmp0 (rv, "yes") == 0 ||
                      g_strcmp0 (rv, "challenge") == 0;

        g_free (rv);

        return can_suspend;
}

static gboolean
gsm_systemd_can_hibernate (GsmSystem *system)
{
        GsmSystemd *manager = GSM_SYSTEMD (system);
        gchar *rv;
        GVariant *res;
        gboolean can_hibernate;

        res = g_dbus_proxy_call_sync (manager->priv->sd_proxy,
                                      "CanHibernate",
                                      NULL,
                                      0,
                                      G_MAXINT,
                                      NULL,
                                      NULL);
        if (!res) {
                g_warning ("Calling CanHibernate failed. Check that logind is "
                           "properly installed and pam_systemd is getting used at login.");
                return FALSE;
        }

        g_variant_get (res, "(s)", &rv);
        g_variant_unref (res);

        can_hibernate = g_strcmp0 (rv, "yes") == 0 ||
                        g_strcmp0 (rv, "challenge") == 0;

        g_free (rv);

        return can_hibernate;
}

static void
suspend_done (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
        GDBusProxy *proxy = G_DBUS_PROXY (source);
        GError *error = NULL;
        GVariant *res;

        res = g_dbus_proxy_call_finish (proxy, result, &error);

        if (!res) {
                g_warning ("Unable to suspend system: %s", error->message);
                g_error_free (error);
        } else {
                g_variant_unref (res);
        }
}

static void
hibernate_done (GObject      *source,
                GAsyncResult *result,
              gpointer      user_data)
{
        GDBusProxy *proxy = G_DBUS_PROXY (source);
        GError *error = NULL;
        GVariant *res;

        res = g_dbus_proxy_call_finish (proxy, result, &error);

        if (!res) {
                g_warning ("Unable to hibernate system: %s", error->message);
                g_error_free (error);
        } else {
                g_variant_unref (res);
        }
}

static void
gsm_systemd_suspend (GsmSystem *system)
{
        GsmSystemd *manager = GSM_SYSTEMD (system);

        g_dbus_proxy_call (manager->priv->sd_proxy,
                           "Suspend",
                           g_variant_new ("(b)", TRUE),
                           0,
                           G_MAXINT,
                           NULL,
                           hibernate_done,
                           manager);
}

static void
gsm_systemd_hibernate (GsmSystem *system)
{
        GsmSystemd *manager = GSM_SYSTEMD (system);

        g_dbus_proxy_call (manager->priv->sd_proxy,
                           "Hibernate",
                           g_variant_new ("(b)", TRUE),
                           0,
                           G_MAXINT,
                           NULL,
                           suspend_done,
                           manager);
}

static void
inhibit_done (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
        GDBusProxy *proxy = G_DBUS_PROXY (source);
        GsmSystemd *manager = GSM_SYSTEMD (user_data);
        GError *error = NULL;
        GVariant *res;
        GUnixFDList *fd_list = NULL;
        gint idx;

        res = g_dbus_proxy_call_with_unix_fd_list_finish (proxy, &fd_list, result, &error);

        if (!res) {
                g_warning ("Unable to inhibit system: %s", error->message);
                g_error_free (error);
        } else {
                g_variant_get (res, "(h)", &idx);
                manager->priv->inhibit_fd = g_unix_fd_list_get (fd_list, idx, &error);
                if (manager->priv->inhibit_fd == -1) {
                        g_warning ("Failed to receive system inhibitor fd: %s", error->message);
                        g_error_free (error);
                }
                g_debug ("System inhibitor fd is %d", manager->priv->inhibit_fd);
                g_object_unref (fd_list);
                g_variant_unref (res);
        }

        if (manager->priv->inhibitors == NULL) {
                drop_system_inhibitor (manager);
        }
}

static void
gsm_systemd_add_inhibitor (GsmSystem        *system,
                           const gchar      *id,
                           GsmInhibitorFlag  flag)
{
        GsmSystemd *manager = GSM_SYSTEMD (system);

        if ((flag & GSM_INHIBITOR_FLAG_SUSPEND) == 0)
                return;

        if (manager->priv->inhibitors == NULL) {
                g_debug ("Adding system inhibitor");
                g_dbus_proxy_call_with_unix_fd_list (manager->priv->sd_proxy,
                                                     "Inhibit",
                                                     g_variant_new ("(ssss)",
                                                                    "sleep:shutdown",
                                                                    g_get_user_name (),
                                                                    "user session inhibited",
                                                                    "block"),
                                                     0,
                                                     G_MAXINT,
                                                     NULL,
                                                     NULL,
                                                     inhibit_done,
                                                     manager);
        }
        manager->priv->inhibitors = g_slist_prepend (manager->priv->inhibitors, g_strdup (id));
}

static void
gsm_systemd_remove_inhibitor (GsmSystem   *system,
                              const gchar *id)
{
        GsmSystemd *manager = GSM_SYSTEMD (system);
        GSList *l;

        l = g_slist_find_custom (manager->priv->inhibitors, id, (GCompareFunc)g_strcmp0);
        if (l == NULL)
                return;

        g_free (l->data);
        manager->priv->inhibitors = g_slist_delete_link (manager->priv->inhibitors, l);
        if (manager->priv->inhibitors == NULL) {
                drop_system_inhibitor (manager);
        }
}

static void
reboot_or_poweroff_done (GObject      *source,
                         GAsyncResult *res,
                         gpointer      user_data)
{
        GsmSystemd *systemd = user_data;
        GVariant *result;
        GError *error = NULL;

        result = g_dbus_proxy_call_finish (G_DBUS_PROXY (source),
                                           res,
                                           &error);

        if (result == NULL) {
                if (!g_error_matches (error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED)) {
                        g_warning ("Shutdown failed: %s", error->message);
                }
                g_error_free (error);
                drop_delay_inhibitor (systemd);
                g_debug ("GsmSystemd: shutdown preparation failed");
                systemd->priv->prepare_for_shutdown_expected = FALSE;
                g_signal_emit_by_name (systemd, "shutdown-prepared", FALSE);
        } else {
                g_variant_unref (result);
        }
}

static void
gsm_systemd_prepare_shutdown (GsmSystem *system,
                              gboolean   restart)
{
        GsmSystemd *systemd = GSM_SYSTEMD (system);
        GUnixFDList *fd_list;
        GVariant *res;
        GError *error = NULL;
        gint idx;

        g_debug ("GsmSystemd: prepare shutdown");

        res = g_dbus_proxy_call_with_unix_fd_list_sync (systemd->priv->sd_proxy,
                                                        "Inhibit",
                                                        g_variant_new ("(ssss)",
                                                                       "shutdown",
                                                                       g_get_user_name (),
                                                                       "Preparing to end the session",
                                                                       "delay"),
                                                        0,
                                                        G_MAXINT,
                                                        NULL,
                                                        &fd_list,
                                                        NULL,
                                                        &error);
        if (res == NULL) {
                g_warning ("Failed to get delay inhibitor: %s", error->message);
                g_error_free (error);
                g_signal_emit_by_name (systemd, "shutdown-prepared", FALSE);
                return;
        }

        g_variant_get (res, "(h)", &idx);

        systemd->priv->delay_inhibit_fd = g_unix_fd_list_get (fd_list, idx, NULL);

        g_debug ("GsmSystemd: got delay inhibitor, fd = %d", systemd->priv->delay_inhibit_fd);

        g_variant_unref (res);
        g_object_unref (fd_list);

        systemd->priv->prepare_for_shutdown_expected = TRUE;

        g_dbus_proxy_call (systemd->priv->sd_proxy,
                           restart ? "Reboot" : "PowerOff",
                           g_variant_new ("(b)", TRUE),
                           0,
                           G_MAXINT,
                           NULL,
                           reboot_or_poweroff_done,
                           systemd);
}

static void
gsm_systemd_complete_shutdown (GsmSystem *system)
{
        GsmSystemd *systemd = GSM_SYSTEMD (system);

        /* remove delay inhibitor, if any */
        drop_delay_inhibitor (systemd);
}

static void
gsm_systemd_system_init (GsmSystemInterface *iface)
{
        iface->can_switch_user = gsm_systemd_can_switch_user;
        iface->can_stop = gsm_systemd_can_stop;
        iface->can_restart = gsm_systemd_can_restart;
        iface->can_suspend = gsm_systemd_can_suspend;
        iface->can_hibernate = gsm_systemd_can_hibernate;
        iface->attempt_stop = gsm_systemd_attempt_stop;
        iface->attempt_restart = gsm_systemd_attempt_restart;
        iface->suspend = gsm_systemd_suspend;
        iface->hibernate = gsm_systemd_hibernate;
        iface->set_session_idle = gsm_systemd_set_session_idle;
        iface->is_login_session = gsm_systemd_is_login_session;
        iface->add_inhibitor = gsm_systemd_add_inhibitor;
        iface->remove_inhibitor = gsm_systemd_remove_inhibitor;
        iface->prepare_shutdown = gsm_systemd_prepare_shutdown;
        iface->complete_shutdown = gsm_systemd_complete_shutdown;
}

GsmSystemd *
gsm_systemd_new (void)
{
        GsmSystemd *manager;

        /* logind is not running ? */
        if (access("/run/systemd/seats/", F_OK) < 0)
                return NULL;

        manager = g_object_new (GSM_TYPE_SYSTEMD, NULL);

        return manager;
}

static void
sd_proxy_signal_cb (GDBusProxy  *proxy,
                    const gchar *sender_name,
                    const gchar *signal_name,
                    GVariant    *parameters,
                    gpointer     user_data)
{
        GsmSystemd *systemd = user_data;
        gboolean is_about_to_shutdown;

        g_debug ("GsmSystemd: received logind signal: %s", signal_name);

        if (g_strcmp0 (signal_name, "PrepareForShutdown") != 0) {
                g_debug ("GsmSystemd: ignoring %s signal", signal_name);
                return;
        }

        g_variant_get (parameters, "(b)", &is_about_to_shutdown);
        if (!is_about_to_shutdown) {
                g_debug ("GsmSystemd: ignoring %s signal since about-to-shutdown is FALSE", signal_name);
                return;
        }

        if (systemd->priv->prepare_for_shutdown_expected) {
                g_debug ("GsmSystemd: shutdown successfully prepared");
                g_signal_emit_by_name (systemd, "shutdown-prepared", TRUE);
                systemd->priv->prepare_for_shutdown_expected = FALSE;
        }
}

#else

GsmSystemd *
gsm_systemd_new (void)
{
        return NULL;
}

#endif

