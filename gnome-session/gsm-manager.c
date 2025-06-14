/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Novell, Inc.
 * Copyright (C) 2008 Red Hat, Inc.
 * Copyright (C) 2008 William Jon McCann <jmccann@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <locale.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "gsm-manager.h"
#include "org.gnome.SessionManager.h"

#include <systemd/sd-journal.h>

#include <systemd/sd-daemon.h>

#include "gsm-store.h"
#include "gsm-inhibitor.h"
#include "gsm-presence.h"
#include "gsm-shell.h"

#include "gsm-dbus-client.h"

#include "gsm-autostart-app.h"

#include "gsm-util.h"
#include "gsm-icon-names.h"
#include "gsm-system.h"
#include "gsm-shell-extensions.h"
#include "gsm-fail-whale.h"

/* UUIDs for log messages */
#define GSM_MANAGER_STARTUP_SUCCEEDED_MSGID     "0ce153587afa4095832d233c17a88001"
#define GSM_MANAGER_UNRECOVERABLE_FAILURE_MSGID "10dd2dc188b54a5e98970f56499d1f73"

#define GSM_MANAGER_DBUS_PATH "/org/gnome/SessionManager"
#define GSM_MANAGER_DBUS_NAME "org.gnome.SessionManager"
#define GSM_MANAGER_DBUS_IFACE "org.gnome.SessionManager"

/* Probably about the longest amount of time someone could reasonably
 * want to wait, at least for something happening more than once.
 * We can get deployed on very slow media though like CDROM devices,
 * often with complex stacking/compressing filesystems on top, which
 * is not a recipie for speed.   Particularly now that we throw up
 * a fail whale if required components don't show up quickly enough,
 * let's make this fairly long.
 */
#define GSM_MANAGER_PHASE_TIMEOUT 90 /* seconds */

#define GDM_FLEXISERVER_COMMAND "gdmflexiserver"
#define GDM_FLEXISERVER_ARGS    "--startnew Standard"

#define SESSION_SCHEMA            "org.gnome.desktop.session"
#define KEY_IDLE_DELAY            "idle-delay"
#define KEY_SESSION_NAME          "session-name"

#define GSM_MANAGER_SCHEMA        "org.gnome.SessionManager"
#define KEY_LOGOUT_PROMPT         "logout-prompt"
#define KEY_SHOW_FALLBACK_WARNING "show-fallback-warning"

#define SCREENSAVER_SCHEMA        "org.gnome.desktop.screensaver"
#define KEY_SLEEP_LOCK            "lock-enabled"

#define LOCKDOWN_SCHEMA           "org.gnome.desktop.lockdown"
#define KEY_DISABLE_LOG_OUT       "disable-log-out"
#define KEY_DISABLE_USER_SWITCHING "disable-user-switching"

static void app_registered (GsmApp     *app, GParamSpec *spec, GsmManager *manager);

typedef enum
{
        GSM_MANAGER_LOGOUT_NONE,
        GSM_MANAGER_LOGOUT_LOGOUT,
        GSM_MANAGER_LOGOUT_REBOOT,
        GSM_MANAGER_LOGOUT_REBOOT_INTERACT,
        GSM_MANAGER_LOGOUT_SHUTDOWN,
        GSM_MANAGER_LOGOUT_SHUTDOWN_INTERACT,
} GsmManagerLogoutType;

typedef struct
{
        gboolean                failsafe;
        gboolean                systemd_managed;
        gboolean                systemd_initialized;
        gboolean                manager_initialized;
        GsmStore               *clients;
        GsmStore               *inhibitors;
        GsmInhibitorFlag        inhibited_actions;
        GsmStore               *apps;
        GsmPresence            *presence;
        char                   *session_name;
        gboolean                is_fallback_session : 1;

        /* Current status */
        GsmManagerPhase         phase;
        guint                   phase_timeout_id;
        GSList                 *required_apps;
        GSList                 *pending_apps;
        GsmManagerLogoutMode    logout_mode;
        GSList                 *query_clients;
        guint                   query_timeout_id;
        /* This is used for GSM_MANAGER_PHASE_END_SESSION only at the moment,
         * since it uses a sublist of all running client that replied in a
         * specific way */
        GSList                 *next_query_clients;
        /* This is the action that will be done just before we exit */
        GsmManagerLogoutType    logout_type;

        /* List of clients which were disconnected due to disabled condition
         * and shouldn't be automatically restarted */
        GSList                 *condition_clients;

        GSList                 *pending_end_session_tasks;
        GCancellable           *end_session_cancellable;

        GSettings              *settings;
        GSettings              *session_settings;
        GSettings              *screensaver_settings;
        GSettings              *lockdown_settings;

        GsmSystem              *system;
        GDBusConnection        *connection;
        GsmExportedManager     *skeleton;
        gboolean                dbus_disconnected : 1;

        GsmShell               *shell;
        guint                   shell_end_session_dialog_canceled_id;
        guint                   shell_end_session_dialog_open_failed_id;
        guint                   shell_end_session_dialog_confirmed_logout_id;
        guint                   shell_end_session_dialog_confirmed_shutdown_id;
        guint                   shell_end_session_dialog_confirmed_reboot_id;
} GsmManagerPrivate;

enum {
        PROP_0,
        PROP_CLIENT_STORE,
        PROP_SESSION_NAME,
        PROP_FALLBACK,
        PROP_FAILSAFE,
        PROP_SYSTEMD_MANAGED
};

enum {
        PHASE_CHANGED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

static void     gsm_manager_class_init  (GsmManagerClass *klass);
static void     gsm_manager_init        (GsmManager      *manager);

static gboolean _log_out_is_locked_down     (GsmManager *manager);

static void     _handle_client_end_session_response (GsmManager *manager,
                                                     GsmClient  *client,
                                                     gboolean    is_ok,
                                                     gboolean    do_last,
                                                     gboolean    cancel,
                                                     const char *reason);
static void     show_shell_end_session_dialog (GsmManager                   *manager,
                                               GsmShellEndSessionDialogType  type);
static gpointer manager_object = NULL;

G_DEFINE_TYPE_WITH_PRIVATE (GsmManager, gsm_manager, G_TYPE_OBJECT)

static const GDBusErrorEntry gsm_manager_error_entries[] = {
        { GSM_MANAGER_ERROR_GENERAL, GSM_MANAGER_DBUS_IFACE ".GeneralError" },
        { GSM_MANAGER_ERROR_NOT_IN_INITIALIZATION, GSM_MANAGER_DBUS_IFACE ".NotInInitialization" },
        { GSM_MANAGER_ERROR_NOT_IN_RUNNING, GSM_MANAGER_DBUS_IFACE ".NotInRunning" },
        { GSM_MANAGER_ERROR_ALREADY_REGISTERED, GSM_MANAGER_DBUS_IFACE ".AlreadyRegistered" },
        { GSM_MANAGER_ERROR_NOT_REGISTERED, GSM_MANAGER_DBUS_IFACE ".NotRegistered" },
        { GSM_MANAGER_ERROR_INVALID_OPTION, GSM_MANAGER_DBUS_IFACE ".InvalidOption" },
        { GSM_MANAGER_ERROR_LOCKED_DOWN, GSM_MANAGER_DBUS_IFACE ".LockedDown" }
};

GQuark
gsm_manager_error_quark (void)
{
        static volatile gsize quark_volatile = 0;

        g_dbus_error_register_error_domain ("gsm_manager_error",
                                            &quark_volatile,
                                            gsm_manager_error_entries,
                                            G_N_ELEMENTS (gsm_manager_error_entries));
        return quark_volatile;
}

static gboolean
start_app_or_warn (GsmManager *manager,
                   GsmApp     *app)
{
        gboolean res;
        GError *error = NULL;

        g_debug ("GsmManager: starting app '%s'", gsm_app_peek_id (app));

        res = gsm_app_start (app, &error);
        if (error != NULL) {
                g_warning ("Failed to start app: %s", error->message);
                g_clear_error (&error);
        }
        return res;
}

static gboolean
is_app_required (GsmManager *manager,
                 GsmApp     *app)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        return g_slist_find (priv->required_apps, app) != NULL;
}

static void
on_required_app_failure (GsmManager  *manager,
                         GsmApp      *app)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        const gchar *app_id;
        gboolean allow_logout;
        GsmShellExtensions *extensions;

        app_id = gsm_app_peek_app_id (app);

        if (g_str_equal (app_id, "org.gnome.Shell.desktop")) {
                extensions = g_object_new (GSM_TYPE_SHELL_EXTENSIONS, NULL);
                gsm_shell_extensions_disable_all (extensions);
        } else {
                extensions = NULL;
        }

        if (gsm_system_is_login_session (priv->system)) {
                allow_logout = FALSE;
        } else {
                allow_logout = !_log_out_is_locked_down (manager);
        }

        sd_journal_send ("MESSAGE_ID=%s", GSM_MANAGER_UNRECOVERABLE_FAILURE_MSGID,
                         "PRIORITY=%d", 3,
                         "MESSAGE=Unrecoverable failure in required component %s", app_id,
                         NULL);

        gsm_fail_whale_dialog_we_failed (FALSE,
                                         allow_logout,
                                         extensions);
}

static void
on_display_server_failure (GsmManager *manager,
                           GsmApp     *app)
{
        const gchar *app_id;
        GsmShellExtensions *extensions;

        app_id = gsm_app_peek_app_id (app);

        if (g_str_equal (app_id, "org.gnome.Shell.desktop")) {
                extensions = g_object_new (GSM_TYPE_SHELL_EXTENSIONS, NULL);
                gsm_shell_extensions_disable_all (extensions);

                g_object_unref (extensions);
        } else {
                extensions = NULL;
        }

        sd_journal_send ("MESSAGE_ID=%s", GSM_MANAGER_UNRECOVERABLE_FAILURE_MSGID,
                         "PRIORITY=%d", 3,
                         "MESSAGE=Unrecoverable failure in required component %s", app_id,
                         NULL);

        gsm_quit ();
}

static gboolean
_debug_client (const char *id,
               GsmClient  *client,
               GsmManager *manager)
{
        g_debug ("GsmManager: Client %s", gsm_client_peek_id (client));
        return FALSE;
}

static void
debug_clients (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        gsm_store_foreach (priv->clients,
                           (GsmStoreFunc)_debug_client,
                           manager);
}

static gboolean
_find_by_cookie (const char   *id,
                 GsmInhibitor *inhibitor,
                 guint        *cookie_ap)
{
        guint cookie_b;

        cookie_b = gsm_inhibitor_peek_cookie (inhibitor);

        return (*cookie_ap == cookie_b);
}

static gboolean
_client_has_startup_id (const char *id,
                        GsmClient  *client,
                        const char *startup_id_a)
{
        const char *startup_id_b;

        startup_id_b = gsm_client_peek_startup_id (client);
        if (IS_STRING_EMPTY (startup_id_b)) {
                return FALSE;
        }

        return (strcmp (startup_id_a, startup_id_b) == 0);
}

static void
app_condition_changed (GsmApp     *app,
                       gboolean    condition,
                       GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        GsmClient *client;

        g_debug ("GsmManager: app:%s condition changed condition:%d",
                 gsm_app_peek_id (app),
                 condition);

        client = (GsmClient *)gsm_store_find (priv->clients,
                                              (GsmStoreFunc)_client_has_startup_id,
                                              (char *)gsm_app_peek_startup_id (app));

        if (condition) {
                if (!gsm_app_is_running (app) && client == NULL) {
                        start_app_or_warn (manager, app);
                } else {
                        g_debug ("GsmManager: not starting - app still running '%s'", gsm_app_peek_id (app));
                }
        } else {
                GError  *error;
                gboolean res;

                if (client != NULL) {
                        /* Kill client in case condition if false and make sure it won't
                         * be automatically restarted by adding the client to
                         * condition_clients */
                        priv->condition_clients =
                                g_slist_prepend (priv->condition_clients, client);

                        g_debug ("GsmManager: stopping client %s for app", gsm_client_peek_id (client));

                        error = NULL;
                        res = gsm_client_stop (client, &error);
                        if (! res) {
                                g_warning ("Not able to stop app client from its condition: %s",
                                           error->message);
                                g_error_free (error);
                        }
                } else {
                        g_debug ("GsmManager: stopping app %s", gsm_app_peek_id (app));

                        /* If we don't have a client then we should try to kill the app */
                        error = NULL;
                        res = gsm_app_stop (app, &error);
                        if (! res) {
                                g_warning ("Not able to stop app from its condition: %s",
                                           error->message);
                                g_error_free (error);
                        }
                }
        }
}

