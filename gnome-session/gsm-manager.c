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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <gtk/gtk.h> /* for logout dialog */
#include <gconf/gconf-client.h>

#include "gsm-manager.h"
#include "gsm-manager-glue.h"

#include "gsm-client-store.h"
#include "gsm-inhibitor-store.h"
#include "gsm-inhibitor.h"

#include "gsm-xsmp-client.h"
#include "gsm-method-client.h"
#include "gsm-service-client.h"

#include "gsm-autostart-app.h"
#include "gsm-resumed-app.h"

#include "util.h"
#include "gdm.h"
#include "gsm-logout-dialog.h"
#include "gsm-logout-inhibit-dialog.h"
#include "gsm-consolekit.h"
#include "gsm-power-manager.h"

#define GSM_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSM_TYPE_MANAGER, GsmManagerPrivate))

#define GSM_MANAGER_DBUS_PATH "/org/gnome/SessionManager"
#define GSM_MANAGER_DBUS_NAME "org.gnome.SessionManager"

#define GSM_MANAGER_PHASE_TIMEOUT 10 /* seconds */

#define GSM_GCONF_DEFAULT_SESSION_KEY           "/desktop/gnome/session/default-session"
#define GSM_GCONF_REQUIRED_COMPONENTS_DIRECTORY "/desktop/gnome/session/required-components"

#define GDM_FLEXISERVER_COMMAND "gdmflexiserver"
#define GDM_FLEXISERVER_ARGS    "--startnew Standard"

struct GsmManagerPrivate
{
        gboolean                failsafe;
        GsmClientStore         *store;
        GsmInhibitorStore      *inhibitors;

        /* Startup/resumed apps */
        GHashTable             *apps_by_id;
        /* Current status */
        GsmManagerPhase         phase;
        guint                   timeout_id;
        GSList                 *pending_apps;

        GtkWidget              *inhibit_dialog;

        /* List of clients which were disconnected due to disabled condition
         * and shouldn't be automatically restarted */
        GSList                 *condition_clients;

        DBusGProxy             *bus_proxy;
        DBusGConnection        *connection;
};

enum {
        PROP_0,
        PROP_CLIENT_STORE,
        PROP_FAILSAFE,
};

enum {
        PHASE_CHANGED,
        SESSION_RUNNING,
        SESSION_OVER,
        SESSION_OVER_NOTICE,
        CLIENT_ADDED,
        CLIENT_REMOVED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gsm_manager_class_init  (GsmManagerClass *klass);
static void     gsm_manager_init        (GsmManager      *manager);
static void     gsm_manager_finalize    (GObject         *object);

static gpointer manager_object = NULL;

G_DEFINE_TYPE (GsmManager, gsm_manager, G_TYPE_OBJECT)

GQuark
gsm_manager_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("gsm_manager_error");
        }

        return ret;
}

#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
gsm_manager_error_get_type (void)
{
        static GType etype = 0;

        if (etype == 0) {
                static const GEnumValue values[] = {
                        ENUM_ENTRY (GSM_MANAGER_ERROR_GENERAL, "GeneralError"),
                        ENUM_ENTRY (GSM_MANAGER_ERROR_NOT_IN_INITIALIZATION, "NotInInitialization"),
                        ENUM_ENTRY (GSM_MANAGER_ERROR_NOT_IN_RUNNING, "NotInRunning"),
                        ENUM_ENTRY (GSM_MANAGER_ERROR_ALREADY_REGISTERED, "AlreadyRegistered"),
                        ENUM_ENTRY (GSM_MANAGER_ERROR_NOT_REGISTERED, "NotRegistered"),
                        { 0, 0, 0 }
                };

                g_assert (GSM_MANAGER_NUM_ERRORS == G_N_ELEMENTS (values) - 1);

                etype = g_enum_register_static ("GsmManagerError", values);
        }

        return etype;
}

static gboolean
_find_by_client_id (const char *id,
                    GsmClient  *client,
                    const char *client_id_a)
{
        const char *client_id_b;

        client_id_b = gsm_client_get_client_id (client);
        if (client_id_b == NULL) {
                return FALSE;
        }

        return (strcmp (client_id_a, client_id_b) == 0);
}

static void
app_condition_changed (GsmApp     *app,
                       gboolean    condition,
                       GsmManager *manager)
{
        GsmClient *client;

        client = gsm_client_store_find (manager->priv->store,
                                        (GsmClientStoreFunc)_find_by_client_id,
                                        (char *)gsm_app_get_client_id (app));

        if (condition) {
                if (!gsm_app_is_running (app) && client == NULL) {
                        GError  *error;
                        gboolean res;

                        error = NULL;
                        res = gsm_app_start (app, &error);
                        if (error != NULL) {
                                g_warning ("Not able to start app from its condition: %s",
                                           error->message);
                                g_error_free (error);
                        }
                }
        } else {
                GError  *error;
                gboolean res;

                /* Kill client in case condition if false and make sure it won't
                 * be automatically restarted by adding the client to
                 * condition_clients */
                manager->priv->condition_clients = g_slist_prepend (manager->priv->condition_clients, client);

                error = NULL;
                res = gsm_client_stop (client, &error);
                if (error != NULL) {
                        g_warning ("Not able to stop app from its condition: %s",
                                   error->message);
                        g_error_free (error);
                }
        }
}

static void start_phase (GsmManager *manager);

static void
end_phase (GsmManager *manager)
{
        g_slist_free (manager->priv->pending_apps);
        manager->priv->pending_apps = NULL;

        g_debug ("GsmManager: ending phase %d\n", manager->priv->phase);

        manager->priv->phase++;

        if (manager->priv->phase < GSM_MANAGER_PHASE_RUNNING) {
                start_phase (manager);
        }

        if (manager->priv->phase == GSM_MANAGER_PHASE_RUNNING) {
                g_signal_emit (manager, signals[SESSION_RUNNING], 0);
        }
}

static void
app_registered (GsmApp     *app,
                GsmManager *manager)
{
        manager->priv->pending_apps = g_slist_remove (manager->priv->pending_apps, app);
        g_signal_handlers_disconnect_by_func (app, app_registered, manager);

        if (manager->priv->pending_apps == NULL) {
                if (manager->priv->timeout_id > 0) {
                        g_source_remove (manager->priv->timeout_id);
                        manager->priv->timeout_id = 0;
                }

                end_phase (manager);
        }
}

static gboolean
phase_timeout (GsmManager *manager)
{
        GSList *a;

        manager->priv->timeout_id = 0;

        for (a = manager->priv->pending_apps; a; a = a->next) {
                g_warning ("Application '%s' failed to register before timeout",
                           gsm_app_get_id (a->data));
                g_signal_handlers_disconnect_by_func (a->data, app_registered, manager);
                /* FIXME: what if the app was filling in a required slot? */
        }

        end_phase (manager);
        return FALSE;
}

static void
on_app_exited (GsmApp     *app,
               GsmManager *manager)
{
        if (manager->priv->phase == GSM_MANAGER_PHASE_INITIALIZATION) {
                /* Applications from Initialization phase are considered
                 * registered when they exit normally. This is because
                 * they are expected to just do "something" and exit */
                app_registered (app, manager);
        }
}

