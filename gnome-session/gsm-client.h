/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Novell, Inc.
 * Copyright (C) 2008 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __GSM_CLIENT_H__
#define __GSM_CLIENT_H__

#include <glib.h>
#include <glib-object.h>
#include <sys/types.h>

G_BEGIN_DECLS

typedef enum {
        GSM_CLIENT_END_SESSION_FLAG_NONE = 0,
        GSM_CLIENT_END_SESSION_FLAG_FORCEFUL = 1 << 0,
} GsmClientEndSessionFlag;

#define GSM_TYPE_CLIENT            (gsm_client_get_type ())
G_DECLARE_FINAL_TYPE (GsmClient, gsm_client, GSM, CLIENT, GObject)

GsmClient *           gsm_client_new                        (const char *app_id,
                                                             const char *bus_name);

const char           *gsm_client_peek_id                    (GsmClient  *client);
const char *          gsm_client_peek_bus_name              (GsmClient  *client);

const char *          gsm_client_peek_app_id                (GsmClient  *client);

void                  gsm_client_set_app_id                 (GsmClient  *client,
                                                             const char *app_id);

gboolean              gsm_client_end_session                (GsmClient                *client,
                                                             GsmClientEndSessionFlag   flags,
                                                             GError                  **error);
gboolean              gsm_client_query_end_session          (GsmClient               *client,
                                                             GsmClientEndSessionFlag  flags,
                                                             GError                  **error);
gboolean              gsm_client_cancel_end_session         (GsmClient  *client,
                                                             GError    **error);

gboolean              gsm_client_stop                       (GsmClient  *client,
                                                             GError    **error);
G_END_DECLS

#endif /* __GSM_CLIENT_H__ */