static const char *
phase_num_to_name (guint phase)
{
        const char *name;

        switch (phase) {
        case GSM_MANAGER_PHASE_STARTUP:
                name = "STARTUP";
                break;
        case GSM_MANAGER_PHASE_EARLY_INITIALIZATION:
                name = "EARLY_INITIALIZATION";
                break;
        case GSM_MANAGER_PHASE_PRE_DISPLAY_SERVER:
                name = "PRE_DISPLAY_SERVER";
                break;
        case GSM_MANAGER_PHASE_DISPLAY_SERVER:
                name = "DISPLAY_SERVER";
                break;
        case GSM_MANAGER_PHASE_INITIALIZATION:
                name = "INITIALIZATION";
                break;
        case GSM_MANAGER_PHASE_WINDOW_MANAGER:
                name = "WINDOW_MANAGER";
                break;
        case GSM_MANAGER_PHASE_PANEL:
                name = "PANEL";
                break;
        case GSM_MANAGER_PHASE_DESKTOP:
                name = "DESKTOP";
                break;
        case GSM_MANAGER_PHASE_APPLICATION:
                name = "APPLICATION";
                break;
        case GSM_MANAGER_PHASE_RUNNING:
                name = "RUNNING";
                break;
        case GSM_MANAGER_PHASE_QUERY_END_SESSION:
                name = "QUERY_END_SESSION";
                break;
        case GSM_MANAGER_PHASE_END_SESSION:
                name = "END_SESSION";
                break;
        case GSM_MANAGER_PHASE_EXIT:
                name = "EXIT";
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        return name;
}

static void start_phase (GsmManager *manager);

static void
gsm_manager_quit (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        /* See the comment in request_reboot() for some more details about how
         * this works. */

        switch (priv->logout_type) {
        case GSM_MANAGER_LOGOUT_LOGOUT:
        case GSM_MANAGER_LOGOUT_NONE:
                gsm_quit ();
                break;
        case GSM_MANAGER_LOGOUT_REBOOT:
        case GSM_MANAGER_LOGOUT_REBOOT_INTERACT:
                gsm_system_complete_shutdown (priv->system);
                gsm_quit ();
                break;
        case GSM_MANAGER_LOGOUT_SHUTDOWN:
        case GSM_MANAGER_LOGOUT_SHUTDOWN_INTERACT:
                gsm_system_complete_shutdown (priv->system);
                gsm_quit ();
                break;
        default:
                g_assert_not_reached ();
                break;
        }
}

static gboolean do_query_end_session_exit (GsmManager *manager);

static void
end_phase (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        gboolean start_next_phase = TRUE;

        g_debug ("GsmManager: ending phase %s",
                 phase_num_to_name (priv->phase));

        g_slist_free (priv->pending_apps);
        priv->pending_apps = NULL;

        g_slist_free (priv->query_clients);
        priv->query_clients = NULL;

        g_slist_free (priv->next_query_clients);
        priv->next_query_clients = NULL;

        if (priv->query_timeout_id > 0) {
                g_source_remove (priv->query_timeout_id);
                priv->query_timeout_id = 0;
        }
        if (priv->phase_timeout_id > 0) {
                g_source_remove (priv->phase_timeout_id);
                priv->phase_timeout_id = 0;
        }

        switch (priv->phase) {
        case GSM_MANAGER_PHASE_STARTUP:
        case GSM_MANAGER_PHASE_EARLY_INITIALIZATION:
        case GSM_MANAGER_PHASE_PRE_DISPLAY_SERVER:
        case GSM_MANAGER_PHASE_DISPLAY_SERVER:
                break;
        case GSM_MANAGER_PHASE_INITIALIZATION:
                priv->manager_initialized = TRUE;
                /* Wait for systemd if it isn't initialized yet*/
                if (priv->systemd_managed && !priv->systemd_initialized) {
                        sd_notify (0, "STATUS=GNOME Session Manager waiting for gnome-session-initialized.target (via signal)");
                        start_next_phase = FALSE;
                }
                break;
        case GSM_MANAGER_PHASE_WINDOW_MANAGER:
        case GSM_MANAGER_PHASE_PANEL:
        case GSM_MANAGER_PHASE_DESKTOP:
        case GSM_MANAGER_PHASE_APPLICATION:
                break;
        case GSM_MANAGER_PHASE_RUNNING:
                if (_log_out_is_locked_down (manager)) {
                        g_warning ("Unable to logout: Logout has been locked down");
                        start_next_phase = FALSE;
                }
                break;
        case GSM_MANAGER_PHASE_QUERY_END_SESSION:
                if (!do_query_end_session_exit (manager))
                        start_next_phase = FALSE;
                break;
        case GSM_MANAGER_PHASE_END_SESSION:
                break;
        case GSM_MANAGER_PHASE_EXIT:
                start_next_phase = FALSE;
                gsm_manager_quit (manager);
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        if (start_next_phase) {
                priv->phase++;
                start_phase (manager);
        }
}

static void
app_event_during_startup (GsmManager *manager,
                          GsmApp     *app)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        if (!(priv->phase < GSM_MANAGER_PHASE_APPLICATION))
                return;

        priv->pending_apps = g_slist_remove (priv->pending_apps, app);

        if (priv->pending_apps == NULL) {
                if (priv->phase_timeout_id > 0) {
                        g_source_remove (priv->phase_timeout_id);
                        priv->phase_timeout_id = 0;
                }

                end_phase (manager);
        }
}

static gboolean
is_app_display_server (GsmManager *manager,
                       GsmApp     *app)
{
        GsmManagerPhase phase;

        /* Apps can only really act as a display server if
         * we're a wayland session.
         */
        if (g_strcmp0 (g_getenv ("XDG_SESSION_TYPE"), "wayland") != 0)
                return FALSE;

        phase = gsm_app_peek_phase (app);

        return (phase == GSM_MANAGER_PHASE_DISPLAY_SERVER &&
                is_app_required (manager, app));
}

static void
_restart_app (GsmManager *manager,
              GsmApp     *app)
{
        GError *error = NULL;

        if (is_app_display_server (manager, app)) {
                on_display_server_failure (manager, app);
                return;
        }

        if (!gsm_app_restart (app, &error)) {
                if (is_app_required (manager, app)) {
                        on_required_app_failure (manager, app);
                } else {
                        g_warning ("Error on restarting session managed app: %s", error->message);
                }
                g_clear_error (&error);

                app_event_during_startup (manager, app);
        }
}

static void
app_died (GsmApp     *app,
          int         signal,
          GsmManager *manager)
{
        g_warning ("Application '%s' killed by signal %d", gsm_app_peek_app_id (app), signal);

        if (gsm_app_get_registered (app) && gsm_app_peek_autorestart (app)) {
                g_debug ("Component '%s' is autorestart, ignoring died signal",
                         gsm_app_peek_app_id (app));
                return;
        }

        _restart_app (manager, app);

        /* For now, we don't do anything with crashes from
         * non-required apps after they hit the restart limit.
         *
         * Note that both required and not-required apps will be
         * caught by ABRT/apport type infrastructure, and it'd be
         * better to pick up the crash from there and do something
         * un-intrusive about it generically.
         */
}

static void
app_exited (GsmApp     *app,
            guchar      exit_code,
            GsmManager *manager)
{
        if (exit_code != 0)
                g_warning ("App '%s' exited with code %d", gsm_app_peek_app_id (app), exit_code);
        else
                g_debug ("App %s exited successfully", gsm_app_peek_app_id (app));

        /* Consider that non-success exit status means "crash" for required components */
        if (exit_code != 0 && is_app_required (manager, app)) {
                if (gsm_app_get_registered (app) && gsm_app_peek_autorestart (app)) {
                        g_debug ("Component '%s' is autorestart, ignoring non-successful exit",
                                 gsm_app_peek_app_id (app));
                        return;
                }

                _restart_app (manager, app);
        } else {
                app_event_during_startup (manager, app);
        }
}

static void
app_registered (GsmApp     *app,
                GParamSpec *spec,
                GsmManager *manager)
{
        if (!gsm_app_get_registered (app)) {
                return;
        }

        g_debug ("App %s registered", gsm_app_peek_app_id (app));

        app_event_during_startup (manager, app);
}

static gboolean
on_phase_timeout (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        GSList *a;

        priv->phase_timeout_id = 0;

        switch (priv->phase) {
        case GSM_MANAGER_PHASE_STARTUP:
        case GSM_MANAGER_PHASE_EARLY_INITIALIZATION:
        case GSM_MANAGER_PHASE_PRE_DISPLAY_SERVER:
        case GSM_MANAGER_PHASE_DISPLAY_SERVER:
        case GSM_MANAGER_PHASE_INITIALIZATION:
        case GSM_MANAGER_PHASE_WINDOW_MANAGER:
        case GSM_MANAGER_PHASE_PANEL:
        case GSM_MANAGER_PHASE_DESKTOP:
        case GSM_MANAGER_PHASE_APPLICATION:
                for (a = priv->pending_apps; a; a = a->next) {
                        GsmApp *app = a->data;
                        g_warning ("Application '%s' failed to register before timeout",
                                   gsm_app_peek_app_id (app));
                        if (is_app_required (manager, app))
                                on_required_app_failure (manager, app);
                }
                break;
        case GSM_MANAGER_PHASE_RUNNING:
                break;
        case GSM_MANAGER_PHASE_QUERY_END_SESSION:
        case GSM_MANAGER_PHASE_END_SESSION:
                break;
        case GSM_MANAGER_PHASE_EXIT:
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        end_phase (manager);

        return FALSE;
}

static gboolean
_start_app (const char *id,
            GsmApp     *app,
            GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        if (gsm_app_peek_phase (app) != priv->phase) {
                goto out;
        }

        /* Keep track of app autostart condition in order to react
         * accordingly in the future. */
        g_signal_connect (app,
                          "condition-changed",
                          G_CALLBACK (app_condition_changed),
                          manager);

        if (gsm_app_peek_is_disabled (app)
            || gsm_app_peek_is_conditionally_disabled (app)) {
                g_debug ("GsmManager: Skipping disabled app: %s", id);
                goto out;
        }

        if (!start_app_or_warn (manager, app))
                goto out;

        if (priv->phase < GSM_MANAGER_PHASE_APPLICATION) {
                /* Historical note - apparently,
                 * e.g. gnome-settings-daemon used to "daemonize", and
                 * so gnome-session assumes process exit means "ok
                 * we're done".  Of course this is broken, we don't
                 * even distinguish between exit code 0 versus not-0,
                 * nor do we have any metadata which tells us a
                 * process is going to "daemonize" or not (and
                 * basically nothing should be anyways).
                 */
                g_signal_connect (app,
                                  "exited",
                                  G_CALLBACK (app_exited),
                                  manager);
                g_signal_connect (app,
                                  "notify::registered",
                                  G_CALLBACK (app_registered),
                                  manager);
                g_signal_connect (app,
                                  "died",
                                  G_CALLBACK (app_died),
                                  manager);
                priv->pending_apps = g_slist_prepend (priv->pending_apps, app);
        }
 out:
        return FALSE;
}

static void
do_phase_startup (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        gsm_store_foreach (priv->apps,
                           (GsmStoreFunc)_start_app,
                           manager);

        if (priv->pending_apps != NULL) {
                if (priv->phase < GSM_MANAGER_PHASE_APPLICATION) {
                        priv->phase_timeout_id = g_timeout_add_seconds (GSM_MANAGER_PHASE_TIMEOUT,
                                                                                 (GSourceFunc)on_phase_timeout,
                                                                                 manager);
                }
        } else {
                end_phase (manager);
        }
}

typedef struct {
        GsmManager *manager;
        guint       flags;
} ClientEndSessionData;


static gboolean
_client_end_session (GsmClient            *client,
                     ClientEndSessionData *data)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (data->manager);
        gboolean ret;
        GError  *error;

        // HACK, but it's temporary until we drop builtin session startup.
        if (g_strcmp0 ("org.gnome.Shell.desktop", gsm_client_peek_app_id (client)) == 0)
            return FALSE;

        error = NULL;
        ret = gsm_client_end_session (client, data->flags, &error);
        if (! ret) {
                g_warning ("Unable to query client: %s", error->message);
                g_error_free (error);
                /* FIXME: what should we do if we can't communicate with client? */
        } else {
                g_debug ("GsmManager: adding client to end-session clients: %s", gsm_client_peek_id (client));
                priv->query_clients = g_slist_prepend (priv->query_clients, client);
        }

        return FALSE;
}

static gboolean
_client_end_session_helper (const char           *id,
                            GsmClient            *client,
                            ClientEndSessionData *data)
{
        return _client_end_session (client, data);
}

static void
complete_end_session_tasks (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        GSList *l;

        for (l = priv->pending_end_session_tasks;
             l != NULL;
             l = l->next) {
                GTask *task = G_TASK (l->data);
                if (!g_task_return_error_if_cancelled (task))
                    g_task_return_boolean (task, TRUE);
        }

        g_slist_free_full (priv->pending_end_session_tasks,
                           (GDestroyNotify) g_object_unref);
        priv->pending_end_session_tasks = NULL;
}

static void
do_phase_end_session (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        ClientEndSessionData data;

        complete_end_session_tasks (manager);

        data.manager = manager;
        data.flags = 0;

        if (priv->logout_mode == GSM_MANAGER_LOGOUT_MODE_FORCE) {
                data.flags |= GSM_CLIENT_END_SESSION_FLAG_FORCEFUL;
        }

        if (priv->phase_timeout_id > 0) {
                g_source_remove (priv->phase_timeout_id);
                priv->phase_timeout_id = 0;
        }

        if (gsm_store_size (priv->clients) > 0) {
                priv->phase_timeout_id = g_timeout_add_seconds (GSM_MANAGER_PHASE_TIMEOUT,
                                                                         (GSourceFunc)on_phase_timeout,
                                                                         manager);

                gsm_store_foreach (priv->clients,
                                   (GsmStoreFunc)_client_end_session_helper,
                                   &data);
        } else {
                end_phase (manager);
        }
}

static void
do_phase_end_session_part_2 (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        ClientEndSessionData data;

        data.manager = manager;
        data.flags = 0;

        if (priv->logout_mode == GSM_MANAGER_LOGOUT_MODE_FORCE) {
                data.flags |= GSM_CLIENT_END_SESSION_FLAG_FORCEFUL;
        }
        data.flags |= GSM_CLIENT_END_SESSION_FLAG_LAST;

        /* keep the timeout that was started at the beginning of the
         * GSM_MANAGER_PHASE_END_SESSION phase */

        if (g_slist_length (priv->next_query_clients) > 0) {
                g_slist_foreach (priv->next_query_clients,
                                 (GFunc)_client_end_session,
                                 &data);

                g_slist_free (priv->next_query_clients);
                priv->next_query_clients = NULL;
        } else {
                end_phase (manager);
        }
}

static gboolean
_client_stop (const char *id,
              GsmClient  *client,
              gpointer    user_data)
{
        gboolean ret;
        GError  *error;

        error = NULL;
        ret = gsm_client_stop (client, &error);
        if (! ret) {
                g_warning ("Unable to stop client: %s", error->message);
                g_error_free (error);
                /* FIXME: what should we do if we can't communicate with client? */
        } else {
                g_debug ("GsmManager: stopped client: %s", gsm_client_peek_id (client));
        }

        return FALSE;
}

static void
maybe_restart_user_bus (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        GsmSystem *system;
        g_autoptr(GVariant) reply = NULL;
        g_autoptr(GError) error = NULL;

        if (priv->dbus_disconnected)
                return;

        system = gsm_get_system ();

        if (!gsm_system_is_last_session_for_user (system))
                return;

        reply = g_dbus_connection_call_sync (priv->connection,
                                             "org.freedesktop.systemd1",
                                             "/org/freedesktop/systemd1",
                                             "org.freedesktop.systemd1.Manager",
                                             "StopUnit",
                                             g_variant_new ("(ss)", "dbus.service", "fail"),
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL,
                                             &error);

        if (error != NULL) {
                g_debug ("GsmManager: reloading user bus failed: %s", error->message);
        }
}

static void
do_phase_exit (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        if (gsm_store_size (priv->clients) > 0) {
                gsm_store_foreach (priv->clients,
                                   (GsmStoreFunc)_client_stop,
                                   NULL);
        }

        if (!priv->systemd_managed)
                maybe_restart_user_bus (manager);

        end_phase (manager);
}

static gboolean
_client_query_end_session (const char           *id,
                           GsmClient            *client,
                           ClientEndSessionData *data)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (data->manager);
        gboolean ret;
        GError  *error;

        // HACK, but it's temporary until we drop builtin session startup.
        if (g_strcmp0 ("org.gnome.Shell.desktop", gsm_client_peek_app_id (client)) == 0)
            return FALSE;

        error = NULL;
        ret = gsm_client_query_end_session (client, data->flags, &error);
        if (! ret) {
                g_warning ("Unable to query client: %s", error->message);
                g_error_free (error);
                /* FIXME: what should we do if we can't communicate with client? */
        } else {
                g_debug ("GsmManager: adding client to query clients: %s", gsm_client_peek_id (client));
                priv->query_clients = g_slist_prepend (priv->query_clients, client);
        }

        return FALSE;
}

