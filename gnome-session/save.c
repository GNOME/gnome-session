/* save.c - Code to save session.
   Written by Tom Tromey <tromey@cygnus.com>.  */

#include <glib.h>
#include <string.h>

#include "libgnome/libgnome.h"
#include "libgnomeui/gnome-session.h"
#include "manager.h"



/* Default session name.  */
#define DEFAULT_SESSION "Default"

/* Name of current session.  A NULL value means the default.  */
static char *session_name = NULL;

/* The name we're using when saving.  This is completely lame.  */
static char prefix[1024];

/* This is used to hold a table of all properties we care to save.  */
typedef struct
{
  const char *name;		/* Name of property.  */
  gboolean is_vector;		/* TRUE if vector.  */
  gboolean required;		/* TRUE if required (by us, not by the
				   spec).  */
} propsave;

static propsave properties[] =
{
  { SmCurrentDirectory, 0, 0 },
  { SmDiscardCommand, 1, 0 },
  { SmRestartCommand, 1, 1 },
  { GNOME_SM_INIT_COMMAND, 1, 0 }
};

#define NUM_PROPERTIES (sizeof (properties) / sizeof (propsave))



/* Write a single client to the current session.  */
static void
write_one_client (const Client *client)
{
  /* We over-allocate; it doesn't matter.  */
  int i, j, vec_count, string_count, argcs[NUM_PROPERTIES], failure;
  char **argvs[NUM_PROPERTIES];
  char *strings[NUM_PROPERTIES];
  const char *argv_names[NUM_PROPERTIES];
  const char *string_names[NUM_PROPERTIES];

  /* Read each property we care to save.  */
  failure = 0;
  vec_count = string_count = 0;
  for (i = 0; i < NUM_PROPERTIES; ++i)
    {
      if (properties[i].is_vector)
	{
	  if (! find_vector_property (client, properties[i].name,
				      &argcs[vec_count], &argvs[vec_count]))
	    {
	      if (properties[i].required)
		{
		  failure = 1;
		  break;
		}
	    }
	  else
	    argv_names[vec_count++] = properties[i].name;
	}
      else
	{
	  if (! find_string_property (client, properties[i].name,
				      &strings[string_count]))
	    {
	      if (properties[i].required)
		{
		  failure = 1;
		  break;
		}
	    }
	  else
	    string_names[string_count++] = properties[i].name;
	}
    }

  /* Write each property we found.  */
  if (! failure)
    {
      for (i = 0; i < vec_count; ++i)
	{
	  char buf[256];
	  for (j = 0; j < argcs[i]; ++j)
	    {
	      sprintf (buf, "%s,%d", argv_names[i], j);
	      gnome_config_set_string (buf, argvs[i][j]);
	    }
	}

      for (i = 0; i < string_count; ++i)
	gnome_config_set_string (string_names[i], strings[i]);
    }

  /* Clean up.  */
  for (i = 0; i < vec_count; ++i)
    free_vector (argcs[i], argvs[i]);
  for (i = 0; i < string_count; ++i)
    free (strings[i]);
}

/* Actually write the session data.  */
void
write_session (const GSList *list, int shutdown)
{
  /* This is somewhat losing.  But we really do want to make sure any
     existing session with this same name has been cleaned up before
     we write the new info.  */
  delete_session (session_name);

  for (; list; list = list->next)
    {
      Client *client = (Client *) list->data;
      sprintf (prefix, "gsm/%s/%s,",
	       session_name ? session_name : DEFAULT_SESSION,
	       client->id);
      gnome_config_push_prefix (prefix);
      write_one_client (client);
      gnome_config_pop_prefix ();
    }

  gnome_config_sync ();
}

/* Set current session name.  */
void
set_session_name (const char *name)
{
  if (session_name)
    free (session_name);
  if (name)
    session_name = strdup (name);
  else
    session_name = NULL;
}

/* Load a new session.  This does not shut down the current session.  */
void
read_session (const char *name)
{
  if (! session_name)
    set_session_name (name);

}

/* Delete a session.  */
void
delete_session (const char *name)
{
  if (! name)
    name = DEFAULT_SESSION;
}
