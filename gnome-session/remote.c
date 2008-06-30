/* $XConsortium: remote.c,v 1.15 95/01/03 17:26:52 mor Exp $ */
/******************************************************************************

Copyright (c) 1993, 1999  X Consortium

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
X CONSORTIUM BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of the X Consortium shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from the X Consortium.
******************************************************************************/

/*
 * We use the rstart protocol to restart clients on remote machines.
 */

#include <config.h>

#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include <X11/ICE/ICElib.h>
#include <X11/ICE/ICEutil.h>

#include "remote.h"
#include "util.h"

static char *format_rstart_env (char *);
static void close_child (GPid pid, gint status, gpointer ignore);

static void
close_child (GPid pid, gint status, gpointer ignore)
{
  g_spawn_close_pid (pid);
}

gboolean
remote_start (char *restart_info, char **argv,
	      char *cwd, char **envp,
	      GPid *child_pid, GError **error)
{
    FILE *fp;
    gint pipefd;
    char *machine;
    int  i;
    GSList *list;
    gchar *rargv[4];

    if (! restart_info)
    {
        return gsm_exec_async (cwd, argv, envp, child_pid, error);
    }

    if (strncmp (restart_info, RSTART_RSH, sizeof (RSTART_RSH))
	|| restart_info[sizeof (RSTART_RSH)] != '/')
    {
        return gsm_exec_async (cwd, argv, envp, child_pid, error);
    }

    machine = restart_info + sizeof (RSTART_RSH) + 1;
    if (! *machine)
    {
        return gsm_exec_async (cwd, argv, envp, child_pid, error);
    }

    g_message ("Attempting to restart remote client on %s\n",
	       machine);

    rargv[0] = RSH_COMMAND;
    rargv[1] = machine;
    rargv[2] = "rstartd";
    rargv[3] = NULL;

    /* TODO: we would like to report whether rstartd failed to a log
       window in the parent.  We could do this by checking all the
       fprintf()s for error return.  */
    if (! g_spawn_async_with_pipes (NULL, rargv, NULL,
				    G_SPAWN_DO_NOT_REAP_CHILD,
				    NULL, NULL, child_pid,
				    &pipefd, NULL, NULL, error))
      return 0;
    g_child_watch_add (*child_pid, close_child, NULL);

    fp = (FILE *) fdopen (pipefd, "w");

    fprintf (fp, "CONTEXT X\n");
    fprintf (fp, "DIR %s\n", cwd);
    fprintf (fp, "DETACH\n");

    if (envp)
      {
	/*
	 * The application saved its environment.
	 */

	for (i = 0; envp[i]; i++)
	  {
	    /*
	     * rstart requires that any spaces, backslashes, or
	     * non-printable characters inside of a string be
	     * represented by octal escape sequences.
	     */

	    char *temp = format_rstart_env (envp[i]);
	    fprintf (fp, "MISC X %s\n", temp);
	    if (temp != envp[i])
	      g_free (temp);
	  }
      }
    else
      {
	/*
	 * The application did not save its environment.
	 * The default PATH set up by rstart may not contain
	 * the program we want to restart.  We play it safe
	 * and pass xsm's PATH.  This will most likely contain
	 * the path we need.
	 */

	const char *path = g_getenv ("PATH");

	if (path)
	  fprintf (fp, "MISC X PATH=%s\n", path);
      }

#if 0
    /* FIXME: implement */
    fprintf (fp, "MISC X %s\n", non_local_display_env);
    fprintf (fp, "MISC X %s\n", non_local_session_env);
#endif

    /*
     * Pass the authentication data.
     * Each transport has auth data for ICE and XSMP.
     * Don't pass local auth data.
     */
    for (list = auth_entries; list != NULL; list = list->next)
      {
	IceAuthFileEntry *entry = (IceAuthFileEntry *) list->data;
	if (strstr (entry->network_id, "local/"))
	  continue;

	fprintf (fp, "AUTH ICE %s \"\" %s %s ",
		 entry->protocol_name,
		 entry->network_id,
		 entry->auth_name);
		
	for (i = 0; i < entry->auth_data_length; ++i)
	  fprintf (fp, "%02x", entry->auth_data[i]);

	fprintf (fp, "\n");
      }

    /*
     * And execute the program
     */

    fprintf (fp, "EXEC %s %s", argv[0], argv[0]);
    for (i = 1; argv[i]; i++)
      fprintf (fp, " %s", argv[i]);
    fprintf (fp, "\n\n");
    fclose (fp);

    return 1;
}


/*
 * rstart requires that any spaces/backslashes/non-printable characters
 * inside of a string be represented by octal escape sequences.
 */

static char *
format_rstart_env (char *str)
{
    int escape_count = 0, i;
    unsigned char *temp = (unsigned char *) str;

    while (*temp != '\0')
    {
	if (!isgraph (*temp) || *temp == '\\')
	    escape_count++;
	temp++;
    }

    if (escape_count == 0)
	return (str);
    else
    {
	int len = strlen (str) + 1 + (escape_count * 3);
	char *ret = (char *) g_malloc (len);
	char *ptr = ret;

	temp = (unsigned char *) str;
	while (*temp != '\0')
	{
	    if (!isgraph (*temp) || *temp == '\\')
	    {
		char octal[3];
		g_snprintf (octal, sizeof (octal), "%o", *temp);
		*(ptr++) = '\\';
		for (i = 0; i < (3 - (int) strlen (octal)); i++)
		    *(ptr++) = '0';
		strcpy (ptr, octal);
		ptr += strlen (octal);
	    }
	    else
		*(ptr++) = *temp;

	    temp++;
	}

	*ptr = '\0';
	return (ret);
    }
}