static gboolean
inhibitor_has_flag (gpointer      key,
                    GsmInhibitor *inhibitor,
                    gpointer      data)
{
        guint flag;
        guint flags;

        flag = GPOINTER_TO_UINT (data);

        flags = gsm_inhibitor_peek_flags (inhibitor);

        return (flags & flag);
}

static gboolean
gsm_manager_is_logout_inhibited (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        GsmInhibitor *inhibitor;

        if (priv->logout_mode == GSM_MANAGER_LOGOUT_MODE_FORCE) {
                return FALSE;
        }

        if (priv->inhibitors == NULL) {
                return FALSE;
        }

        inhibitor = (GsmInhibitor *)gsm_store_find (priv->inhibitors,
                                                    (GsmStoreFunc)inhibitor_has_flag,
                                                    GUINT_TO_POINTER (GSM_INHIBITOR_FLAG_LOGOUT));
        if (inhibitor == NULL) {
                return FALSE;
        }
        return TRUE;
}

static gboolean
gsm_manager_is_idle_inhibited (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        GsmInhibitor *inhibitor;

        if (priv->inhibitors == NULL) {
                return FALSE;
        }

        inhibitor = (GsmInhibitor *)gsm_store_find (priv->inhibitors,
                                                    (GsmStoreFunc)inhibitor_has_flag,
                                                    GUINT_TO_POINTER (GSM_INHIBITOR_FLAG_IDLE));
        if (inhibitor == NULL) {
                return FALSE;
        }
        return TRUE;
}

static gboolean
_client_cancel_end_session (const char *id,
                            GsmClient  *client,
                            GsmManager *manager)
{
        gboolean res;
        GError  *error;

        error = NULL;
        res = gsm_client_cancel_end_session (client, &error);
        if (! res) {
                g_warning ("Unable to cancel end session: %s", error->message);
                g_error_free (error);
        }

        return FALSE;
}

static gboolean
inhibitor_is_jit (gpointer      key,
                  GsmInhibitor *inhibitor,
                  GsmManager   *manager)
{
        gboolean    matches;
        const char *id;

        id = gsm_inhibitor_peek_client_id (inhibitor);

        matches = (id != NULL && id[0] != '\0');

        return matches;
}

static void
cancel_end_session (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        /* just ignore if received outside of shutdown */
        if (priv->phase < GSM_MANAGER_PHASE_QUERY_END_SESSION) {
                return;
        }

        /* switch back to running phase */
        g_debug ("GsmManager: Cancelling the end of session");

        g_cancellable_cancel (priv->end_session_cancellable);

        gsm_manager_set_phase (manager, GSM_MANAGER_PHASE_RUNNING);
        priv->logout_mode = GSM_MANAGER_LOGOUT_MODE_NORMAL;

        priv->logout_type = GSM_MANAGER_LOGOUT_NONE;

        /* clear all JIT inhibitors */
        gsm_store_foreach_remove (priv->inhibitors,
                                  (GsmStoreFunc)inhibitor_is_jit,
                                  (gpointer)manager);

        gsm_store_foreach (priv->clients,
                           (GsmStoreFunc)_client_cancel_end_session,
                           NULL);

        start_phase (manager);
}

static void
end_session_or_show_shell_dialog (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        gboolean logout_prompt;
        GsmShellEndSessionDialogType type;
        gboolean logout_inhibited;

        switch (priv->logout_type) {
        case GSM_MANAGER_LOGOUT_LOGOUT:
                type = GSM_SHELL_END_SESSION_DIALOG_TYPE_LOGOUT;
                break;
        case GSM_MANAGER_LOGOUT_REBOOT:
        case GSM_MANAGER_LOGOUT_REBOOT_INTERACT:
                type = GSM_SHELL_END_SESSION_DIALOG_TYPE_RESTART;
                break;
        case GSM_MANAGER_LOGOUT_SHUTDOWN:
        case GSM_MANAGER_LOGOUT_SHUTDOWN_INTERACT:
                type = GSM_SHELL_END_SESSION_DIALOG_TYPE_SHUTDOWN;
                break;
        default:
                g_warning ("Unexpected logout type %d when creating end session dialog",
                           priv->logout_type);
                type = GSM_SHELL_END_SESSION_DIALOG_TYPE_LOGOUT;
                break;
        }

        logout_inhibited = gsm_manager_is_logout_inhibited (manager);
        logout_prompt = g_settings_get_boolean (priv->settings,
                                                KEY_LOGOUT_PROMPT);

        switch (priv->logout_mode) {
        case GSM_MANAGER_LOGOUT_MODE_NORMAL:
                if (logout_inhibited || logout_prompt) {
                        show_shell_end_session_dialog (manager, type);
                } else {
                        end_phase (manager);
                }
                break;

        case GSM_MANAGER_LOGOUT_MODE_NO_CONFIRMATION:
                if (logout_inhibited) {
                        show_shell_end_session_dialog (manager, type);
                } else {
                        end_phase (manager);
                }
                break;

        case GSM_MANAGER_LOGOUT_MODE_FORCE:
                end_phase (manager);
                break;
        default:
                g_assert_not_reached ();
                break;
        }

}

static void
query_end_session_complete (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        g_debug ("GsmManager: query end session complete");

        /* Remove the timeout since this can be called from outside the timer
         * and we don't want to have it called twice */
        if (priv->query_timeout_id > 0) {
                g_source_remove (priv->query_timeout_id);
                priv->query_timeout_id = 0;
        }

        end_session_or_show_shell_dialog (manager);
}

static guint32
generate_cookie (void)
{
        guint32 cookie;

        cookie = (guint32)g_random_int_range (1, G_MAXINT32);

        return cookie;
}

static guint32
_generate_unique_cookie (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        guint32 cookie;

        do {
                cookie = generate_cookie ();
        } while (gsm_store_find (priv->inhibitors, (GsmStoreFunc)_find_by_cookie, &cookie) != NULL);

        return cookie;
}

static gboolean
_on_query_end_session_timeout (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        GSList *l;

        priv->query_timeout_id = 0;

        g_debug ("GsmManager: query end session timed out");

        for (l = priv->query_clients; l != NULL; l = l->next) {
                guint         cookie;
                GsmInhibitor *inhibitor;
                const char   *bus_name;
                char         *app_id;

                g_warning ("Client '%s' failed to reply before timeout",
                           gsm_client_peek_id (l->data));

                /* Don't add "not responding" inhibitors if logout is forced
                 */
                if (priv->logout_mode == GSM_MANAGER_LOGOUT_MODE_FORCE) {
                        continue;
                }

                /* Add JIT inhibit for unresponsive client */
                if (GSM_IS_DBUS_CLIENT (l->data)) {
                        bus_name = gsm_dbus_client_get_bus_name (l->data);
                } else {
                        bus_name = NULL;
                }

                app_id = g_strdup (gsm_client_peek_app_id (l->data));
                if (IS_STRING_EMPTY (app_id)) {
                        /* XSMP clients don't give us an app id unless we start them */
                        g_free (app_id);
                        app_id = gsm_client_get_app_name (l->data);
                }

                cookie = _generate_unique_cookie (manager);
                inhibitor = gsm_inhibitor_new_for_client (gsm_client_peek_id (l->data),
                                                          app_id,
                                                          GSM_INHIBITOR_FLAG_LOGOUT,
                                                          _("Not responding"),
                                                          bus_name,
                                                          cookie);
                g_free (app_id);
                gsm_store_add (priv->inhibitors, gsm_inhibitor_peek_id (inhibitor), G_OBJECT (inhibitor));
                g_object_unref (inhibitor);
        }

        g_slist_free (priv->query_clients);
        priv->query_clients = NULL;

        query_end_session_complete (manager);

        return FALSE;
}

static void
do_phase_query_end_session (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        ClientEndSessionData data;

        data.manager = manager;
        data.flags = 0;

        if (priv->logout_mode == GSM_MANAGER_LOGOUT_MODE_FORCE) {
                data.flags |= GSM_CLIENT_END_SESSION_FLAG_FORCEFUL;
        }
        /* We only query if an app is ready to log out, so we don't use
         * GSM_CLIENT_END_SESSION_FLAG_SAVE here.
         */

        debug_clients (manager);
        g_debug ("GsmManager: sending query-end-session to clients (logout mode: %s)",
                 priv->logout_mode == GSM_MANAGER_LOGOUT_MODE_NORMAL? "normal" :
                 priv->logout_mode == GSM_MANAGER_LOGOUT_MODE_FORCE? "forceful":
                 "no confirmation");
        gsm_store_foreach (priv->clients,
                           (GsmStoreFunc)_client_query_end_session,
                           &data);

        /* This phase doesn't time out unless logout is forced. Typically, this
         * separate timer is only used to show UI. */
        priv->query_timeout_id = g_timeout_add_seconds (1, (GSourceFunc)_on_query_end_session_timeout, manager);
}

static void
update_idle (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        if (gsm_manager_is_idle_inhibited (manager)) {
                gsm_presence_set_idle_enabled (priv->presence, FALSE);
        } else {
                gsm_presence_set_idle_enabled (priv->presence, TRUE);
        }
}

static void
start_phase (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        g_debug ("GsmManager: starting phase %s\n",
                 phase_num_to_name (priv->phase));

        /* reset state */
        g_slist_free (priv->pending_apps);
        priv->pending_apps = NULL;
        g_slist_free (priv->query_clients);
        priv->query_clients = NULL;
        g_slist_free (priv->next_query_clients);
        priv->next_query_clients = NULL;

        if (priv->query_timeout_id > 0) {
                g_source_remove (priv->query_timeout_id);
                priv->query_timeout_id = 0;
        }
        if (priv->phase_timeout_id > 0) {
                g_source_remove (priv->phase_timeout_id);
                priv->phase_timeout_id = 0;
        }

        sd_notifyf (0, "STATUS=GNOME Session Manager phase is %s", phase_num_to_name (priv->phase));

        switch (priv->phase) {
        case GSM_MANAGER_PHASE_STARTUP:
        case GSM_MANAGER_PHASE_EARLY_INITIALIZATION:
        case GSM_MANAGER_PHASE_PRE_DISPLAY_SERVER:
                do_phase_startup (manager);
                break;
        case GSM_MANAGER_PHASE_DISPLAY_SERVER:
                sd_notify (0, "READY=1");
                do_phase_startup (manager);
                break;
        case GSM_MANAGER_PHASE_INITIALIZATION:
        case GSM_MANAGER_PHASE_WINDOW_MANAGER:
        case GSM_MANAGER_PHASE_PANEL:
        case GSM_MANAGER_PHASE_DESKTOP:
        case GSM_MANAGER_PHASE_APPLICATION:
                do_phase_startup (manager);
                break;
        case GSM_MANAGER_PHASE_RUNNING:
                sd_journal_send ("MESSAGE_ID=%s", GSM_MANAGER_STARTUP_SUCCEEDED_MSGID,
                                 "PRIORITY=%d", 5,
                                 "MESSAGE=Entering running state",
                                 NULL);
                if (priv->pending_end_session_tasks != NULL)
                        complete_end_session_tasks (manager);
                g_object_unref (priv->end_session_cancellable);
                priv->end_session_cancellable = g_cancellable_new ();
                gsm_exported_manager_emit_session_running (priv->skeleton);
                update_idle (manager);
                break;
        case GSM_MANAGER_PHASE_QUERY_END_SESSION:
                do_phase_query_end_session (manager);
                break;
        case GSM_MANAGER_PHASE_END_SESSION:
                sd_notify (0, "STOPPING=1");

                do_phase_end_session (manager);
                break;
        case GSM_MANAGER_PHASE_EXIT:
                sd_notify (0, "STOPPING=1");

                do_phase_exit (manager);
                break;
        default:
                g_assert_not_reached ();
                break;
        }
}

