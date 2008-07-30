/* gsm-gconf.h
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

#ifndef __GSM_GCONF_H__
#define __GSM_GCONF_H__

#include <gconf/gconf-client.h>

void         gsm_gconf_init       (void);
void         gsm_gconf_check      (void);
void         gsm_gconf_shutdown   (void);

GConfClient *gsm_gconf_get_client (void);

#endif /* __GSM_GCONF_H__ */
