/* prop.c - Functions to manipulate client properties.
   Written by Tom Tromey <tromey@cygnus.com>.  */

#include <stdlib.h>

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

  /* FIXME: trailing \0?  */
  *result = malloc (prop->vals[0].length);
  memcpy (*result, prop->vals[0].value, prop->vals[0].length);

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
      (*argvp)[i] = malloc (prop->vals[i].length);
      memcpy ((*argvp)[i], prop->vals[i].value, prop->vals[i].length);
    }

  return TRUE;
}

void
free_vector (int argc, char **argv)
{
  int i;

  for (i = 0; i < argc; ++i)
    free (argv[i]);
  free (argv);
}