static void
_start_app (const char *id,
            GsmApp     *app,
            GsmManager *manager)
{
        GError  *error;
        gboolean res;

        if (gsm_app_get_phase (app) != manager->priv->phase) {
                return;
        }

        /* Keep track of app autostart condition in order to react
         * accordingly in the future. */
        g_signal_connect (app,
                          "condition-changed",
                          G_CALLBACK (app_condition_changed),
                          manager);

        if (gsm_app_is_disabled (app)) {
                g_debug ("GsmManager: Skipping disabled app: %s", id);
                return;
        }

        error = NULL;
        res = gsm_app_start (app, &error);
        if (!res) {
                if (error != NULL) {
                        g_warning ("Could not launch application '%s': %s",
                                   gsm_app_get_id (app),
                                   error->message);
                        g_error_free (error);
                        error = NULL;
                }
                return;
        }

        g_signal_connect (app,
                          "exited",
                          G_CALLBACK (on_app_exited),
                          manager);

        if (manager->priv->phase < GSM_MANAGER_PHASE_APPLICATION) {
                g_signal_connect (app,
                                  "registered",
                                  G_CALLBACK (app_registered),
                                  manager);
                manager->priv->pending_apps = g_slist_prepend (manager->priv->pending_apps, app);
        }
}

static void
start_phase (GsmManager *manager)
{

        g_debug ("GsmManager: starting phase %d\n", manager->priv->phase);

        g_slist_free (manager->priv->pending_apps);
        manager->priv->pending_apps = NULL;

        g_hash_table_foreach (manager->priv->apps_by_id,
                              (GHFunc)_start_app,
                              manager);

        if (manager->priv->pending_apps != NULL) {
                if (manager->priv->phase < GSM_MANAGER_PHASE_APPLICATION) {
                        manager->priv->timeout_id = g_timeout_add_seconds (GSM_MANAGER_PHASE_TIMEOUT,
                                                                           (GSourceFunc)phase_timeout,
                                                                           manager);
                }
        } else {
                end_phase (manager);
        }
}

void
gsm_manager_start (GsmManager *manager)
{
        g_debug ("GsmManager: GSM starting to manage");

        manager->priv->phase = GSM_MANAGER_PHASE_INITIALIZATION;

        start_phase (manager);
}

static GsmApp *
find_app_for_app_id (GsmManager *manager,
                     const char *app_id)
{
        GsmApp *app;
        app = g_hash_table_lookup (manager->priv->apps_by_id, app_id);
        return app;
}

static void
disconnect_client (GsmManager *manager,
                   GsmClient  *client)
{
        gboolean    is_condition_client;
        GsmApp     *app;
        GError     *error;
        gboolean    res;
        const char *app_id;

        g_debug ("GsmManager: disconnect client");

        /* take a ref so it doesn't get finalized */
        g_object_ref (client);

        gsm_client_store_remove (manager->priv->store, client);

        is_condition_client = FALSE;
        if (g_slist_find (manager->priv->condition_clients, client)) {
                manager->priv->condition_clients = g_slist_remove (manager->priv->condition_clients, client);

                is_condition_client = TRUE;
        }

        app_id = gsm_client_get_app_id (client);
        if (app_id == NULL) {
                g_debug ("GsmManager: no application associated with client, not restarting application");
                goto out;
        }

        g_debug ("GsmManager: disconnect for app '%s'", app_id);
        app = find_app_for_app_id (manager, app_id);
        if (app == NULL) {
                g_debug ("GsmManager: invalid application id, not restarting application");
                goto out;
        }

        if (manager->priv->phase == GSM_MANAGER_PHASE_SHUTDOWN) {
                g_debug ("GsmManager: in shutdown, not restarting application");
                goto out;
        }

        if (! gsm_app_get_autorestart (app)) {
                g_debug ("GsmManager: autorestart not set, not restarting application");
                goto out;
        }

        if (is_condition_client) {
                g_debug ("GsmManager: app conditionally disabled, not restarting application");
                goto out;
        }

        g_debug ("GsmManager: restarting app");

        error = NULL;
        res = gsm_app_restart (app, &error);
        if (error != NULL) {
                g_warning ("Error on restarting session managed app: %s", error->message);
                g_error_free (error);
        }

 out:
        g_object_unref (client);

        if (manager->priv->phase == GSM_MANAGER_PHASE_SHUTDOWN
            && gsm_client_store_size (manager->priv->store) == 0) {
                gtk_main_quit ();
        }
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

        name = gsm_dbus_client_get_bus_name (GSM_DBUS_CLIENT (client));
        if (name == NULL) {
                return FALSE;
        }

        if (strcmp (data->service_name, name) == 0) {
                disconnect_client (data->manager, client);
        }

        return FALSE;
}

static void
remove_clients_for_connection (GsmManager *manager,
                               const char *service_name)
{
        RemoveClientData data;

        data.service_name = service_name;
        data.manager = manager;

        /* disconnect dbus clients for name */
        gsm_client_store_foreach (manager->priv->store,
                                  (GsmClientStoreFunc)_disconnect_dbus_client,
                                  &data);
}

static gboolean
inhibitor_has_bus_name (gpointer      key,
                        GsmInhibitor *inhibitor,
                        const char   *bus_name_a)
{
        gboolean    matches;
        const char *bus_name_b;

        bus_name_b = gsm_inhibitor_get_bus_name (inhibitor);

        matches = FALSE;
        if (bus_name_a != NULL && bus_name_b != NULL) {
                matches = (strcmp (bus_name_a, bus_name_b) == 0);
                if (matches) {
                        g_debug ("GsmManager: removing inhibitor from %s for reason '%s' on connection %s",
                                 gsm_inhibitor_get_app_id (inhibitor),
                                 gsm_inhibitor_get_reason (inhibitor),
                                 gsm_inhibitor_get_bus_name (inhibitor));
                }
        }

        return matches;
}

static void
remove_inhibitors_for_connection (GsmManager *manager,
                                  const char *service_name)
{
        guint n_removed;

        g_debug ("GsmManager: removing inhibitors for bus name");

        n_removed = gsm_inhibitor_store_foreach_remove (manager->priv->inhibitors,
                                                        (GsmInhibitorStoreFunc)inhibitor_has_bus_name,
                                                        (gpointer)service_name);
}

static gboolean
_app_has_client_id (const char *id,
                    GsmApp     *app,
                    const char *client_id_a)
{
        const char *client_id_b;

        client_id_b = gsm_app_get_client_id (app);

        if (client_id_b == NULL) {
                return FALSE;
        }

        return (strcmp (client_id_a, client_id_b) == 0);
}

static GsmApp *
find_app_for_client_id (GsmManager *manager,
                        const char *client_id)
{
        GsmApp *found_app;
        GSList *a;

        found_app = NULL;

        /* If we're starting up the session, try to match the new client
         * with one pending apps for the current phase. If not, try to match
         * with any of the autostarted apps. */
        if (manager->priv->phase < GSM_MANAGER_PHASE_APPLICATION) {
                for (a = manager->priv->pending_apps; a != NULL; a = a->next) {
                        GsmApp *app = GSM_APP (a->data);

                        if (strcmp (client_id, gsm_app_get_client_id (app)) == 0) {
                                found_app = app;
                                goto out;
                        }
                }
        } else {
                GsmApp *app;

                app = g_hash_table_find (manager->priv->apps_by_id,
                                         (GHRFunc)_app_has_client_id,
                                         (char *)client_id);
                if (app != NULL) {
                        found_app = app;
                        goto out;
                }
        }
 out:
        return found_app;
}

