/* dbus.h
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

#include <dbus/dbus-glib.h>

#ifndef __GSM_DBUS_H__
#define __GSM_DBUS_H__

void gsm_dbus_init          (void);
void gsm_dbus_check         (void);
void gsm_dbus_session_over  (void);
void gsm_dbus_run           (void);
void gsm_dbus_shutdown      (void);

DBusGConnection *gsm_dbus_get_connection (void);

#endif /* __GSM_DBUS_H__ */
