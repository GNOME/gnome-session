/* save.c - Code to save session.
   Written by Tom Tromey <tromey@cygnus.com>.  */

#include <string.h>

#include "manager.h"



/* Default session name.  */
#define DEFAULT_SESSION "Default"

/* Name of current session.  A NULL value means the default.  */
static char *session_name = NULL;

/* Actually write the session data.  */
void
write_session (GSList *list, int shutdown)
{
}

/* Set current session name.  */
void
set_session_name (char *name)
{
  if (session_name)
    free (session_name);
  session_name = name ? strdup (name) : name;
}

/* Load a new session.  This does not shut down the current session.  */
void
read_session (char *name)
{
  if (! session_name)
    set_session_name (name);

}
