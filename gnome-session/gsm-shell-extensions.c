/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011 Red Hat, Inc
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
 * Authors:
 *      Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include <config.h>

#include <glib.h>
#include <gio/gio.h>
#include <json-glib/json-glib.h>

#include "gsm-shell-extensions.h"


struct _GsmShellExtension
{
  gchar *uuid;

  gchar *name;
  gchar *description;
  gboolean enabled;
};

static void
gsm_shell_extension_free (gpointer data)
{
  GsmShellExtension *extension = (GsmShellExtension *)data;

  g_free (extension->uuid);
  g_free (extension->name);
  g_free (extension->description);
}

gchar *
gsm_shell_extension_get_uuid (GsmShellExtension *extension)
{
  return extension->uuid;
}

gchar *
gsm_shell_extension_get_name (GsmShellExtension *extension)
{
  return extension->name;
}

gchar *
gsm_shell_extension_get_description (GsmShellExtension *extension)
{
  return extension->description;
}

gboolean
gsm_shell_extension_get_is_enabled (GsmShellExtension *extension)
{
  return extension->enabled;
}

#define SHELL_SCHEMA "org.gnome.shell"
#define ENABLED_EXTENSIONS_KEY "enabled-extensions"

#define SHELL_EXTENSIONS_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSM_TYPE_SHELL_EXTENSIONS, GsmShellExtensionsPrivate))

struct _GsmShellExtensionsPrivate
{
  GSettings *settings;

  /* uuid => GsmShellExtension */
  GHashTable *uuid_to_extension;
};

G_DEFINE_TYPE (GsmShellExtensions, gsm_shell_extensions, G_TYPE_OBJECT);

/**
 * gsm_shell_extensions_finalize:
 * @object: (in): A #GsmShellExtensions.
 *
 * Finalizer for a #GsmShellExtensions instance.  Frees any resources held by
 * the instance.
 */
static void
gsm_shell_extensions_finalize (GObject *object)
{
  GsmShellExtensions *extensions = GSM_SHELL_EXTENSIONS (object);
  GsmShellExtensionsPrivate *priv = extensions->priv;

  if (priv->settings != NULL)
    {
      g_object_unref (priv->settings);
      priv->settings = NULL;
    }

  if (priv->uuid_to_extension != NULL)
    {
      g_hash_table_unref (priv->uuid_to_extension);
      priv->uuid_to_extension = NULL;
    }

  G_OBJECT_CLASS (gsm_shell_extensions_parent_class)->finalize (object);
}

/**
 * gsm_shell_extensions_class_init:
 * @klass: (in): A #GsmShellExtensionsClass.
 *
 * Initializes the #GsmShellExtensionsClass and prepares the vtable.
 */
static void
gsm_shell_extensions_class_init (GsmShellExtensionsClass *klass)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = gsm_shell_extensions_finalize;
  g_type_class_add_private (object_class, sizeof (GsmShellExtensionsPrivate));
}

static void
gsm_shell_extensions_fetch_enabled (GsmShellExtensions *self)
{
  gchar **enabled_uuids;
  gchar **uuids;

  enabled_uuids = g_settings_get_strv (self->priv->settings, ENABLED_EXTENSIONS_KEY);

  uuids = enabled_uuids;
  while (*uuids != '\0')
    {
      GsmShellExtension *extension;
      extension = g_hash_table_lookup (self->priv->uuid_to_extension, *uuids);
      if (extension != NULL)
        extension->enabled = TRUE;

      uuids ++;
    }

  g_strfreev (enabled_uuids);
}

static void
gsm_shell_extensions_scan_dir (GsmShellExtensions *self,
                               GFile              *dir)
{
  GFileEnumerator *enumerator;
  GFileInfo *info;
  JsonParser *metadata_parser;

  metadata_parser = json_parser_new ();

  enumerator = g_file_enumerate_children (dir,
                                          "standard::*",
                                          G_FILE_QUERY_INFO_NONE,
                                          NULL,
                                          NULL);

  if (enumerator == NULL)
    return;

  while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL)
    {
      GsmShellExtension *extension = g_slice_new (GsmShellExtension);
      gchar *metadata_filename;
      gchar *metadata_uuid;
      JsonObject *metadata_root;

      extension->uuid = (char *) g_file_info_get_name (info);

      metadata_filename = g_build_filename (g_file_get_path (dir),
                                            extension->uuid,
                                            "metadata.json",
                                            NULL);

      if (!json_parser_load_from_file (metadata_parser, metadata_filename, NULL))
        continue;

      g_free (metadata_filename);

      metadata_root = json_node_get_object (json_parser_get_root (metadata_parser));

      metadata_uuid = g_strdup (json_object_get_string_member (metadata_root, "uuid"));
      if (!g_str_equal (metadata_uuid, extension->uuid))
        {
          g_warning ("Extension with dirname '%s' does not match metadata's UUID of '%s'. Skipping.",
                     extension->uuid,
                     metadata_uuid);
          continue;
        }

      extension->enabled = FALSE;
      extension->name = g_strdup (json_object_get_string_member (metadata_root, "name"));
      extension->description = g_strdup (json_object_get_string_member (metadata_root, "description"));

      g_hash_table_insert (self->priv->uuid_to_extension,
                           g_strdup (extension->uuid), extension);
    }
}

