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

#define GSM_TYPE_CLIENT            (gsm_client_get_type ())
G_DECLARE_DERIVABLE_TYPE (GsmClient, gsm_client, GSM, CLIENT, GObject)

typedef struct _GsmApp           GsmApp;
typedef struct _GsmClient        GsmClient;
typedef struct _GsmClientClass   GsmClientClass;

typedef enum {
        GSM_CLIENT_UNREGISTERED = 0,
        GSM_CLIENT_REGISTERED,
        GSM_CLIENT_FINISHED,
        GSM_CLIENT_FAILED
} GsmClientStatus;

typedef enum {
        GSM_CLIENT_END_SESSION_FLAG_FORCEFUL = 1 << 0,
        GSM_CLIENT_END_SESSION_FLAG_SAVE     = 1 << 1,
        GSM_CLIENT_END_SESSION_FLAG_LAST     = 1 << 2
} GsmClientEndSessionFlag;

struct _GsmClientClass
{
        GObjectClass parent_class;

        /* signals */
        void         (*disconnected)               (GsmClient  *client);
        void         (*end_session_response)       (GsmClient  *client,
                                                    gboolean    ok,
                                                    gboolean    do_last,
                                                    gboolean    cancel,
                                                    const char *reason);

        /* virtual methods */
        char *                (*impl_get_app_name)           (GsmClient *client);
        guint                 (*impl_get_unix_process_id)    (GsmClient *client);
        gboolean              (*impl_query_end_session)      (GsmClient *client,
                                                              GsmClientEndSessionFlag flags,
                                                              GError   **error);
        gboolean              (*impl_end_session)            (GsmClient *client,
                                                              GsmClientEndSessionFlag flags,
                                                              GError   **error);
        gboolean              (*impl_cancel_end_session)     (GsmClient *client,
                                                              GError   **error);
        gboolean              (*impl_stop)                   (GsmClient *client,
                                                              GError   **error);
};

const char           *gsm_client_peek_id                    (GsmClient  *client);


const char *          gsm_client_peek_startup_id            (GsmClient  *client);
const char *          gsm_client_peek_app_id                (GsmClient  *client);
guint                 gsm_client_peek_status                (GsmClient  *client);


char                 *gsm_client_get_app_name               (GsmClient  *client);
void                  gsm_client_set_app_id                 (GsmClient  *client,
                                                             const char *app_id);
void                  gsm_client_set_status                 (GsmClient  *client,
                                                             guint       status);

gboolean              gsm_client_end_session                (GsmClient                *client,
                                                             GsmClientEndSessionFlag   flags,
                                                             GError                  **error);
gboolean              gsm_client_query_end_session          (GsmClient               *client,
                                                             GsmClientEndSessionFlag  flags,
                                                             GError                  **error);
gboolean              gsm_client_cancel_end_session         (GsmClient  *client,
                                                             GError    **error);

void                  gsm_client_disconnected               (GsmClient  *client);

gboolean              gsm_client_stop                       (GsmClient  *client,
                                                             GError    **error);

/* private */

void                  gsm_client_end_session_response       (GsmClient  *client,
                                                             gboolean    is_ok,
                                                             gboolean    do_last,
                                                             gboolean    cancel,
                                                             const char *reason);

G_END_DECLS

#endif /* __GSM_CLIENT_H__ */
