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

#define SYSTEMD_SESSION_REQUIRE_ONLINE 0 /* active or online sessions only */

#define SD_LOGIND_SKIP_INHIBITORS (UINT64_C(1) << 4)

struct _GsmSystemd
{
        GsmSystem        parent_instance;

        GSource         *sd_source;
        GDBusProxy      *sd_proxy;
        char            *session_id;
        gchar           *session_path;

        GsmInhibitorFlag inhibited;
        gint             strong_inhibit_fd;
        gint             weak_inhibit_fd;

        gint             delay_inhibit_fd;
        gboolean         prepare_for_shutdown_expected;
};

G_DEFINE_FINAL_TYPE (GsmSystemd, gsm_systemd, GSM_TYPE_SYSTEM)

static void
drop_strong_system_inhibitor (GsmSystemd *manager)
{

        if (manager->strong_inhibit_fd != -1) {
                g_debug ("GsmSystemd: Dropping strong system inhibitor fd %d", manager->strong_inhibit_fd);
                close (manager->strong_inhibit_fd);
                manager->strong_inhibit_fd = -1;
        }
}

static void
drop_weak_system_inhibitor (GsmSystemd *manager)
{
        if (manager->weak_inhibit_fd != -1) {
                g_debug ("GsmSystemd: Dropping weak system inhibitor fd %d", manager->weak_inhibit_fd);
                close (manager->weak_inhibit_fd);
                manager->weak_inhibit_fd = -1;
        }
}

static void
drop_delay_inhibitor (GsmSystemd *manager)
{
        if (manager->delay_inhibit_fd != -1) {
                g_debug ("GsmSystemd: Dropping delay inhibitor");
                close (manager->delay_inhibit_fd);
                manager->delay_inhibit_fd = -1;
        }
}

static void
gsm_systemd_finalize (GObject *object)
{
        GsmSystemd *systemd = GSM_SYSTEMD (object);

        g_clear_object (&systemd->sd_proxy);
        free (systemd->session_id);
        g_free (systemd->session_path);

        if (systemd->sd_source) {
                g_source_destroy (systemd->sd_source);
        }

        drop_strong_system_inhibitor (systemd);
        drop_weak_system_inhibitor (systemd);
        drop_delay_inhibitor (systemd);

        G_OBJECT_CLASS (gsm_systemd_parent_class)->finalize (object);
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
        gboolean old_active;
        gboolean active;

        old_active = gsm_system_is_active (GSM_SYSTEM (self));
        active = sd_session_is_active (self->session_id) == 1;

        if (old_active != active)
                g_object_set (self, "active", active, NULL);

        return TRUE;
}

static void sd_proxy_signal_cb (GDBusProxy  *proxy,
                                const gchar *sender_name,
                                const gchar *signal_name,
                                GVariant    *parameters,
                                gpointer     user_data);

static gboolean
_systemd_session_is_graphical (const char *session_id)
{
        const gchar * const graphical_session_types[] = { "wayland", "x11", "mir", NULL };
        int saved_errno;
        g_autofree gchar *type = NULL;

        saved_errno = sd_session_get_type (session_id, &type);
        if (saved_errno < 0) {
                g_warning ("Couldn't get type for session '%s': %s",
                           session_id,
                           g_strerror (-saved_errno));
                return FALSE;
        }

        if (!g_strv_contains (graphical_session_types, type)) {
                g_debug ("Session '%s' is not a graphical session (type: '%s')",
                         session_id,
                         type);
                return FALSE;
        }

        return TRUE;
}

static gboolean
_systemd_session_is_active (const char *session_id)
{
        const gchar * const active_states[] = { "active", "online", NULL };
        int saved_errno;
        g_autofree gchar *state = NULL;

        /*
         * display sessions can be 'closing' if they are logged out but some
         * processes are lingering; we shouldn't consider these (this is
         * checking for a race condition since we specified
         * SYSTEMD_SESSION_REQUIRE_ONLINE)
         */
        saved_errno = sd_session_get_state (session_id, &state);
        if (saved_errno < 0) {
                g_warning ("Couldn't get state for session '%s': %s",
                           session_id,
                           g_strerror (-saved_errno));
                return FALSE;
        }

        if (!g_strv_contains (active_states, state)) {
                g_debug ("Session '%s' is not active or online", session_id);
                return FALSE;
        }

        return TRUE;
}