static gboolean
_debug_app_for_phase (const char *id,
                      GsmApp     *app,
                      gpointer    data)
{
        guint phase;

        phase = GPOINTER_TO_UINT (data);

        if (gsm_app_peek_phase (app) != phase) {
                return FALSE;
        }

        g_debug ("GsmManager:\tID: %s\tapp-id:%s\tis-disabled:%d\tis-conditionally-disabled:%d",
                 gsm_app_peek_id (app),
                 gsm_app_peek_app_id (app),
                 gsm_app_peek_is_disabled (app),
                 gsm_app_peek_is_conditionally_disabled (app));

        return FALSE;
}

static void
debug_app_summary (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        guint phase;

        g_debug ("GsmManager: App startup summary");
        for (phase = GSM_MANAGER_PHASE_EARLY_INITIALIZATION; phase < GSM_MANAGER_PHASE_RUNNING; phase++) {
                g_debug ("GsmManager: Phase %s", phase_num_to_name (phase));
                gsm_store_foreach (priv->apps,
                                   (GsmStoreFunc)_debug_app_for_phase,
                                   GUINT_TO_POINTER (phase));
        }
}

void
gsm_manager_start (GsmManager *manager)
{
        g_debug ("GsmManager: GSM starting to manage");

        g_return_if_fail (GSM_IS_MANAGER (manager));

        gsm_manager_set_phase (manager, GSM_MANAGER_PHASE_EARLY_INITIALIZATION);
        debug_app_summary (manager);
        start_phase (manager);
}

char *
_gsm_manager_get_default_session (GsmManager     *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        g_autoptr(GSettings) session_settings = NULL;

        if (manager)
                session_settings = g_object_ref (priv->session_settings);
        else
                session_settings  = g_settings_new (SESSION_SCHEMA);
        return g_settings_get_string (session_settings,
                                      KEY_SESSION_NAME);
}

void
_gsm_manager_set_active_session (GsmManager     *manager,
                                 const char     *session_name,
                                 gboolean        is_fallback)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        g_free (priv->session_name);
        priv->session_name = g_strdup (session_name);
        priv->is_fallback_session = is_fallback;

        gsm_exported_manager_set_session_name (priv->skeleton, session_name);
}

static gboolean
_app_has_app_id (const char   *id,
                 GsmApp       *app,
                 const char   *app_id_a)
{
        const char *app_id_b;

        app_id_b = gsm_app_peek_app_id (app);
        return (app_id_b != NULL && strcmp (app_id_a, app_id_b) == 0);
}

static GsmApp *
find_app_for_app_id (GsmManager *manager,
                     const char *app_id)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        GsmApp *app;

        app = (GsmApp *)gsm_store_find (priv->apps,
                                        (GsmStoreFunc)_app_has_app_id,
                                        (char *)app_id);
        return app;
}

static gboolean
inhibitor_has_client_id (gpointer      key,
                         GsmInhibitor *inhibitor,
                         const char   *client_id_a)
{
        gboolean    matches;
        const char *client_id_b;

        client_id_b = gsm_inhibitor_peek_client_id (inhibitor);

        matches = FALSE;
        if (! IS_STRING_EMPTY (client_id_a) && ! IS_STRING_EMPTY (client_id_b)) {
                matches = (strcmp (client_id_a, client_id_b) == 0);
                if (matches) {
                        g_debug ("GsmManager: removing JIT inhibitor for %s for reason '%s'",
                                 gsm_inhibitor_peek_client_id (inhibitor),
                                 gsm_inhibitor_peek_reason (inhibitor));
                }
        }

        return matches;
}

static gboolean
_app_has_startup_id (const char *id,
                     GsmApp     *app,
                     const char *startup_id_a)
{
        const char *startup_id_b;

        startup_id_b = gsm_app_peek_startup_id (app);

        if (IS_STRING_EMPTY (startup_id_b)) {
                return FALSE;
        }

        return (strcmp (startup_id_a, startup_id_b) == 0);
}

static GsmApp *
find_app_for_startup_id (GsmManager *manager,
                        const char *startup_id)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        GsmApp *found_app;
        GSList *a;

        found_app = NULL;

        /* If we're starting up the session, try to match the new client
         * with one pending apps for the current phase. If not, try to match
         * with any of the autostarted apps. */
        if (priv->phase < GSM_MANAGER_PHASE_APPLICATION) {
                for (a = priv->pending_apps; a != NULL; a = a->next) {
                        GsmApp *app = GSM_APP (a->data);

                        if (strcmp (startup_id, gsm_app_peek_startup_id (app)) == 0) {
                                found_app = app;
                                goto out;
                        }
                }
        } else {
                GsmApp *app;

                app = (GsmApp *)gsm_store_find (priv->apps,
                                                (GsmStoreFunc)_app_has_startup_id,
                                                (char *)startup_id);
                if (app != NULL) {
                        found_app = app;
                        goto out;
                }
        }
 out:
        return found_app;
}

static void
_disconnect_client (GsmManager *manager,
                    GsmClient  *client)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        gboolean              is_condition_client;
        GsmApp               *app;
        const char           *app_id;
        const char           *startup_id;
        gboolean              app_restart;
        GsmClientRestartStyle client_restart_hint;

        g_debug ("GsmManager: disconnect client: %s", gsm_client_peek_id (client));

        /* take a ref so it doesn't get finalized */
        g_object_ref (client);

        gsm_client_set_status (client, GSM_CLIENT_FINISHED);

        is_condition_client = FALSE;
        if (g_slist_find (priv->condition_clients, client)) {
                priv->condition_clients = g_slist_remove (priv->condition_clients, client);

                is_condition_client = TRUE;
        }

        /* remove any inhibitors for this client */
        gsm_store_foreach_remove (priv->inhibitors,
                                  (GsmStoreFunc)inhibitor_has_client_id,
                                  (gpointer)gsm_client_peek_id (client));

        app = NULL;

        /* first try to match on startup ID */
        startup_id = gsm_client_peek_startup_id (client);
        if (! IS_STRING_EMPTY (startup_id)) {
                app = find_app_for_startup_id (manager, startup_id);

        }

        /* then try to find matching app-id */
        if (app == NULL) {
                app_id = gsm_client_peek_app_id (client);
                if (! IS_STRING_EMPTY (app_id)) {
                        g_debug ("GsmManager: disconnect for app '%s'", app_id);
                        app = find_app_for_app_id (manager, app_id);
                }
        }

        switch (priv->phase) {
        case GSM_MANAGER_PHASE_QUERY_END_SESSION:
                /* Instead of answering our end session query, the client just exited.
                 * Treat that as an "okay, end the session" answer.
                 *
                 * This call implicitly removes any inhibitors for the client, along
                 * with removing the client from the pending query list.
                 */
                _handle_client_end_session_response (manager,
                                                     client,
                                                     TRUE,
                                                     FALSE,
                                                     FALSE,
                                                     "Client exited in "
                                                     "query end session phase "
                                                     "instead of end session "
                                                     "phase");
                break;
        case GSM_MANAGER_PHASE_END_SESSION:
                if (! g_slist_find (priv->query_clients, client)) {
                        /* the client sent its EndSessionResponse and we already
                         * processed it.
                         */
                        break;
                }

                /* Client exited without sending EndSessionResponse.
                 * The likely reason is that its exit code is written in a way
                 * that never returns, and sending EndSessionResponse is handled
                 * in library code after the callback. Or maybe the application
                 * crashed while handling EndSession. Or it was lazy.
                 */
                _handle_client_end_session_response (manager,
                                                     client,
                                                     TRUE,
                                                     FALSE,
                                                     FALSE,
                                                     "Client exited in "
                                                     "end session phase without "
                                                     "sending EndSessionResponse");
        default:
                /* do nothing */
                break;
        }

        if (priv->dbus_disconnected && GSM_IS_DBUS_CLIENT (client)) {
                g_debug ("GsmManager: dbus disconnected, not restarting application");
                goto out;
        }

        if (app == NULL) {
                g_debug ("GsmManager: unable to find application for client - not restarting");
                goto out;
        }

        if (priv->phase >= GSM_MANAGER_PHASE_QUERY_END_SESSION) {
                g_debug ("GsmManager: in shutdown, not restarting application");
                goto out;
        }

        app_restart = gsm_app_peek_autorestart (app);
        client_restart_hint = gsm_client_peek_restart_style_hint (client);

        /* allow legacy clients to override the app info */
        if (! app_restart
            && client_restart_hint != GSM_CLIENT_RESTART_IMMEDIATELY) {
                g_debug ("GsmManager: autorestart not set, not restarting application");
                goto out;
        }

        if (is_condition_client) {
                g_debug ("GsmManager: app conditionally disabled, not restarting application");
                goto out;
        }

        g_debug ("GsmManager: restarting app");

        _restart_app (manager, app);

 out:
        g_object_unref (client);
}

typedef struct {
        const char *service_name;
        GsmManager *manager;
} RemoveClientData;

static gboolean
_disconnect_dbus_client (const char       *id,
                         GsmClient        *client,
                         RemoveClientData *data)
{
        const char *name;

        if (! GSM_IS_DBUS_CLIENT (client)) {
                return FALSE;
        }

        /* If no service name, then we simply disconnect all clients */
        if (!data->service_name) {
                _disconnect_client (data->manager, client);
                return TRUE;
        }

        name = gsm_dbus_client_get_bus_name (GSM_DBUS_CLIENT (client));
        if (IS_STRING_EMPTY (name)) {
                return FALSE;
        }

        if (strcmp (data->service_name, name) == 0) {
                _disconnect_client (data->manager, client);
                return TRUE;
        }

        return FALSE;
}

/**
 * remove_clients_for_connection:
 * @manager: a #GsmManager
 * @service_name: a service name
 *
 * Disconnects clients that own @service_name.
 *
 * If @service_name is NULL, then disconnects all clients for the connection.
 */
static void
remove_clients_for_connection (GsmManager *manager,
                               const char *service_name)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        RemoveClientData data;

        data.service_name = service_name;
        data.manager = manager;

        /* disconnect dbus clients for name */
        gsm_store_foreach_remove (priv->clients,
                                  (GsmStoreFunc)_disconnect_dbus_client,
                                  &data);

        if (priv->phase >= GSM_MANAGER_PHASE_QUERY_END_SESSION
            && gsm_store_size (priv->clients) == 0) {
                g_debug ("GsmManager: last client disconnected - exiting");
                end_phase (manager);
        }
}

static void
gsm_manager_set_failsafe (GsmManager *manager,
                          gboolean    enabled)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        g_return_if_fail (GSM_IS_MANAGER (manager));

        priv->failsafe = enabled;
}

gboolean
gsm_manager_get_dbus_disconnected (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);

        return priv->dbus_disconnected;
}

gboolean
gsm_manager_get_failsafe (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);

        return priv->failsafe;
}

gboolean
gsm_manager_get_systemd_managed (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);

        return priv->systemd_managed;
}

static void
on_client_disconnected (GsmClient  *client,
                        GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        g_debug ("GsmManager: disconnect client");
        _disconnect_client (manager, client);
        gsm_store_remove (priv->clients, gsm_client_peek_id (client));
        if (priv->phase >= GSM_MANAGER_PHASE_QUERY_END_SESSION
            && gsm_store_size (priv->clients) == 0) {
                g_debug ("GsmManager: last client disconnected - exiting");
                end_phase (manager);
        }
}

