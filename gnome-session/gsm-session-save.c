/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>

#include "gsm-session-save.h"
#include "gsm-util.h"

struct _GsmSessionSave
{
        GObject          parent;

        char *session_id;
        char *file_path;
        gboolean sealed;
        char *fallback_instance_id;
        GHashTable *apps;
};

enum {
        PROP_0,
        PROP_SESSION_ID,
        LAST_PROP
};

G_DEFINE_TYPE (GsmSessionSave, gsm_session_save, G_TYPE_OBJECT);

typedef struct {
        char *app_id;
        GSList *instances;
} SavedApp;

typedef struct {
        SavedApp *app;
        char *instance_id;
        char *dbus_name;
        gboolean crashed;
} SavedAppInstance;

static SavedApp *
saved_app_new (const char *app_id)
{
        SavedApp *app = g_new (SavedApp, 1);
        *app = (SavedApp) {
                .app_id = g_strdup (app_id),
                .instances = NULL,
        };
        return app;
}

static SavedAppInstance *
saved_app_instance_new (SavedApp   *app,
                        const char *instance_id,
                        const char *dbus_name)
{
        SavedAppInstance *instance = g_new (SavedAppInstance, 1);
        *instance = (SavedAppInstance) {
                .app = app,
                .instance_id = instance_id ? g_strdup (instance_id) : g_uuid_string_random (),
                .dbus_name = dbus_name ? g_strdup (dbus_name) : NULL,
                .crashed = FALSE,
        };
        return instance;
}

static void
saved_app_instance_free (SavedAppInstance *app)
{
        g_free (app->instance_id);
        g_free (app->dbus_name);
        g_free (app);
}

static void
saved_app_free (SavedApp *app)
{
        g_slist_free_full (app->instances,
                           (GDestroyNotify) saved_app_instance_free);
        g_free (app->app_id);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SavedApp, saved_app_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SavedAppInstance, saved_app_instance_free)

static void
gsm_session_save_init (GsmSessionSave *save)
{
}

static void
gsm_session_save_set_session_id (GsmSessionSave *save, const char *id)
{
        g_set_str (&save->session_id, id);

        g_clear_pointer (&save->file_path, g_free);
        save->file_path = g_build_filename (g_get_user_state_dir(),
                                            "gnome-session",
                                            id,
                                            NULL);
}

static void
gsm_session_save_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
        GsmSessionSave *save = GSM_SESSION_SAVE (object);

        switch (prop_id) {
        case PROP_SESSION_ID:
                gsm_session_save_set_session_id (save, g_value_get_string (value));
                break;
        default:
                break;
        }
}

static void
gsm_session_save_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
        GsmSessionSave *save = GSM_SESSION_SAVE (object);

        switch (prop_id) {
        case PROP_SESSION_ID:
                g_value_set_string (value, save->session_id);
                break;
        default:
                break;
        }
}

static void
gsm_session_save_dispose (GObject *object)
{
        GsmSessionSave *save = GSM_SESSION_SAVE (object);

        g_clear_pointer (&save->file_path, g_free);
        g_clear_object (&save->apps);
        g_clear_pointer (&save->fallback_instance_id, g_free);

        G_OBJECT_CLASS (gsm_session_save_parent_class)->dispose (object);
}

static void gsm_session_save_constructed (GObject*);

