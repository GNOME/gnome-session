/* prop.h - Functions to manipulate client properties.

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

#ifndef PROP_H
#define PROP_H

#include "headers.h"

/* Call this to find the named property for a client.  Returns NULL if
   not found.  */
SmProp *find_property_by_name (const Client *client, const char *name);

/* Find property NAME attached to CLIENT.  If not found, or type is
   not CARD8, then return FALSE.  Otherwise set *RESULT to the value
   and return TRUE.  */
gboolean find_card8_property (const Client *client, const char *name,
                              int *result);

/* Find property NAME attached to CLIENT.  If not found, or type is
   not ARRAY8, then return FALSE.  Otherwise set *RESULT to the value
   and return TRUE.  *RESULT is g_malloc()d and must be freed by the
   caller.  */
gboolean find_string_property (const Client *client, const char *name,
                               char **result);

/* Find property NAME attached to CLIENT.  If not found, or type is
   not LISTofARRAY8, then return FALSE.	 Otherwise set *ARGCP to the
   number of vector elements, *ARGVP to the elements themselves, and
   return TRUE.	 Each element of *ARGVP is g_malloc()d, as is *ARGVP
   itself.  You can use `g_strfreev' to free the result.  */
gboolean find_vector_property (const Client *client, const char *name,
	int *argcp, char ***argvp);

#endif /* PROP_H */