static void
_handle_client_end_session_response (GsmManager *manager,
                                     GsmClient  *client,
                                     gboolean    is_ok,
                                     gboolean    do_last,
                                     gboolean    cancel,
                                     const char *reason)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        /* just ignore if received outside of shutdown */
        if (priv->phase < GSM_MANAGER_PHASE_QUERY_END_SESSION) {
                return;
        }

        g_debug ("GsmManager: Response from end session request: is-ok=%d do-last=%d cancel=%d reason=%s", is_ok, do_last, cancel, reason ? reason :"");

        if (cancel) {
                cancel_end_session (manager);
                return;
        }

        priv->query_clients = g_slist_remove (priv->query_clients, client);

        if (! is_ok && priv->logout_mode != GSM_MANAGER_LOGOUT_MODE_FORCE) {
                guint         cookie;
                GsmInhibitor *inhibitor;
                char         *app_id;
                const char   *bus_name;

                /* FIXME: do we support updating the reason? */

                /* Create JIT inhibit */
                if (GSM_IS_DBUS_CLIENT (client)) {
                        bus_name = gsm_dbus_client_get_bus_name (GSM_DBUS_CLIENT (client));
                } else {
                        bus_name = NULL;
                }

                app_id = g_strdup (gsm_client_peek_app_id (client));
                if (IS_STRING_EMPTY (app_id)) {
                        /* XSMP clients don't give us an app id unless we start them */
                        g_free (app_id);
                        app_id = gsm_client_get_app_name (client);
                }

                cookie = _generate_unique_cookie (manager);
                inhibitor = gsm_inhibitor_new_for_client (gsm_client_peek_id (client),
                                                          app_id,
                                                          GSM_INHIBITOR_FLAG_LOGOUT,
                                                          reason != NULL ? reason : _("Not responding"),
                                                          bus_name,
                                                          cookie);
                g_free (app_id);
                gsm_store_add (priv->inhibitors, gsm_inhibitor_peek_id (inhibitor), G_OBJECT (inhibitor));
                g_object_unref (inhibitor);
        } else {
                gsm_store_foreach_remove (priv->inhibitors,
                                          (GsmStoreFunc)inhibitor_has_client_id,
                                          (gpointer)gsm_client_peek_id (client));
        }

        if (priv->phase == GSM_MANAGER_PHASE_QUERY_END_SESSION) {
                if (priv->query_clients == NULL) {
                        query_end_session_complete (manager);
                }
        } else if (priv->phase == GSM_MANAGER_PHASE_END_SESSION) {
                if (do_last) {
                        /* This only makes sense if we're in part 1 of
                         * GSM_MANAGER_PHASE_END_SESSION. Doing this in part 2
                         * can only happen because of a buggy client that loops
                         * wanting to be last again and again. The phase
                         * timeout will take care of this issue. */
                        priv->next_query_clients = g_slist_prepend (priv->next_query_clients,
                                                                             client);
                }

                /* we can continue to the next step if all clients have replied
                 * and if there's no inhibitor */
                if (priv->query_clients != NULL
                    || gsm_manager_is_logout_inhibited (manager)) {
                        return;
                }

                if (priv->next_query_clients != NULL) {
                        do_phase_end_session_part_2 (manager);
                } else {
                        end_phase (manager);
                }
        }
}

static void
on_client_end_session_response (GsmClient  *client,
                                gboolean    is_ok,
                                gboolean    do_last,
                                gboolean    cancel,
                                const char *reason,
                                GsmManager *manager)
{
        _handle_client_end_session_response (manager,
                                             client,
                                             is_ok,
                                             do_last,
                                             cancel,
                                             reason);
}

static void
on_store_client_added (GsmStore   *store,
                       const char *id,
                       GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        GsmClient *client;

        g_debug ("GsmManager: Client added: %s", id);

        client = (GsmClient *)gsm_store_lookup (store, id);

        g_signal_connect (client,
                          "end-session-response",
                          G_CALLBACK (on_client_end_session_response),
                          manager);

        gsm_exported_manager_emit_client_added (priv->skeleton, id);
        /* FIXME: disconnect signal handler */
}

static void
on_store_client_removed (GsmStore   *store,
                         const char *id,
                         GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        g_debug ("GsmManager: Client removed: %s", id);

        gsm_exported_manager_emit_client_removed (priv->skeleton, id);
}

static void
gsm_manager_set_client_store (GsmManager *manager,
                              GsmStore   *store)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        g_return_if_fail (GSM_IS_MANAGER (manager));

        if (store != NULL) {
                g_object_ref (store);
        }

        if (priv->clients != NULL) {
                g_signal_handlers_disconnect_by_func (priv->clients,
                                                      on_store_client_added,
                                                      manager);
                g_signal_handlers_disconnect_by_func (priv->clients,
                                                      on_store_client_removed,
                                                      manager);

                g_object_unref (priv->clients);
        }


        g_debug ("GsmManager: setting client store %p", store);

        priv->clients = store;

        if (priv->clients != NULL) {
                g_signal_connect (priv->clients,
                                  "added",
                                  G_CALLBACK (on_store_client_added),
                                  manager);
                g_signal_connect (priv->clients,
                                  "removed",
                                  G_CALLBACK (on_store_client_removed),
                                  manager);
        }
}

