/* gsm-session-save.h
 * Copyright (C) 2008 Lucas Rocha.
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __GSM_SESSION_SAVE_H__
#define __GSM_SESSION_SAVE_H__

#include <glib.h>

#include "gsm-store.h"

G_BEGIN_DECLS

void      gsm_session_save                 (GsmStore  *client_store,
                                            GsmStore  *app_store,
                                            GSList    *ignored_apps,
                                            GError   **error);
void      gsm_session_save_clear           (void);

G_END_DECLS

#endif /* __GSM_SESSION_SAVE_H__ */
