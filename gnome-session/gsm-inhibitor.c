/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "config.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "gsm-inhibitor.h"

static guint32 inhibitor_serial = 1;

#define GSM_INHIBITOR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSM_TYPE_INHIBITOR, GsmInhibitorPrivate))

struct GsmInhibitorPrivate
{
        char *id;
        char *bus_name;
        char *app_id;
        char *client_id;
        char *reason;
        guint flags;
        guint toplevel_xid;
        guint cookie;
};

enum {
        PROP_0,
        PROP_BUS_NAME,
        PROP_REASON,
        PROP_APP_ID,
        PROP_CLIENT_ID,
        PROP_FLAGS,
        PROP_TOPLEVEL_XID,
        PROP_COOKIE,
};

G_DEFINE_TYPE (GsmInhibitor, gsm_inhibitor, G_TYPE_OBJECT)

static guint32
get_next_inhibitor_serial (void)
{
        guint32 serial;

        serial = inhibitor_serial++;

        if ((gint32)inhibitor_serial < 0) {
                inhibitor_serial = 1;
        }

        return serial;
}

static GObject *
gsm_inhibitor_constructor (GType                  type,
                           guint                  n_construct_properties,
                           GObjectConstructParam *construct_properties)
{
        GsmInhibitor *inhibitor;

        inhibitor = GSM_INHIBITOR (G_OBJECT_CLASS (gsm_inhibitor_parent_class)->constructor (type,
                                                                                             n_construct_properties,
                                                                                             construct_properties));

        g_free (inhibitor->priv->id);
        inhibitor->priv->id = g_strdup_printf ("/org/gnome/SessionManager/Inhibitor%u", get_next_inhibitor_serial ());

        return G_OBJECT (inhibitor);
}

static void
gsm_inhibitor_init (GsmInhibitor *inhibitor)
{
        inhibitor->priv = GSM_INHIBITOR_GET_PRIVATE (inhibitor);
}

static void
gsm_inhibitor_set_bus_name (GsmInhibitor  *inhibitor,
                            const char    *bus_name)
{
        g_return_if_fail (GSM_IS_INHIBITOR (inhibitor));

        g_free (inhibitor->priv->bus_name);

        inhibitor->priv->bus_name = g_strdup (bus_name);
        g_object_notify (G_OBJECT (inhibitor), "bus-name");
}

static void
gsm_inhibitor_set_app_id (GsmInhibitor  *inhibitor,
                          const char    *app_id)
{
        g_return_if_fail (GSM_IS_INHIBITOR (inhibitor));

        g_free (inhibitor->priv->app_id);

        inhibitor->priv->app_id = g_strdup (app_id);
        g_object_notify (G_OBJECT (inhibitor), "app-id");
}

static void
gsm_inhibitor_set_client_id (GsmInhibitor  *inhibitor,
                             const char    *client_id)
{
        g_return_if_fail (GSM_IS_INHIBITOR (inhibitor));

        g_free (inhibitor->priv->client_id);

        inhibitor->priv->client_id = g_strdup (client_id);
        g_object_notify (G_OBJECT (inhibitor), "client-id");
}

static void
gsm_inhibitor_set_reason (GsmInhibitor  *inhibitor,
                          const char    *reason)
{
        g_return_if_fail (GSM_IS_INHIBITOR (inhibitor));

        g_free (inhibitor->priv->reason);

        inhibitor->priv->reason = g_strdup (reason);
        g_object_notify (G_OBJECT (inhibitor), "reason");
}

static void
gsm_inhibitor_set_cookie (GsmInhibitor  *inhibitor,
                          guint          cookie)
{
        g_return_if_fail (GSM_IS_INHIBITOR (inhibitor));

        if (inhibitor->priv->cookie != cookie) {
                inhibitor->priv->cookie = cookie;
                g_object_notify (G_OBJECT (inhibitor), "cookie");
        }
}

