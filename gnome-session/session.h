/* session.h - Information shared between gnome-session and other programs.

   Copyright (C) 1998 Tom Tromey

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA.  */

#ifndef SESSION_H
#define SESSION_H

/* Default session name. */
#define DEFAULT_SESSION "Default"

/* Config prefix used to store the sysadmin's default sessions. */
#define DEFAULT_CONFIG_PREFIX "=" DEFAULTDIR "/default.session=/"

/* Config prefix used to store the users' sessions. */
#define CONFIG_PREFIX "session/"

/* Config prefix used for gnome-session's own config details. */
#define GSM_CONFIG_PREFIX CONFIG_PREFIX "__GSM__/"

/* Name of key used to store the current session name. */
#define CURRENT_SESSION_KEY "Current Session"

/* Name of key used to store number of clients in a session. */
#define CLIENT_COUNT_KEY "num_clients"

/* A config section reserved for internal usage */
#define RESERVED_SECTION CONFIG_PREFIX "__GSM_RESERVED__/"

#endif /* SESSION_H */