static void
gsm_manager_set_property (GObject       *object,
                          guint          prop_id,
                          const GValue  *value,
                          GParamSpec    *pspec)
{
        GsmManager *self = GSM_MANAGER (object);
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (self);

        switch (prop_id) {
        case PROP_FAILSAFE:
                gsm_manager_set_failsafe (self, g_value_get_boolean (value));
                break;
         case PROP_FALLBACK:
                priv->is_fallback_session = g_value_get_boolean (value);
                break;
         case PROP_CLIENT_STORE:
                gsm_manager_set_client_store (self, g_value_get_object (value));
                break;
        case PROP_SYSTEMD_MANAGED:
                priv->systemd_managed = g_value_get_boolean (value);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_manager_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
        GsmManager *self = GSM_MANAGER (object);
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (self);

        switch (prop_id) {
        case PROP_FAILSAFE:
                g_value_set_boolean (value, priv->failsafe);
                break;
        case PROP_SESSION_NAME:
                g_value_set_string (value, priv->session_name);
                break;
        case PROP_FALLBACK:
                g_value_set_boolean (value, priv->is_fallback_session);
                break;
        case PROP_CLIENT_STORE:
                g_value_set_object (value, priv->clients);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static gboolean
_find_app_provides (const char *id,
                    GsmApp     *app,
                    const char *service)
{
        return gsm_app_provides (app, service);
}

static GObject *
gsm_manager_constructor (GType                  type,
                         guint                  n_construct_properties,
                         GObjectConstructParam *construct_properties)
{
        GsmManager *manager;

        manager = GSM_MANAGER (G_OBJECT_CLASS (gsm_manager_parent_class)->constructor (type,
                                                                                       n_construct_properties,
                                                                                       construct_properties));
        return G_OBJECT (manager);
}

static void
update_inhibited_actions (GsmManager *manager,
                          GsmInhibitorFlag new_inhibited_actions)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        if (priv->inhibited_actions == new_inhibited_actions)
                return;

        gsm_system_set_inhibitors (priv->system, new_inhibited_actions);

        priv->inhibited_actions = new_inhibited_actions;
        gsm_exported_manager_set_inhibited_actions (priv->skeleton,
                                                    priv->inhibited_actions);
}

static void
on_inhibitor_vanished (GsmInhibitor *inhibitor,
                       GsmManager   *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        gsm_store_remove (priv->inhibitors, gsm_inhibitor_peek_id (inhibitor));
}

static void
on_store_inhibitor_added (GsmStore   *store,
                          const char *id,
                          GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        GsmInhibitor *i;
        GsmInhibitorFlag new_inhibited_actions;

        g_debug ("GsmManager: Inhibitor added: %s", id);

        i = GSM_INHIBITOR (gsm_store_lookup (store, id));

        new_inhibited_actions = priv->inhibited_actions | gsm_inhibitor_peek_flags (i);
        update_inhibited_actions (manager, new_inhibited_actions);

        g_signal_connect_object (i, "vanished", G_CALLBACK (on_inhibitor_vanished), manager, 0);

        gsm_exported_manager_emit_inhibitor_added (priv->skeleton, id);

        update_idle (manager);
}

static gboolean
collect_inhibition_flags (const char *id,
                          GObject    *object,
                          gpointer    user_data)
{
        GsmInhibitorFlag *new_inhibited_actions = user_data;

        *new_inhibited_actions |= gsm_inhibitor_peek_flags (GSM_INHIBITOR (object));

        return FALSE;
}

static void
on_store_inhibitor_removed (GsmStore   *store,
                            const char *id,
                            GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        GsmInhibitorFlag new_inhibited_actions;

        g_debug ("GsmManager: Inhibitor removed: %s", id);

        new_inhibited_actions = 0;
        gsm_store_foreach (priv->inhibitors,
                           collect_inhibition_flags,
                           &new_inhibited_actions);
        update_inhibited_actions (manager, new_inhibited_actions);

        gsm_exported_manager_emit_inhibitor_removed (priv->skeleton, id);

        update_idle (manager);

        if (priv->phase >= GSM_MANAGER_PHASE_QUERY_END_SESSION) {
                end_session_or_show_shell_dialog (manager);
        }
}

static void
gsm_manager_dispose (GObject *object)
{
        GsmManager *manager = GSM_MANAGER (object);
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        g_debug ("GsmManager: disposing manager");

        g_clear_object (&priv->end_session_cancellable);
        g_clear_pointer (&priv->session_name, g_free);

        if (priv->clients != NULL) {
                g_signal_handlers_disconnect_by_func (priv->clients,
                                                      on_store_client_added,
                                                      manager);
                g_signal_handlers_disconnect_by_func (priv->clients,
                                                      on_store_client_removed,
                                                      manager);
                g_object_unref (priv->clients);
                priv->clients = NULL;
        }

        g_clear_object (&priv->apps);
        g_slist_free (priv->required_apps);
        priv->required_apps = NULL;
        g_slist_free (priv->pending_apps);
        priv->pending_apps = NULL;

        if (priv->inhibitors != NULL) {
                g_signal_handlers_disconnect_by_func (priv->inhibitors,
                                                      on_store_inhibitor_added,
                                                      manager);
                g_signal_handlers_disconnect_by_func (priv->inhibitors,
                                                      on_store_inhibitor_removed,
                                                      manager);

                g_object_unref (priv->inhibitors);
                priv->inhibitors = NULL;
        }

        g_clear_object (&priv->presence);
        g_clear_object (&priv->settings);
        g_clear_object (&priv->session_settings);
        g_clear_object (&priv->screensaver_settings);
        g_clear_object (&priv->lockdown_settings);
        g_clear_object (&priv->system);
        g_clear_object (&priv->shell);

        if (priv->skeleton != NULL) {
                g_dbus_interface_skeleton_unexport_from_connection (G_DBUS_INTERFACE_SKELETON (priv->skeleton),
                                                                    priv->connection);
                g_clear_object (&priv->skeleton);
        }

        g_clear_object (&priv->connection);

        G_OBJECT_CLASS (gsm_manager_parent_class)->dispose (object);
}

static void
gsm_manager_class_init (GsmManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsm_manager_get_property;
        object_class->set_property = gsm_manager_set_property;
        object_class->constructor = gsm_manager_constructor;
        object_class->dispose = gsm_manager_dispose;

        signals [PHASE_CHANGED] =
                g_signal_new ("phase-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmManagerClass, phase_changed),
                              NULL, NULL, NULL,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);

        g_object_class_install_property (object_class,
                                         PROP_FAILSAFE,
                                         g_param_spec_boolean ("failsafe",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        /**
         * GsmManager::session-name
         *
         * Then name of the currently active session, typically "gnome" or "gnome-fallback".
         * This may be the name of the configured default session, or the name of a fallback
         * session in case we fell back.
         */
        g_object_class_install_property (object_class,
                                         PROP_SESSION_NAME,
                                         g_param_spec_string ("session-name",
                                                              NULL,
                                                              NULL,
                                                              NULL,
                                                              G_PARAM_READABLE));

        /**
         * GsmManager::fallback
         *
         * If %TRUE, the current session is running in the "fallback" mode;
         * this is distinct from whether or not it was configured as default.
         */
        g_object_class_install_property (object_class,
                                         PROP_FALLBACK,
                                         g_param_spec_boolean ("fallback",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_object_class_install_property (object_class,
                                         PROP_CLIENT_STORE,
                                         g_param_spec_object ("client-store",
                                                              NULL,
                                                              NULL,
                                                              GSM_TYPE_STORE,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_object_class_install_property (object_class,
                                         PROP_SYSTEMD_MANAGED,
                                         g_param_spec_boolean ("systemd-managed",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
on_presence_status_changed (GsmPresence  *presence,
                            guint         status,
                            GsmManager   *manager)
{
        GsmSystem *system;

        system = gsm_get_system ();
        gsm_system_set_session_idle (system,
                                     (status == GSM_PRESENCE_STATUS_IDLE));
        g_object_unref (system);
}

static void
on_gsm_system_active_changed (GsmSystem  *system,
                              GParamSpec *pspec,
                              GsmManager *self)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (self);
        gboolean is_active;

        is_active = gsm_system_is_active (priv->system);

        g_debug ("emitting SessionIsActive");
        gsm_exported_manager_set_session_is_active (priv->skeleton, is_active);
}

static gboolean
_log_out_is_locked_down (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        return g_settings_get_boolean (priv->lockdown_settings,
                                       KEY_DISABLE_LOG_OUT);
}

static void
complete_end_session_task (GsmManager            *manager,
                           GAsyncResult          *result,
                           GDBusMethodInvocation *invocation)
{
        GError *error = NULL;

        if (!g_task_propagate_boolean (G_TASK (result), &error))
                g_dbus_method_invocation_take_error (invocation, error);
        else
                g_dbus_method_invocation_return_value (invocation, NULL);
}

static void
request_reboot (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        g_debug ("GsmManager: requesting reboot");

        /* FIXME: We need to support a more structured shutdown here,
         * but that's blocking on an improved ConsoleKit api.
         *
         * See https://bugzilla.gnome.org/show_bug.cgi?id=585614
         */
        priv->logout_type = GSM_MANAGER_LOGOUT_REBOOT_INTERACT;
        end_phase (manager);
}

static void
request_shutdown (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        g_debug ("GsmManager: requesting shutdown");

        /* See the comment in request_reboot() for some more details about
         * what work needs to be done here. */
        priv->logout_type = GSM_MANAGER_LOGOUT_SHUTDOWN_INTERACT;
        end_phase (manager);
}

static void
request_logout (GsmManager           *manager,
                GsmManagerLogoutMode  mode)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        g_debug ("GsmManager: requesting logout");

        priv->logout_mode = mode;
        priv->logout_type = GSM_MANAGER_LOGOUT_LOGOUT;

        end_phase (manager);
}

static gboolean
gsm_manager_shutdown (GsmExportedManager    *skeleton,
                      GDBusMethodInvocation *invocation,
                      GsmManager            *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        GTask *task;

        g_debug ("GsmManager: Shutdown called");

        if (priv->phase < GSM_MANAGER_PHASE_RUNNING) {
                g_dbus_method_invocation_return_error (invocation,
                                                       GSM_MANAGER_ERROR,
                                                       GSM_MANAGER_ERROR_NOT_IN_RUNNING,
                                                       "Shutdown interface is only available after the Running phase starts");
                return TRUE;
        }

        if (_log_out_is_locked_down (manager)) {
                g_dbus_method_invocation_return_error (invocation,
                                                       GSM_MANAGER_ERROR,
                                                       GSM_MANAGER_ERROR_LOCKED_DOWN,
                                                       "Logout has been locked down");
                return TRUE;
        }

        task = g_task_new (manager, priv->end_session_cancellable, (GAsyncReadyCallback) complete_end_session_task, invocation);

        priv->pending_end_session_tasks = g_slist_prepend (priv->pending_end_session_tasks,
                                                                    task);

        request_shutdown (manager);

        return TRUE;
}

static gboolean
gsm_manager_reboot (GsmExportedManager    *skeleton,
                    GDBusMethodInvocation *invocation,
                    GsmManager            *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        GTask *task;

        g_debug ("GsmManager: Reboot called");

        if (priv->phase < GSM_MANAGER_PHASE_RUNNING) {
                g_dbus_method_invocation_return_error (invocation,
                                                       GSM_MANAGER_ERROR,
                                                       GSM_MANAGER_ERROR_NOT_IN_RUNNING,
                                                       "Reboot interface is only available after the Running phase starts");
                return TRUE;
        }

        if (_log_out_is_locked_down (manager)) {
                g_dbus_method_invocation_return_error (invocation,
                                                       GSM_MANAGER_ERROR,
                                                       GSM_MANAGER_ERROR_LOCKED_DOWN,
                                                       "Logout has been locked down");
                return TRUE;
        }

        task = g_task_new (manager, priv->end_session_cancellable, (GAsyncReadyCallback) complete_end_session_task, invocation);

        priv->pending_end_session_tasks = g_slist_prepend (priv->pending_end_session_tasks,
                                                                    task);

        request_reboot (manager);

        return TRUE;
}

static gboolean
gsm_manager_can_shutdown (GsmExportedManager    *skeleton,
                          GDBusMethodInvocation *invocation,
                          GsmManager            *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        gboolean shutdown_available;

        g_debug ("GsmManager: CanShutdown called");

        shutdown_available = !_log_out_is_locked_down (manager) &&
                (gsm_system_can_stop (priv->system)
                 || gsm_system_can_restart (priv->system)
                 || gsm_system_can_suspend (priv->system)
                 || gsm_system_can_hibernate (priv->system));

        gsm_exported_manager_complete_can_shutdown (skeleton, invocation, shutdown_available);

        return TRUE;
}

static gboolean
gsm_manager_can_reboot_to_firmware_setup (GsmExportedManager    *skeleton,
                                          GDBusMethodInvocation *invocation,
                                          GsmManager            *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        gboolean reboot_to_firmware_available;

        g_debug ("GsmManager: CanRebootToFirmwareSetup called");

        reboot_to_firmware_available = !_log_out_is_locked_down (manager) &&
                gsm_system_can_restart_to_firmware_setup (priv->system);

        gsm_exported_manager_complete_can_reboot_to_firmware_setup (skeleton, invocation, reboot_to_firmware_available);

        return TRUE;
}

static gboolean
gsm_manager_set_reboot_to_firmware_setup (GsmExportedManager    *skeleton,
                                          GDBusMethodInvocation *invocation,
                                          gboolean               enable,
                                          GsmManager            *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        g_debug ("GsmManager: SetRebootToFirmwareSetup called");

        gsm_system_set_restart_to_firmware_setup (priv->system, enable);

        gsm_exported_manager_complete_set_reboot_to_firmware_setup (skeleton, invocation);

        return TRUE;
}

static gboolean
gsm_manager_setenv (GsmExportedManager    *skeleton,
                    GDBusMethodInvocation *invocation,
                    const char            *variable,
                    const char            *value,
                    GsmManager            *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        if (priv->phase > GSM_MANAGER_PHASE_INITIALIZATION) {
                g_dbus_method_invocation_return_error (invocation,
                                                       GSM_MANAGER_ERROR,
                                                       GSM_MANAGER_ERROR_NOT_IN_INITIALIZATION,
                                                       "Setenv interface is only available during the DisplayServer and Initialization phase");
        } else {
                gsm_util_setenv (variable, value);
                gsm_exported_manager_complete_setenv (skeleton, invocation);
        }

        return TRUE;
}

static gboolean
gsm_manager_initialized (GsmExportedManager    *skeleton,
                         GDBusMethodInvocation *invocation,
                         GsmManager            *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        /* Signaled by helper when gnome-session-initialized.target is reached. */
        if (!priv->systemd_managed) {
                g_dbus_method_invocation_return_error (invocation,
                                                       GSM_MANAGER_ERROR,
                                                       GSM_MANAGER_ERROR_GENERAL,
                                                       "Initialized interface is only available when gnome-session is managed by systemd");
        } else if (priv->systemd_initialized) {
                g_dbus_method_invocation_return_error (invocation,
                                                       GSM_MANAGER_ERROR,
                                                       GSM_MANAGER_ERROR_NOT_IN_INITIALIZATION,
                                                       "Systemd initialization was already signaled");
        } else if (priv->phase > GSM_MANAGER_PHASE_INITIALIZATION) {
                g_dbus_method_invocation_return_error (invocation,
                                                       GSM_MANAGER_ERROR,
                                                       GSM_MANAGER_ERROR_NOT_IN_INITIALIZATION,
                                                       "Initialized interface is only available during startup");
        } else {
                priv->systemd_initialized = TRUE;

                if (priv->manager_initialized) {
                        g_assert (priv->phase == GSM_MANAGER_PHASE_INITIALIZATION);
                        priv->phase++;
                        start_phase (manager);
                }

                gsm_exported_manager_complete_initialized (skeleton, invocation);
        }

        return TRUE;
}

static gboolean
is_valid_category (int category)
{
        int categories[] = {
                LC_CTYPE,
                LC_NUMERIC,
                LC_TIME,
                LC_COLLATE,
                LC_MONETARY,
                LC_MESSAGES,
#if defined (LC_PAPER)
                LC_PAPER,
#endif
#if defined (LC_NAME)
                LC_NAME,
#endif
#if defined (LC_ADDRESS)
                LC_ADDRESS,
#endif
#if defined (LC_TELEPHONE)
                LC_TELEPHONE,
#endif
#if defined (LC_MEASUREMENT)
                LC_MEASUREMENT,
#endif
#if defined (LC_IDENTIFICATION)
                LC_IDENTIFICATION,
#endif
                LC_ALL
        };
        guint i;

        for (i = 0; i < G_N_ELEMENTS(categories); i++)
                if (categories[i] == category)
                        return TRUE;

        return FALSE;
}

static gboolean
gsm_manager_get_locale (GsmExportedManager    *skeleton,
                        GDBusMethodInvocation *invocation,
                        int                    category,
                        GsmManager            *manager)
{
        if (!is_valid_category (category)) {
                g_dbus_method_invocation_return_error (invocation,
                                                       GSM_MANAGER_ERROR,
                                                       GSM_MANAGER_ERROR_INVALID_OPTION,
                                                       "GetLocale doesn't support locale category '%d'", category);
        } else {
                const char *value;
                value = setlocale (category, NULL);
                if (value == NULL)
                        value = "";

                gsm_exported_manager_complete_get_locale (skeleton, invocation, value);
        }

        return TRUE;
}

static gboolean
gsm_manager_initialization_error (GsmExportedManager    *skeleton,
                                  GDBusMethodInvocation *invocation,
                                  const char            *message,
                                  gboolean               fatal,
                                  GsmManager            *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        if (priv->phase != GSM_MANAGER_PHASE_INITIALIZATION) {
                g_dbus_method_invocation_return_error (invocation,
                                                       GSM_MANAGER_ERROR,
                                                       GSM_MANAGER_ERROR_NOT_IN_INITIALIZATION,
                                                       "InitializationError interface is only available during the Initialization phase");
                return TRUE;
        }

        gsm_util_init_error (fatal, "%s", message);
        gsm_exported_manager_complete_initialization_error (skeleton, invocation);

        return TRUE;
}

static void
user_logout (GsmManager           *manager,
             GsmManagerLogoutMode  mode)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        if (priv->phase >= GSM_MANAGER_PHASE_QUERY_END_SESSION) {
                priv->logout_mode = mode;
                end_session_or_show_shell_dialog (manager);
                return;
        }

        request_logout (manager, mode);
}

gboolean
gsm_manager_logout (GsmManager *manager,
                    guint logout_mode,
                    GError **error)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        if (priv->phase < GSM_MANAGER_PHASE_RUNNING) {
                g_set_error (error,
                             GSM_MANAGER_ERROR,
                             GSM_MANAGER_ERROR_NOT_IN_RUNNING,
                             "Logout interface is only available after the Running phase starts");
                return FALSE;
        }

        if (_log_out_is_locked_down (manager)) {
                g_set_error (error,
                             GSM_MANAGER_ERROR,
                             GSM_MANAGER_ERROR_LOCKED_DOWN,
                             "Logout has been locked down");
                return FALSE;
        }

        switch (logout_mode) {
        case GSM_MANAGER_LOGOUT_MODE_NORMAL:
        case GSM_MANAGER_LOGOUT_MODE_NO_CONFIRMATION:
        case GSM_MANAGER_LOGOUT_MODE_FORCE:
                user_logout (manager, logout_mode);
                break;

        default:
                g_debug ("Unknown logout mode option");

                g_set_error (error,
                             GSM_MANAGER_ERROR,
                             GSM_MANAGER_ERROR_INVALID_OPTION,
                             "Unknown logout mode flag");
                return FALSE;
        }

        return TRUE;
}

static gboolean
gsm_manager_logout_dbus (GsmExportedManager    *skeleton,
                         GDBusMethodInvocation *invocation,
                         guint                  logout_mode,
                         GsmManager            *manager)
{
        GError *error = NULL;

        g_debug ("GsmManager: Logout called");

        if (!gsm_manager_logout (manager, logout_mode, &error)) {
                g_dbus_method_invocation_take_error (invocation, error);
        } else {
                gsm_exported_manager_complete_logout (skeleton, invocation);
        }

        return TRUE;
}

static gboolean
gsm_manager_register_client (GsmExportedManager    *skeleton,
                             GDBusMethodInvocation *invocation,
                             const char            *app_id,
                             const char            *startup_id,
                             GsmManager            *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        char       *new_startup_id;
        const char *sender;
        GsmClient  *client;
        GsmApp     *app;

        app = NULL;
        client = NULL;

        g_debug ("GsmManager: RegisterClient %s", startup_id);

        if (priv->phase >= GSM_MANAGER_PHASE_QUERY_END_SESSION) {
                g_debug ("Unable to register client: shutting down");

                g_dbus_method_invocation_return_error (invocation,
                                                       GSM_MANAGER_ERROR,
                                                       GSM_MANAGER_ERROR_NOT_IN_RUNNING,
                                                       "Unable to register client");
                return TRUE;
        }

        if (IS_STRING_EMPTY (startup_id)) {
                new_startup_id = gsm_util_generate_startup_id ();
        } else {

                client = (GsmClient *)gsm_store_find (priv->clients,
                                                      (GsmStoreFunc)_client_has_startup_id,
                                                      (char *)startup_id);
                /* We can't have two clients with the same startup id. */
                if (client != NULL) {
                        g_debug ("Unable to register client: already registered");

                        g_dbus_method_invocation_return_error (invocation,
                                                               GSM_MANAGER_ERROR,
                                                               GSM_MANAGER_ERROR_ALREADY_REGISTERED,
                                                               "Unable to register client");
                        return TRUE;
                }

                new_startup_id = g_strdup (startup_id);
        }

        g_debug ("GsmManager: Adding new client %s to session", new_startup_id);

        if (app == NULL && !IS_STRING_EMPTY (startup_id)) {
                app = find_app_for_startup_id (manager, startup_id);
        }
        if (app == NULL && !IS_STRING_EMPTY (app_id)) {
                /* try to associate this app id with a known app */
                app = find_app_for_app_id (manager, app_id);
        }

        sender = g_dbus_method_invocation_get_sender (invocation);
        client = gsm_dbus_client_new (new_startup_id, sender);
        if (client == NULL) {
                g_debug ("Unable to create client");

                g_dbus_method_invocation_return_error (invocation,
                                                       GSM_MANAGER_ERROR,
                                                       GSM_MANAGER_ERROR_GENERAL,
                                                       "Unable to register client");
                return TRUE;
        }

        gsm_store_add (priv->clients, gsm_client_peek_id (client), G_OBJECT (client));
        /* the store will own the ref */
        g_object_unref (client);

        g_signal_connect (client,
                          "disconnected",
                          G_CALLBACK (on_client_disconnected),
                          manager);

        if (app != NULL) {
                gsm_client_set_app_id (client, gsm_app_peek_app_id (app));
                gsm_app_set_registered (app, TRUE);
        } else {
                /* if an app id is specified store it in the client
                   so we can save it later */
                gsm_client_set_app_id (client, app_id);
        }

        gsm_client_set_status (client, GSM_CLIENT_REGISTERED);

        g_assert (new_startup_id != NULL);
        g_free (new_startup_id);

        gsm_exported_manager_complete_register_client (skeleton, invocation, gsm_client_peek_id (client));

        return TRUE;
}

static gboolean
gsm_manager_unregister_client (GsmExportedManager    *skeleton,
                               GDBusMethodInvocation *invocation,
                               const char            *client_id,
                               GsmManager            *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        GsmClient *client;

        g_debug ("GsmManager: UnregisterClient %s", client_id);

        client = (GsmClient *)gsm_store_lookup (priv->clients, client_id);
        if (client == NULL) {
                g_debug ("Unable to unregister client: not registered");

                g_dbus_method_invocation_return_error (invocation,
                                                       GSM_MANAGER_ERROR,
                                                       GSM_MANAGER_ERROR_NOT_REGISTERED,
                                                       "Unable to unregister client");
                return TRUE;
        }

        /* don't disconnect client here, only change the status.
           Wait until it leaves the bus before disconnecting it */
        gsm_client_set_status (client, GSM_CLIENT_UNREGISTERED);

        gsm_exported_manager_complete_unregister_client (skeleton, invocation);

        return TRUE;
}

static gboolean
gsm_manager_inhibit (GsmExportedManager    *skeleton,
                     GDBusMethodInvocation *invocation,
                     const char            *app_id,
                     guint                  toplevel_xid,
                     const char            *reason,
                     guint                  flags,
                     GsmManager            *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        GsmInhibitor *inhibitor;
        guint         cookie;

        g_debug ("GsmManager: Inhibit xid=%u app_id=%s reason=%s flags=%u",
                 toplevel_xid,
                 app_id,
                 reason,
                 flags);

        if (priv->logout_mode == GSM_MANAGER_LOGOUT_MODE_FORCE) {
                GError *new_error;

                new_error = g_error_new (GSM_MANAGER_ERROR,
                                         GSM_MANAGER_ERROR_GENERAL,
                                         "Forced logout cannot be inhibited");
                g_debug ("GsmManager: Unable to inhibit: %s", new_error->message);
                g_dbus_method_invocation_take_error (invocation, new_error);
                return TRUE;
        }

        if (IS_STRING_EMPTY (reason)) {
                GError *new_error;

                new_error = g_error_new (GSM_MANAGER_ERROR,
                                         GSM_MANAGER_ERROR_GENERAL,
                                         "Reason not specified");
                g_debug ("GsmManager: Unable to inhibit: %s", new_error->message);
                g_dbus_method_invocation_take_error (invocation, new_error);
                return TRUE;
        }

        if (flags == 0) {
                GError *new_error;

                new_error = g_error_new (GSM_MANAGER_ERROR,
                                         GSM_MANAGER_ERROR_GENERAL,
                                         "Invalid inhibit flags");
                g_debug ("GsmManager: Unable to inhibit: %s", new_error->message);
                g_dbus_method_invocation_take_error (invocation, new_error);
                return TRUE;
        }

        cookie = _generate_unique_cookie (manager);
        inhibitor = gsm_inhibitor_new (app_id,
                                       toplevel_xid,
                                       flags,
                                       reason,
                                       g_dbus_method_invocation_get_sender (invocation),
                                       cookie);
        gsm_store_add (priv->inhibitors, gsm_inhibitor_peek_id (inhibitor), G_OBJECT (inhibitor));
        g_object_unref (inhibitor);

        gsm_exported_manager_complete_inhibit (skeleton, invocation, cookie);

        return TRUE;
}

static gboolean
gsm_manager_uninhibit (GsmExportedManager    *skeleton,
                       GDBusMethodInvocation *invocation,
                       guint                  cookie,
                       GsmManager            *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        GsmInhibitor *inhibitor;

        g_debug ("GsmManager: Uninhibit %u", cookie);

        inhibitor = (GsmInhibitor *)gsm_store_find (priv->inhibitors,
                                                    (GsmStoreFunc)_find_by_cookie,
                                                    &cookie);
        if (inhibitor == NULL) {
                GError *new_error;

                new_error = g_error_new (GSM_MANAGER_ERROR,
                                         GSM_MANAGER_ERROR_GENERAL,
                                         "Unable to uninhibit: Invalid cookie");
                g_debug ("Unable to uninhibit: %s", new_error->message);
                g_dbus_method_invocation_take_error (invocation, new_error);
                return TRUE;
        }

        g_debug ("GsmManager: removing inhibitor %s %u reason '%s' %u connection %s",
                 gsm_inhibitor_peek_app_id (inhibitor),
                 gsm_inhibitor_peek_toplevel_xid (inhibitor),
                 gsm_inhibitor_peek_reason (inhibitor),
                 gsm_inhibitor_peek_flags (inhibitor),
                 gsm_inhibitor_peek_bus_name (inhibitor));

        gsm_store_remove (priv->inhibitors, gsm_inhibitor_peek_id (inhibitor));

        gsm_exported_manager_complete_uninhibit (skeleton, invocation);

        return TRUE;
}

static gboolean
gsm_manager_is_inhibited (GsmExportedManager    *skeleton,
                          GDBusMethodInvocation *invocation,
                          guint                  flags,
                          GsmManager            *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        GsmInhibitor *inhibitor;
        gboolean is_inhibited;

        if (priv->inhibitors == NULL
            || gsm_store_size (priv->inhibitors) == 0) {
                is_inhibited = FALSE;
        } else {
                inhibitor = (GsmInhibitor *) gsm_store_find (priv->inhibitors,
                                                             (GsmStoreFunc)inhibitor_has_flag,
                                                             GUINT_TO_POINTER (flags));
                if (inhibitor == NULL) {
                        is_inhibited = FALSE;
                } else {
                        is_inhibited = TRUE;
                }
        }

        gsm_exported_manager_complete_is_inhibited (skeleton, invocation, is_inhibited);

        return TRUE;
}

static gboolean
listify_store_ids (char       *id,
                   GObject    *object,
                   GPtrArray **array)
{
        g_ptr_array_add (*array, g_strdup (id));
        return FALSE;
}

static gboolean
gsm_manager_get_clients (GsmExportedManager    *skeleton,
                         GDBusMethodInvocation *invocation,
                         GsmManager            *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        GPtrArray *clients;

        clients = g_ptr_array_new_with_free_func (g_free);
        gsm_store_foreach (priv->clients,
                           (GsmStoreFunc) listify_store_ids,
                           &clients);
        g_ptr_array_add (clients, NULL);

        gsm_exported_manager_complete_get_clients (skeleton, invocation,
                                                   (const gchar * const *) clients->pdata);
        g_ptr_array_unref (clients);

        return TRUE;
}

static gboolean
gsm_manager_get_inhibitors (GsmExportedManager    *skeleton,
                            GDBusMethodInvocation *invocation,
                            GsmManager            *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        GPtrArray *inhibitors;

        inhibitors = g_ptr_array_new_with_free_func (g_free);
        gsm_store_foreach (priv->inhibitors,
                           (GsmStoreFunc) listify_store_ids,
                           &inhibitors);
        g_ptr_array_add (inhibitors, NULL);

        gsm_exported_manager_complete_get_inhibitors (skeleton, invocation,
                                                      (const gchar * const *) inhibitors->pdata);
        g_ptr_array_unref (inhibitors);

        return TRUE;
}

static gboolean
_app_has_autostart_condition (const char *id,
                              GsmApp     *app,
                              const char *condition)
{
        gboolean has;
        gboolean disabled;

        has = gsm_app_has_autostart_condition (app, condition);
        disabled = gsm_app_peek_is_disabled (app);

        return has && !disabled;
}

static gboolean
gsm_manager_is_autostart_condition_handled (GsmExportedManager    *skeleton,
                                            GDBusMethodInvocation *invocation,
                                            const char            *condition,
                                            GsmManager            *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        GsmApp *app;
        gboolean handled;

        app = (GsmApp *) gsm_store_find (priv->apps,(
                                         GsmStoreFunc) _app_has_autostart_condition,
                                         (char *)condition);

        if (app != NULL) {
                handled = TRUE;
        } else {
                handled = FALSE;
        }

        gsm_exported_manager_complete_is_autostart_condition_handled (skeleton, invocation, handled);

        return TRUE;
}

static gboolean
gsm_manager_is_session_running (GsmExportedManager    *skeleton,
                                GDBusMethodInvocation *invocation,
                                GsmManager            *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        gsm_exported_manager_complete_is_session_running (skeleton, invocation,
                                                          priv->phase == GSM_MANAGER_PHASE_RUNNING);
        return TRUE;
}

static void
on_session_connection_closed (GDBusConnection *connection,
                              gboolean remote_peer_vanished,
                              GError *error,
                              gpointer user_data)
{
        GsmManager *manager;
        GsmManagerPrivate *priv;

        manager = GSM_MANAGER (user_data);
        priv = gsm_manager_get_instance_private (manager);

        g_debug ("GsmManager: dbus disconnected; disconnecting dbus clients...");
        priv->dbus_disconnected = TRUE;
        remove_clients_for_connection (manager, NULL);
}

static gboolean
register_manager (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        GDBusConnection *connection;
        GsmExportedManager *skeleton;
        GError *error = NULL;

        connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);

        if (error != NULL) {
                g_critical ("error getting session bus: %s", error->message);
                g_error_free (error);

                exit (1);
        }

        skeleton = gsm_exported_manager_skeleton_new ();
        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (skeleton),
                                          connection,
                                          GSM_MANAGER_DBUS_PATH, &error);

        if (error != NULL) {
                g_critical ("error exporting manager on session bus: %s", error->message);
                g_error_free (error);

                exit (1);
        }

        g_signal_connect (skeleton, "handle-can-reboot-to-firmware-setup",
                          G_CALLBACK (gsm_manager_can_reboot_to_firmware_setup), manager);
        g_signal_connect (skeleton, "handle-can-shutdown",
                          G_CALLBACK (gsm_manager_can_shutdown), manager);
        g_signal_connect (skeleton, "handle-get-clients",
                          G_CALLBACK (gsm_manager_get_clients), manager);
        g_signal_connect (skeleton, "handle-get-inhibitors",
                          G_CALLBACK (gsm_manager_get_inhibitors), manager);
        g_signal_connect (skeleton, "handle-get-locale",
                          G_CALLBACK (gsm_manager_get_locale), manager);
        g_signal_connect (skeleton, "handle-inhibit",
                          G_CALLBACK (gsm_manager_inhibit), manager);
        g_signal_connect (skeleton, "handle-initialization-error",
                          G_CALLBACK (gsm_manager_initialization_error), manager);
        g_signal_connect (skeleton, "handle-is-autostart-condition-handled",
                          G_CALLBACK (gsm_manager_is_autostart_condition_handled), manager);
        g_signal_connect (skeleton, "handle-is-inhibited",
                          G_CALLBACK (gsm_manager_is_inhibited), manager);
        g_signal_connect (skeleton, "handle-is-session-running",
                          G_CALLBACK (gsm_manager_is_session_running), manager);
        g_signal_connect (skeleton, "handle-logout",
                          G_CALLBACK (gsm_manager_logout_dbus), manager);
        g_signal_connect (skeleton, "handle-reboot",
                          G_CALLBACK (gsm_manager_reboot), manager);
        g_signal_connect (skeleton, "handle-register-client",
                          G_CALLBACK (gsm_manager_register_client), manager);
        g_signal_connect (skeleton, "handle-set-reboot-to-firmware-setup",
                          G_CALLBACK (gsm_manager_set_reboot_to_firmware_setup), manager);
        g_signal_connect (skeleton, "handle-setenv",
                          G_CALLBACK (gsm_manager_setenv), manager);
        g_signal_connect (skeleton, "handle-initialized",
                          G_CALLBACK (gsm_manager_initialized), manager);
        g_signal_connect (skeleton, "handle-shutdown",
                          G_CALLBACK (gsm_manager_shutdown), manager);
        g_signal_connect (skeleton, "handle-uninhibit",
                          G_CALLBACK (gsm_manager_uninhibit), manager);
        g_signal_connect (skeleton, "handle-unregister-client",
                          G_CALLBACK (gsm_manager_unregister_client), manager);

        priv->dbus_disconnected = FALSE;
        g_signal_connect (connection, "closed",
                          G_CALLBACK (on_session_connection_closed), manager);

        priv->connection = connection;
        priv->skeleton = skeleton;

        g_signal_connect (priv->system, "notify::active",
                          G_CALLBACK (on_gsm_system_active_changed), manager);

        /* cold-plug SessionIsActive */
        on_gsm_system_active_changed (priv->system, NULL, manager);

        return TRUE;
}

static gboolean
idle_timeout_get_mapping (GValue *value,
                          GVariant *variant,
                          gpointer user_data)
{
        guint32 idle_timeout;

        idle_timeout = g_variant_get_uint32 (variant);
        g_value_set_uint (value, idle_timeout * 1000);

        return TRUE;
}

static void
gsm_manager_init (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        priv->settings = g_settings_new (GSM_MANAGER_SCHEMA);
        priv->session_settings = g_settings_new (SESSION_SCHEMA);
        priv->screensaver_settings = g_settings_new (SCREENSAVER_SCHEMA);
        priv->lockdown_settings = g_settings_new (LOCKDOWN_SCHEMA);

        priv->inhibitors = gsm_store_new ();
        g_signal_connect (priv->inhibitors,
                          "added",
                          G_CALLBACK (on_store_inhibitor_added),
                          manager);
        g_signal_connect (priv->inhibitors,
                          "removed",
                          G_CALLBACK (on_store_inhibitor_removed),
                          manager);

        priv->apps = gsm_store_new ();

        priv->presence = gsm_presence_new ();
        g_signal_connect (priv->presence,
                          "status-changed",
                          G_CALLBACK (on_presence_status_changed),
                          manager);

        g_settings_bind_with_mapping (priv->session_settings,
                                      KEY_IDLE_DELAY,
                                      priv->presence,
                                      "idle-timeout",
                                      G_SETTINGS_BIND_GET,
                                      idle_timeout_get_mapping,
                                      NULL,
                                      NULL, NULL);

        priv->system = gsm_get_system ();
        priv->shell = gsm_get_shell ();
        priv->end_session_cancellable = g_cancellable_new ();
}

GsmManager *
gsm_manager_get (void)
{
        return manager_object;
}

GsmManager *
gsm_manager_new (GsmStore *client_store,
                 gboolean  failsafe,
                 gboolean  systemd_managed)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                gboolean res;

                manager_object = g_object_new (GSM_TYPE_MANAGER,
                                               "client-store", client_store,
                                               "failsafe", failsafe,
                                               "systemd-managed", systemd_managed,
                                               NULL);

                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
                res = register_manager (manager_object);
                if (! res) {
                        g_object_unref (manager_object);
                        return NULL;
                }
        }

        return GSM_MANAGER (manager_object);
}

static void
disconnect_shell_dialog_signals (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        if (priv->shell_end_session_dialog_canceled_id != 0) {
                g_signal_handler_disconnect (priv->shell,
                                             priv->shell_end_session_dialog_canceled_id);
                priv->shell_end_session_dialog_canceled_id = 0;
        }

        if (priv->shell_end_session_dialog_confirmed_logout_id != 0) {
                g_signal_handler_disconnect (priv->shell,
                                             priv->shell_end_session_dialog_confirmed_logout_id);
                priv->shell_end_session_dialog_confirmed_logout_id = 0;
        }

        if (priv->shell_end_session_dialog_confirmed_shutdown_id != 0) {
                g_signal_handler_disconnect (priv->shell,
                                             priv->shell_end_session_dialog_confirmed_shutdown_id);
                priv->shell_end_session_dialog_confirmed_shutdown_id = 0;
        }

        if (priv->shell_end_session_dialog_confirmed_reboot_id != 0) {
                g_signal_handler_disconnect (priv->shell,
                                             priv->shell_end_session_dialog_confirmed_reboot_id);
                priv->shell_end_session_dialog_confirmed_reboot_id = 0;
        }

        if (priv->shell_end_session_dialog_open_failed_id != 0) {
                g_signal_handler_disconnect (priv->shell,
                                             priv->shell_end_session_dialog_open_failed_id);
                priv->shell_end_session_dialog_open_failed_id = 0;
        }
}

static void
on_shell_end_session_dialog_canceled (GsmShell   *shell,
                                      GsmManager *manager)
{
        cancel_end_session (manager);
        disconnect_shell_dialog_signals (manager);
}

static void
_handle_end_session_dialog_response (GsmManager           *manager,
                                     GsmManagerLogoutType  logout_type)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        /* Note we're checking for END_SESSION here and
         * QUERY_END_SESSION in the fallback cases elsewhere.
         *
         * That's because they run at different times in the logout
         * process. The shell combines the inhibit and
         * confirmation dialogs, so it gets displayed after we've collected
         * inhibitors. The fallback code has two distinct dialogs, once of
         * which we can (and do show) before collecting the inhibitors.
         */
        if (priv->phase >= GSM_MANAGER_PHASE_END_SESSION) {
                /* Already shutting down, nothing more to do */
                return;
        }

        priv->logout_mode = GSM_MANAGER_LOGOUT_MODE_FORCE;
        priv->logout_type = logout_type;
        end_phase (manager);
}

static void
on_shell_end_session_dialog_confirmed_logout (GsmShell   *shell,
                                              GsmManager *manager)
{
        _handle_end_session_dialog_response (manager, GSM_MANAGER_LOGOUT_LOGOUT);
        disconnect_shell_dialog_signals (manager);
}

static void
on_shell_end_session_dialog_confirmed_shutdown (GsmShell   *shell,
                                                GsmManager *manager)
{
        _handle_end_session_dialog_response (manager, GSM_MANAGER_LOGOUT_SHUTDOWN);
        disconnect_shell_dialog_signals (manager);
}

static void
on_shell_end_session_dialog_confirmed_reboot (GsmShell   *shell,
                                              GsmManager *manager)
{
        _handle_end_session_dialog_response (manager, GSM_MANAGER_LOGOUT_REBOOT);
        disconnect_shell_dialog_signals (manager);
}

static void
connect_shell_dialog_signals (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        if (priv->shell_end_session_dialog_canceled_id != 0)
                return;

        priv->shell_end_session_dialog_canceled_id =
                g_signal_connect (priv->shell,
                                  "end-session-dialog-canceled",
                                  G_CALLBACK (on_shell_end_session_dialog_canceled),
                                  manager);

        priv->shell_end_session_dialog_open_failed_id =
                g_signal_connect (priv->shell,
                                  "end-session-dialog-open-failed",
                                  G_CALLBACK (on_shell_end_session_dialog_canceled),
                                  manager);

        priv->shell_end_session_dialog_confirmed_logout_id =
                g_signal_connect (priv->shell,
                                  "end-session-dialog-confirmed-logout",
                                  G_CALLBACK (on_shell_end_session_dialog_confirmed_logout),
                                  manager);

        priv->shell_end_session_dialog_confirmed_shutdown_id =
                g_signal_connect (priv->shell,
                                  "end-session-dialog-confirmed-shutdown",
                                  G_CALLBACK (on_shell_end_session_dialog_confirmed_shutdown),
                                  manager);

        priv->shell_end_session_dialog_confirmed_reboot_id =
                g_signal_connect (priv->shell,
                                  "end-session-dialog-confirmed-reboot",
                                  G_CALLBACK (on_shell_end_session_dialog_confirmed_reboot),
                                  manager);
}

static void
show_shell_end_session_dialog (GsmManager                   *manager,
                               GsmShellEndSessionDialogType  type)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        if (!gsm_shell_is_running (priv->shell))
                return;

        gsm_shell_open_end_session_dialog (priv->shell,
                                           type,
                                           priv->inhibitors);
        connect_shell_dialog_signals (manager);
}

/*
  dbus-send --session --type=method_call --print-reply
      --dest=org.gnome.SessionManager
      /org/gnome/SessionManager
      org.freedesktop.DBus.Introspectable.Introspect
*/

gboolean
gsm_manager_set_phase (GsmManager      *manager,
                       GsmManagerPhase  phase)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);
        priv->phase = phase;
        return (TRUE);
}

