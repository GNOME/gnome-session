/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Novell, Inc.
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
#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <gtk/gtk.h> /* for logout dialog */
#include <gconf/gconf-client.h>

#include "gsm-manager.h"
#include "gsm-manager-glue.h"

#include "gsm-autostart-app.h"
#include "gsm-resumed-app.h"
#include "util.h"
#include "gdm.h"
#include "logout-dialog.h"
#include "power-manager.h"

#define GSM_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSM_TYPE_MANAGER, GsmManagerPrivate))

#define GSM_DBUS_PATH         "/org/gnome/SessionManager"
#define GSM_MANAGER_DBUS_PATH GSM_DBUS_PATH
#define GSM_MANAGER_DBUS_NAME "org.gnome.DisplayManager"

#define GSM_MANAGER_PHASE_TIMEOUT 10 /* seconds */

#define GSM_GCONF_DEFAULT_SESSION_KEY           "/desktop/gnome/session/default-session"
#define GSM_GCONF_REQUIRED_COMPONENTS_DIRECTORY "/desktop/gnome/session/required-components"


struct GsmManagerPrivate
{
        gboolean                failsafe;
        GsmClientStore         *store;

        /* Startup/resumed apps */
        GSList                 *apps;
        GHashTable             *apps_by_name;

        /* Current status */
        char                   *name;
        GsmManagerPhase         phase;
        guint                   timeout_id;
        GSList                 *pending_apps;

        /* SM clients */
        GSList                 *clients;

        /* When shutdown starts, all clients are put into shutdown_clients.
         * If they request phase2, they are moved from shutdown_clients to
         * phase2_clients. If they request interaction, they are appended
         * to interact_clients (the first client in interact_clients is
         * the one currently interacting). If they report that they're done,
         * they're removed from shutdown_clients/phase2_clients.
         *
         * Once shutdown_clients is empty, phase2 starts. Once phase2_clients
         * is empty, shutdown is complete.
         */
        GSList                 *shutdown_clients;
        GSList                 *interact_clients;
        GSList                 *phase2_clients;

        /* List of clients which were disconnected due to disabled condition
         * and shouldn't be automatically restarted */
        GSList                 *condition_clients;

        int                     logout_response_id;

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

static void
app_condition_changed (GsmApp     *app,
                       gboolean    condition,
                       GsmManager *manager)
{
        GsmClient  *client;
        GSList     *cl;

        client = NULL;

        /* Check for an existing session client for this app */
        for (cl = manager->priv->clients; cl; cl = cl->next) {
                GsmClient *c = GSM_CLIENT (cl->data);

                if (!strcmp (app->client_id, gsm_client_get_id (c))) {
                        client = c;
                }
        }

        if (condition) {
                GError *error = NULL;

                if (app->pid <= 0 && client == NULL) {
                        gsm_app_launch (app, &error);
                }

                if (error != NULL) {
                        g_warning ("Not able to launch autostart app from its condition: %s",
                                   error->message);

                        g_error_free (error);
                }
        } else {
                /* Kill client in case condition if false and make sure it won't
                 * be automatically restarted by adding the client to
                 * condition_clients */
                manager->priv->condition_clients = g_slist_prepend (manager->priv->condition_clients, client);
                gsm_client_die (client);
                app->pid = -1;
        }
}

static void start_phase (GsmManager *manager);

static void
end_phase (GsmManager *manager)
{
        g_slist_free (manager->priv->pending_apps);
        manager->priv->pending_apps = NULL;

        g_debug ("ending phase %d\n", manager->priv->phase);

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
                           gsm_app_get_basename (a->data));
                g_signal_handlers_disconnect_by_func (a->data, app_registered, manager);
                /* FIXME: what if the app was filling in a required slot? */
        }

        end_phase (manager);
        return FALSE;
}

