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
#include "gsm-xdp-session.h"

#include "org.freedesktop.impl.portal.SaveRestore.h"

/* We're not generating quite so many instance IDs that we'll actually need the
 * entire length of the SHA 256 hash. So let's follow in git's footsteps and
 * truncate the instance ID to a more manageable length. It's still vanishingly
 * unlikely to run into a collision, given that apps won't have more than a couple
 * dozen instances running (and that's on the extreme high end...) */
#define INSTANCE_ID_LEN 10

struct _GsmSessionSave
{
        GObject          parent;

        XdpImplSaveRestore *skeleton;

        char *session_id;
        char *file_path;
        gboolean sealed;
        GHashTable *apps;
};

enum {
        PROP_0,
        PROP_SESSION_ID,
        LAST_PROP
};

G_DEFINE_TYPE (GsmSessionSave, gsm_session_save, G_TYPE_OBJECT);

typedef enum {
        RESTORE_REASON_PRISTINE,
        RESTORE_REASON_LAUNCH,
        RESTORE_REASON_RECOVER,
        RESTORE_REASON_RESTORE,
} RestoreReason;

typedef struct {
        char *app_id;
        GDesktopAppInfo *info;

        GSList *instances;

        GSList *discarded_ids;
        char *next_id;
} SavedApp;

typedef enum {
        CRASH_STATE_NO_CRASH = 0,
        CRASH_STATE_INSTANCE_CRASHED,
        CRASH_STATE_SESSION_CRASHED,
} CrashState;

typedef struct {
        char *instance_id;
        GsmXdpSession *session;
        CrashState crash_state;
} SavedAppInstance;

static SavedApp *
saved_app_new (const char *app_id)
{
        g_autofree char *desktop_id = NULL;
        GDesktopAppInfo *info;
        SavedApp *app;

        desktop_id = g_strconcat (app_id, ".desktop", NULL);
        info = g_desktop_app_info_new (desktop_id);
        if (!info)
                return NULL;

        app = g_new (SavedApp, 1);
        *app = (SavedApp) {
                .app_id = g_strdup (app_id),
                .info = info,
        };
        return app;
}

static SavedAppInstance *
saved_app_instance_new (char *instance_id)
{
        SavedAppInstance *instance = g_new (SavedAppInstance, 1);
        *instance = (SavedAppInstance) {
                .instance_id = instance_id,
                .session = NULL,
                .crash_state = CRASH_STATE_NO_CRASH,
        };
        return instance;
}

static void
saved_app_instance_free (SavedAppInstance *instance)
{
        g_free (instance->instance_id);
        g_clear_object (&instance->session);
        g_free (instance);
}

static void
saved_app_discard_instance (SavedApp *app,
                            SavedAppInstance *instance)
{
        /* This is a UX quirk: basically we want the next normal launch of the
         * app to be a relaunch of the last instance the user closed. This
         * won't be a full restore (if the app is well-behaved, anyway), and
         * will restore only window positions of the app's last instance. */
        if (app->next_id)
                app->discarded_ids = g_slist_prepend (app->discarded_ids,
                                                      app->next_id);
        app->next_id = g_steal_pointer (&instance->instance_id);
}

