/* ice.h - Handle session manager / ICE integration.

   Copyright (C) 1998, 1999 Tom Tromey

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA.  */

#ifndef ICE_H 
#define ICE_H

#include "headers.h"

#define MAGIC_COOKIE_LEN 16
#define ENVNAME "SESSION_MANAGER"

/* List of all auth entries.  */
extern GSList *auth_entries;

typedef struct {
  IceConn	 connection;
  GDestroyNotify clean_up;
  gpointer	 data;
} Watch;

/* Set a clean up callback for when the ice_conn is closed */
void ice_set_clean_up_handler (IceConn ice_conn,
	GDestroyNotify clean_up, gpointer data);

/* Initialize the ICE part of the session manager. */
void initialize_ice (void);

/* Clean up ICE when exiting.  */
void clean_ice (void);

/* ICE recursion guards */
void ice_frozen(void);
void ice_thawed(void);

#endif /* ICE_H */