static gboolean
gsm_systemd_find_session (char **session_id)
{
        char *local_session_id = NULL;
        g_auto(GStrv) sessions = NULL;
        int n_sessions;

        g_return_val_if_fail (session_id != NULL, FALSE);

        g_debug ("Finding a graphical session for user %d", getuid ());

        n_sessions = sd_uid_get_sessions (getuid (),
                                          SYSTEMD_SESSION_REQUIRE_ONLINE,
                                          &sessions);

        if (n_sessions < 0) {
                g_critical ("Failed to get sessions for user %d", getuid ());
                return FALSE;
        }

        for (int i = 0; i < n_sessions; ++i) {
                g_debug ("Considering session '%s'", sessions[i]);

                if (!_systemd_session_is_graphical (sessions[i]))
                        continue;

                if (!_systemd_session_is_active (sessions[i]))
                        continue;

                /*
                 * We get the sessions from newest to oldest, so take the last
                 * one we find that's good
                 */
                local_session_id = sessions[i];
        }

        if (local_session_id == NULL)
                return FALSE;

        *session_id = g_strdup (local_session_id);

        return TRUE;
}

static void
gsm_systemd_init (GsmSystemd *manager)
{
        GError *error = NULL;
        GDBusConnection *bus;
        GVariant *res;

        manager->strong_inhibit_fd = -1;
        manager->weak_inhibit_fd = -1;
        manager->delay_inhibit_fd = -1;

        bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
        if (bus == NULL)
                g_error ("Failed to connect to system bus: %s",
                         error->message);
        manager->sd_proxy =
                g_dbus_proxy_new_sync (bus,
                                       0,
                                       NULL,
                                       SD_NAME,
                                       SD_PATH,
                                       SD_INTERFACE,
                                       NULL,
                                       &error);
        if (manager->sd_proxy == NULL) {
                g_warning ("Failed to connect to systemd: %s",
                           error->message);
                g_clear_error (&error);
        }

        g_signal_connect (manager->sd_proxy, "g-signal",
                          G_CALLBACK (sd_proxy_signal_cb), manager);

        gsm_systemd_find_session (&manager->session_id);

        if (manager->session_id == NULL) {
                g_warning ("Could not get session id for session. Check that logind is "
                           "properly installed and pam_systemd is getting used at login.");
                return;
        }

        g_debug ("Found session ID: %s", manager->session_id);

        res = g_dbus_proxy_call_sync (manager->sd_proxy,
                                      "GetSession",
                                      g_variant_new ("(s)", manager->session_id),
                                      0,
                                      G_MAXINT,
                                      NULL,
                                      &error);
        if (res == NULL) {
                g_warning ("Could not get session id for session. Check that logind is "
                           "properly installed and pam_systemd is getting used at login: %s",
                           error->message);
                g_error_free (error);
                return;
        }

        g_variant_get (res, "(o)", &manager->session_path);
        g_variant_unref (res);

        manager->sd_source = sd_source_new ();
        g_source_set_callback (manager->sd_source, on_sd_source_changed, manager, NULL);
        g_source_attach (manager->sd_source, NULL);

        on_sd_source_changed (manager);

        g_object_unref (bus);
}

