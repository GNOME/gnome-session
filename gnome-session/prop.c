/* prop.c - Functions to manipulate client properties.

   Copyright (C) 1998, 1999 Tom Tromey

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

#include <stdlib.h>
#include <string.h>

#include "manager.h"

/* Find a property given the client and the property name.  Returns
   NULL if not found.  */
SmProp *
find_property_by_name (const Client *client, const char *name)
{
  GSList *list;

  for (list = client->properties; list; list = list->next)
    {
      SmProp *prop = (SmProp *) list->data;
      if (! strcmp (prop->name, name))
	return prop;
    }

  return NULL;
}

gboolean
find_card8_property (const Client *client, const char *name,
		     int *result)
{
  SmProp *prop;
  char *p;

  prop = find_property_by_name (client, name);
  if (! prop || strcmp (prop->type, SmCARD8))
    return FALSE;

  p = prop->vals[0].value;
  *result = *p;

  return TRUE;
}

gboolean
find_string_property (const Client *client, const char *name,
		      char **result)
{
  SmProp *prop;

  prop = find_property_by_name (client, name);
  if (! prop || strcmp (prop->type, SmARRAY8))
    return FALSE;

  *result = malloc (prop->vals[0].length + 1);
  memcpy (*result, prop->vals[0].value, prop->vals[0].length);
  (*result)[prop->vals[0].length] = '\0';

  return TRUE;
}

gboolean
find_vector_property (const Client *client, const char *name,
		      int *argcp, char ***argvp)
{
  SmProp *prop;
  int i;

  prop = find_property_by_name (client, name);
  if (! prop || strcmp (prop->type, SmLISTofARRAY8))
    return FALSE;

  *argcp = prop->num_vals;
  *argvp = (char **) malloc (*argcp * sizeof (char *));
  for (i = 0; i < *argcp; ++i)
    {
      (*argvp)[i] = malloc (prop->vals[i].length + 1);
      memcpy ((*argvp)[i], prop->vals[i].value, prop->vals[i].length);
      (*argvp)[i][prop->vals[i].length] = '\0';
    }

  return TRUE;
}

void
free_vector (int argc, char **argv)
{
  int i;

  for (i = 0; i < argc; ++i)
    free (argv[i]);
  if (argv)
    free (argv);
}