static void
gsm_shell_extensions_scan (GsmShellExtensions *self)
{
  gchar *dirname;
  GFile *dir;
  const gchar * const * system_data_dirs;

  /* User data dir first. */
  dirname = g_build_filename (g_get_user_data_dir (), "gnome-shell", "extensions", NULL);
  dir = g_file_new_for_path (dirname);
  g_free (dirname);

  gsm_shell_extensions_scan_dir (self, dir);
  g_object_unref (dir);

  system_data_dirs = g_get_system_data_dirs ();
  while ((*system_data_dirs) != '\0')
    {
      dirname = g_build_filename (*system_data_dirs, "gnome-shell", "extensions", NULL);
      dir = g_file_new_for_path (dirname);
      g_free (dirname);

      gsm_shell_extensions_scan_dir (self, dir);
      g_object_unref (dir);
      system_data_dirs ++;
    }
}

/**
 * gsm_shell_extensions_init:
 * @self: (in): A #GsmShellExtensions.
 *
 * Initializes the newly created #GsmShellExtensions instance.
 */
static void
gsm_shell_extensions_init (GsmShellExtensions *self)
{
  gchar * const * schemas;

  self->priv = SHELL_EXTENSIONS_PRIVATE (self);

  /* Unfortunately, gsettings does not have a way to test
   * for the existance of a schema, so hack around it. */
  schemas = g_settings_list_schemas ();
  while (schemas != '\0')
    {
      if (g_str_equal (*schemas, SHELL_SCHEMA))
        {
          self->priv->settings = g_settings_new (SHELL_SCHEMA);
          break;
        }

      schemas ++;
    }

  if (self->priv->settings)
    {
      self->priv->uuid_to_extension = g_hash_table_new_full (g_str_hash,
                                                             g_str_equal,
                                                             g_free,
                                                             gsm_shell_extension_free);
      gsm_shell_extensions_scan (self);
      gsm_shell_extensions_fetch_enabled (self);
    }
}

gboolean
gsm_shell_extensions_set_enabled (GsmShellExtensions *self,
                                  gchar              *uuid,
                                  gboolean            enabled)
{
  gsize i, length;
  gchar **uuids;
  const gchar **new_uuids;
  GsmShellExtension *extension;

  if (self->priv->settings == NULL)
    return FALSE;

  extension = g_hash_table_lookup (self->priv->uuid_to_extension,
                                   uuid);

  if (extension == NULL)
    return FALSE;

  if (extension->enabled == enabled)
    return TRUE;

  uuids = g_settings_get_strv (self->priv->settings, ENABLED_EXTENSIONS_KEY);
  length = g_strv_length (uuids);

  if (enabled)
    {
      new_uuids = g_new (const gchar *, length + 2); /* New key, NULL */
      for (i = 0; i < length; i ++)
        new_uuids[i] = g_strdup (uuids[i]);

      new_uuids[i++] = g_strdup (uuid);
      new_uuids[i] = NULL;
    }
  else
    {
      gsize j = 0;
      new_uuids = g_new (const gchar *, length);
      for (i = 0; i < length; i ++)
        {
          if (g_str_equal (uuids[i], uuid))
            continue;

          new_uuids[j] = g_strdup (uuids[i]);
          j ++;
        }

      new_uuids[j] = NULL;
    }

  g_strfreev (uuids);

  if (g_settings_set_strv (self->priv->settings,
                           ENABLED_EXTENSIONS_KEY,
                           new_uuids))
    {
      extension->enabled = enabled;
      return TRUE;
    }

  return FALSE;
}

void
gsm_shell_extensions_foreach (GsmShellExtensions    *self,
                              GsmShellExtensionFunc  func,
                              gpointer               user_data)
{
  GHashTableIter iter;
  GsmShellExtension *extension;

  if (self->priv->settings == NULL)
    return;

  g_hash_table_iter_init (&iter, self->priv->uuid_to_extension);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer) &extension))
    (*func) (self, extension, user_data);
}

GsmShellExtension *
gsm_shell_extensions_get_for_uuid (GsmShellExtensions *self,
                                   gchar              *uuid)
{
  if (self->priv->settings == NULL)
    return NULL;

  return g_hash_table_lookup (self->priv->uuid_to_extension, uuid);
}

guint
gsm_shell_extensions_n_extensions (GsmShellExtensions *self)
{
  if (self->priv->settings == NULL)
    return 0;

  return g_hash_table_size (self->priv->uuid_to_extension);
}