static void
gsm_systemd_set_session_idle (GsmSystem *system,
                              gboolean   is_idle)
{
        GsmSystemd *manager = GSM_SYSTEMD (system);
        GDBusConnection *bus;

        if (manager->session_path == NULL) {
                g_warning ("Could not get session path for session. Check that logind is "
                           "properly installed and pam_systemd is getting used at login.");
                return;
        }

        g_debug ("Updating systemd idle status: %d", is_idle);
        bus = g_dbus_proxy_get_connection (manager->sd_proxy);
        g_dbus_connection_call (bus,
                                SD_NAME,
                                manager->session_path,
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
        return TRUE;
}

static GsmActionAvailability
can_take_action (GsmSystem  *system,
                 const char *method)
{
#define entry(a, ...) { a, (const char *[]) { __VA_ARGS__, NULL } }
        const struct {
                GsmActionAvailability availability;
                const char **values;
        } known_actions[] = {
                entry (GSM_ACTION_AVAILABLE,
                       "yes"),
                entry (GSM_ACTION_CHALLANGE,
                       "challenge",
                       "inhibited"),
                entry (GSM_ACTION_BLOCKED,
                       "inhibitor-blocked",
                       "challenge-inhibitor-blocked"),
                entry (GSM_ACTION_UNAVAILABLE,
                       "no",
                       "na"),
        };
#undef entry

        GsmSystemd *manager = GSM_SYSTEMD (system);
        g_autoptr (GVariant) res = NULL;
        gchar *availability;

        res = g_dbus_proxy_call_sync (manager->sd_proxy, method, NULL,
                                      0, G_MAXINT, NULL, NULL);
        if (!res) {
                g_warning ("Failed to call %s. Something is very wrong with logind!",
                           method);
                return GSM_ACTION_UNAVAILABLE;
        }

        g_variant_get (res, "(&s)", &availability);

        for (size_t i = 0; i < G_N_ELEMENTS (known_actions); i++) {
                if (g_strv_contains (known_actions[i].values, availability))
                        return known_actions[i].availability;
        }

        g_warning ("GsmSystemd: Unknown action availability: %s",
                   availability);
        return GSM_ACTION_UNAVAILABLE;
}

static GsmActionAvailability
gsm_systemd_can_restart (GsmSystem *system)
{
        return can_take_action (system, "CanReboot");
}

static GsmActionAvailability
gsm_systemd_can_suspend (GsmSystem *system)
{
        return can_take_action (system, "CanSuspend");
}

static void
gsm_systemd_suspend (GsmSystem *system)
{
        GsmSystemd *manager = GSM_SYSTEMD (system);
        g_autoptr (GVariant) result = NULL;
        g_autoptr (GError) error = NULL;

        result = g_dbus_proxy_call_sync (manager->sd_proxy,
                                         "SuspendWithFlags",
                                         g_variant_new ("(t)", SD_LOGIND_SKIP_INHIBITORS),
                                         G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION,
                                         G_MAXINT,
                                         NULL,
                                         &error);
        if (!result)
                g_warning ("Failed to call Suspend(): %s", error->message);
}

static gboolean
gsm_systemd_can_restart_to_firmware_setup (GsmSystem *system)
{
        GsmActionAvailability ret;
        ret = can_take_action (system, "CanRebootToFirmwareSetup");
        return ret == GSM_ACTION_AVAILABLE || ret == GSM_ACTION_CHALLANGE;
}

static void
gsm_systemd_set_restart_to_firmware_setup (GsmSystem *system,
                                           gboolean   enable)
{
        GsmSystemd *manager = GSM_SYSTEMD (system);
        GVariant *res;
        GError *error = NULL;

        res = g_dbus_proxy_call_sync (manager->sd_proxy,
                                      "SetRebootToFirmwareSetup",
                                      g_variant_new ("(b)", enable),
                                      G_DBUS_CALL_FLAGS_NONE,
                                      G_MAXINT,
                                      NULL,
                                      &error);
        if (!res) {
                g_warning ("Calling SetRebootToFirmwareSetup failed. Check that logind is "
                           "properly installed and pam_systemd is getting used at login: %s",
                           error->message);
                g_error_free (error);
        }

        g_variant_unref (res);
}

static GsmActionAvailability
gsm_systemd_can_shutdown (GsmSystem *system)
{
        return can_take_action (system, "CanPowerOff");
}

static int
get_system_inhibitor_finish (GObject      *source,
                             GAsyncResult *result)
{
        GDBusProxy *proxy = G_DBUS_PROXY (source);
        g_autoptr (GError) error = NULL;
        g_autoptr (GVariant) ret = NULL;
        g_autoptr (GUnixFDList) fd_list = NULL;
        gint idx;
        int fd;

        ret = g_dbus_proxy_call_with_unix_fd_list_finish (proxy, &fd_list, result, &error);

        if (!ret) {
                g_warning ("Unable to inhibit system: %s", error->message);
                return -EBADF;
        }

        g_variant_get (ret, "(h)", &idx);

        fd = g_unix_fd_list_get (fd_list, idx, &error);
        if (fd < 0) {
                g_warning ("Failed to receive system inhibitor fd: %s", error->message);
                return -EBADF;
        }

        return fd;
}

static void
inhibit_strong_done (GObject      *source,
                     GAsyncResult *result,
                     gpointer      user_data)
{
        GsmSystemd *manager = GSM_SYSTEMD (user_data);

        /* Drop any previous inhibit before recording the new one */
        drop_strong_system_inhibitor (manager);

        manager->strong_inhibit_fd = get_system_inhibitor_finish (source, result);
        g_debug ("System strong inhibitor fd is %d", manager->strong_inhibit_fd);

        /* Handle a race condition, where inhibitors got unset during dbus call */
        if ((manager->inhibited & GSM_INHIBITOR_FLAG_LOGOUT) == 0)
                drop_strong_system_inhibitor (manager);
}

static void
inhibit_weak_done (GObject      *source,
                   GAsyncResult *result,
                   gpointer      user_data)
{
        GsmSystemd *manager = GSM_SYSTEMD (user_data);

        /* Drop any previous inhibit before recording the new one */
        drop_weak_system_inhibitor (manager);

        manager->weak_inhibit_fd = get_system_inhibitor_finish (source, result);
        g_debug ("System weak inhibitor fd is %d", manager->weak_inhibit_fd);

        /* Handle a race condition, where inhibitors got unset during dbus call */
        if ((manager->inhibited & GSM_INHIBITOR_FLAG_SUSPEND) == 0)
                drop_weak_system_inhibitor (manager);
}

static void
gsm_systemd_call_inhibit (GsmSystemd          *manager,
                          const char          *what,
                          const char          *mode)
{
        GAsyncReadyCallback callback;

        if (strcmp (mode, "block") == 0)
                callback = inhibit_strong_done;
        else
                callback = inhibit_weak_done;

        g_dbus_proxy_call_with_unix_fd_list (manager->sd_proxy,
                                             "Inhibit",
                                             g_variant_new ("(ssss)",
                                                            what,
                                                            g_get_user_name (),
                                                            "user session inhibited",
                                                            mode),
                                             0,
                                             G_MAXINT,
                                             NULL,
                                             NULL,
                                             callback,
                                             manager);
}

static void
gsm_systemd_set_inhibitors (GsmSystem        *system,
                            GsmInhibitorFlag  flags)
{
        GsmSystemd *manager = GSM_SYSTEMD (system);
        gboolean inhibit_logout;
        gboolean inhibit_suspend;

        inhibit_logout = (flags & GSM_INHIBITOR_FLAG_LOGOUT) != 0;
        inhibit_suspend = (flags & GSM_INHIBITOR_FLAG_SUSPEND) != 0;

        if (inhibit_logout) {
                g_debug ("Adding strong system inhibitor on shutdown");
                gsm_systemd_call_inhibit (manager, "shutdown", "block");
        } else
                drop_strong_system_inhibitor (manager);

        if (inhibit_suspend) {
                g_debug ("Adding weak system inhibitor on suspend");
                gsm_systemd_call_inhibit (manager, "sleep", "block-weak");
        } else
                drop_weak_system_inhibitor (manager);

        manager->inhibited = flags;
}

static void
gsm_systemd_reacquire_inhibitors (GsmSystemd *manager)
{
        g_debug ("Reacquiring system inhibitors");
        gsm_systemd_set_inhibitors (GSM_SYSTEM (manager),
                                    manager->inhibited);
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
                systemd->prepare_for_shutdown_expected = FALSE;
                g_signal_emit_by_name (systemd, "shutdown-prepared", FALSE);
                gsm_systemd_reacquire_inhibitors (systemd);
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

        res = g_dbus_proxy_call_with_unix_fd_list_sync (systemd->sd_proxy,
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
                gsm_systemd_reacquire_inhibitors (systemd);
                return;
        }

        g_variant_get (res, "(h)", &idx);

        systemd->delay_inhibit_fd = g_unix_fd_list_get (fd_list, idx, NULL);

        g_debug ("GsmSystemd: got delay inhibitor, fd = %d", systemd->delay_inhibit_fd);

        g_variant_unref (res);
        g_object_unref (fd_list);

        systemd->prepare_for_shutdown_expected = TRUE;

        /* if we're holding a blocking inhibitor to inhibit shutdown, systemd
         * will prevent us from shutting down */
        drop_strong_system_inhibitor (systemd);

        g_dbus_proxy_call (systemd->sd_proxy,
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
gsm_systemd_class_init (GsmSystemdClass *class)
{
        GObjectClass *object_class = G_OBJECT_CLASS (class);
        GsmSystemClass *system_class = GSM_SYSTEM_CLASS (class);

        object_class->finalize = gsm_systemd_finalize;

        system_class->can_switch_user = gsm_systemd_can_switch_user;
        system_class->can_shutdown = gsm_systemd_can_shutdown;
        system_class->can_restart = gsm_systemd_can_restart;
        system_class->can_suspend = gsm_systemd_can_suspend;
        system_class->suspend = gsm_systemd_suspend;
        system_class->can_restart_to_firmware_setup = gsm_systemd_can_restart_to_firmware_setup;
        system_class->set_restart_to_firmware_setup = gsm_systemd_set_restart_to_firmware_setup;
        system_class->set_session_idle = gsm_systemd_set_session_idle;
        system_class->set_inhibitors = gsm_systemd_set_inhibitors;
        system_class->prepare_shutdown = gsm_systemd_prepare_shutdown;
        system_class->complete_shutdown = gsm_systemd_complete_shutdown;
}

GsmSystem *
gsm_systemd_new (void)
{
        /* logind is not running ? */
        if (access("/run/systemd/seats/", F_OK) < 0)
                return NULL;

        return g_object_new (GSM_TYPE_SYSTEMD, NULL);
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

        if (systemd->prepare_for_shutdown_expected) {
                g_debug ("GsmSystemd: shutdown successfully prepared");
                g_signal_emit_by_name (systemd, "shutdown-prepared", TRUE);
                systemd->prepare_for_shutdown_expected = FALSE;
        }
}