static void
start_phase (GsmManager *manager)
{
        GsmApp *app;
        GSList *a;
        GError *err = NULL;

        g_debug ("starting phase %d\n", manager->priv->phase);

        g_slist_free (manager->priv->pending_apps);
        manager->priv->pending_apps = NULL;

        for (a = manager->priv->apps; a; a = a->next) {
                app = a->data;

                if (gsm_app_get_phase (app) != manager->priv->phase) {
                        continue;
                }

                /* Keep track of app autostart condition in order to react
                 * accordingly in the future. */
                g_signal_connect (app,
                                  "condition-changed",
                                  G_CALLBACK (app_condition_changed),
                                  manager);

                if (gsm_app_is_disabled (app)) {
                        continue;
                }

                if (gsm_app_launch (app, &err) > 0) {
                        if (manager->priv->phase == GSM_MANAGER_PHASE_INITIALIZATION) {
                                /* Applications from Initialization phase are considered
                                 * registered when they exit normally. This is because
                                 * they are expected to just do "something" and exit */
                                g_signal_connect (app,
                                                  "exited",
                                                  G_CALLBACK (app_registered),
                                                  manager);
                        }

                        if (manager->priv->phase < GSM_MANAGER_PHASE_APPLICATION) {
                                g_signal_connect (app,
                                                  "registered",
                                                  G_CALLBACK (app_registered),
                                                  manager);

                                manager->priv->pending_apps = g_slist_prepend (manager->priv->pending_apps, app);
                        }
                } else if (err != NULL) {
                        g_warning ("Could not launch application '%s': %s",
                                   gsm_app_get_basename (app),
                                   err->message);
                        g_error_free (err);
                        err = NULL;
                }
        }

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

typedef struct {
        const char *service_name;
        GsmManager *manager;
} RemoveClientData;

static gboolean
remove_client_for_connection (char             *id,
                              GsmClient        *client,
                              RemoveClientData *data)
{
        g_assert (client != NULL);
        g_assert (data->service_name != NULL);

        /* FIXME: compare service name to that of client */
#if 0
        if (strcmp (info->service_name, data->service_name) == 0) {

                return TRUE;
        }
#endif

        return FALSE;
}

static void
remove_clients_for_connection (GsmManager *manager,
                               const char *service_name)
{
        RemoveClientData data;

        data.service_name = service_name;
        data.manager = manager;

        /* FIXME */
}

static void
bus_name_owner_changed (DBusGProxy  *bus_proxy,
                        const char  *service_name,
                        const char  *old_service_name,
                        const char  *new_service_name,
                        GsmManager  *manager)
{
        if (strlen (new_service_name) == 0) {
                remove_clients_for_connection (manager, old_service_name);
        }
}

static gboolean
register_manager (GsmManager *manager)
{
        GError *error = NULL;

        error = NULL;
        manager->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (manager->priv->connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting system bus: %s", error->message);
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

static void
gsm_manager_set_client_store (GsmManager     *manager,
                              GsmClientStore *store)
{
        g_return_if_fail (GSM_IS_MANAGER (manager));

        if (store != NULL) {
                g_object_ref (store);
        }

        if (manager->priv->store != NULL) {
                g_object_unref (manager->priv->store);
        }

        manager->priv->store = store;
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
        const char *basename;
        GsmApp     *dup;

        basename = gsm_app_get_basename (app);
        if (basename == NULL) {
                g_object_unref (app);
                return;
        }

        dup = g_hash_table_lookup (manager->priv->apps_by_name, basename);
        if (dup != NULL) {
                /* FIXME */
                g_object_unref (app);
                return;
        }

        manager->priv->apps = g_slist_append (manager->priv->apps, app);
        g_hash_table_insert (manager->priv->apps_by_name, g_strdup (basename), app);
}

static void
append_default_apps (GsmManager *manager,
                     char      **autostart_dirs)
{
        GSList      *default_apps;
        GSList      *a;
        char       **app_dirs;
        GConfClient *client;

        g_debug ("append_default_apps ()");

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

                g_debug ("Look for: %s", desktop_file);

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
                        char   *client_id;

                        g_debug ("Found in: %s", app_path);

                        client_id = gsm_util_generate_client_id ();
                        app = gsm_autostart_app_new (app_path, client_id);
                        g_free (client_id);
                        g_free (app_path);

                        if (app != NULL) {
                                g_debug ("read %s\n", desktop_file);
                                append_app (manager, app);
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

        g_debug ("append_autostart_apps (%s)", path);

        dir = g_dir_open (path, 0, NULL);
        if (dir == NULL) {
                return;
        }

        while ((name = g_dir_read_name (dir))) {
                GsmApp *app;
                char   *desktop_file;
                char   *client_id;

                if (!g_str_has_suffix (name, ".desktop")) {
                        continue;
                }

                desktop_file = g_build_filename (path, name, NULL);

                client_id = gsm_util_generate_client_id ();
                app = gsm_autostart_app_new (desktop_file, client_id);
                if (app != NULL) {
                        g_debug ("read %s\n", desktop_file);
                        append_app (manager, app);
                } else {
                        g_warning ("could not read %s\n", desktop_file);
                }

                g_free (desktop_file);
                g_free (client_id);
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

        num_clients = g_key_file_get_integer (saved, "Default", "num_clients", NULL);
        for (i = 0; i < num_clients; i++) {
                GsmApp *app = gsm_resumed_app_new_from_legacy_session (saved, i);
                if (app != NULL) {
                        append_app (manager, app);
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

static void
append_required_apps (GsmManager *manager)
{
        GSList      *required_components;
        GSList      *r;
        GsmApp      *app;
        gboolean     found;
        GConfClient *client;

        client = gconf_client_get_default ();
        required_components = gconf_client_all_entries (client,
                                                        GSM_GCONF_REQUIRED_COMPONENTS_DIRECTORY,
                                                        NULL);
        g_object_unref (client);

        for (r = required_components; r; r = r->next) {
                GSList     *a;
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

                for (a = manager->priv->apps, found = FALSE; a; a = a->next) {
                        app = a->data;

                        if (gsm_app_provides (app, service)) {
                                found = TRUE;
                                break;
                        }
                }

                if (!found) {
                        char *client_id;

                        client_id = gsm_util_generate_client_id ();
                        app = gsm_autostart_app_new (default_provider, client_id);
                        g_free (client_id);
                        if (app)
                                append_app (manager, app);
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
                                         PROP_FAILSAFE,
                                         g_param_spec_object ("client-store",
                                                              NULL,
                                                              NULL,
                                                              GSM_TYPE_CLIENT_STORE,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_type_class_add_private (klass, sizeof (GsmManagerPrivate));

        dbus_g_object_type_install_info (GSM_TYPE_MANAGER, &dbus_glib_gsm_manager_object_info);
}

static void
gsm_manager_init (GsmManager *manager)
{

        manager->priv = GSM_MANAGER_GET_PRIVATE (manager);

        manager->priv->apps_by_name = g_hash_table_new (g_str_hash, g_str_equal);

        manager->priv->logout_response_id = GTK_RESPONSE_NONE;
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

        /* FIXME */

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
manager_shutdown (GsmManager *manager)
{
        GSList *cl;

        /* Emit session over signal */
        g_signal_emit (manager, signals[SESSION_OVER], 0);

        /* FIXME: do this in reverse phase order */
        for (cl = manager->priv->clients; cl; cl = cl->next) {
                gsm_client_die (cl->data);
        }

        switch (manager->priv->logout_response_id) {
        case GSM_LOGOUT_RESPONSE_SHUTDOWN:
                gdm_set_logout_action (GDM_LOGOUT_ACTION_SHUTDOWN);
                break;
        case GSM_LOGOUT_RESPONSE_REBOOT:
                gdm_set_logout_action (GDM_LOGOUT_ACTION_REBOOT);
                break;
        default:
                gtk_main_quit ();
                break;
        }
}

static void
initiate_shutdown (GsmManager *manager)
{
        GSList *cl;

        manager->priv->phase = GSM_MANAGER_PHASE_SHUTDOWN;

        if (manager->priv->clients == NULL) {
                manager_shutdown (manager);
        }

        for (cl = manager->priv->clients; cl; cl = cl->next) {
                GsmClient *client = GSM_CLIENT (cl->data);

                manager->priv->shutdown_clients = g_slist_prepend (manager->priv->shutdown_clients, client);

                gsm_client_save_yourself (client, FALSE);
        }
}

static void
logout_dialog_response (GsmLogoutDialog *logout_dialog,
                        guint            response_id,
                        GsmManager      *manager)
{
        GsmPowerManager *power_manager;

        gtk_widget_destroy (GTK_WIDGET (logout_dialog));

        /* In case of dialog cancel, switch user, hibernate and suspend, we just
         * perform the respective action and return, without shutting down the
         * session. */
        switch (response_id) {
        case GTK_RESPONSE_CANCEL:
        case GTK_RESPONSE_NONE:
        case GTK_RESPONSE_DELETE_EVENT:
                return;

        case GSM_LOGOUT_RESPONSE_SWITCH_USER:
                gdm_new_login ();
                return;

        case GSM_LOGOUT_RESPONSE_STD:
                power_manager = gsm_get_power_manager ();

                if (gsm_power_manager_can_hibernate (power_manager)) {
                        gsm_power_manager_attempt_hibernate (power_manager);
                }

                g_object_unref (power_manager);

                return;

        case GSM_LOGOUT_RESPONSE_STR:
                power_manager = gsm_get_power_manager ();

                if (gsm_power_manager_can_suspend (power_manager)) {
                        gsm_power_manager_attempt_suspend (power_manager);
                }

                g_object_unref (power_manager);

                return;

        default:
                break;
        }

        manager->priv->logout_response_id = response_id;

        initiate_shutdown (manager);
}

static void
gsm_manager_initiate_shutdown (GsmManager           *manager,
                               gboolean              show_confirmation,
                               GsmManagerLogoutType  logout_type)
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
        logout_prompt = (logout_prompt && show_confirmation) ||
                (logout_type == GSM_MANAGER_LOGOUT_TYPE_SHUTDOWN);

        if (logout_prompt) {
                GtkWidget *logout_dialog;

                logout_dialog = gsm_get_logout_dialog (logout_type,
                                                       gdk_screen_get_default (),
                                                       gtk_get_current_event_time ());

                g_signal_connect (G_OBJECT (logout_dialog),
                                  "response",
                                  G_CALLBACK (logout_dialog_response),
                                  manager);

                gtk_widget_show (logout_dialog);

                return;
        }

        initiate_shutdown (manager);
}

gboolean
gsm_manager_shutdown (GsmManager *manager,
                      GError    **error)
{
        if (manager->priv->phase != GSM_MANAGER_PHASE_RUNNING) {
                g_set_error (error,
                             GSM_MANAGER_ERROR,
                             GSM_MANAGER_ERROR_NOT_IN_RUNNING,
                             "Logout interface is only available during the Running phase");
                return FALSE;
        }

        gsm_manager_initiate_shutdown (manager,
                                       TRUE,
                                       GSM_MANAGER_LOGOUT_TYPE_SHUTDOWN);

        return TRUE;
}

gboolean
gsm_manager_logout (GsmManager *manager,
                    gint        logout_mode,
                    GError    **error)
{
        if (manager->priv->phase != GSM_MANAGER_PHASE_RUNNING) {
                g_set_error (error,
                             GSM_MANAGER_ERROR,
                             GSM_MANAGER_ERROR_NOT_IN_RUNNING,
                             "Shutdown interface is only available during the Running phase");
                return FALSE;
        }

        switch (logout_mode) {
        case GSM_MANAGER_LOGOUT_MODE_NORMAL:
                gsm_manager_initiate_shutdown (manager, TRUE, GSM_MANAGER_LOGOUT_TYPE_LOGOUT);
                break;

        case GSM_MANAGER_LOGOUT_MODE_NO_CONFIRMATION:
                gsm_manager_initiate_shutdown (manager, FALSE, GSM_MANAGER_LOGOUT_TYPE_LOGOUT);
                break;

        case GSM_MANAGER_LOGOUT_MODE_FORCE:
                /* FIXME: Implement when session state saving is ready */
                break;

        default:
                g_assert_not_reached ();
        }

        return TRUE;
}

static void
manager_set_name (GsmManager *manager,
                  const char *name)
{
        g_free (manager->priv->name);
        manager->priv->name = g_strdup (name);
}

gboolean
gsm_manager_set_name (GsmManager *manager,
                      const char *session_name,
                      GError    **error)
{
        if (manager->priv->phase != GSM_MANAGER_PHASE_RUNNING) {
                g_set_error (error,
                             GSM_MANAGER_ERROR,
                             GSM_MANAGER_ERROR_NOT_IN_RUNNING,
                             "SetName interface is only available during the Running phase");
                return FALSE;
        }

        manager_set_name (manager, session_name);

        return TRUE;
}
