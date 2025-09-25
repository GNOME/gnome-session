/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <gio/gio.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GSM_TYPE_XDP_SESSION (gsm_xdp_session_get_type ())
G_DECLARE_FINAL_TYPE (GsmXdpSession, gsm_xdp_session,
                      GSM, XDP_SESSION,
                      GObject)

GsmXdpSession *gsm_xdp_session_new        (const char       *session_id,
                                           const char       *app_id,
                                           GDBusConnection  *connection,
                                           GError          **error);
GsmXdpSession *gsm_xdp_session_find       (const char       *session_id);
const char    *gsm_xdp_session_get_app_id (GsmXdpSession    *session);

G_END_DECLS
