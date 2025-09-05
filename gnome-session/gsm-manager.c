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

#include "gsm-app.h"
#include "gsm-client.h"
#include "gsm-inhibitor.h"
#include "gsm-presence.h"
#include "gsm-session-save.h"
#include "gsm-shell.h"
#include "gsm-store.h"
#include "gsm-system.h"
#include "gsm-util.h"

#define GSM_MANAGER_DBUS_PATH "/org/gnome/SessionManager"
#define GSM_MANAGER_DBUS_NAME "org.gnome.SessionManager"
#define GSM_MANAGER_DBUS_IFACE "org.gnome.SessionManager"

#define SESSION_SCHEMA            "org.gnome.desktop.session"
#define KEY_IDLE_DELAY            "idle-delay"

#define GSM_MANAGER_SCHEMA        "org.gnome.SessionManager"
#define KEY_LOGOUT_PROMPT         "logout-prompt"

#define LOCKDOWN_SCHEMA           "org.gnome.desktop.lockdown"
#define KEY_DISABLE_LOG_OUT       "disable-log-out"
#define KEY_DISABLE_USER_SWITCHING "disable-user-switching"

typedef enum
{
        GSM_MANAGER_LOGOUT_NONE,
        GSM_MANAGER_LOGOUT_LOGOUT,
        GSM_MANAGER_LOGOUT_REBOOT,
        GSM_MANAGER_LOGOUT_SHUTDOWN,
} GsmManagerLogoutType;

struct _GsmManager
{
        GObject                 parent;

        gboolean                systemd_initialized;
        GsmStore               *clients;
        GsmStore               *inhibitors;
        GsmInhibitorFlag        inhibited_actions;
        GsmStore               *apps;
        GsmPresence            *presence;
        GsmSessionSave         *session_save;
        char                   *session_name;

        /* Current status */
        GsmManagerPhase         phase;
        guint                   phase_timeout_id;
        GsmManagerLogoutMode    logout_mode;
        GSList                 *query_clients;
        /* This is the action that will be done just before we exit */
        GsmManagerLogoutType    logout_type;

        GSList                 *pending_end_session_tasks;
        GCancellable           *end_session_cancellable;

        GSettings              *settings;
        GSettings              *session_settings;
        GSettings              *lockdown_settings;

        GsmSystem              *system;
        GDBusConnection        *connection;
        GsmExportedManager     *skeleton;
        gboolean                dbus_disconnected : 1;

        GsmShell               *shell;
        gulong                  shell_end_session_dialog_canceled_id;
        gulong                  shell_end_session_dialog_open_failed_id;
        gulong                  shell_end_session_dialog_confirmed_logout_id;
        gulong                  shell_end_session_dialog_confirmed_shutdown_id;
        gulong                  shell_end_session_dialog_confirmed_reboot_id;
};

enum {
        PROP_0,
        PROP_SESSION_NAME,
};

static void     gsm_manager_class_init  (GsmManagerClass *klass);
static void     gsm_manager_init        (GsmManager      *manager);

static gboolean _log_out_is_locked_down     (GsmManager *manager);

static void     on_client_end_session_response (GsmClient  *client,
                                                gboolean    is_ok,
                                                const char *reason,
                                                GsmManager *manager);

static void     show_shell_end_session_dialog (GsmManager                   *manager,
                                               GsmShellEndSessionDialogType  type);
static gpointer manager_object = NULL;