static void
append_app (GsmManager *manager,
            GsmApp     *app,
            gboolean    is_required)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        const char *id;
        const char *app_id;
        GsmApp     *dup;

        id = gsm_app_peek_id (app);
        if (IS_STRING_EMPTY (id)) {
                g_debug ("GsmManager: not adding app: no id");
                return;
        }

        dup = (GsmApp *)gsm_store_lookup (priv->apps, id);
        if (dup != NULL) {
                g_debug ("GsmManager: not adding app: already added");
                return;
        }

        app_id = gsm_app_peek_app_id (app);
        if (IS_STRING_EMPTY (app_id)) {
                g_debug ("GsmManager: not adding app: no app-id");
                return;
        }

        dup = find_app_for_app_id (manager, app_id);
        if (dup != NULL) {
                g_debug ("GsmManager: not adding app: app-id '%s' already exists", app_id);

                if (is_required &&
                    !g_slist_find (priv->required_apps, dup)) {
                        g_debug ("GsmManager: making app '%s' required", gsm_app_peek_app_id (dup));
                        priv->required_apps = g_slist_prepend (priv->required_apps, dup);
                }

                return;
        }

        gsm_store_add (priv->apps, id, G_OBJECT (app));
        if (is_required) {
                g_debug ("GsmManager: adding required app %s", gsm_app_peek_app_id (app));
                priv->required_apps = g_slist_prepend (priv->required_apps, app);
        }
}