static void
register_client_for_name (GsmManager *manager,
                          const char *dbus_name)
{
        GsmApp    *app;
        GsmClient *client;

        app = find_app_for_client_id (manager, dbus_name);
        if (app == NULL) {
                return;
        }

        client = gsm_service_client_new (dbus_name);
        if (client == NULL) {
                g_warning ("GsmManager: Unable to create client for name '%s'", dbus_name);
                return;
        }

        gsm_client_store_add (manager->priv->store, client);

        gsm_client_set_app_id (client, gsm_app_get_id (app));
        gsm_app_registered (app);

        gsm_client_set_status (client, GSM_CLIENT_REGISTERED);
}

static void
bus_name_owner_changed (DBusGProxy  *bus_proxy,
                        const char  *service_name,
                        const char  *old_service_name,
                        const char  *new_service_name,
                        GsmManager  *manager)
{
        if (strlen (new_service_name) == 0
            && strlen (old_service_name) > 0) {
                /* service removed */
                remove_clients_for_connection (manager, old_service_name);
                remove_inhibitors_for_connection (manager, old_service_name);
        } else if (strlen (old_service_name) == 0
                   && strlen (new_service_name) > 0) {
                /* service added */
                if (new_service_name[0] == '/') {
                        register_client_for_name (manager, new_service_name);
                }
        }
}

static gboolean
register_manager (GsmManager *manager)
{
        GError *error = NULL;

        error = NULL;
        manager->priv->connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
        if (manager->priv->connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting session bus: %s", error->message);
                        g_error_free (error);
                }
                exit (1);
        }

        manager->priv->bus_proxy = dbus_g_proxy_new_for_name (manager->priv->connection,
                                                              DBUS_SERVICE_DBUS,
                                                              DBUS_PATH_DBUS,
                                                              DBUS_INTERFACE_DBUS);
        dbus_g_proxy_add_signal (manager->priv->bus_proxy,
                                 "NameOwnerChanged",
                                 G_TYPE_STRING,
                                 G_TYPE_STRING,
                                 G_TYPE_STRING,
                                 G_TYPE_INVALID);
        dbus_g_proxy_connect_signal (manager->priv->bus_proxy,
                                     "NameOwnerChanged",
                                     G_CALLBACK (bus_name_owner_changed),
                                     manager,
                                     NULL);

        dbus_g_connection_register_g_object (manager->priv->connection, GSM_MANAGER_DBUS_PATH, G_OBJECT (manager));

        return TRUE;
}

static void
gsm_manager_set_failsafe (GsmManager *manager,
                          gboolean    enabled)
{
        g_return_if_fail (GSM_IS_MANAGER (manager));

        manager->priv->failsafe = enabled;
}

static gboolean
_client_has_client_id (const char *id,
                       GsmClient  *client,
                       const char *client_id_a)
{
        const char *client_id_b;

        client_id_b = gsm_client_get_client_id (client);

        if (client_id_b == NULL) {
                return FALSE;
        }

        return (strcmp (client_id_a, client_id_b) == 0);
}

static void
on_client_disconnected (GsmClient  *client,
                        GsmManager *manager)
{
        g_debug ("GsmManager: disconnect client");
        disconnect_client (manager, client);
}

static gboolean
on_xsmp_client_register_request (GsmXSMPClient *client,
                                 char         **id,
                                 GsmManager    *manager)
{
        gboolean handled;
        char    *new_id;
        GsmApp  *app;

        handled = TRUE;
        new_id = NULL;

        if (manager->priv->phase == GSM_MANAGER_PHASE_SHUTDOWN) {
                goto out;
        }

        if (*id == NULL) {
                new_id = gsm_util_generate_client_id ();
        } else {
                GsmClient *client;

                client = gsm_client_store_find (manager->priv->store,
                                                (GsmClientStoreFunc)_client_has_client_id,
                                                *id);
                /* We can't have two clients with the same id. */
                if (client != NULL) {
                        goto out;
                }

                new_id = g_strdup (*id);
        }

        g_debug ("GsmManager: Adding new client %s to session", new_id);

#if 0
        g_signal_connect (client, "saved_state",
                          G_CALLBACK (client_saved_state), session);
        g_signal_connect (client, "request_phase2",
                          G_CALLBACK (client_request_phase2), session);
        g_signal_connect (client, "request_interaction",
                          G_CALLBACK (client_request_interaction), session);
        g_signal_connect (client, "interaction_done",
                          G_CALLBACK (client_interaction_done), session);
        g_signal_connect (client, "save_yourself_done",
                          G_CALLBACK (client_save_yourself_done), session);
#endif
        g_signal_connect (client,
                          "disconnected",
                          G_CALLBACK (on_client_disconnected),
                          manager);

        /* If it's a brand new client id, we just accept the client*/
        if (*id == NULL) {
                goto out;
        }

        app = find_app_for_client_id (manager, new_id);
        if (app != NULL) {
                gsm_client_set_app_id (GSM_CLIENT (client), gsm_app_get_id (app));
                gsm_app_registered (app);
                goto out;
        }

        /* app not found */
        g_free (new_id);
        new_id = NULL;

 out:
        g_free (*id);
        *id = new_id;

        return handled;
}

static void
on_store_client_added (GsmClientStore *store,
                       const char     *id,
                       GsmManager     *manager)
{
        GsmClient *client;

        g_debug ("GsmManager: Client added: %s", id);

        client = gsm_client_store_lookup (store, id);

        /* a bit hacky */
        if (GSM_IS_XSMP_CLIENT (client)) {
                g_signal_connect (client,
                                  "register-request",
                                  G_CALLBACK (on_xsmp_client_register_request),
                                  manager);
        }

        /* FIXME: disconnect signal handler */
}

static void
gsm_manager_set_client_store (GsmManager     *manager,
                              GsmClientStore *store)
{
        g_return_if_fail (GSM_IS_MANAGER (manager));

        if (store != NULL) {
                g_object_ref (store);
        }

        if (manager->priv->store != NULL) {
                g_signal_handlers_disconnect_by_func (manager->priv->store,
                                                      on_store_client_added,
                                                      manager);

                g_object_unref (manager->priv->store);
        }


        g_debug ("GsmManager: setting store %p", store);

        manager->priv->store = store;

        if (manager->priv->store != NULL) {
                g_signal_connect (manager->priv->store,
                                  "client-added",
                                  G_CALLBACK (on_store_client_added),
                                  manager);
        }
}

