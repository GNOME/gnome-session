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

#include "prop.h"

gboolean
gsm_parse_vector_prop (SmProp   *prop,
		       int      *argcp,
		       char   ***argvp)
{
  int i;

  if (!prop || strcmp (prop->type, SmLISTofARRAY8))
    {
      *argcp = 0;
      *argvp = NULL;
      return FALSE;
    }

  *argcp = prop->num_vals;
  *argvp = g_new0 (char *, prop->num_vals + 1);
  for (i = 0; i < prop->num_vals; ++i)
    {
      (*argvp) [i] = g_malloc (prop->vals [i].length + 1);
      memcpy ((*argvp) [i], prop->vals [i].value, prop->vals [i].length);
      (*argvp) [i][prop->vals [i].length] = '\0';
    }

  return TRUE;
}

SmProp *
gsm_prop_copy (const SmProp *prop)
{
  SmProp *retval;
  int     i;

  if (!prop)
    return NULL;

  retval = (SmProp *)malloc (sizeof (SmProp));

  retval->name     = strdup (prop->name);
  retval->type     = strdup (prop->type);
  retval->num_vals = prop->num_vals;
  retval->vals     = (SmPropValue *)malloc (sizeof (SmPropValue) * prop->num_vals);

  for (i = 0; i < retval->num_vals; i++)
    {
      retval->vals [i].length = prop->vals [i].length;
      retval->vals [i].value  = strdup (prop->vals [i].value);
    }

  return retval;
}

static gboolean
gsm_array8_compare (SmPropValue *a,
		    SmPropValue *b)
{
  int i;

  if (a->length != b->length)
    return FALSE;

  for (i = 0; i < a->length; i++)
    if (((char *) a->value) [i] != ((char *) b->value) [i])
      return FALSE;

  return TRUE;
}

gboolean
gsm_prop_compare (const SmProp *a,
                  const SmProp *b)
{
  if (!a || !b)
    return FALSE;

  if (a->num_vals != b->num_vals)
    return FALSE;

  if (strcmp (a->type, b->type))
    return FALSE;

  if (!strcmp (a->type, SmCARD8))
    {
      if (*(char *) a->vals [0].value !=
          *(char *) b->vals [0].value)
	return FALSE;
    }
  else if (!strcmp (a->type, SmARRAY8))
    {
      if (!gsm_array8_compare (&a->vals [0], &b->vals [0]))
	return FALSE;
    }
  else if (!strcmp (a->type, SmLISTofARRAY8))
    {
      int i;

      for (i = 0; i < a->num_vals; i++)
	if (!gsm_array8_compare (&a->vals [i], &b->vals [i]))
	  return FALSE;
    }
  else
    return FALSE;

  return TRUE;
}

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

  g_return_val_if_fail (result != NULL, FALSE);

  prop = find_property_by_name (client, name);
  if (! prop || strcmp (prop->type, SmCARD8))
    {
      *result = 0;
      return FALSE;
    }

  p = prop->vals[0].value;
  *result = *p;

  return TRUE;
}

gboolean
find_string_property (const Client *client, const char *name,
		      char **result)
{
  SmProp *prop;

  g_return_val_if_fail (result != NULL, FALSE);

  prop = find_property_by_name (client, name);
  if (! prop || strcmp (prop->type, SmARRAY8))
    {
      *result = NULL;
      return FALSE;
    }

  *result = g_malloc (prop->vals[0].length + 1);
  memcpy (*result, prop->vals[0].value, prop->vals[0].length);
  (*result)[prop->vals[0].length] = '\0';

  return TRUE;
}

gboolean
find_vector_property (const Client *client, const char *name,
		      int *argcp, char ***argvp)
{
  SmProp *prop;

  g_return_val_if_fail (argcp != NULL, FALSE);
  g_return_val_if_fail (argvp != NULL, FALSE);

  prop = find_property_by_name (client, name);

  return gsm_parse_vector_prop (prop, argcp, argvp);
}