static void
gsm_session_save_class_init (GsmSessionSaveClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->set_property = gsm_session_save_set_property;
        object_class->get_property = gsm_session_save_get_property;
        object_class->constructed = gsm_session_save_constructed;
        object_class->dispose = gsm_session_save_dispose;

        g_object_class_install_property (object_class,
                                         PROP_SESSION_ID,
                                         g_param_spec_object ("session-id",
                                                              "session-id",
                                                              "session-id",
                                                              G_TYPE_STRING,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
flush_to_disk (GsmSessionSave *save)
{
        GVariantBuilder builder, instances;
        g_autoptr (GVariant) variant = NULL;
        g_autoptr (GError) error = NULL;
        GHashTableIter app_iter;
        gpointer value;

        g_variant_builder_init_static (&builder, G_VARIANT_TYPE_VARDICT);

        g_variant_builder_add (&builder, "{sv}", "version",
                               g_variant_new_uint32 (1));
        g_variant_builder_add (&builder, "{sv}", "dirty",
                               g_variant_new_boolean (!save->sealed));
        g_variant_builder_add (&builder, "{sv}", "fallback-instance-id",
                               g_variant_new_string (save->fallback_instance_id));

        g_variant_builder_init_static (&instances, G_VARIANT_TYPE ("a{saa{sv}}"));
        g_hash_table_iter_init (&app_iter, save->apps);
        while (g_hash_table_iter_next (&app_iter, NULL, &value)) {
                SavedApp *app = value;

                g_variant_builder_open (&instances, G_VARIANT_TYPE ("{saa{sv}}"));
                g_variant_builder_add (&instances, "s", app->app_id);

                g_variant_builder_open (&instances, G_VARIANT_TYPE ("aa{sv}"));
                for (const GSList *iter = app->instances; iter; iter = iter->next) {
                        SavedAppInstance *instance = iter->data;
                        g_variant_builder_open (&instances, G_VARIANT_TYPE ("a{sv}"));
                        g_variant_builder_add (&instances, "{sv}", "instance-id",
                                               g_variant_new_string (instance->instance_id));
                        g_variant_builder_add (&instances, "{sv}", "crashed",
                                               g_variant_new_boolean (instance->crashed));
                        g_variant_builder_close (&instances);
                }
                g_variant_builder_close (&instances);

                g_variant_builder_close (&instances);
        }
        g_variant_builder_add (&builder, "{sv}", "instances",
                               g_variant_builder_end (&instances));

        variant = g_variant_ref_sink (g_variant_builder_end (&builder));
        if (!g_file_set_contents (save->file_path,
                                  g_variant_get_data (variant),
                                  g_variant_get_size (variant),
                                  &error)) {
                g_warning ("GsmSessionSave: Failed to write save file to disk: %s",
                           error->message);
                return;
        }
}

#define malformed_save_error(error, msg) \
        (g_set_error_literal ((error), G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Malformed save file: " msg), FALSE)

static GHashTable *
make_hash_table (void)
{
        return g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
                                      (GDestroyNotify) saved_app_free);
}

static gboolean
deserialize_app (GVariantIter *iter, gboolean dirty, SavedApp **out_app)
{
        const char *app_id;
        g_autoptr (GVariantIter) instances = NULL;
        GVariant *instance_variant = NULL;
        g_autoptr (SavedApp) app = NULL;

        if (!g_variant_iter_next (iter, "{&saa{sv}}", &app_id, &instances))
                return FALSE;

        app = saved_app_new (app_id);

        while (g_variant_iter_loop (instances, "@a{sv}", &instance_variant)) {
                g_auto(GVariantDict) dict = G_VARIANT_DICT_INIT (instance_variant);
                const char *instance_id = NULL;
                g_autoptr (SavedAppInstance) instance = NULL;

                if (!g_variant_dict_lookup (&dict, "instance-id", "&s", &instance_id)) {
                        g_warning ("GsmSessionSave: Dropping malformed instance of app %s",
                                   app_id);
                        continue;
                }

                instance = saved_app_instance_new (app, instance_id, NULL);

                if (dirty)
                        instance->crashed = TRUE;
                else
                        g_variant_dict_lookup (&dict, "crashed", "b", &instance->crashed);

                app->instances = g_slist_prepend (app->instances,
                                                  g_steal_pointer (&instance));
        }

        *out_app = g_steal_pointer (&app);
        return TRUE;
}

static gboolean
load_from_disk (GsmSessionSave  *save,
                GError         **error)
{
        g_autoptr (GMappedFile) file = NULL;
        g_autoptr (GVariant) variant = NULL;
        g_autoptr (GVariantDict) dict = NULL;
        g_autoptr (GVariant) instances = NULL;
        guint32 version = 0;
        gboolean dirty = TRUE;
        g_autofree char *fallback_instance_id = NULL;
        g_autoptr (GVariantIter) instances_iter = NULL;
        g_autoptr (GHashTable) apps = NULL;
        SavedApp *deserialized_app;

        file = g_mapped_file_new (save->file_path, FALSE, error);
        if (!file) {
                if (!g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
                        return FALSE;

                g_clear_error (error);
                return TRUE;
        }

        if (g_mapped_file_get_length (file) == 0)
                return TRUE;

        variant = g_variant_new_from_data (G_VARIANT_TYPE_VARDICT,
                                           g_mapped_file_get_contents (file),
                                           g_mapped_file_get_length (file),
                                           FALSE,
                                           NULL,
                                           NULL);

        if (!variant)
                return malformed_save_error (error, "Failed to parse as variant");

        g_variant_ref_sink (variant);
        dict = g_variant_dict_new (variant);

        if (!g_variant_dict_lookup (dict, "version", "u", &version))
                return malformed_save_error (error, "Missing 'version' field");
        if (version != 1) {
                /* Not necessarily an error. This might happen if the user
                 * downgrades the OS. Best we can do here is discard the save */
                g_warning ("GsmSessionSave: Save file has unsupported version %u, discarding.",
                           version);
                return TRUE;
        }

        if (!g_variant_dict_lookup (dict, "dirty", "b", &dirty))
                return malformed_save_error (error, "Missing 'dirty' field");

        if (!g_variant_dict_lookup (dict, "fallback-instance-id", "s",
                                    &fallback_instance_id))
                return malformed_save_error (error, "Missing 'fallback-instance-id' field");

        if (!g_variant_dict_lookup (dict, "instances", "@a{saa{sv}}", &instances))
                return malformed_save_error (error, "Missing 'instances' field");

        apps = make_hash_table ();
        instances_iter = g_variant_iter_new (instances);
        while (deserialize_app (instances_iter, dirty, &deserialized_app))
                g_hash_table_insert (apps, deserialized_app->app_id, deserialized_app);

        g_set_object (&save->apps, g_steal_pointer (&apps));
        g_set_str (&save->fallback_instance_id, g_steal_pointer (&fallback_instance_id));

        g_debug ("Loaded %s saved session from disk",
                 dirty ? "dirty" : "clean");
        return TRUE;
}

static void
gsm_session_save_constructed (GObject *object)
{
        GsmSessionSave *save = GSM_SESSION_SAVE (object);
        GError *error;

        if (!load_from_disk (save, &error)) {
                g_warning ("GsmSessionSave: Failed to load from disk, discarding: %s",
                           error->message);
                g_clear_error (&error);

                save->apps = make_hash_table ();
                save->fallback_instance_id = g_uuid_string_random ();
        }
}

static gboolean
launch_app (SavedAppInstance *instance)
{
        g_autoptr (GDesktopAppInfo) app = NULL;
        g_autoptr (GError) error = NULL;

        g_debug ("Launching instance of app (%s): %s (crashed: %s)",
                 instance->app->app_id,
                 instance->instance_id,
                 instance->crashed ? "yes" : "no");

        app = g_desktop_app_info_new (instance->app->app_id);
        if (!app) {
                g_warning ("GsmSessionSave: Failed to look up saved app: %s",
                           instance->app->app_id);
                return FALSE;
        }

        if (!gsm_util_launch_app (app, &error)) {
                g_warning ("GsmSessionSave: Failed to launch saved app (%s): %s",
                           instance->app->app_id,
                           error->message);
                return FALSE;
        }

        return TRUE;
}

gboolean
gsm_session_save_restore (GsmSessionSave *save)
{
        gboolean launched_any = FALSE;
        GHashTableIter iter;
        gpointer value;

        g_hash_table_iter_init (&iter, save->apps);
        while (g_hash_table_iter_next (&iter, NULL, &value)) {
                SavedApp *app = value;
                for (const GSList *instance = app->instances; instance; instance = instance->next)
                        launched_any |= launch_app(instance->data);
        }

        return launched_any;
}

void
gsm_session_save_seal (GsmSessionSave *save)
{
        if (save->sealed)
                return;

        save->sealed = TRUE;
        flush_to_disk (save);
}

void
gsm_session_save_unseal (GsmSessionSave *save)
{
        if (!save->sealed)
                return;

        save->sealed = FALSE;
        flush_to_disk (save);
}

void
gsm_session_save_register (GsmSessionSave   *save,
                           const char       *app_id,
                           const char       *dbus_name,
                           GsmRestoreReason *out_restore_reason,
                           char            **out_instance_id)
{
        SavedApp *app;
        SavedAppInstance *instance = NULL;

        app = g_hash_table_lookup (save->apps, app_id);
        if (!app) {
                app = saved_app_new (app_id);
                g_hash_table_insert (save->apps, app->app_id, app);
        }

        for (const GSList *iter = app->instances; iter; iter = iter->next) {
                SavedAppInstance *candidate = iter->data;
                if (!candidate->dbus_name) { /* Check if the instance is running */
                        instance = candidate;
                        break;
                }
        }

        if (!instance) {
                char *instance_id = NULL; /* NULL -> generate a UUID for us */

                /* The first instance of each app always gets the same instance id,
                 * even if the app didn't appear in the save file. This allows
                 * the LAUNCH restore reason to function */
                if (!app->instances)
                        instance_id = save->fallback_instance_id;

                instance = saved_app_instance_new (app, instance_id, dbus_name);
                app->instances = g_slist_prepend (app->instances, instance);

                *out_restore_reason = GSM_RESTORE_REASON_LAUNCH;
        } else if (instance->crashed) {
                instance->crashed = FALSE;
                *out_restore_reason = GSM_RESTORE_REASON_RECOVER;
        } else
                *out_restore_reason = GSM_RESTORE_REASON_RESTORE;

        // TODO: Start monitoring the dbus name for crash

        *out_instance_id = instance->instance_id;

        if (!save->sealed)
                flush_to_disk (save);
}

gboolean
gsm_session_save_unregister (GsmSessionSave *save,
                             const char     *app_id,
                             const char     *instance_id)
{
        SavedApp *app;
        SavedAppInstance *instance = NULL;

        app = g_hash_table_lookup (save->apps, app_id);
        if (!app)
                return FALSE;

        for (const GSList *iter = app->instances; iter; iter = iter->next) {
                SavedAppInstance *candidate = iter->data;
                if (g_str_equal (instance_id, candidate->instance_id)) {
                        instance = candidate;
                        break;
                }
        }
        if (!instance)
                return FALSE;

        app->instances = g_slist_remove (app->instances, instance);
        saved_app_instance_free (g_steal_pointer (&instance));

        if (!app->instances) {
                g_hash_table_remove (save->apps, app_id);
                app = NULL; /* Free'd by the hash table */
        }

        if (!save->sealed)
                flush_to_disk (save);

        return TRUE;
}

GsmSessionSave *
gsm_session_save_new (const char *session_id)
{
        return g_object_new (GSM_TYPE_SESSION_SAVE,
                             "session-id", session_id,
                             NULL);
}
