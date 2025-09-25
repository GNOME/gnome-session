/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <gio/gio.h>

#include "gsm-xdp-session.h"
#include "org.freedesktop.impl.portal.Session.h"

struct _GsmXdpSession
{
        GObject parent;
        XdpImplSession *skeleton;

        char *session_id;
        char *app_id;
};

enum {
        PROP_0,
        PROP_SESSION_ID,
        PROP_APP_ID,
        LAST_PROP
};

enum {
        CLOSED,
        LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static GHashTable *all_sessions = NULL;

G_DEFINE_FINAL_TYPE (GsmXdpSession, gsm_xdp_session, G_TYPE_OBJECT)

static void
gsm_xdp_session_init (GsmXdpSession *session)
{
}

static void
gsm_xdp_session_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
        GsmXdpSession *session = GSM_XDP_SESSION (object);

        switch (prop_id) {
        case PROP_SESSION_ID:
                g_set_str (&session->session_id, g_value_get_string (value));
                break;

        case PROP_APP_ID:
                g_set_str (&session->app_id, g_value_get_string (value));
                break;

        default:
                break;
        }
}

const char *
gsm_xdp_session_get_app_id (GsmXdpSession *session)
{
        return session->app_id;
}

static void
gsm_xdp_session_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
        GsmXdpSession *session = GSM_XDP_SESSION (object);

        switch (prop_id) {
        case PROP_SESSION_ID:
                g_value_set_string (value, session->session_id);
                break;

        case PROP_APP_ID:
                g_value_set_string (value, session->app_id);
                break;

        default:
                break;
        }
}

static void
unexport_skeleton (GsmXdpSession *session)
{
        g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (session->skeleton));
        g_signal_handlers_disconnect_by_data (session->skeleton, session); /* just in case... */
        g_clear_object (&session->skeleton);
}

static gboolean
handle_close_common (GsmXdpSession         *session,
                     gboolean               voluntary)
{
        unexport_skeleton (session);

        g_signal_emit (session, signals[CLOSED], 0, voluntary);
        /* Careful! The signal handler likely unref'd and finalized the session object */

        return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
handle_close (XdpImplSession        *skeleton,
              GDBusMethodInvocation *invocation,
              GsmXdpSession         *session)
{
        xdp_impl_session_complete_close (skeleton, invocation);
        return handle_close_common (session, TRUE);
}

static gboolean
handle_close2 (XdpImplSession        *skeleton,
               GDBusMethodInvocation *invocation,
               GVariant              *options,
               GsmXdpSession         *session)
{
        gboolean voluntary = FALSE;

        g_variant_lookup (options, "voluntary", "b", &voluntary);

        xdp_impl_session_complete_close2 (skeleton, invocation);
        return handle_close_common (session, voluntary);
}

static gboolean
gsm_xdp_session_export (GsmXdpSession    *session,
                        GDBusConnection  *connection,
                        GError          **error)
{
        g_autoptr (XdpImplSession) skeleton = NULL;

        skeleton = xdp_impl_session_skeleton_new ();

        g_signal_connect (skeleton, "handle-close",
                          G_CALLBACK (handle_close),
                          session);
        g_signal_connect (skeleton, "handle-close2",
                          G_CALLBACK (handle_close2),
                          session);

        xdp_impl_session_set_version (skeleton, 2);

        if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (skeleton),
                                               connection, session->session_id, error))
                return FALSE;

        session->skeleton = g_steal_pointer (&skeleton);
        return TRUE;
}

static void
gsm_xdp_session_dispose (GObject *object)
{
        GsmXdpSession *session = GSM_XDP_SESSION (object);

        g_hash_table_remove (all_sessions, session->session_id);

        if (session->skeleton) {
                xdp_impl_session_emit_closed (session->skeleton);
                unexport_skeleton (session);
        }

        g_clear_pointer (&session->session_id, g_free);
        g_clear_pointer (&session->app_id, g_free);

        G_OBJECT_CLASS (gsm_xdp_session_parent_class)->dispose (object);
}

static void
gsm_xdp_session_class_init (GsmXdpSessionClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->dispose = gsm_xdp_session_dispose;
        object_class->set_property = gsm_xdp_session_set_property;
        object_class->get_property = gsm_xdp_session_get_property;

        signals[CLOSED] = g_signal_new ("closed",
                                        G_OBJECT_CLASS_TYPE (object_class),
                                        G_SIGNAL_RUN_LAST,
                                        0, NULL, NULL, NULL,
                                        G_TYPE_NONE,
                                        1, G_TYPE_BOOLEAN);

        g_object_class_install_property (object_class,
                                         PROP_SESSION_ID,
                                         g_param_spec_string ("session-id",
                                                              "session-id",
                                                              "session-id",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

        g_object_class_install_property (object_class,
                                         PROP_APP_ID,
                                         g_param_spec_string ("app-id",
                                                              "app-id",
                                                              "app-id",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

        all_sessions = g_hash_table_new (g_str_hash, g_str_equal);
}

GsmXdpSession *
gsm_xdp_session_new (const char       *session_id,
                     const char       *app_id,
                     GDBusConnection  *connection,
                     GError          **error)
{
        g_autoptr (GsmXdpSession) session = NULL;

        session = g_object_new (GSM_TYPE_XDP_SESSION,
                                "session-id", session_id,
                                "app-id", app_id,
                                NULL);

        if (!gsm_xdp_session_export (session, connection, error))
                return NULL;

        g_hash_table_insert (all_sessions, session->session_id, session);

        return g_steal_pointer (&session);
}

GsmXdpSession *
gsm_xdp_session_find (const char *session_id)
{
        return g_hash_table_lookup (all_sessions, session_id);
}