static void
gsm_inhibitor_set_flags (GsmInhibitor  *inhibitor,
                         guint          flags)
{
        g_return_if_fail (GSM_IS_INHIBITOR (inhibitor));

        if (inhibitor->priv->flags != flags) {
                inhibitor->priv->flags = flags;
                g_object_notify (G_OBJECT (inhibitor), "flags");
        }
}

static void
gsm_inhibitor_set_toplevel_xid (GsmInhibitor  *inhibitor,
                                guint          xid)
{
        g_return_if_fail (GSM_IS_INHIBITOR (inhibitor));

        if (inhibitor->priv->toplevel_xid != xid) {
                inhibitor->priv->toplevel_xid = xid;
                g_object_notify (G_OBJECT (inhibitor), "toplevel-xid");
        }
}

const char *
gsm_inhibitor_get_bus_name (GsmInhibitor  *inhibitor)
{
        g_return_val_if_fail (GSM_IS_INHIBITOR (inhibitor), NULL);

        return inhibitor->priv->bus_name;
}

const char *
gsm_inhibitor_get_id (GsmInhibitor *inhibitor)
{
        g_return_val_if_fail (GSM_IS_INHIBITOR (inhibitor), NULL);

        return inhibitor->priv->id;
}

const char *
gsm_inhibitor_get_app_id (GsmInhibitor  *inhibitor)
{
        g_return_val_if_fail (GSM_IS_INHIBITOR (inhibitor), NULL);

        return inhibitor->priv->app_id;
}

const char *
gsm_inhibitor_get_client_id (GsmInhibitor  *inhibitor)
{
        g_return_val_if_fail (GSM_IS_INHIBITOR (inhibitor), NULL);

        return inhibitor->priv->client_id;
}

const char *
gsm_inhibitor_get_reason (GsmInhibitor  *inhibitor)
{
        g_return_val_if_fail (GSM_IS_INHIBITOR (inhibitor), NULL);

        return inhibitor->priv->reason;
}

guint
gsm_inhibitor_get_flags (GsmInhibitor  *inhibitor)
{
        g_return_val_if_fail (GSM_IS_INHIBITOR (inhibitor), 0);

        return inhibitor->priv->flags;
}

guint
gsm_inhibitor_get_toplevel_xid (GsmInhibitor  *inhibitor)
{
        g_return_val_if_fail (GSM_IS_INHIBITOR (inhibitor), 0);

        return inhibitor->priv->toplevel_xid;
}

guint
gsm_inhibitor_get_cookie (GsmInhibitor  *inhibitor)
{
        g_return_val_if_fail (GSM_IS_INHIBITOR (inhibitor), 0);

        return inhibitor->priv->cookie;
}