static void
gsm_manager_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue  *value,
                          GParamSpec    *pspec)
{
        GsmManager *self;

        self = GSM_MANAGER (object);

        switch (prop_id) {
        case PROP_FAILSAFE:
                gsm_manager_set_failsafe (self, g_value_get_boolean (value));
                break;
         case PROP_CLIENT_STORE:
                gsm_manager_set_client_store (self, g_value_get_object (value));
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
        GsmManager *self;

        self = GSM_MANAGER (object);

        switch (prop_id) {
        case PROP_FAILSAFE:
                g_value_set_boolean (value, self->priv->failsafe);
                break;
        case PROP_CLIENT_STORE:
                g_value_set_object (value, self->priv->store);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
append_app (GsmManager *manager,
            GsmApp     *app)
{
        const char *id;
        GsmApp     *dup;

        id = gsm_app_get_id (app);
        if (id == NULL) {
                g_debug ("GsmManager: not adding app: no ID");
                return;
        }

        dup = g_hash_table_lookup (manager->priv->apps_by_id, id);
        if (dup != NULL) {
                g_debug ("GsmManager: not adding app: already added");
                return;
        }

        g_hash_table_insert (manager->priv->apps_by_id, g_strdup (id), g_object_ref (app));
}

static void
append_default_apps (GsmManager *manager,
                     char      **autostart_dirs)
{
        GSList      *default_apps;
        GSList      *a;
        char       **app_dirs;
        GConfClient *client;

        g_debug ("GsmManager: append_default_apps ()");

        app_dirs = gsm_util_get_app_dirs ();

        client = gconf_client_get_default ();
        default_apps = gconf_client_get_list (client,
                                              GSM_GCONF_DEFAULT_SESSION_KEY,
                                              GCONF_VALUE_STRING,
                                              NULL);
        g_object_unref (client);

        for (a = default_apps; a; a = a->next) {
                GKeyFile *key_file;
                char     *app_path = NULL;
                char     *desktop_file;

                key_file = g_key_file_new ();

                if (a->data == NULL) {
                        continue;
                }

                desktop_file = g_strdup_printf ("%s.desktop", (char *) a->data);

                g_debug ("GsmManager: Looking for: %s", desktop_file);

                g_key_file_load_from_dirs (key_file,
                                           desktop_file,
                                           (const gchar**) app_dirs,
                                           &app_path,
                                           G_KEY_FILE_NONE,
                                           NULL);

                if (app_path == NULL) {
                        g_key_file_load_from_dirs (key_file,
                                                   desktop_file,
                                                   (const gchar**) autostart_dirs,
                                                   &app_path,
                                                   G_KEY_FILE_NONE,
                                                   NULL);
                }

                if (app_path != NULL) {
                        GsmApp *app;

                        g_debug ("GsmManager: Found in: %s", app_path);

                        app = gsm_autostart_app_new (app_path);
                        g_free (app_path);

                        if (app != NULL) {
                                g_debug ("GsmManager: read %s\n", desktop_file);
                                append_app (manager, app);
                                g_object_unref (app);
                        } else {
                                g_warning ("could not read %s\n", desktop_file);
                        }
                }

                g_free (desktop_file);
                g_key_file_free (key_file);
        }

        g_slist_foreach (default_apps, (GFunc) g_free, NULL);
        g_slist_free (default_apps);
        g_strfreev (app_dirs);
}

static void
append_autostart_apps (GsmManager *manager,
                       const char *path)
{
        GDir       *dir;
        const char *name;

        g_debug ("GsmManager: append_autostart_apps (%s)", path);

        dir = g_dir_open (path, 0, NULL);
        if (dir == NULL) {
                return;
        }

        while ((name = g_dir_read_name (dir))) {
                GsmApp *app;
                char   *desktop_file;

                if (!g_str_has_suffix (name, ".desktop")) {
                        continue;
                }

                desktop_file = g_build_filename (path, name, NULL);

                app = gsm_autostart_app_new (desktop_file);
                if (app != NULL) {
                        g_debug ("GsmManager: read %s\n", desktop_file);
                        append_app (manager, app);
                        g_object_unref (app);
                } else {
                        g_warning ("could not read %s\n", desktop_file);
                }

                g_free (desktop_file);
        }

        g_dir_close (dir);
}

/* FIXME: need to make sure this only happens once */
static void
append_legacy_session_apps (GsmManager *manager,
                            const char *session_filename)
{
        GKeyFile *saved;
        int num_clients, i;

        saved = g_key_file_new ();
        if (!g_key_file_load_from_file (saved, session_filename, 0, NULL)) {
                /* FIXME: error handling? */
                g_key_file_free (saved);
                return;
        }

        num_clients = g_key_file_get_integer (saved,
                                              "Default",
                                              "num_clients",
                                              NULL);
        for (i = 0; i < num_clients; i++) {
                GsmApp *app = gsm_resumed_app_new_from_legacy_session (saved, i);
                if (app != NULL) {
                        append_app (manager, app);
                        g_object_unref (app);
                }
        }

        g_key_file_free (saved);
}

static void
append_saved_session_apps (GsmManager *manager)
{
        char *session_filename;

        /* try resuming from the old gnome-session's files */
        session_filename = g_build_filename (g_get_home_dir (),
                                             ".gnome2",
                                             "session",
                                             NULL);
        if (g_file_test (session_filename, G_FILE_TEST_EXISTS)) {
                append_legacy_session_apps (manager, session_filename);
                g_free (session_filename);
                return;
        }

        g_free (session_filename);
}

static gboolean
_find_app_provides (const char *id,
                    GsmApp     *app,
                    const char *service)
{
        return gsm_app_provides (app, service);
}

static void
append_required_apps (GsmManager *manager)
{
        GSList      *required_components;
        GSList      *r;
        GsmApp      *app;
        GConfClient *client;

        client = gconf_client_get_default ();
        required_components = gconf_client_all_entries (client,
                                                        GSM_GCONF_REQUIRED_COMPONENTS_DIRECTORY,
                                                        NULL);
        g_object_unref (client);

        for (r = required_components; r; r = r->next) {
                GConfEntry *entry;
                const char *default_provider;
                const char *service;

                entry = (GConfEntry *) r->data;

                service = strrchr (entry->key, '/');
                if (service == NULL) {
                        continue;
                }
                service++;

                default_provider = gconf_value_get_string (entry->value);
                if (default_provider == NULL) {
                        continue;
                }

                app = g_hash_table_find (manager->priv->apps_by_id,
                                         (GHRFunc)_find_app_provides,
                                         (char *)service);
                if (app == NULL) {
                        app = gsm_autostart_app_new (default_provider);
                        if (app != NULL) {
                                append_app (manager, app);
                                g_object_unref (app);
                        }
                        /* FIXME: else error */
                }

                gconf_entry_free (entry);
        }

        g_slist_free (required_components);
}

static void
load_apps (GsmManager *manager)
{
        char **autostart_dirs;
        int    i;

        autostart_dirs = gsm_util_get_autostart_dirs ();

        append_default_apps (manager, autostart_dirs);

        if (manager->priv->failsafe) {
                goto out;
        }

        for (i = 0; autostart_dirs[i]; i++) {
                append_autostart_apps (manager, autostart_dirs[i]);
        }

        append_saved_session_apps (manager);

        /* We don't do this in the failsafe case, because the default
         * session should include all requirements anyway. */
        append_required_apps (manager);

 out:
        g_strfreev (autostart_dirs);
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
        load_apps (manager);

        return G_OBJECT (manager);
}

static void
gsm_manager_class_init (GsmManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsm_manager_get_property;
        object_class->set_property = gsm_manager_set_property;
        object_class->constructor = gsm_manager_constructor;
        object_class->finalize = gsm_manager_finalize;

        signals [PHASE_CHANGED] =
                g_signal_new ("phase-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmManagerClass, phase_changed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);

        signals [SESSION_RUNNING] =
                g_signal_new ("session-running",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmManagerClass, session_running),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

        signals [SESSION_OVER] =
                g_signal_new ("session-over",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmManagerClass, session_over),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

        signals [SESSION_OVER_NOTICE] =
                g_signal_new ("session-over-notice",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmManagerClass, session_over_notice),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

        signals [CLIENT_ADDED] =
                g_signal_new ("client-added",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmManagerClass, client_added),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [CLIENT_REMOVED] =
                g_signal_new ("client-removed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmManagerClass, client_removed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);

        g_object_class_install_property (object_class,
                                         PROP_FAILSAFE,
                                         g_param_spec_boolean ("failsafe",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_CLIENT_STORE,
                                         g_param_spec_object ("client-store",
                                                              NULL,
                                                              NULL,
                                                              GSM_TYPE_CLIENT_STORE,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_type_class_add_private (klass, sizeof (GsmManagerPrivate));

        dbus_g_object_type_install_info (GSM_TYPE_MANAGER, &dbus_glib_gsm_manager_object_info);
        dbus_g_error_domain_register (GSM_MANAGER_ERROR, NULL, GSM_MANAGER_TYPE_ERROR);
}

static void
gsm_manager_init (GsmManager *manager)
{

        manager->priv = GSM_MANAGER_GET_PRIVATE (manager);

        manager->priv->apps_by_id = g_hash_table_new_full (g_str_hash,
                                                           g_str_equal,
                                                           g_free,
                                                           g_object_unref);

        manager->priv->inhibitors = gsm_inhibitor_store_new ();
}

static void
gsm_manager_finalize (GObject *object)
{
        GsmManager *manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSM_IS_MANAGER (object));

        manager = GSM_MANAGER (object);

        g_return_if_fail (manager->priv != NULL);

        if (manager->priv->store != NULL) {
                g_object_unref (manager->priv->store);
        }

        if (manager->priv->apps_by_id != NULL) {
                g_hash_table_destroy (manager->priv->apps_by_id);
        }

        if (manager->priv->inhibitors != NULL) {
                g_object_unref (manager->priv->inhibitors);
        }

        G_OBJECT_CLASS (gsm_manager_parent_class)->finalize (object);
}

GsmManager *
gsm_manager_new (GsmClientStore *store,
                 gboolean        failsafe)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                gboolean res;

                manager_object = g_object_new (GSM_TYPE_MANAGER,
                                               "client-store", store,
                                               "failsafe", failsafe,
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

gboolean
gsm_manager_setenv (GsmManager  *manager,
                    const char  *variable,
                    const char  *value,
                    GError     **error)
{
        if (manager->priv->phase > GSM_MANAGER_PHASE_INITIALIZATION) {
                g_set_error (error,
                             GSM_MANAGER_ERROR,
                             GSM_MANAGER_ERROR_NOT_IN_INITIALIZATION,
                             "Setenv interface is only available during the Initialization phase");
                return FALSE;
        }

        g_setenv (variable, value, TRUE);

        return TRUE;
}

gboolean
gsm_manager_initialization_error (GsmManager  *manager,
                                  const char  *message,
                                  gboolean     fatal,
                                  GError     **error)
{
        if (manager->priv->phase > GSM_MANAGER_PHASE_INITIALIZATION) {
                g_set_error (error,
                             GSM_MANAGER_ERROR,
                             GSM_MANAGER_ERROR_NOT_IN_INITIALIZATION,
                             "InitializationError interface is only available during the Initialization phase");
                return FALSE;
        }

        gsm_util_init_error (fatal, "%s", message);

        return TRUE;
}

static void
do_attempt_reboot (GsmConsolekit *consolekit)
{
        if (gsm_consolekit_can_restart (consolekit)) {
                gdm_set_logout_action (GDM_LOGOUT_ACTION_NONE);
                gsm_consolekit_attempt_restart (consolekit);
        } else {
                gdm_set_logout_action (GDM_LOGOUT_ACTION_REBOOT);
        }
}

static void
do_attempt_shutdown (GsmConsolekit *consolekit)
{
        if (gsm_consolekit_can_stop (consolekit)) {
                gdm_set_logout_action (GDM_LOGOUT_ACTION_NONE);
                gsm_consolekit_attempt_stop (consolekit);
        } else {
                gdm_set_logout_action (GDM_LOGOUT_ACTION_SHUTDOWN);
        }
}

static void
manager_attempt_reboot (GsmManager *manager)
{
        GsmConsolekit *consolekit;

        consolekit = gsm_get_consolekit ();
        do_attempt_reboot (consolekit);
        g_object_unref (consolekit);
}

static void
manager_attempt_shutdown (GsmManager *manager)
{
        GsmConsolekit *consolekit;

        consolekit = gsm_get_consolekit ();
        do_attempt_shutdown (consolekit);
        g_object_unref (consolekit);
}

static gboolean
_shutdown_client (const char *id,
                  GsmClient  *client,
                  GsmManager *manager)
{
        gsm_client_notify_session_over (client);
        return FALSE;
}

static void
manager_logout (GsmManager *manager)
{
        gsm_client_store_foreach (manager->priv->store,
                                  (GsmClientStoreFunc)_shutdown_client,
                                  NULL);

        gtk_main_quit ();
}

static void
manager_attempt_hibernate (GsmManager *manager)
{
        GsmPowerManager *power_manager;

        power_manager = gsm_get_power_manager ();

        if (gsm_power_manager_can_hibernate (power_manager)) {
                gsm_power_manager_attempt_hibernate (power_manager);
        }

        g_object_unref (power_manager);
}

static void
manager_attempt_suspend (GsmManager *manager)
{
        GsmPowerManager *power_manager;

        power_manager = gsm_get_power_manager ();

        if (gsm_power_manager_can_suspend (power_manager)) {
                gsm_power_manager_attempt_suspend (power_manager);
        }

        g_object_unref (power_manager);
}

static gboolean
inhibitor_has_flag (gpointer      key,
                    GsmInhibitor *inhibitor,
                    gpointer      data)
{
        int flag;
        int flags;

        flag = GPOINTER_TO_INT (data);
        flags = gsm_inhibitor_get_flags (inhibitor);

        return (flags & flag);
}

static gboolean
gsm_manager_is_switch_user_inhibited (GsmManager *manager)
{
        GsmInhibitor *inhibitor;

        if (manager->priv->inhibitors == NULL) {
                return FALSE;
        }

        inhibitor = gsm_inhibitor_store_find (manager->priv->inhibitors,
                                              (GsmInhibitorStoreFunc)inhibitor_has_flag,
                                              GINT_TO_POINTER (GSM_INHIBITOR_FLAG_SWITCH_USER));
        if (inhibitor == NULL) {
                return FALSE;
        }
        return TRUE;
}

static gboolean
gsm_manager_is_suspend_inhibited (GsmManager *manager)
{
        GsmInhibitor *inhibitor;

        if (manager->priv->inhibitors == NULL) {
                return FALSE;
        }

        inhibitor = gsm_inhibitor_store_find (manager->priv->inhibitors,
                                              (GsmInhibitorStoreFunc)inhibitor_has_flag,
                                              GINT_TO_POINTER (GSM_INHIBITOR_FLAG_SUSPEND));
        if (inhibitor == NULL) {
                return FALSE;
        }
        return TRUE;
}

static gboolean
gsm_manager_is_logout_inhibited (GsmManager *manager)
{
        GsmInhibitor *inhibitor;

        if (manager->priv->inhibitors == NULL) {
                return FALSE;
        }

        inhibitor = gsm_inhibitor_store_find (manager->priv->inhibitors,
                                              (GsmInhibitorStoreFunc)inhibitor_has_flag,
                                              GINT_TO_POINTER (GSM_INHIBITOR_FLAG_LOGOUT));
        if (inhibitor == NULL) {
                return FALSE;
        }
        return TRUE;
}

static void
manager_switch_user (GsmManager *manager)
{
        GError  *error;
        gboolean res;
        char    *command;

        command = g_strdup_printf ("%s %s",
                                   GDM_FLEXISERVER_COMMAND,
                                   GDM_FLEXISERVER_ARGS);

        error = NULL;
        res = gdk_spawn_command_line_on_screen (gdk_screen_get_default (),
                                                command,
                                                &error);

        g_free (command);

        if (! res) {
                g_debug ("GsmManager: Unable to start GDM greeter: %s", error->message);
                g_error_free (error);
        }
}

static void
do_action (GsmManager *manager,
           int         action)
{
        switch (action) {
        case GSM_LOGOUT_ACTION_SWITCH_USER:
                manager_switch_user (manager);
                break;
        case GSM_LOGOUT_ACTION_HIBERNATE:
                manager_attempt_hibernate (manager);
                break;
        case GSM_LOGOUT_ACTION_SLEEP:
                manager_attempt_suspend (manager);
                break;
        case GSM_LOGOUT_ACTION_SHUTDOWN:
                manager_attempt_shutdown (manager);
                break;
        case GSM_LOGOUT_ACTION_REBOOT:
                manager_attempt_reboot (manager);
                break;
        case GSM_LOGOUT_ACTION_LOGOUT:
                manager_logout (manager);
                break;
        default:
                g_assert_not_reached ();
                break;
        }
}

static void
logout_inhibit_dialog_response (GsmLogoutInhibitDialog *dialog,
                                guint                   response_id,
                                GsmManager             *manager)
{
        int action;

        g_debug ("GsmManager: Logout inhibit dialog response: %d", response_id);

        /* In case of dialog cancel, switch user, hibernate and
         * suspend, we just perform the respective action and return,
         * without shutting down the session. */
        switch (response_id) {
        case GTK_RESPONSE_CANCEL:
        case GTK_RESPONSE_NONE:
        case GTK_RESPONSE_DELETE_EVENT:
                break;
        case GTK_RESPONSE_ACCEPT:
                g_object_get (dialog, "action", &action, NULL);
                g_debug ("GsmManager: doing action %d", action);
                do_action (manager, action);
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        gtk_widget_destroy (GTK_WIDGET (dialog));
        manager->priv->inhibit_dialog = NULL;
}

static void
request_reboot (GsmManager *manager)
{
        g_debug ("GsmManager: requesting reboot");

        /* shutdown uses logout inhibit */
        if (! gsm_manager_is_logout_inhibited (manager)) {
                manager_attempt_reboot (manager);
                return;
        }

        if (manager->priv->inhibit_dialog != NULL) {
                g_debug ("GsmManager: inhibit dialog already up");
                gtk_window_present (GTK_WINDOW (manager->priv->inhibit_dialog));
                return;
        }

        manager->priv->inhibit_dialog = gsm_logout_inhibit_dialog_new (manager->priv->inhibitors,
                                                                       GSM_LOGOUT_ACTION_REBOOT);

        g_signal_connect (manager->priv->inhibit_dialog,
                          "response",
                          G_CALLBACK (logout_inhibit_dialog_response),
                          manager);
        gtk_widget_show (manager->priv->inhibit_dialog);
}

static void
request_shutdown (GsmManager *manager)
{
        g_debug ("GsmManager: requesting shutdown");

        /* shutdown uses logout inhibit */
        if (! gsm_manager_is_logout_inhibited (manager)) {
                manager_attempt_shutdown (manager);
                return;
        }

        if (manager->priv->inhibit_dialog != NULL) {
                g_debug ("GsmManager: inhibit dialog already up");
                gtk_window_present (GTK_WINDOW (manager->priv->inhibit_dialog));
                return;
        }

        manager->priv->inhibit_dialog = gsm_logout_inhibit_dialog_new (manager->priv->inhibitors,
                                                                       GSM_LOGOUT_ACTION_SHUTDOWN);

        g_signal_connect (manager->priv->inhibit_dialog,
                          "response",
                          G_CALLBACK (logout_inhibit_dialog_response),
                          manager);
        gtk_widget_show (manager->priv->inhibit_dialog);
}

static void
request_suspend (GsmManager *manager)
{
        g_debug ("GsmManager: requesting suspend");

        if (! gsm_manager_is_suspend_inhibited (manager)) {
                manager_attempt_suspend (manager);
                return;
        }

        if (manager->priv->inhibit_dialog != NULL) {
                g_debug ("GsmManager: inhibit dialog already up");
                gtk_window_present (GTK_WINDOW (manager->priv->inhibit_dialog));
                return;
        }

        manager->priv->inhibit_dialog = gsm_logout_inhibit_dialog_new (manager->priv->inhibitors,
                                                                       GSM_LOGOUT_ACTION_SLEEP);

        g_signal_connect (manager->priv->inhibit_dialog,
                          "response",
                          G_CALLBACK (logout_inhibit_dialog_response),
                          manager);
        gtk_widget_show (manager->priv->inhibit_dialog);
}

static void
request_hibernate (GsmManager *manager)
{
        g_debug ("GsmManager: requesting hibernate");

        /* hibernate uses suspend inhibit */
        if (! gsm_manager_is_suspend_inhibited (manager)) {
                manager_attempt_hibernate (manager);
                return;
        }

        if (manager->priv->inhibit_dialog != NULL) {
                g_debug ("GsmManager: inhibit dialog already up");
                gtk_window_present (GTK_WINDOW (manager->priv->inhibit_dialog));
                return;
        }

        manager->priv->inhibit_dialog = gsm_logout_inhibit_dialog_new (manager->priv->inhibitors,
                                                                       GSM_LOGOUT_ACTION_HIBERNATE);

        g_signal_connect (manager->priv->inhibit_dialog,
                          "response",
                          G_CALLBACK (logout_inhibit_dialog_response),
                          manager);
        gtk_widget_show (manager->priv->inhibit_dialog);
}

static void
request_logout (GsmManager *manager)
{
        g_debug ("GsmManager: requesting logout");

        if (! gsm_manager_is_logout_inhibited (manager)) {
                manager_logout (manager);
                return;
        }

        if (manager->priv->inhibit_dialog != NULL) {
                g_debug ("GsmManager: inhibit dialog already up");
                gtk_window_present (GTK_WINDOW (manager->priv->inhibit_dialog));
                return;
        }

        manager->priv->inhibit_dialog = gsm_logout_inhibit_dialog_new (manager->priv->inhibitors,
                                                                       GSM_LOGOUT_ACTION_LOGOUT);

        g_signal_connect (manager->priv->inhibit_dialog,
                          "response",
                          G_CALLBACK (logout_inhibit_dialog_response),
                          manager);
        gtk_widget_show (manager->priv->inhibit_dialog);
}

static void
request_switch_user (GsmManager *manager)
{
        g_debug ("GsmManager: requesting user switch");

        if (! gsm_manager_is_switch_user_inhibited (manager)) {
                manager_switch_user (manager);
                return;
        }

        if (manager->priv->inhibit_dialog != NULL) {
                g_debug ("GsmManager: inhibit dialog already up");
                gtk_window_present (GTK_WINDOW (manager->priv->inhibit_dialog));
                return;
        }

        manager->priv->inhibit_dialog = gsm_logout_inhibit_dialog_new (manager->priv->inhibitors,
                                                                       GSM_LOGOUT_ACTION_SWITCH_USER);

        g_signal_connect (manager->priv->inhibit_dialog,
                          "response",
                          G_CALLBACK (logout_inhibit_dialog_response),
                          manager);
        gtk_widget_show (manager->priv->inhibit_dialog);
}

static void
logout_dialog_response (GsmLogoutDialog *logout_dialog,
                        guint            response_id,
                        GsmManager      *manager)
{
        g_debug ("GsmManager: Logout dialog response: %d", response_id);

        gtk_widget_destroy (GTK_WIDGET (logout_dialog));

        /* In case of dialog cancel, switch user, hibernate and
         * suspend, we just perform the respective action and return,
         * without shutting down the session. */
        switch (response_id) {
        case GTK_RESPONSE_CANCEL:
        case GTK_RESPONSE_NONE:
        case GTK_RESPONSE_DELETE_EVENT:
                break;
        case GSM_LOGOUT_RESPONSE_SWITCH_USER:
                request_switch_user (manager);
                break;
        case GSM_LOGOUT_RESPONSE_HIBERNATE:
                request_hibernate (manager);
                break;
        case GSM_LOGOUT_RESPONSE_SLEEP:
                request_suspend (manager);
                break;
        case GSM_LOGOUT_RESPONSE_SHUTDOWN:
                request_shutdown (manager);
                break;
        case GSM_LOGOUT_RESPONSE_REBOOT:
                request_reboot (manager);
                break;
        case GSM_LOGOUT_RESPONSE_LOGOUT:
                request_logout (manager);
                break;
        default:
                g_assert_not_reached ();
                break;
        }
}

static void
show_shutdown_dialog (GsmManager *manager)
{
        GtkWidget *dialog;

        if (manager->priv->phase == GSM_MANAGER_PHASE_SHUTDOWN) {
                /* Already shutting down, nothing more to do */
                return;
        }

        dialog = gsm_get_shutdown_dialog (gdk_screen_get_default (),
                                          gtk_get_current_event_time ());

        g_signal_connect (dialog,
                          "response",
                          G_CALLBACK (logout_dialog_response),
                          manager);
        gtk_widget_show (dialog);
}

static void
show_logout_dialog (GsmManager *manager)
{
        GtkWidget *dialog;

        if (manager->priv->phase == GSM_MANAGER_PHASE_SHUTDOWN) {
                /* Already shutting down, nothing more to do */
                return;
        }

        dialog = gsm_get_logout_dialog (gdk_screen_get_default (),
                                        gtk_get_current_event_time ());

        g_signal_connect (dialog,
                          "response",
                          G_CALLBACK (logout_dialog_response),
                          manager);
        gtk_widget_show (dialog);
}

static void
initiate_logout (GsmManager *manager,
                 gboolean    show_confirmation)
{
        gboolean     logout_prompt;
        GConfClient *client;

        if (manager->priv->phase == GSM_MANAGER_PHASE_SHUTDOWN) {
                /* Already shutting down, nothing more to do */
                return;
        }

        client = gconf_client_get_default ();
        logout_prompt = gconf_client_get_bool (client,
                                               "/apps/gnome-session/options/logout_prompt",
                                               NULL);
        g_object_unref (client);

        /* Global settings overides input parameter in order to disable confirmation
         * dialog accordingly. If we're shutting down, we always show the confirmation
         * dialog */
        logout_prompt = (logout_prompt && show_confirmation);

        if (logout_prompt) {
                show_logout_dialog (manager);
        } else {
                request_logout (manager);
        }
}

/*
  dbus-send --session --type=method_call --print-reply
      --dest=org.gnome.SessionManager
      /org/gnome/SessionManager
      org.freedesktop.DBus.Introspectable.Introspect
*/

gboolean
gsm_manager_shutdown (GsmManager *manager,
                      GError    **error)
{
        g_debug ("GsmManager: Shutdown called");

        if (manager->priv->phase != GSM_MANAGER_PHASE_RUNNING) {
                g_set_error (error,
                             GSM_MANAGER_ERROR,
                             GSM_MANAGER_ERROR_NOT_IN_RUNNING,
                             "Logout interface is only available during the Running phase");
                return FALSE;
        }

        show_shutdown_dialog (manager);

        return TRUE;
}

gboolean
gsm_manager_logout (GsmManager *manager,
                    gint        logout_mode,
                    GError    **error)
{
        g_debug ("GsmManager: Logout called");

        if (manager->priv->phase != GSM_MANAGER_PHASE_RUNNING) {
                g_set_error (error,
                             GSM_MANAGER_ERROR,
                             GSM_MANAGER_ERROR_NOT_IN_RUNNING,
                             "Shutdown interface is only available during the Running phase");
                return FALSE;
        }

        switch (logout_mode) {
        case GSM_MANAGER_LOGOUT_MODE_NORMAL:
                initiate_logout (manager, TRUE);
                break;

        case GSM_MANAGER_LOGOUT_MODE_NO_CONFIRMATION:
                initiate_logout (manager, FALSE);
                break;

        case GSM_MANAGER_LOGOUT_MODE_FORCE:
                /* FIXME: Implement when session state saving is ready */
                break;

        default:
                g_assert_not_reached ();
        }

        return TRUE;
}

/* adapted from PolicyKit */
static gboolean
get_caller_info (GsmManager  *manager,
                 const char  *sender,
                 uid_t       *calling_uid,
                 pid_t       *calling_pid)
{
        gboolean res;
        gboolean ret;
        GError  *error = NULL;

        ret = FALSE;

        if (sender == NULL) {
                goto out;
        }

        res = dbus_g_proxy_call (manager->priv->bus_proxy,
                                 "GetConnectionUnixUser",
                                 &error,
                                 G_TYPE_STRING, sender,
                                 G_TYPE_INVALID,
                                 G_TYPE_UINT, calling_uid,
                                 G_TYPE_INVALID);
        if (! res) {
                g_debug ("GetConnectionUnixUser() failed: %s", error->message);
                g_error_free (error);
                goto out;
        }

        res = dbus_g_proxy_call (manager->priv->bus_proxy,
                                 "GetConnectionUnixProcessID",
                                 &error,
                                 G_TYPE_STRING, sender,
                                 G_TYPE_INVALID,
                                 G_TYPE_UINT, calling_pid,
                                 G_TYPE_INVALID);
        if (! res) {
                g_debug ("GetConnectionUnixProcessID() failed: %s", error->message);
                g_error_free (error);
                goto out;
        }

        ret = TRUE;

        g_debug ("uid = %d", *calling_uid);
        g_debug ("pid = %d", *calling_pid);

out:
        return ret;
}

gboolean
gsm_manager_register_client (GsmManager            *manager,
                             const char            *client_startup_id,
                             const char            *app_id,
                             DBusGMethodInvocation *context)
{
        char      *client_id;
        char      *sender;
        GsmClient *client;
        GsmApp    *app;

        g_debug ("GsmManager: RegisterClient %s", client_startup_id);

        if (manager->priv->phase == GSM_MANAGER_PHASE_SHUTDOWN) {
                GError *new_error;

                g_debug ("Unable to register client: shutting down");

                new_error = g_error_new (GSM_MANAGER_ERROR,
                                         GSM_MANAGER_ERROR_NOT_IN_RUNNING,
                                         "Unable to register client");
                dbus_g_method_return_error (context, new_error);
                g_error_free (new_error);
                return FALSE;
        }

        if (client_startup_id == NULL
            || client_startup_id[0] == '\0') {
                client_id = gsm_util_generate_client_id ();
        } else {
                GsmClient *client;

                client = gsm_client_store_find (manager->priv->store,
                                                (GsmClientStoreFunc)_client_has_client_id,
                                                (char *)client_startup_id);
                /* We can't have two clients with the same id. */
                if (client != NULL) {
                        GError *new_error;

                        g_debug ("Unable to register client: already registered");

                        new_error = g_error_new (GSM_MANAGER_ERROR,
                                                 GSM_MANAGER_ERROR_ALREADY_REGISTERED,
                                                 "Unable to register client");
                        dbus_g_method_return_error (context, new_error);
                        g_error_free (new_error);
                        return FALSE;
                }

                client_id = g_strdup (client_startup_id);
        }

        g_debug ("GsmManager: Adding new client %s to session", client_id);

        if ((client_startup_id == NULL || client_startup_id[0] == '\0')
            && app_id == NULL) {
                /* just accept the client - we can't associate with an
                   existing App */
                goto out;
        } else if (client_startup_id != NULL
                   && client_startup_id[0] != '\0') {
                app = find_app_for_client_id (manager, client_startup_id);
        } else if (app_id != NULL) {
                /* try to associate this app id with a known app */
                app = find_app_for_app_id (manager, app_id);
        }

        sender = dbus_g_method_get_sender (context);
        client = gsm_method_client_new (client_id, sender);
        g_free (sender);
        if (client == NULL) {
                GError *new_error;

                g_debug ("Unable to create client");

                new_error = g_error_new (GSM_MANAGER_ERROR,
                                         GSM_MANAGER_ERROR_GENERAL,
                                         "Unable to register client");
                dbus_g_method_return_error (context, new_error);
                g_error_free (new_error);
                return FALSE;
        }

        gsm_client_store_add (manager->priv->store, client);

        if (app != NULL) {
                gsm_client_set_app_id (client, gsm_app_get_id (app));
                gsm_app_registered (app);
        } else {
                /* if an app id is specified store it in the client
                   so we can save it later */
                gsm_client_set_app_id (client, app_id);
        }

        gsm_client_set_status (client, GSM_CLIENT_REGISTERED);

 out:
        g_assert (client_id != NULL);
        dbus_g_method_return (context, client_id);
        g_free (client_id);

        return TRUE;
}

gboolean
gsm_manager_unregister_client (GsmManager            *manager,
                               const char            *session_client_id,
                               DBusGMethodInvocation *context)
{
        GsmClient *client;

        g_debug ("GsmManager: UnregisterClient %s", session_client_id);

        client = gsm_client_store_find (manager->priv->store,
                                        (GsmClientStoreFunc)_client_has_client_id,
                                        (char *)session_client_id);

        if (client == NULL) {
                GError *new_error;

                g_debug ("Unable to unregister client: not registered");

                new_error = g_error_new (GSM_MANAGER_ERROR,
                                         GSM_MANAGER_ERROR_NOT_REGISTERED,
                                         "Unable to unregister client");
                dbus_g_method_return_error (context, new_error);
                g_error_free (new_error);
                return FALSE;
        }


        /* don't disconnect client here, only change the status.
           Wait until it leaves the bus before disconnecting it */
        gsm_client_set_status (client, GSM_CLIENT_UNREGISTERED);

        dbus_g_method_return (context);

        return TRUE;
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
        } while (gsm_inhibitor_store_lookup (manager->priv->inhibitors, cookie) != NULL);

        return cookie;
}

gboolean
gsm_manager_inhibit (GsmManager            *manager,
                     const char            *app_id,
                     guint                  toplevel_xid,
                     const char            *reason,
                     guint                  flags,
                     DBusGMethodInvocation *context)
{
        GsmInhibitor *inhibitor;
        guint         cookie;

        g_debug ("GsmManager: Inhibit xid=%u app_id=%s reason=%s flags=%u",
                 toplevel_xid,
                 app_id,
                 reason,
                 flags);

        if (app_id == NULL || app_id[0] == '\0') {
                GError *new_error;

                new_error = g_error_new (GSM_MANAGER_ERROR,
                                         GSM_MANAGER_ERROR_GENERAL,
                                         "Application ID not specified");
                g_debug ("GsmManager: Unable to inhibit: %s", new_error->message);
                dbus_g_method_return_error (context, new_error);
                g_error_free (new_error);
                return FALSE;
        }

        if (reason == NULL || reason[0] == '\0') {
                GError *new_error;

                new_error = g_error_new (GSM_MANAGER_ERROR,
                                         GSM_MANAGER_ERROR_GENERAL,
                                         "Reason not specified");
                g_debug ("GsmManager: Unable to inhibit: %s", new_error->message);
                dbus_g_method_return_error (context, new_error);
                g_error_free (new_error);
                return FALSE;
        }

        if (flags == 0) {
                GError *new_error;

                new_error = g_error_new (GSM_MANAGER_ERROR,
                                         GSM_MANAGER_ERROR_GENERAL,
                                         "Invalid inhibit flags");
                g_debug ("GsmManager: Unable to inhibit: %s", new_error->message);
                dbus_g_method_return_error (context, new_error);
                g_error_free (new_error);
                return FALSE;
        }

        cookie = _generate_unique_cookie (manager);
        inhibitor = gsm_inhibitor_new (app_id,
                                       toplevel_xid,
                                       flags,
                                       reason,
                                       dbus_g_method_get_sender (context),
                                       cookie);
        gsm_inhibitor_store_add (manager->priv->inhibitors, inhibitor);
        g_object_unref (inhibitor);

        dbus_g_method_return (context, cookie);

        return TRUE;
}

gboolean
gsm_manager_uninhibit (GsmManager            *manager,
                       guint                  cookie,
                       DBusGMethodInvocation *context)
{
        GsmInhibitor *inhibitor;

        g_debug ("GsmManager: Uninhibit %u", cookie);

        inhibitor = gsm_inhibitor_store_lookup (manager->priv->inhibitors, cookie);
        if (inhibitor == NULL) {
                GError *new_error;

                new_error = g_error_new (GSM_MANAGER_ERROR,
                                         GSM_MANAGER_ERROR_GENERAL,
                                         "Unable to uninhibit: Invalid cookie");
                dbus_g_method_return_error (context, new_error);
                g_debug ("Unable to uninhibit: %s", new_error->message);
                g_error_free (new_error);
                return FALSE;
        }

        g_debug ("GsmManager: removing inhibitor %s %u reason '%s' %u connection %s",
                 gsm_inhibitor_get_app_id (inhibitor),
                 gsm_inhibitor_get_toplevel_xid (inhibitor),
                 gsm_inhibitor_get_reason (inhibitor),
                 gsm_inhibitor_get_flags (inhibitor),
                 gsm_inhibitor_get_bus_name (inhibitor));

        gsm_inhibitor_store_remove (manager->priv->inhibitors, inhibitor);

        dbus_g_method_return (context);

        return TRUE;
}