G_DEFINE_TYPE (GsmManager, gsm_manager, G_TYPE_OBJECT)

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

        g_debug ("GsmManager: starting app '%s'", gsm_app_peek_app_id (app));

        res = gsm_app_start (app, &error);
        if (error != NULL) {
                g_warning ("Failed to start app: %s", error->message);
                g_clear_error (&error);
        }
        return res;
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
        gsm_store_foreach (manager->clients,
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

static const char *
phase_num_to_name (guint phase)
{
        switch (phase) {
        case GSM_MANAGER_PHASE_INITIALIZATION:
                return "INITIALIZATION";
        case GSM_MANAGER_PHASE_APPLICATION:
                return "APPLICATION";
        case GSM_MANAGER_PHASE_RUNNING:
                return "RUNNING";
        case GSM_MANAGER_PHASE_QUERY_END_SESSION:
                return "QUERY_END_SESSION";
        case GSM_MANAGER_PHASE_END_SESSION:
                return "END_SESSION";
        case GSM_MANAGER_PHASE_EXIT:
                return "EXIT";
        default:
                g_assert_not_reached ();
        }
}

static void start_phase (GsmManager *manager);

static void
gsm_manager_quit (GsmManager *manager)
{
        switch (manager->logout_type) {
        case GSM_MANAGER_LOGOUT_LOGOUT:
        case GSM_MANAGER_LOGOUT_NONE:
                gsm_quit ();
                break;
        case GSM_MANAGER_LOGOUT_REBOOT:
                gsm_system_complete_shutdown (manager->system);
                gsm_quit ();
                break;
        case GSM_MANAGER_LOGOUT_SHUTDOWN:
                gsm_system_complete_shutdown (manager->system);
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
        gboolean start_next_phase = TRUE;

        g_debug ("GsmManager: ending phase %s",
                 phase_num_to_name (manager->phase));

        g_slist_free (manager->query_clients);
        manager->query_clients = NULL;

        g_clear_handle_id (&manager->phase_timeout_id, g_source_remove);

        switch (manager->phase) {
        case GSM_MANAGER_PHASE_INITIALIZATION:
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
                manager->phase++;
                start_phase (manager);
        }
}

static gboolean
_start_app (const char *id,
            GsmApp     *app,
            GsmManager *manager)
{
        if (gsm_app_peek_is_disabled (app)) {
                g_debug ("GsmManager: Skipping disabled app: %s", id);
                goto out;
        }

        if (!start_app_or_warn (manager, app))
                goto out;

 out:
        return FALSE;
}

static void
do_phase_startup (GsmManager *manager)
{
        if (manager->session_save && gsm_session_save_restore (manager->session_save))
                g_info ("Restored from save, skipping normal autostart apps.");
        else
                gsm_store_foreach (manager->apps,
                                   (GsmStoreFunc)_start_app,
                                   manager);

        end_phase (manager);
}

typedef struct {
        GsmManager *manager;
        guint       flags;
} ClientEndSessionData;


static gboolean
_client_end_session (const char           *id,
                     GsmClient            *client,
                     ClientEndSessionData *data)
{
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
                data->manager->query_clients = g_slist_prepend (data->manager->query_clients, client);
        }

        return FALSE;
}

static void
complete_end_session_tasks (GsmManager *manager)
{
        GSList *l;

        for (l = manager->pending_end_session_tasks;
             l != NULL;
             l = l->next) {
                GTask *task = G_TASK (l->data);
                if (!g_task_return_error_if_cancelled (task))
                    g_task_return_boolean (task, TRUE);
        }

        g_slist_free_full (manager->pending_end_session_tasks,
                           (GDestroyNotify) g_object_unref);
        manager->pending_end_session_tasks = NULL;
}

static gboolean
on_end_session_timeout (GsmManager *manager)
{
        manager->phase_timeout_id = 0;

        for (GSList *l = manager->query_clients; l != NULL; l = l->next) {
                g_warning ("Client '%s' failed to reply before timeout",
                           gsm_client_peek_id (l->data));
        }

        end_phase (manager);
        return FALSE;
}

static void
do_phase_end_session (GsmManager *manager)
{
        ClientEndSessionData data;

        complete_end_session_tasks (manager);

        data.manager = manager;
        data.flags = GSM_CLIENT_END_SESSION_FLAG_NONE;

        if (manager->logout_mode == GSM_MANAGER_LOGOUT_MODE_FORCE) {
                data.flags |= GSM_CLIENT_END_SESSION_FLAG_FORCEFUL;
        }

        g_clear_handle_id (&manager->phase_timeout_id, g_source_remove);

        if (gsm_store_size (manager->clients) > 0) {
                manager->phase_timeout_id = g_timeout_add_seconds (10,
                                                                   (GSourceFunc)on_end_session_timeout,
                                                                   manager);

                gsm_store_foreach (manager->clients,
                                   (GsmStoreFunc)_client_end_session,
                                   &data);
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
do_phase_exit (GsmManager *manager)
{
        if (gsm_store_size (manager->clients) > 0) {
                gsm_store_foreach (manager->clients,
                                   (GsmStoreFunc)_client_stop,
                                   NULL);
        }

        end_phase (manager);
}

static gboolean
_client_query_end_session (const char           *id,
                           GsmClient            *client,
                           ClientEndSessionData *data)
{
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
                data->manager->query_clients = g_slist_prepend (data->manager->query_clients, client);
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
        GsmInhibitor *inhibitor;

        if (manager->logout_mode == GSM_MANAGER_LOGOUT_MODE_FORCE) {
                return FALSE;
        }

        if (manager->inhibitors == NULL) {
                return FALSE;
        }

        inhibitor = (GsmInhibitor *)gsm_store_find (manager->inhibitors,
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
        GsmInhibitor *inhibitor;

        if (manager->inhibitors == NULL) {
                return FALSE;
        }

        inhibitor = (GsmInhibitor *)gsm_store_find (manager->inhibitors,
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
        /* just ignore if received outside of shutdown */
        if (manager->phase < GSM_MANAGER_PHASE_QUERY_END_SESSION) {
                return;
        }

        /* switch back to running phase */
        g_debug ("GsmManager: Cancelling the end of session");

        g_cancellable_cancel (manager->end_session_cancellable);

        gsm_manager_set_phase (manager, GSM_MANAGER_PHASE_RUNNING);
        manager->logout_mode = GSM_MANAGER_LOGOUT_MODE_NORMAL;

        manager->logout_type = GSM_MANAGER_LOGOUT_NONE;

        /* clear all JIT inhibitors */
        gsm_store_foreach_remove (manager->inhibitors,
                                  (GsmStoreFunc)inhibitor_is_jit,
                                  (gpointer)manager);

        gsm_store_foreach (manager->clients,
                           (GsmStoreFunc)_client_cancel_end_session,
                           NULL);

        if (manager->session_save)
                gsm_session_save_unseal (manager->session_save);

        start_phase (manager);
}

static void
end_session_or_show_shell_dialog (GsmManager *manager)
{
        gboolean logout_prompt;
        GsmShellEndSessionDialogType type;
        gboolean logout_inhibited;

        switch (manager->logout_type) {
        case GSM_MANAGER_LOGOUT_LOGOUT:
                type = GSM_SHELL_END_SESSION_DIALOG_TYPE_LOGOUT;
                break;
        case GSM_MANAGER_LOGOUT_REBOOT:
                type = GSM_SHELL_END_SESSION_DIALOG_TYPE_RESTART;
                break;
        case GSM_MANAGER_LOGOUT_SHUTDOWN:
                type = GSM_SHELL_END_SESSION_DIALOG_TYPE_SHUTDOWN;
                break;
        default:
                g_warning ("Unexpected logout type %d when creating end session dialog",
                           manager->logout_type);
                type = GSM_SHELL_END_SESSION_DIALOG_TYPE_LOGOUT;
                break;
        }

        logout_inhibited = gsm_manager_is_logout_inhibited (manager);
        logout_prompt = g_settings_get_boolean (manager->settings,
                                                KEY_LOGOUT_PROMPT);

        switch (manager->logout_mode) {
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
        g_debug ("GsmManager: query end session complete");

        /* Remove the timeout since this can be called from outside the timer
         * and we don't want to have it called twice */
        g_clear_handle_id (&manager->phase_timeout_id, g_source_remove);

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
        guint32 cookie;

        do {
                cookie = generate_cookie ();
        } while (gsm_store_find (manager->inhibitors, (GsmStoreFunc)_find_by_cookie, &cookie) != NULL);

        return cookie;
}

static gboolean
_on_query_end_session_timeout (GsmManager *manager)
{
        GSList *l;

        manager->phase_timeout_id = 0;

        g_debug ("GsmManager: query end session timed out");

        for (l = manager->query_clients; l != NULL; l = l->next) {
                GsmInhibitor *inhibitor;

                g_warning ("Client '%s' failed to reply before timeout",
                           gsm_client_peek_id (l->data));

                /* Don't add "not responding" inhibitors if logout is forced
                 */
                if (manager->logout_mode == GSM_MANAGER_LOGOUT_MODE_FORCE) {
                        continue;
                }

                /* Add JIT inhibit for unresponsive client */
                inhibitor = gsm_inhibitor_new_for_client (gsm_client_peek_id (l->data),
                                                          gsm_client_peek_app_id (l->data),
                                                          GSM_INHIBITOR_FLAG_LOGOUT,
                                                          _("Not responding"),
                                                          gsm_client_peek_bus_name (l->data),
                                                          _generate_unique_cookie (manager));
                gsm_store_add (manager->inhibitors, gsm_inhibitor_peek_id (inhibitor), G_OBJECT (inhibitor));
                g_object_unref (inhibitor);
        }

        g_slist_free (manager->query_clients);
        manager->query_clients = NULL;

        query_end_session_complete (manager);

        return FALSE;
}

static void
do_phase_query_end_session (GsmManager *manager)
{
        ClientEndSessionData data;

        data.manager = manager;
        data.flags = GSM_CLIENT_END_SESSION_FLAG_NONE;

        if (manager->logout_mode == GSM_MANAGER_LOGOUT_MODE_FORCE) {
                data.flags |= GSM_CLIENT_END_SESSION_FLAG_FORCEFUL;
        }

        debug_clients (manager);
        g_debug ("GsmManager: sending query-end-session to clients (logout mode: %s)",
                 manager->logout_mode == GSM_MANAGER_LOGOUT_MODE_NORMAL? "normal" :
                 manager->logout_mode == GSM_MANAGER_LOGOUT_MODE_FORCE? "forceful":
                 "no confirmation");
        gsm_store_foreach (manager->clients,
                           (GsmStoreFunc)_client_query_end_session,
                           &data);

        manager->phase_timeout_id = g_timeout_add_seconds (1, (GSourceFunc)_on_query_end_session_timeout, manager);
}

static void
update_idle (GsmManager *manager)
{
        if (gsm_manager_is_idle_inhibited (manager)) {
                gsm_presence_set_idle_enabled (manager->presence, FALSE);
        } else {
                gsm_presence_set_idle_enabled (manager->presence, TRUE);
        }
}

static void
start_phase (GsmManager *manager)
{
        g_debug ("GsmManager: starting phase %s\n",
                 phase_num_to_name (manager->phase));

        /* reset state */
        g_slist_free (manager->query_clients);
        manager->query_clients = NULL;

        g_clear_handle_id (&manager->phase_timeout_id, g_source_remove);

        switch (manager->phase) {
        case GSM_MANAGER_PHASE_INITIALIZATION:
                sd_notify (0, "READY=1\nSTATUS=Waiting for session to start");
                break;
        case GSM_MANAGER_PHASE_APPLICATION:
                sd_notify (0, "STATUS=Starting applications");
                gsm_exported_manager_emit_session_running (manager->skeleton);
                do_phase_startup (manager);
                break;
        case GSM_MANAGER_PHASE_RUNNING:
                sd_notify (0, "STATUS=Running");
                sd_journal_send ("MESSAGE_ID=%s", GSM_MANAGER_STARTUP_SUCCEEDED_MSGID,
                                 "PRIORITY=%d", 5,
                                 "MESSAGE=Entering running state",
                                 NULL);
                if (manager->pending_end_session_tasks != NULL)
                        complete_end_session_tasks (manager);
                g_object_unref (manager->end_session_cancellable);
                manager->end_session_cancellable = g_cancellable_new ();
                update_idle (manager);
                break;
        case GSM_MANAGER_PHASE_QUERY_END_SESSION:
                sd_notify (0, "STATUS=Querying end of session");
                do_phase_query_end_session (manager);
                break;
        case GSM_MANAGER_PHASE_END_SESSION:
                sd_notify (0, "STOPPING=1\nSTATUS=Logging out");
                gsm_exported_manager_emit_session_over (manager->skeleton);
                do_phase_end_session (manager);
                break;
        case GSM_MANAGER_PHASE_EXIT:
                sd_notify (0, "STOPPING=1\nSTATUS=Quitting");
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
        g_debug ("GsmManager:\tapp-id:%s\tis-disabled:%d",
                 gsm_app_peek_app_id (app),
                 gsm_app_peek_is_disabled (app));

        return FALSE;
}

static void
debug_app_summary (GsmManager *manager)
{
        g_debug ("GsmManager: Autostart app summary");
        gsm_store_foreach (manager->apps,
                           (GsmStoreFunc)_debug_app_for_phase,
                           NULL);
}

void
gsm_manager_start (GsmManager *manager)
{
        g_debug ("GsmManager: Starting");

        g_return_if_fail (GSM_IS_MANAGER (manager));
        g_return_if_fail (manager->phase == GSM_MANAGER_PHASE_INITIALIZATION);

        debug_app_summary (manager);
        start_phase (manager);
}

void
_gsm_manager_set_active_session (GsmManager *manager,
                                 const char *session_name,
                                 gboolean    is_kiosk)
{
        g_free (manager->session_name);
        manager->session_name = g_strdup (session_name);

        if (!is_kiosk)
                manager->session_save = gsm_session_save_new (session_name);

        gsm_exported_manager_set_session_name (manager->skeleton, session_name);
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

static void
_disconnect_client (GsmManager *manager,
                    GsmClient  *client)
{
        g_debug ("GsmManager: disconnect client: %s (app: %s)",
                 gsm_client_peek_id (client),
                 gsm_client_peek_app_id (client));

        /* take a ref so it doesn't get finalized */
        g_object_ref (client);

        /* remove any inhibitors for this client */
        gsm_store_foreach_remove (manager->inhibitors,
                                  (GsmStoreFunc)inhibitor_has_client_id,
                                  (gpointer)gsm_client_peek_id (client));

        switch (manager->phase) {
        case GSM_MANAGER_PHASE_QUERY_END_SESSION:
                /* Instead of answering our end session query, the client just exited.
                 * Treat that as an "okay, end the session" answer.
                 *
                 * This call implicitly removes any inhibitors for the client, along
                 * with removing the client from the pending query list.
                 */
                on_client_end_session_response (client,
                                                TRUE,
                                                "Client exited in query end "
                                                "session phase instead of end "
                                                "session phase",
                                                manager);
                break;
        case GSM_MANAGER_PHASE_END_SESSION:
                if (! g_slist_find (manager->query_clients, client)) {
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
                on_client_end_session_response (client,
                                                TRUE,
                                                "Client exited in end session "
                                                "phase without sending "
                                                "EndSessionResponse",
                                                manager);
        default:
                /* do nothing */
                break;
        }

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

        /* If no service name, then we simply disconnect all clients */
        if (!data->service_name) {
                _disconnect_client (data->manager, client);
                return TRUE;
        }

        name = gsm_client_peek_bus_name (client);
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
        RemoveClientData data;

        data.service_name = service_name;
        data.manager = manager;

        /* disconnect dbus clients for name */
        gsm_store_foreach_remove (manager->clients,
                                  (GsmStoreFunc)_disconnect_dbus_client,
                                  &data);

        if (manager->phase >= GSM_MANAGER_PHASE_QUERY_END_SESSION
            && gsm_store_size (manager->clients) == 0) {
                g_debug ("GsmManager: last client disconnected - exiting");
                end_phase (manager);
        }
}

gboolean
gsm_manager_get_dbus_disconnected (GsmManager *manager)
{
        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);

        return manager->dbus_disconnected;
}

static void
on_client_disconnected (GsmClient  *client,
                        GsmManager *manager)
{
        g_debug ("GsmManager: disconnect client");
        _disconnect_client (manager, client);
        gsm_store_remove (manager->clients, gsm_client_peek_id (client));
        if (manager->phase >= GSM_MANAGER_PHASE_QUERY_END_SESSION
            && gsm_store_size (manager->clients) == 0) {
                g_debug ("GsmManager: last client disconnected - exiting");
                end_phase (manager);
        }
}

static void
on_client_end_session_response (GsmClient  *client,
                                gboolean    is_ok,
                                const char *reason,
                                GsmManager *manager)
{
        /* just ignore if received outside of shutdown */
        if (manager->phase < GSM_MANAGER_PHASE_QUERY_END_SESSION) {
                return;
        }

        g_debug ("GsmManager: Response from end session request: is-ok=%d reason=%s", is_ok, reason ?: "(none)");

        manager->query_clients = g_slist_remove (manager->query_clients, client);

        if (!is_ok && manager->logout_mode != GSM_MANAGER_LOGOUT_MODE_FORCE) {
                GsmInhibitor *inhibitor;

                /* Create JIT inhibit */
                inhibitor = gsm_inhibitor_new_for_client (gsm_client_peek_id (client),
                                                          gsm_client_peek_app_id (client),
                                                          GSM_INHIBITOR_FLAG_LOGOUT,
                                                          reason != NULL ? reason : _("Not responding"),
                                                          gsm_client_peek_bus_name (client),
                                                          _generate_unique_cookie (manager));
                gsm_store_add (manager->inhibitors, gsm_inhibitor_peek_id (inhibitor), G_OBJECT (inhibitor));
                g_object_unref (inhibitor);
        } else {
                gsm_store_foreach_remove (manager->inhibitors,
                                          (GsmStoreFunc)inhibitor_has_client_id,
                                          (gpointer)gsm_client_peek_id (client));
        }

        if (manager->phase == GSM_MANAGER_PHASE_QUERY_END_SESSION) {
                if (manager->query_clients == NULL)
                        query_end_session_complete (manager);
        } else if (manager->phase == GSM_MANAGER_PHASE_END_SESSION) {
                /* we can continue to the next step if all clients have replied
                 * and if there's no inhibitor */
                if (manager->query_clients != NULL || gsm_manager_is_logout_inhibited (manager))
                        return;

                end_phase (manager);
        }
}

static void
on_store_client_added (GsmStore   *store,
                       const char *id,
                       GsmManager *manager)
{
        GsmClient *client;

        g_debug ("GsmManager: Client added: %s", id);

        client = (GsmClient *)gsm_store_lookup (store, id);

        g_signal_connect (client,
                          "end-session-response",
                          G_CALLBACK (on_client_end_session_response),
                          manager);

        gsm_exported_manager_emit_client_added (manager->skeleton, id);
        /* FIXME: disconnect signal handler */
}

static void
on_store_client_removed (GsmStore   *store,
                         const char *id,
                         GsmManager *manager)
{
        g_debug ("GsmManager: Client removed: %s", id);

        gsm_exported_manager_emit_client_removed (manager->skeleton, id);
}

static void
gsm_manager_set_property (GObject       *object,
                          guint          prop_id,
                          const GValue  *value,
                          GParamSpec    *pspec)
{
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
gsm_manager_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
        GsmManager *self = GSM_MANAGER (object);

        switch (prop_id) {
        case PROP_SESSION_NAME:
                g_value_set_string (value, self->session_name);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
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
        if (manager->inhibited_actions == new_inhibited_actions)
                return;

        gsm_system_set_inhibitors (manager->system, new_inhibited_actions);

        manager->inhibited_actions = new_inhibited_actions;
        gsm_exported_manager_set_inhibited_actions (manager->skeleton,
                                                    manager->inhibited_actions);
}

static void
on_inhibitor_vanished (GsmInhibitor *inhibitor,
                       GsmManager   *manager)
{
        gsm_store_remove (manager->inhibitors, gsm_inhibitor_peek_id (inhibitor));
}

static void
on_store_inhibitor_added (GsmStore   *store,
                          const char *id,
                          GsmManager *manager)
{
        GsmInhibitor *i;
        GsmInhibitorFlag new_inhibited_actions;

        g_debug ("GsmManager: Inhibitor added: %s", id);

        i = GSM_INHIBITOR (gsm_store_lookup (store, id));

        new_inhibited_actions = manager->inhibited_actions | gsm_inhibitor_peek_flags (i);
        update_inhibited_actions (manager, new_inhibited_actions);

        g_signal_connect_object (i, "vanished", G_CALLBACK (on_inhibitor_vanished), manager, 0);

        gsm_exported_manager_emit_inhibitor_added (manager->skeleton, id);

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
        GsmInhibitorFlag new_inhibited_actions;

        g_debug ("GsmManager: Inhibitor removed: %s", id);

        new_inhibited_actions = 0;
        gsm_store_foreach (manager->inhibitors,
                           collect_inhibition_flags,
                           &new_inhibited_actions);
        update_inhibited_actions (manager, new_inhibited_actions);

        gsm_exported_manager_emit_inhibitor_removed (manager->skeleton, id);

        update_idle (manager);

        if (manager->phase >= GSM_MANAGER_PHASE_QUERY_END_SESSION) {
                end_session_or_show_shell_dialog (manager);
        }
}

static void
gsm_manager_dispose (GObject *object)
{
        GsmManager *manager = GSM_MANAGER (object);

        g_debug ("GsmManager: disposing manager");

        g_clear_object (&manager->end_session_cancellable);
        g_clear_pointer (&manager->session_name, g_free);

        if (manager->clients != NULL) {
                g_signal_handlers_disconnect_by_func (manager->clients,
                                                      on_store_client_added,
                                                      manager);
                g_signal_handlers_disconnect_by_func (manager->clients,
                                                      on_store_client_removed,
                                                      manager);
                g_object_unref (manager->clients);
                manager->clients = NULL;
        }

        g_clear_object (&manager->apps);

        if (manager->inhibitors != NULL) {
                g_signal_handlers_disconnect_by_func (manager->inhibitors,
                                                      on_store_inhibitor_added,
                                                      manager);
                g_signal_handlers_disconnect_by_func (manager->inhibitors,
                                                      on_store_inhibitor_removed,
                                                      manager);

                g_object_unref (manager->inhibitors);
                manager->inhibitors = NULL;
        }

        g_clear_object (&manager->presence);
        g_clear_object (&manager->settings);
        g_clear_object (&manager->session_settings);
        g_clear_object (&manager->lockdown_settings);
        g_clear_object (&manager->system);
        g_clear_object (&manager->shell);

        if (manager->skeleton != NULL) {
                g_dbus_interface_skeleton_unexport_from_connection (G_DBUS_INTERFACE_SKELETON (manager->skeleton),
                                                                    manager->connection);
                g_clear_object (&manager->skeleton);
        }

        g_clear_object (&manager->connection);

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

        g_object_class_install_property (object_class,
                                         PROP_SESSION_NAME,
                                         g_param_spec_string ("session-name",
                                                              NULL,
                                                              NULL,
                                                              NULL,
                                                              G_PARAM_READABLE));
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
                              GsmManager *manager)
{
        gboolean is_active;

        is_active = gsm_system_is_active (manager->system);

        g_debug ("emitting SessionIsActive");
        gsm_exported_manager_set_session_is_active (manager->skeleton, is_active);
}

static gboolean
_log_out_is_locked_down (GsmManager *manager)
{
        return g_settings_get_boolean (manager->lockdown_settings,
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

static gboolean
gsm_manager_shutdown (GsmExportedManager    *skeleton,
                      GDBusMethodInvocation *invocation,
                      GsmManager            *manager)
{
        GTask *task;

        g_debug ("GsmManager: Shutdown called");

        if (manager->phase < GSM_MANAGER_PHASE_RUNNING) {
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

        if (manager->session_save)
                gsm_session_save_seal (manager->session_save);

        task = g_task_new (manager, manager->end_session_cancellable, (GAsyncReadyCallback) complete_end_session_task, invocation);

        manager->pending_end_session_tasks = g_slist_prepend (manager->pending_end_session_tasks,
                                                                    task);

        g_debug ("GsmManager: requesting shutdown");

        manager->logout_type = GSM_MANAGER_LOGOUT_SHUTDOWN;
        end_phase (manager);

        return TRUE;
}

static gboolean
gsm_manager_reboot (GsmExportedManager    *skeleton,
                    GDBusMethodInvocation *invocation,
                    GsmManager            *manager)
{
        GTask *task;

        g_debug ("GsmManager: Reboot called");

        if (manager->phase < GSM_MANAGER_PHASE_RUNNING) {
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

        if (manager->session_save)
                gsm_session_save_seal (manager->session_save);

        task = g_task_new (manager, manager->end_session_cancellable, (GAsyncReadyCallback) complete_end_session_task, invocation);

        manager->pending_end_session_tasks = g_slist_prepend (manager->pending_end_session_tasks,
                                                                    task);

        g_debug ("GsmManager: requesting reboot");

        manager->logout_type = GSM_MANAGER_LOGOUT_REBOOT;
        end_phase (manager);

        return TRUE;
}

static gboolean
gsm_manager_can_shutdown (GsmExportedManager    *skeleton,
                          GDBusMethodInvocation *invocation,
                          GsmManager            *manager)
{
        gboolean shutdown_available;

        g_debug ("GsmManager: CanShutdown called");

        shutdown_available = !_log_out_is_locked_down (manager) &&
                (gsm_system_can_stop (manager->system)
                 || gsm_system_can_restart (manager->system)
                 || gsm_system_can_suspend (manager->system)
                 || gsm_system_can_hibernate (manager->system));

        gsm_exported_manager_complete_can_shutdown (skeleton, invocation, shutdown_available);

        return TRUE;
}

static gboolean
gsm_manager_can_reboot_to_firmware_setup (GsmExportedManager    *skeleton,
                                          GDBusMethodInvocation *invocation,
                                          GsmManager            *manager)
{
        gboolean reboot_to_firmware_available;

        g_debug ("GsmManager: CanRebootToFirmwareSetup called");

        reboot_to_firmware_available = !_log_out_is_locked_down (manager) &&
                gsm_system_can_restart_to_firmware_setup (manager->system);

        gsm_exported_manager_complete_can_reboot_to_firmware_setup (skeleton, invocation, reboot_to_firmware_available);

        return TRUE;
}

static gboolean
gsm_manager_set_reboot_to_firmware_setup (GsmExportedManager    *skeleton,
                                          GDBusMethodInvocation *invocation,
                                          gboolean               enable,
                                          GsmManager            *manager)
{
        g_debug ("GsmManager: SetRebootToFirmwareSetup called");

        gsm_system_set_restart_to_firmware_setup (manager->system, enable);

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
        if (manager->phase > GSM_MANAGER_PHASE_INITIALIZATION) {
                g_dbus_method_invocation_return_error (invocation,
                                                       GSM_MANAGER_ERROR,
                                                       GSM_MANAGER_ERROR_NOT_IN_INITIALIZATION,
                                                       "Setenv interface is only available during the Initialization phase");
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
        /* Signaled by helper when gnome-session-initialized.target is reached. */
        if (manager->systemd_initialized) {
                g_dbus_method_invocation_return_error (invocation,
                                                       GSM_MANAGER_ERROR,
                                                       GSM_MANAGER_ERROR_NOT_IN_INITIALIZATION,
                                                       "Systemd initialization was already signaled");
        } else if (manager->phase > GSM_MANAGER_PHASE_INITIALIZATION) {
                g_dbus_method_invocation_return_error (invocation,
                                                       GSM_MANAGER_ERROR,
                                                       GSM_MANAGER_ERROR_NOT_IN_INITIALIZATION,
                                                       "Initialized interface is only available during startup");
        } else {
                manager->systemd_initialized = TRUE;

                g_assert (manager->phase == GSM_MANAGER_PHASE_INITIALIZATION);
                manager->phase++;
                start_phase (manager);

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
        if (manager->phase != GSM_MANAGER_PHASE_INITIALIZATION) {
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

gboolean
gsm_manager_logout (GsmManager *manager,
                    guint logout_mode,
                    GError **error)
{
        if (manager->phase < GSM_MANAGER_PHASE_RUNNING) {
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
                g_debug ("GsmManager: requesting logout");
                break;
        case GSM_MANAGER_LOGOUT_MODE_NO_CONFIRMATION:
                g_debug ("GsmManager: requesting no-confirmation logout");
                break;
        case GSM_MANAGER_LOGOUT_MODE_FORCE:
                g_debug ("GsmManager: requesting forced logout");
                break;

        default:
                g_debug ("Unknown logout mode option");

                g_set_error (error,
                             GSM_MANAGER_ERROR,
                             GSM_MANAGER_ERROR_INVALID_OPTION,
                             "Unknown logout mode flag");
                return FALSE;
        }

        if (manager->session_save && logout_mode != GSM_MANAGER_LOGOUT_MODE_FORCE)
                gsm_session_save_seal (manager->session_save);

        manager->logout_mode = logout_mode;

        if (manager->phase >= GSM_MANAGER_PHASE_QUERY_END_SESSION) {
                /* Someone can upgrade a normal logout to a forced logout
                 * while we're busy prompting. In this case, re-evaluate */
                end_session_or_show_shell_dialog (manager);
                return TRUE;
        }

        manager->logout_type = GSM_MANAGER_LOGOUT_LOGOUT;
        end_phase (manager);

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
                             const char            *ignored, /* was startup_id */
                             GsmManager            *manager)
{
        const char *sender;
        GsmClient  *client;

        client = NULL;

        g_debug ("GsmManager: RegisterClient %s", app_id);

        if (manager->phase >= GSM_MANAGER_PHASE_QUERY_END_SESSION) {
                g_debug ("Unable to register client: shutting down");

                g_dbus_method_invocation_return_error (invocation,
                                                       GSM_MANAGER_ERROR,
                                                       GSM_MANAGER_ERROR_NOT_IN_RUNNING,
                                                       "Unable to register client");
                return TRUE;
        }

        sender = g_dbus_method_invocation_get_sender (invocation);
        client = gsm_client_new (app_id, sender);
        if (client == NULL) {
                g_debug ("Unable to create client");

                g_dbus_method_invocation_return_error (invocation,
                                                       GSM_MANAGER_ERROR,
                                                       GSM_MANAGER_ERROR_GENERAL,
                                                       "Unable to register client");
                return TRUE;
        }

        gsm_store_add (manager->clients, gsm_client_peek_id (client), G_OBJECT (client));
        g_object_unref (client); /* the store will own the ref */

        g_signal_connect (client,
                          "disconnected",
                          G_CALLBACK (on_client_disconnected),
                          manager);

        gsm_exported_manager_complete_register_client (skeleton, invocation, gsm_client_peek_id (client));

        return TRUE;
}

static gboolean
gsm_manager_unregister_client (GsmExportedManager    *skeleton,
                               GDBusMethodInvocation *invocation,
                               const char            *client_id,
                               GsmManager            *manager)
{
        g_debug ("GsmManager: UnregisterClient %s (ignoring)", client_id);
        gsm_exported_manager_complete_unregister_client (skeleton, invocation);
        return TRUE;
}

static gboolean
gsm_manager_register_restore (GsmExportedManager    *skeleton,
                              GDBusMethodInvocation *invocation,
                              const char            *app_id,
                              const char            *dbus_name,
                              GsmManager            *manager)
{
        GsmRestoreReason reason;
        char *instance_id = NULL;
        g_auto (GStrv) cleanup_ids = NULL;
        gboolean valid = TRUE;

        if (IS_STRING_EMPTY (dbus_name))
                dbus_name = g_dbus_method_invocation_get_sender (invocation);

        if (manager->session_save) {
                g_debug ("GsmManager: RegisterRestore %s", app_id);
                valid = gsm_session_save_register (manager->session_save, app_id,
                                                   dbus_name, &reason, &instance_id,
                                                   &cleanup_ids);
        } else {
                g_debug ("GsmManager: RegisterRestore %s (ignoring due to kiosk session)",
                         app_id);
                reason = GSM_RESTORE_REASON_PRISTINE;
                instance_id = "";
        }

        if (valid) {
                const char *const *cleanup = (const char**) cleanup_ids ?:
                                             (const char *[]) { NULL };
                gsm_exported_manager_complete_register_restore (skeleton, invocation,
                                                                reason, instance_id,
                                                                cleanup);
        } else
                g_dbus_method_invocation_return_error (invocation,
                                                       G_DBUS_ERROR,
                                                       G_DBUS_ERROR_INVALID_ARGS,
                                                       "Invalid app id specified");
        return TRUE;
}

static gboolean
gsm_manager_deleted_instance_ids (GsmExportedManager    *skeleton,
                                  GDBusMethodInvocation *invocation,
                                  const char            *app_id,
                                  const char *const     *ids,
                                  GsmManager            *manager)
{
        if (manager->session_save) {
                g_debug ("GsmManager: DeletedInstanceIds (%s, ...)", app_id);
                gsm_session_save_deleted_ids (manager->session_save, app_id, ids);
        } else {
                g_debug ("GsmManager: DeletedInstanceIds (ignoring due to kiosk session)");
        }

        gsm_exported_manager_complete_deleted_instance_ids (skeleton, invocation);
        return TRUE;
}

static gboolean
gsm_manager_unregister_restore (GsmExportedManager    *skeleton,
                                GDBusMethodInvocation *invocation,
                                const char            *app_id,
                                const char            *instance_id,
                                GsmManager            *manager)
{
        gboolean found;

        if (manager->session_save) {
                g_debug ("GsmManager: UnregisterRestore (%s, instance %s)", app_id, instance_id);
                found = gsm_session_save_unregister (manager->session_save, app_id, instance_id);
        } else {
                g_debug ("GsmManager: UnregisterRestore (ignoring due to kiosk session)");
                found = TRUE; /* Not an error for an app to try and use save/restore in a kiosk session */
        }

        if (found)
                gsm_exported_manager_complete_unregister_restore (skeleton, invocation);
        else
                g_dbus_method_invocation_return_error (invocation,
                                                       GSM_MANAGER_ERROR,
                                                       GSM_MANAGER_ERROR_NOT_REGISTERED,
                                                       "Provided instance not found");
        return TRUE;
}

static gboolean
gsm_manager_inhibit (GsmExportedManager    *skeleton,
                     GDBusMethodInvocation *invocation,
                     const char            *app_id,
                     guint                  unused, /* Was an X window ID */
                     const char            *reason,
                     guint                  flags,
                     GsmManager            *manager)
{
        const char *validation_error = NULL;
        GsmInhibitor *inhibitor;
        guint         cookie;

        g_debug ("GsmManager: Inhibit app_id=%s reason=%s flags=%u",
                 app_id, reason, flags);

        if (manager->logout_mode == GSM_MANAGER_LOGOUT_MODE_FORCE)
                validation_error = "Forced logout cannot be inhibited";
        else if (IS_STRING_EMPTY (reason))
                validation_error = "Reason not specified";
        else if (flags == 0)
                validation_error = "Invalid inhibit flags";

        if (validation_error != NULL) {
                g_debug ("GsmManager: Unable to inhibit: %s", validation_error);
                g_dbus_method_invocation_return_error (invocation,
                                                       GSM_MANAGER_ERROR,
                                                       GSM_MANAGER_ERROR_GENERAL,
                                                       "%s", validation_error);
                return TRUE;
        }

        cookie = _generate_unique_cookie (manager);
        inhibitor = gsm_inhibitor_new (app_id,
                                       flags,
                                       reason,
                                       g_dbus_method_invocation_get_sender (invocation),
                                       cookie);
        gsm_store_add (manager->inhibitors, gsm_inhibitor_peek_id (inhibitor), G_OBJECT (inhibitor));
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
        GsmInhibitor *inhibitor;

        g_debug ("GsmManager: Uninhibit %u", cookie);

        inhibitor = (GsmInhibitor *)gsm_store_find (manager->inhibitors,
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

        g_debug ("GsmManager: removing inhibitor %s reason '%s' %u connection %s",
                 gsm_inhibitor_peek_app_id (inhibitor),
                 gsm_inhibitor_peek_reason (inhibitor),
                 gsm_inhibitor_peek_flags (inhibitor),
                 gsm_inhibitor_peek_bus_name (inhibitor));

        gsm_store_remove (manager->inhibitors, gsm_inhibitor_peek_id (inhibitor));

        gsm_exported_manager_complete_uninhibit (skeleton, invocation);

        return TRUE;
}

static gboolean
gsm_manager_is_inhibited (GsmExportedManager    *skeleton,
                          GDBusMethodInvocation *invocation,
                          guint                  flags,
                          GsmManager            *manager)
{
        GsmInhibitor *inhibitor;
        gboolean is_inhibited;

        if (manager->inhibitors == NULL
            || gsm_store_size (manager->inhibitors) == 0) {
                is_inhibited = FALSE;
        } else {
                inhibitor = (GsmInhibitor *) gsm_store_find (manager->inhibitors,
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
gsm_manager_get_inhibitors (GsmExportedManager    *skeleton,
                            GDBusMethodInvocation *invocation,
                            GsmManager            *manager)
{
        GPtrArray *inhibitors;

        inhibitors = g_ptr_array_new_with_free_func (g_free);
        gsm_store_foreach (manager->inhibitors,
                           (GsmStoreFunc) listify_store_ids,
                           &inhibitors);
        g_ptr_array_add (inhibitors, NULL);

        gsm_exported_manager_complete_get_inhibitors (skeleton, invocation,
                                                      (const gchar * const *) inhibitors->pdata);
        g_ptr_array_unref (inhibitors);

        return TRUE;
}

static gboolean
gsm_manager_is_session_running (GsmExportedManager    *skeleton,
                                GDBusMethodInvocation *invocation,
                                GsmManager            *manager)
{
        gboolean running;

        running = (manager->phase == GSM_MANAGER_PHASE_APPLICATION ||
                   manager->phase == GSM_MANAGER_PHASE_RUNNING ||
                   manager->phase == GSM_MANAGER_PHASE_QUERY_END_SESSION);

        gsm_exported_manager_complete_is_session_running (skeleton, invocation, running);
        return TRUE;
}

static void
on_session_connection_closed (GDBusConnection *connection,
                              gboolean remote_peer_vanished,
                              GError *error,
                              gpointer user_data)
{
        GsmManager *manager = GSM_MANAGER (user_data);
        g_debug ("GsmManager: dbus disconnected; disconnecting dbus clients...");
        manager->dbus_disconnected = TRUE;
        remove_clients_for_connection (manager, NULL);
}

static gboolean
register_manager (GsmManager *manager)
{
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
        g_signal_connect (skeleton, "handle-get-inhibitors",
                          G_CALLBACK (gsm_manager_get_inhibitors), manager);
        g_signal_connect (skeleton, "handle-get-locale",
                          G_CALLBACK (gsm_manager_get_locale), manager);
        g_signal_connect (skeleton, "handle-inhibit",
                          G_CALLBACK (gsm_manager_inhibit), manager);
        g_signal_connect (skeleton, "handle-initialization-error",
                          G_CALLBACK (gsm_manager_initialization_error), manager);
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
        g_signal_connect (skeleton, "handle-register-restore",
                          G_CALLBACK (gsm_manager_register_restore), manager);
        g_signal_connect (skeleton, "handle-deleted-instance-ids",
                          G_CALLBACK (gsm_manager_deleted_instance_ids), manager);
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
        g_signal_connect (skeleton, "handle-unregister-restore",
                          G_CALLBACK (gsm_manager_unregister_restore), manager);

        manager->dbus_disconnected = FALSE;
        g_signal_connect (connection, "closed",
                          G_CALLBACK (on_session_connection_closed), manager);

        manager->connection = connection;
        manager->skeleton = skeleton;

        g_signal_connect (manager->system, "notify::active",
                          G_CALLBACK (on_gsm_system_active_changed), manager);

        /* cold-plug SessionIsActive */
        on_gsm_system_active_changed (manager->system, NULL, manager);

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
        manager->settings = g_settings_new (GSM_MANAGER_SCHEMA);
        manager->session_settings = g_settings_new (SESSION_SCHEMA);
        manager->lockdown_settings = g_settings_new (LOCKDOWN_SCHEMA);

        manager->clients = gsm_store_new ();
        g_signal_connect (manager->clients,
                          "added",
                          G_CALLBACK (on_store_client_added),
                          manager);
        g_signal_connect (manager->clients,
                          "removed",
                          G_CALLBACK (on_store_client_removed),
                          manager);

        manager->inhibitors = gsm_store_new ();
        g_signal_connect (manager->inhibitors,
                          "added",
                          G_CALLBACK (on_store_inhibitor_added),
                          manager);
        g_signal_connect (manager->inhibitors,
                          "removed",
                          G_CALLBACK (on_store_inhibitor_removed),
                          manager);

        manager->apps = gsm_store_new ();

        manager->presence = gsm_presence_new ();
        g_signal_connect (manager->presence,
                          "status-changed",
                          G_CALLBACK (on_presence_status_changed),
                          manager);

        g_settings_bind_with_mapping (manager->session_settings,
                                      KEY_IDLE_DELAY,
                                      manager->presence,
                                      "idle-timeout",
                                      G_SETTINGS_BIND_GET,
                                      idle_timeout_get_mapping,
                                      NULL,
                                      NULL, NULL);

        manager->system = gsm_get_system ();
        manager->shell = gsm_get_shell ();
        manager->end_session_cancellable = g_cancellable_new ();
}

GsmManager *
gsm_manager_get (void)
{
        return manager_object;
}

GsmManager *
gsm_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                gboolean res;

                manager_object = g_object_new (GSM_TYPE_MANAGER, NULL);

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
        g_clear_signal_handler (&manager->shell_end_session_dialog_canceled_id,
                                manager->shell);
        g_clear_signal_handler (&manager->shell_end_session_dialog_open_failed_id,
                                manager->shell);
        g_clear_signal_handler (&manager->shell_end_session_dialog_confirmed_logout_id,
                                manager->shell);
        g_clear_signal_handler (&manager->shell_end_session_dialog_confirmed_shutdown_id,
                                manager->shell);
        g_clear_signal_handler (&manager->shell_end_session_dialog_confirmed_reboot_id,
                                manager->shell);
}

static void
on_shell_end_session_dialog_canceled (GsmShell   *shell,
                                      GsmManager *manager)
{
        cancel_end_session (manager);
        disconnect_shell_dialog_signals (manager);
}

static void
handle_end_session_dialog_confirmation (GsmManager           *manager,
                                        GsmManagerLogoutType  logout_type)
{
        disconnect_shell_dialog_signals (manager);

        if (manager->phase >= GSM_MANAGER_PHASE_END_SESSION)
                return; /* Already logging out, nothing to do */

        if (manager->logout_type != logout_type) {
                g_warning ("GsmManager: Shell confirmed unexpected logout type");
                return; /* Make it obvious that something went wrong */
        }

        manager->logout_mode = GSM_MANAGER_LOGOUT_MODE_FORCE;
        end_phase (manager);
}

static void
on_shell_end_session_dialog_confirmed_logout (GsmShell   *shell,
                                              GsmManager *manager)
{
        handle_end_session_dialog_confirmation (manager, GSM_MANAGER_LOGOUT_LOGOUT);
}

static void
on_shell_end_session_dialog_confirmed_shutdown (GsmShell   *shell,
                                                GsmManager *manager)
{
        handle_end_session_dialog_confirmation (manager, GSM_MANAGER_LOGOUT_SHUTDOWN);
}

static void
on_shell_end_session_dialog_confirmed_reboot (GsmShell   *shell,
                                              GsmManager *manager)
{
        handle_end_session_dialog_confirmation (manager, GSM_MANAGER_LOGOUT_REBOOT);
}

static void
connect_shell_dialog_signals (GsmManager *manager)
{
        if (manager->shell_end_session_dialog_canceled_id != 0)
                return;

        manager->shell_end_session_dialog_canceled_id =
                g_signal_connect (manager->shell,
                                  "end-session-dialog-canceled",
                                  G_CALLBACK (on_shell_end_session_dialog_canceled),
                                  manager);

        manager->shell_end_session_dialog_open_failed_id =
                g_signal_connect (manager->shell,
                                  "end-session-dialog-open-failed",
                                  G_CALLBACK (on_shell_end_session_dialog_canceled),
                                  manager);

        manager->shell_end_session_dialog_confirmed_logout_id =
                g_signal_connect (manager->shell,
                                  "end-session-dialog-confirmed-logout",
                                  G_CALLBACK (on_shell_end_session_dialog_confirmed_logout),
                                  manager);

        manager->shell_end_session_dialog_confirmed_shutdown_id =
                g_signal_connect (manager->shell,
                                  "end-session-dialog-confirmed-shutdown",
                                  G_CALLBACK (on_shell_end_session_dialog_confirmed_shutdown),
                                  manager);

        manager->shell_end_session_dialog_confirmed_reboot_id =
                g_signal_connect (manager->shell,
                                  "end-session-dialog-confirmed-reboot",
                                  G_CALLBACK (on_shell_end_session_dialog_confirmed_reboot),
                                  manager);
}

static void
show_shell_end_session_dialog (GsmManager                   *manager,
                               GsmShellEndSessionDialogType  type)
{
        if (!gsm_shell_is_running (manager->shell))
                return;

        connect_shell_dialog_signals (manager);

        gsm_shell_open_end_session_dialog (manager->shell,
                                           type,
                                           manager->inhibitors);
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
        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);
        manager->phase = phase;
        return (TRUE);
}

static void
append_app (GsmManager *manager,
            GsmApp     *app)
{
        const char *app_id;
        GsmApp     *dup;

        app_id = gsm_app_peek_app_id (app);
        if (IS_STRING_EMPTY (app_id)) {
                g_debug ("GsmManager: not adding app: no app-id");
                return;
        }

        dup = (GsmApp *) gsm_store_lookup (manager->apps, app_id);
        if (dup != NULL) {
                g_debug ("GsmManager: not adding app: app-id '%s' already exists", app_id);
                return;
        }

        gsm_store_add (manager->apps, app_id, G_OBJECT (app));
}

gboolean
gsm_manager_add_autostart_app (GsmManager *manager,
                               const char *path)
{
        GsmApp  *app;
        GError *error = NULL;

        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);
        g_return_val_if_fail (path != NULL, FALSE);

        app = gsm_app_new_for_path (path, &error);
        if (app == NULL) {
                g_warning ("%s", error->message);
                g_clear_error (&error);
                return FALSE;
        }

        g_debug ("GsmManager: read %s", path);
        append_app (manager, app);
        g_object_unref (app);

        return TRUE;
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
        g_debug ("GsmManager: on_shutdown_prepared, success: %d", success);
        g_signal_handlers_disconnect_by_func (system, on_shutdown_prepared, manager);

        if (success) {
                /* move to end-session phase */
                g_assert (manager->phase == GSM_MANAGER_PHASE_QUERY_END_SESSION);
                manager->phase++;
                start_phase (manager);
        } else {
                disconnect_shell_dialog_signals (manager);
                gsm_shell_close_end_session_dialog (manager->shell);
                /* back to running phase */
                cancel_end_session (manager);
        }
}

static gboolean
do_query_end_session_exit (GsmManager *manager)
{
        if (manager->logout_type != GSM_MANAGER_LOGOUT_SHUTDOWN &&
            manager->logout_type != GSM_MANAGER_LOGOUT_REBOOT)
                return TRUE; /* Continue to end session phase */

        g_signal_connect (manager->system, "shutdown-prepared",
                          G_CALLBACK (on_shutdown_prepared), manager);
        gsm_system_prepare_shutdown (manager->system,
                                     manager->logout_type == GSM_MANAGER_LOGOUT_REBOOT);

        return FALSE; /* don't leave query end session yet */
}
