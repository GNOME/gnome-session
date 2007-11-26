/* session.h
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

#ifndef __GSM_SESSION_H__
#define __GSM_SESSION_H__

#include <glib.h>
#include "client.h"

G_BEGIN_DECLS

typedef struct _GsmSession GsmSession;
extern GsmSession *global_session;

typedef enum {
  /* gsm's own startup/initialization phase */
  GSM_SESSION_PHASE_STARTUP,
  
  /* xrandr setup, gnome-settings-daemon, etc */
  GSM_SESSION_PHASE_INITIALIZATION,
  
  /* window/compositing managers */
  GSM_SESSION_PHASE_WINDOW_MANAGER,
  
  /* apps that will create _NET_WM_WINDOW_TYPE_PANEL windows */
  GSM_SESSION_PHASE_PANEL,
  
  /* apps that will create _NET_WM_WINDOW_TYPE_DESKTOP windows */
  GSM_SESSION_PHASE_DESKTOP,
  
  /* everything else */
  GSM_SESSION_PHASE_APPLICATION,
  
  /* done launching */
  GSM_SESSION_PHASE_RUNNING,
  
  /* shutting down */
  GSM_SESSION_PHASE_SHUTDOWN
} GsmSessionPhase;

typedef enum {
  GSM_SESSION_LOGOUT_TYPE_LOGOUT,
  GSM_SESSION_LOGOUT_TYPE_SHUTDOWN
} GsmSessionLogoutType;

typedef enum {
  GSM_SESSION_LOGOUT_MODE_NORMAL,
  GSM_SESSION_LOGOUT_MODE_NO_CONFIRMATION,
  GSM_SESSION_LOGOUT_MODE_FORCE
} GsmSessionLogoutMode;

GsmSession      *gsm_session_new               (gboolean    failsafe);

void             gsm_session_start             (GsmSession *session);

GsmSessionPhase  gsm_session_get_phase         (GsmSession *session);

void             gsm_session_initiate_shutdown (GsmSession *session,
						gboolean    show_confirmation,
                                                GsmSessionLogoutType logout_type);

char            *gsm_session_register_client   (GsmSession *session,
						GsmClient  *client,
						const char *previous_id);

G_END_DECLS

#endif /* __GSM_SESSION_H__ */
