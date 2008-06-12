/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Novell, Inc.
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

#ifndef __GSM_CLIENT_H__
#define __GSM_CLIENT_H__

#include <glib-object.h>
#include <sys/types.h>

G_BEGIN_DECLS

#define GSM_TYPE_CLIENT            (gsm_client_get_type ())
#define GSM_CLIENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSM_TYPE_CLIENT, GsmClient))
#define GSM_CLIENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GSM_TYPE_CLIENT, GsmClientClass))
#define GSM_IS_CLIENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSM_TYPE_CLIENT))
#define GSM_IS_CLIENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GSM_TYPE_CLIENT))
#define GSM_CLIENT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GSM_TYPE_CLIENT, GsmClientClass))

typedef struct _GsmClient        GsmClient;
typedef struct _GsmClientClass   GsmClientClass;

typedef struct GsmClientPrivate GsmClientPrivate;

typedef enum {
        GSM_CLIENT_UNMANAGED = 0,
        GSM_CLIENT_MANAGED,
        GSM_CLIENT_FINISHED,
        GSM_CLIENT_FAILED,
} GsmClientStatus;

struct _GsmClient
{
        GObject           parent;
        GsmClientPrivate *priv;
};

struct _GsmClientClass
{
        GObjectClass parent_class;

        /* signals */
        gboolean (*manage_request)   (GsmClient *client,
                                      char     **client_id);
        gboolean (*logout_request)   (GsmClient *client,
                                      gboolean   prompt);


        void (*saved_state)          (GsmClient *client);

        void (*request_phase2)       (GsmClient *client);

        void (*request_interaction)  (GsmClient *client);
        void (*interaction_done)     (GsmClient *client,
                                      gboolean   cancel_shutdown);

        void (*save_yourself_done)   (GsmClient *client);

        void (*disconnected)         (GsmClient *client);

        /* virtual methods */
        void (*stop)                 (GsmClient *client);


        char       * (*get_restart_command) (GsmClient *client);
        char       * (*get_discard_command) (GsmClient *client);
        gboolean     (*get_autorestart)     (GsmClient *client);

        void (*restart)              (GsmClient *client,
                                      GError   **error);
        void (*save_yourself)        (GsmClient *client,
                                      gboolean   save_state);
        void (*save_yourself_phase2) (GsmClient *client);
        void (*interact)             (GsmClient *client);
        void (*shutdown_cancelled)   (GsmClient *client);
};

GType       gsm_client_get_type             (void) G_GNUC_CONST;

const char *gsm_client_get_id               (GsmClient  *client);
const char *gsm_client_get_client_id        (GsmClient  *client);
int         gsm_client_get_status           (GsmClient  *client);
void        gsm_client_set_status           (GsmClient  *client,
                                             int         status);

gboolean    gsm_client_manage               (GsmClient  *client,
                                             const char *client_id);
void        gsm_client_stop                 (GsmClient  *client);



char       *gsm_client_get_restart_command  (GsmClient  *client);
char       *gsm_client_get_discard_command  (GsmClient  *client);
gboolean    gsm_client_get_autorestart      (GsmClient  *client);

void        gsm_client_save_state           (GsmClient  *client);

void        gsm_client_restart              (GsmClient  *client,
                                             GError    **error);
void        gsm_client_save_yourself        (GsmClient  *client,
                                             gboolean    save_state);
void        gsm_client_save_yourself_phase2 (GsmClient  *client);
void        gsm_client_interact             (GsmClient  *client);
void        gsm_client_shutdown_cancelled   (GsmClient  *client);

/* protected */
gboolean    gsm_client_manage_request       (GsmClient  *client,
                                             char      **id);
void        gsm_client_logout_request       (GsmClient  *client,
                                             gboolean    prompt);


void        gsm_client_saved_state          (GsmClient  *client);
void        gsm_client_request_phase2       (GsmClient  *client);
void        gsm_client_request_interaction  (GsmClient  *client);
void        gsm_client_interaction_done     (GsmClient  *client,
                                             gboolean    cancel_shutdown);
void        gsm_client_save_yourself_done   (GsmClient  *client);
void        gsm_client_disconnected         (GsmClient  *client);


G_END_DECLS

#endif /* __GSM_CLIENT_H__ */