static void
saved_app_free (SavedApp *app)
{
        g_free (app->app_id);
        g_object_unref (app->info);
        g_slist_free_full (app->instances,
                           (GDestroyNotify) saved_app_instance_free);
        g_slist_free_full (app->discarded_ids, g_free);
        g_free (app->next_id);
        g_free (app);
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
        save->file_path = g_strconcat (g_get_user_state_dir (),
                                       G_DIR_SEPARATOR_S,
                                       "gnome-session@",
                                       id,
                                       ".state",
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

        g_variant_builder_init_static (&instances, G_VARIANT_TYPE ("a{s(a{sv}aa{sv})}"));
        g_hash_table_iter_init (&app_iter, save->apps);
        while (g_hash_table_iter_next (&app_iter, NULL, &value)) {
                SavedApp *app = value;

                g_variant_builder_open (&instances, G_VARIANT_TYPE ("{s(a{sv}aa{sv})}"));
                g_variant_builder_add (&instances, "s", app->app_id);

                g_variant_builder_open (&instances, G_VARIANT_TYPE ("(a{sv}aa{sv})"));
                g_variant_builder_open (&instances, G_VARIANT_TYPE ("a{sv}"));
                if (app->next_id)
                        g_variant_builder_add (&instances,
                                               "{sv}", "next-instance-id",
                                               g_variant_new_string (app->next_id));
                if (app->discarded_ids) {
                        GVariantBuilder discarded;
                        g_variant_builder_init_static (&discarded, G_VARIANT_TYPE ("as"));
                        for (const GSList *iter = app->discarded_ids; iter; iter = iter->next) {
                                g_variant_builder_add (&discarded, "s", iter->data);
                        }
                        g_variant_builder_add (&instances, "{sv}", "discarded-instance-ids",
                                               g_variant_builder_end (&discarded));
                }
                g_variant_builder_close (&instances);

                g_variant_builder_open (&instances, G_VARIANT_TYPE ("aa{sv}"));
                for (const GSList *iter = app->instances; iter; iter = iter->next) {
                        SavedAppInstance *instance = iter->data;
                        g_variant_builder_open (&instances, G_VARIANT_TYPE ("a{sv}"));
                        g_variant_builder_add (&instances, "{sv}", "instance-id",
                                               g_variant_new_string (instance->instance_id));
                        g_variant_builder_add (&instances, "{sv}", "crashed",
                                               g_variant_new_boolean (instance->crash_state == CRASH_STATE_INSTANCE_CRASHED));
                        g_variant_builder_close (&instances);
                }
                g_variant_builder_close (&instances);

                g_variant_builder_close (&instances);
                g_variant_builder_close (&instances);
        }
        g_variant_builder_add (&builder, "{sv}", "instances",
                               g_variant_builder_end (&instances));

        variant = g_variant_ref_sink (g_variant_builder_end (&builder));

        if (g_log_get_debug_enabled ())
                g_debug ("GsmSessionSave: Flushing to disk:\n%s",
                         g_variant_print (variant, TRUE));

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
        g_autoptr (GVariant) app_props_variant = NULL;
        g_autoptr (GVariantDict) app_props = NULL;
        g_autoptr (GVariantIter) discarded_ids = NULL;
        g_autoptr (GVariantIter) instances = NULL;
        GVariant *instance_variant = NULL;
        g_autoptr (SavedApp) app = NULL;

        if (!g_variant_iter_next (iter, "{&s(@a{sv}aa{sv})}", &app_id, &app_props_variant, &instances))
                return FALSE;

        app = saved_app_new (app_id);
        if (!app) {
                g_warning ("GsmSessionSave: It appears that saved app '%s' was uninstalled. Discarding",
                           app_id);
                *out_app = NULL;
                return TRUE;
        }

        app_props = g_variant_dict_new (app_props_variant);
        g_variant_dict_lookup (app_props, "next-instance-id", "s", &app->next_id);
        if (g_variant_dict_lookup (app_props, "discarded-instance-ids", "as", &discarded_ids)) {
                char *discarded_id = NULL;
                while (g_variant_iter_loop (discarded_ids, "s", &discarded_id))
                        app->discarded_ids = g_slist_prepend (app->discarded_ids,
                                                              discarded_id);
        }

        while (g_variant_iter_loop (instances, "@a{sv}", &instance_variant)) {
                g_auto (GVariantDict) dict = G_VARIANT_DICT_INIT (instance_variant);
                g_autofree char *instance_id = NULL;
                g_autoptr (SavedAppInstance) instance = NULL;
                gboolean crashed;

                if (!g_variant_dict_lookup (&dict, "instance-id", "s", &instance_id)) {
                        g_warning ("GsmSessionSave: Dropping malformed instance of app %s",
                                   app_id);
                        continue;
                }

                instance = saved_app_instance_new (g_steal_pointer (&instance_id));

                g_variant_dict_lookup (&dict, "crashed", "b", &crashed);
                if (crashed)
                        instance->crash_state = CRASH_STATE_INSTANCE_CRASHED;
                else if (dirty)
                        instance->crash_state = CRASH_STATE_SESSION_CRASHED;

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
        g_autoptr (GVariantIter) instances_iter = NULL;
        g_autoptr (GHashTable) apps = NULL;
        SavedApp *deserialized_app;

        file = g_mapped_file_new (save->file_path, FALSE, error);
        if (!file) {
                if (!g_error_matches (*error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
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

        if (!g_variant_dict_lookup (dict, "instances", "@a{s(a{sv}aa{sv})}", &instances))
                return malformed_save_error (error, "Missing 'instances' field");

        apps = make_hash_table ();
        instances_iter = g_variant_iter_new (instances);
        while (deserialize_app (instances_iter, dirty, &deserialized_app))
                if (deserialized_app)
                        g_hash_table_insert (apps,
                                             deserialized_app->app_id,
                                             deserialized_app);

        save->apps = g_steal_pointer (&apps);

        if (g_log_get_debug_enabled ())
                g_debug ("GsmSessionSave: Loaded %s saved session from disk:\n%s",
                         dirty ? "dirty" : "clean",
                         g_variant_print (variant, TRUE));

        return TRUE;
}

static gboolean
launch_app_instance (SavedApp         *app,
                     SavedAppInstance *instance)
{
        g_autoptr (GError) error = NULL;

        if (instance->crash_state == CRASH_STATE_INSTANCE_CRASHED)
                return FALSE;

        g_debug ("GsmSessionSave: Launching instance of app (%s): %s",
                 app->app_id,
                 instance->instance_id);

        if (!gsm_util_launch_app (app->info, &error)) {
                g_warning ("GsmSessionSave: Failed to launch saved app (%s): %s",
                           app->app_id,
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
                        launched_any |= launch_app_instance(app, instance->data);
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

static char *
next_instance_id (GsmSessionSave *save,
                  SavedApp       *app)
{
        g_autoptr (GChecksum) checksum = NULL;
        SavedAppInstance *collision = NULL;
        guint32 random;

        if (app->next_id)
                return g_steal_pointer (&app->next_id);

        checksum = g_checksum_new (G_CHECKSUM_SHA256);

        while (TRUE) {
                const char *digest;

                g_checksum_update (checksum, (gpointer) "gnome-session", -1);
                g_checksum_update (checksum, (gpointer) save->session_id, -1);
                g_checksum_update (checksum, (gpointer) app->app_id, -1);
                random = g_random_int ();
                g_checksum_update (checksum, (gpointer) &random, sizeof (random));

                digest = g_checksum_get_string (checksum);

                /* Paranoia */
                for (const GSList *iter = app->instances; iter; iter = iter->next) {
                        SavedAppInstance *candidate = iter->data;
                        if (strncmp (candidate->instance_id, digest, INSTANCE_ID_LEN) == 0) {
                                collision = candidate;
                                break;
                        }
                }

                if (!collision)
                        return g_strndup (digest, INSTANCE_ID_LEN);

                g_checksum_reset (checksum);
        }
}

static void
on_session_closed (GsmXdpSession  *session,
                   gboolean        voluntary,
                   GsmSessionSave *save)
{
        const char *app_id = NULL;
        SavedApp *app = NULL;
        SavedAppInstance *instance = NULL;

        app_id = gsm_xdp_session_get_app_id (session);
        app = g_hash_table_lookup (save->apps, app_id);
        if (!app) {
                g_critical ("GsmSessionSave: Session closed for app we couldn't find");
                return;
        }

        for (const GSList *iter = app->instances; iter; iter = iter->next) {
                SavedAppInstance *candidate = iter->data;
                if (candidate->session == session) {
                        instance = candidate;
                        break;
                }
        }
        if (!instance) {
                g_critical ("GsmSessionSave: Session closed for instance we couldn't find");
                return;
        }

        if (voluntary) { /* App is voluntarily un-registering */
                saved_app_discard_instance (app, instance);
                app->instances = g_slist_remove (app->instances, instance);
                saved_app_instance_free (instance);
        } else {
                instance->crash_state = CRASH_STATE_INSTANCE_CRASHED;
                g_clear_object (&instance->session);
        }

        if (!save->sealed)
                flush_to_disk (save);
}

static gboolean
handle_register (XdpImplSaveRestore    *skeleton,
                 GDBusMethodInvocation *invocation,
                 const char            *session_handle,
                 const char            *app_id,
                 GVariant              *options,
                 GsmSessionSave        *save)
{
        g_autoptr (GError) error = NULL;
        g_autoptr (GsmXdpSession) session = NULL;
        g_auto (GVariantBuilder) out_builder =
                G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
        g_auto (GVariantBuilder) discard_ids_builder =
                G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE ("as"));
        SavedApp *app = NULL;
        SavedAppInstance *instance = NULL;
        RestoreReason reason = RESTORE_REASON_PRISTINE;

        app = g_hash_table_lookup (save->apps, app_id);
        if (!app) {
                app = saved_app_new (app_id);
                if (!app) {
                        g_dbus_method_invocation_return_error (invocation,
                                                               G_DBUS_ERROR,
                                                               G_DBUS_ERROR_INVALID_ARGS,
                                                               "Invalid app id");
                        return G_DBUS_METHOD_INVOCATION_HANDLED;
                }
                g_hash_table_insert (save->apps, app->app_id, app);
        }

        for (const GSList *iter = app->instances; iter; iter = iter->next) {
                SavedAppInstance *candidate = iter->data;
                if (!candidate->session) { /* Check if the instance is running */
                        instance = candidate;
                        break;
                }
        }

        if (!instance) {
                char *instance_id = next_instance_id (save, app);
                instance = saved_app_instance_new (instance_id);
                app->instances = g_slist_prepend (app->instances, instance);
                reason = RESTORE_REASON_LAUNCH;
        } else if (instance->crash_state != CRASH_STATE_NO_CRASH) {
                instance->crash_state = CRASH_STATE_NO_CRASH;
                reason = RESTORE_REASON_RECOVER;
        } else
                reason = RESTORE_REASON_RESTORE;

        session = gsm_xdp_session_new (session_handle,
                                       app_id,
                                       g_dbus_method_invocation_get_connection (invocation),
                                       &error);
        if (!session) {
                g_dbus_method_invocation_return_gerror (invocation, error);
                return G_DBUS_METHOD_INVOCATION_HANDLED;
        }
        g_signal_connect (session, "closed", G_CALLBACK (on_session_closed), save);
        instance->session = g_steal_pointer (&session);

        g_variant_builder_add (&out_builder, "{sv}", "instance-id",
                               g_variant_new_string (instance->instance_id));
        g_variant_builder_add (&out_builder, "{sv}", "restore-reason",
                               g_variant_new_uint32 (reason));

        for (const GSList *iter = app->discarded_ids; iter; iter = iter->next)
                g_variant_builder_add (&discard_ids_builder, "s", iter->data);
        g_variant_builder_add (&out_builder, "{sv}", "discard-instance-ids",
                               g_variant_builder_end (&discard_ids_builder));

        if (!save->sealed)
                flush_to_disk (save);

        xdp_impl_save_restore_complete_register (skeleton, invocation,
                                                 g_variant_builder_end (&out_builder));
        return G_DBUS_METHOD_INVOCATION_HANDLED;
}

typedef gboolean (*DestroyPredicate)(void *obj, void *userdata);

static GSList *
remove_all_predicate (GSList           *head,
                      DestroyPredicate  predicate,
                      void             *userdata)
{
        GSList *current, **previous_ptr = &head;

        while (*previous_ptr) {
                current = *previous_ptr;

                if (predicate (current->data, userdata)) {
                        *previous_ptr = current->next;
                        g_slist_free_1 (current);
                } else {
                        previous_ptr = &current->next;
                }
        }

        return head;
}

static gboolean
free_if_streq (char       *obj,
               const char *userdata)
{
        if (!g_str_equal (obj, userdata))
                return FALSE;

        g_free (obj);
        return TRUE;
}

static gboolean
handle_discarded_instance_ids (XdpImplSaveRestore    *skeleton,
                               GDBusMethodInvocation *invocation,
                               const char            *session_handle,
                               const char *const     *arg_instance_ids,
                               GsmSessionSave        *save)
{
        GsmXdpSession *session = NULL;
        const char *app_id = NULL;
        SavedApp *app = NULL;

        session = gsm_xdp_session_find (session_handle);
        if (!session) {
                g_dbus_method_invocation_return_error (invocation,
                                                       G_DBUS_ERROR,
                                                       G_DBUS_ERROR_INVALID_ARGS,
                                                       "Unknown session handle");
                return G_DBUS_METHOD_INVOCATION_HANDLED;
        }

        app_id = gsm_xdp_session_get_app_id (session);
        app = g_hash_table_lookup (save->apps, app_id);
        if (!app) {
                g_critical ("GsmSessionStore: DiscardedInstanceIds called for session with app we couldn't find");
                xdp_impl_save_restore_complete_discarded_instance_ids (skeleton, invocation);
                return G_DBUS_METHOD_INVOCATION_HANDLED;
        }

        for (; *arg_instance_ids; arg_instance_ids++)
                app->discarded_ids = remove_all_predicate (app->discarded_ids,
                                                           (DestroyPredicate) free_if_streq,
                                                           (void*) *arg_instance_ids);

        if (!save->sealed)
                flush_to_disk (save);

        xdp_impl_save_restore_complete_discarded_instance_ids (skeleton, invocation);
        return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
handle_save_hint_response (XdpImplSaveRestore    *skeleton,
                           GDBusMethodInvocation *invocation,
                           const char            *session_handle,
                           GsmSessionSave        *save)
{
        /* We don't ever emit Save() and so apps should never be calling SaveHintResponse */
        g_warning ("GsmSessionStore: Unexpected call to SaveHintResponse by %s",
                   gsm_xdp_session_get_app_id (gsm_xdp_session_find (session_handle)));
        xdp_impl_save_restore_complete_save_hint_response (skeleton, invocation);
        return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
free_if_not_crashed (SavedAppInstance *instance,
                     SavedApp *app)
{
        /* Remember instances of crashed apps, so that we communicate the right
         * restore reason on next launch */
        if (instance->crash_state == CRASH_STATE_INSTANCE_CRASHED)
                return FALSE;

        saved_app_discard_instance (app, instance);
        saved_app_instance_free (instance);
        return TRUE;
}

void
gsm_session_save_discard (GsmSessionSave *save)
{
        GHashTableIter iter;
        gpointer value;

        g_hash_table_iter_init (&iter, save->apps);
        while (g_hash_table_iter_next (&iter, NULL, &value)) {
                SavedApp *app = value;
                app->instances = remove_all_predicate (app->instances,
                                                       (DestroyPredicate) free_if_not_crashed,
                                                       app);
        }

        flush_to_disk (save);
}

static gboolean
portal_export (GsmSessionSave   *save,
               GError          **error)
{
        g_autoptr (GDBusConnection) connection = NULL;
        g_autoptr (XdpImplSaveRestore) skeleton = NULL;

        connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, error);
        if (!connection)
                return FALSE;

        skeleton = xdp_impl_save_restore_skeleton_new ();

        g_signal_connect (skeleton, "handle-register",
                          G_CALLBACK (handle_register),
                          save);
        g_signal_connect (skeleton, "handle-discarded-instance-ids",
                          G_CALLBACK (handle_discarded_instance_ids),
                          save);
        g_signal_connect (skeleton, "handle-save-hint-response",
                          G_CALLBACK (handle_save_hint_response),
                          save);

        xdp_impl_save_restore_set_version (skeleton, 1);

        if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (skeleton),
                                               connection, XDP_IMPL_OBJECT_PATH,
                                               error))
                return FALSE;

        save->skeleton = g_steal_pointer (&skeleton);
        return TRUE;
}

static void
gsm_session_save_constructed (GObject *object)
{
        GsmSessionSave *save = GSM_SESSION_SAVE (object);
        GError *error = NULL;

        if (g_mkdir_with_parents (g_get_user_state_dir (), 0755) < 0)
                g_warning ("GsmSessionSave: Failed to ensure state dir exists: %m");

        if (!load_from_disk (save, &error)) {
                g_warning ("GsmSessionSave: Failed to load from disk, discarding: %s",
                           error->message);
                g_clear_error (&error);
        }

        if (!save->apps)
                save->apps = make_hash_table ();

        if (!portal_export (save, &error)) {
                g_warning ("GsmSessionSave: Failed to export save/restore portal impl: %s",
                           error->message);
                g_clear_error (&error);
        }
}

static void
gsm_session_save_dispose (GObject *object)
{
        GsmSessionSave *save = GSM_SESSION_SAVE (object);

        g_clear_pointer (&save->file_path, g_free);
        g_clear_pointer (&save->apps, g_hash_table_unref);

        if (save->skeleton) {
                g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (save->skeleton));
                g_clear_object (&save->skeleton);
        }

        G_OBJECT_CLASS (gsm_session_save_parent_class)->dispose (object);
}

static void
gsm_session_save_class_init (GsmSessionSaveClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->constructed = gsm_session_save_constructed;
        object_class->dispose = gsm_session_save_dispose;
        object_class->set_property = gsm_session_save_set_property;
        object_class->get_property = gsm_session_save_get_property;

        g_object_class_install_property (object_class,
                                         PROP_SESSION_ID,
                                         g_param_spec_string ("session-id",
                                                              "session-id",
                                                              "session-id",
                                                              "gnome",
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

GsmSessionSave *
gsm_session_save_new (const char *session_id)
{
        return g_object_new (GSM_TYPE_SESSION_SAVE,
                             "session-id", session_id,
                             NULL);
}