static void
gsm_inhibitor_set_property (GObject       *object,
                            guint          prop_id,
                            const GValue  *value,
                            GParamSpec    *pspec)
{
        GsmInhibitor *self;

        self = GSM_INHIBITOR (object);

        switch (prop_id) {
        case PROP_BUS_NAME:
                gsm_inhibitor_set_bus_name (self, g_value_get_string (value));
                break;
        case PROP_APP_ID:
                gsm_inhibitor_set_app_id (self, g_value_get_string (value));
                break;
        case PROP_CLIENT_ID:
                gsm_inhibitor_set_client_id (self, g_value_get_string (value));
                break;
        case PROP_REASON:
                gsm_inhibitor_set_reason (self, g_value_get_string (value));
                break;
        case PROP_FLAGS:
                gsm_inhibitor_set_flags (self, g_value_get_uint (value));
                break;
        case PROP_COOKIE:
                gsm_inhibitor_set_cookie (self, g_value_get_uint (value));
                break;
        case PROP_TOPLEVEL_XID:
                gsm_inhibitor_set_toplevel_xid (self, g_value_get_uint (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_inhibitor_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
        GsmInhibitor *self;

        self = GSM_INHIBITOR (object);

        switch (prop_id) {
        case PROP_BUS_NAME:
                g_value_set_string (value, self->priv->bus_name);
                break;
        case PROP_APP_ID:
                g_value_set_string (value, self->priv->app_id);
                break;
        case PROP_CLIENT_ID:
                g_value_set_string (value, self->priv->client_id);
                break;
        case PROP_REASON:
                g_value_set_string (value, self->priv->reason);
                break;
        case PROP_FLAGS:
                g_value_set_uint (value, self->priv->flags);
                break;
        case PROP_COOKIE:
                g_value_set_uint (value, self->priv->cookie);
                break;
        case PROP_TOPLEVEL_XID:
                g_value_set_uint (value, self->priv->toplevel_xid);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_inhibitor_finalize (GObject *object)
{
        GsmInhibitor *inhibitor = (GsmInhibitor *) object;

        g_free (inhibitor->priv->id);
        g_free (inhibitor->priv->bus_name);
        g_free (inhibitor->priv->app_id);
        g_free (inhibitor->priv->client_id);
        g_free (inhibitor->priv->reason);

        G_OBJECT_CLASS (gsm_inhibitor_parent_class)->finalize (object);
}

static void
gsm_inhibitor_class_init (GsmInhibitorClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize             = gsm_inhibitor_finalize;
        object_class->constructor          = gsm_inhibitor_constructor;
        object_class->get_property         = gsm_inhibitor_get_property;
        object_class->set_property         = gsm_inhibitor_set_property;

        g_object_class_install_property (object_class,
                                         PROP_BUS_NAME,
                                         g_param_spec_string ("bus-name",
                                                              "bus-name",
                                                              "bus-name",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_APP_ID,
                                         g_param_spec_string ("app-id",
                                                              "app-id",
                                                              "app-id",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_CLIENT_ID,
                                         g_param_spec_string ("client-id",
                                                              "client-id",
                                                              "client-id",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_REASON,
                                         g_param_spec_string ("reason",
                                                              "reason",
                                                              "reason",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_FLAGS,
                                         g_param_spec_uint ("flags",
                                                            "flags",
                                                            "flags",
                                                            0,
                                                            G_MAXINT,
                                                            0,
                                                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_TOPLEVEL_XID,
                                         g_param_spec_uint ("toplevel-xid",
                                                            "toplevel-xid",
                                                            "toplevel-xid",
                                                            0,
                                                            G_MAXINT,
                                                            0,
                                                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_COOKIE,
                                         g_param_spec_uint ("cookie",
                                                            "cookie",
                                                            "cookie",
                                                            0,
                                                            G_MAXINT,
                                                            0,
                                                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_type_class_add_private (klass, sizeof (GsmInhibitorPrivate));
}

GsmInhibitor *
gsm_inhibitor_new (const char    *app_id,
                   guint          toplevel_xid,
                   guint          flags,
                   const char    *reason,
                   const char    *bus_name,
                   guint          cookie)
{
        GsmInhibitor *inhibitor;

        inhibitor = g_object_new (GSM_TYPE_INHIBITOR,
                                  "app-id", app_id,
                                  "reason", reason,
                                  "bus-name", bus_name,
                                  "flags", flags,
                                  "toplevel-xid", toplevel_xid,
                                  "cookie", cookie,
                                  NULL);

        return inhibitor;
}

GsmInhibitor *
gsm_inhibitor_new_for_client (const char    *client_id,
                              const char    *app_id,
                              guint          flags,
                              const char    *reason,
                              const char    *bus_name,
                              guint          cookie)
{
        GsmInhibitor *inhibitor;

        inhibitor = g_object_new (GSM_TYPE_INHIBITOR,
                                  "client-id", client_id,
                                  "app-id", app_id,
                                  "reason", reason,
                                  "bus-name", bus_name,
                                  "flags", flags,
                                  "cookie", cookie,
                                  NULL);

        return inhibitor;
}