static gboolean
add_autostart_app_internal (GsmManager *manager,
                            const char *path,
                            gboolean    is_required)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        GsmApp  *app;
        char   **internal_provides;
        GError *error = NULL;

        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);
        g_return_val_if_fail (path != NULL, FALSE);

        /* Note: if we cannot add the app because its service is already
         * provided, because its app-id is taken, or because of any other
         * reason meaning there is already an app playing its role, then we
         * should make sure that relevant properties (like
         * provides/is_required) are set in the pre-existing app if needed. */
        app = gsm_autostart_app_new (path, priv->systemd_managed, &error);
        if (app == NULL) {
                g_warning ("%s", error->message);
                g_clear_error (&error);
                return FALSE;
        }

        internal_provides = gsm_app_get_provides (app);
        if (internal_provides) {
                int i;
                gboolean provided = FALSE;

                for (i = 0; internal_provides[i] != NULL; i++) {
                        GsmApp *dup;

                        dup = (GsmApp *)gsm_store_find (priv->apps,
                                                        (GsmStoreFunc)_find_app_provides,
                                                        (char *)internal_provides[i]);
                        if (dup != NULL) {
                                g_debug ("GsmManager: service '%s' is already provided", internal_provides[i]);

                                if (is_required &&
                                    !g_slist_find (priv->required_apps, dup)) {
                                        g_debug ("GsmManager: making app '%s' required", gsm_app_peek_app_id (dup));
                                        priv->required_apps = g_slist_prepend (priv->required_apps, dup);
                                }

                                provided = TRUE;
                                break;
                        }
                }

                g_strfreev (internal_provides);

                if (provided) {
                        g_object_unref (app);
                        return FALSE;
                }
        }

        g_debug ("GsmManager: read %s", path);
        append_app (manager, app, is_required);
        g_object_unref (app);

        return TRUE;
}

gboolean
gsm_manager_add_autostart_app (GsmManager *manager,
                               const char *path)
{
        return add_autostart_app_internal (manager, path, FALSE);
}

/**
 * gsm_manager_add_required_app:
 * @manager: a #GsmManager
 * @path: Path to desktop file
 *
 * Similar to gsm_manager_add_autostart_app(), except marks the
 * component as being required; we then try harder to ensure
 * it's running and inform the user if we can't.
 *
 */
gboolean
gsm_manager_add_required_app (GsmManager *manager,
                              const char *path)
{
        return add_autostart_app_internal (manager, path, TRUE);
}


gboolean
gsm_manager_add_autostart_apps_from_dir (GsmManager *manager,
                                         const char *path)
{
        GDir       *dir;
        const char *name;

        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);
        g_return_val_if_fail (path != NULL, FALSE);

        g_debug ("GsmManager: *** Adding autostart apps for %s", path);

        dir = g_dir_open (path, 0, NULL);
        if (dir == NULL) {
                return FALSE;
        }

        while ((name = g_dir_read_name (dir))) {
                char *desktop_file;

                if (!g_str_has_suffix (name, ".desktop")) {
                        continue;
                }

                desktop_file = g_build_filename (path, name, NULL);
                gsm_manager_add_autostart_app (manager, desktop_file);
                g_free (desktop_file);
        }

        g_dir_close (dir);

        return TRUE;
}

static void
on_shutdown_prepared (GsmSystem  *system,
                      gboolean    success,
                      GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);

        g_debug ("GsmManager: on_shutdown_prepared, success: %d", success);
        g_signal_handlers_disconnect_by_func (system, on_shutdown_prepared, manager);

        if (success) {
                /* move to end-session phase */
                g_assert (priv->phase == GSM_MANAGER_PHASE_QUERY_END_SESSION);
                priv->phase++;
                start_phase (manager);
        } else {
                disconnect_shell_dialog_signals (manager);
                gsm_shell_close_end_session_dialog (priv->shell);
                /* back to running phase */
                cancel_end_session (manager);
        }
}

static gboolean
do_query_end_session_exit (GsmManager *manager)
{
        GsmManagerPrivate *priv = gsm_manager_get_instance_private (manager);
        gboolean reboot = FALSE;
        gboolean shutdown = FALSE;

        switch (priv->logout_type) {
        case GSM_MANAGER_LOGOUT_LOGOUT:
                break;
        case GSM_MANAGER_LOGOUT_REBOOT:
        case GSM_MANAGER_LOGOUT_REBOOT_INTERACT:
                reboot = TRUE;
                break;
        case GSM_MANAGER_LOGOUT_SHUTDOWN:
        case GSM_MANAGER_LOGOUT_SHUTDOWN_INTERACT:
                shutdown = TRUE;
                break;
        default:
                g_warning ("Unexpected logout type %d in do_query_end_session_exit()",
                           priv->logout_type);
                break;
        }

        if (reboot || shutdown) {
                g_signal_connect (priv->system, "shutdown-prepared",
                                  G_CALLBACK (on_shutdown_prepared), manager);
                gsm_system_prepare_shutdown (priv->system, reboot);
                return FALSE; /* don't leave query end session yet */
        }

        return TRUE; /* go to end session phase */
}
