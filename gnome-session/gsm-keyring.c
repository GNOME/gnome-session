#include <config.h>

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <glib.h>
#include <gnome-keyring.h>

#include "gsm-keyring.h"

static pid_t gnome_keyring_daemon_pid = 0;
static int keyring_lifetime_pipe[2];

void
gsm_keyring_daemon_stop (void)
{
  if (gnome_keyring_daemon_pid != 0)
    {
      kill (gnome_keyring_daemon_pid, SIGTERM);
      gnome_keyring_daemon_pid = 0;
    }
  
}

static void
child_setup (gpointer user_data)
{
  gint open_max;
  gint fd;
  char *fd_str;

  open_max = sysconf (_SC_OPEN_MAX);
  for (fd = 3; fd < open_max; fd++)
    {
      if (fd != keyring_lifetime_pipe[0])
	fcntl (fd, F_SETFD, FD_CLOEXEC);
    }

  fd_str = g_strdup_printf ("%d", keyring_lifetime_pipe[0]);
  g_setenv ("GNOME_KEYRING_LIFETIME_FD",
	    fd_str,
	    TRUE);
}


void
gsm_keyring_daemon_start (void)
{
  GError *err;
  char *standard_out;
  char **lines, **l;
  int status;
  long pid;
  char *t, *end;
  const char *old_keyring;
  char *argv[2];

  /* If there is already a working keyring, don't start a new daemon */
  old_keyring = g_getenv ("GNOME_KEYRING_SOCKET");
  if (old_keyring != NULL &&
      access (old_keyring, R_OK | W_OK) == 0)
    {
      gnome_keyring_daemon_prepare_environment_sync ();
      return;
    }  

  /* Pipe to slave keyring lifetime to */
  pipe (keyring_lifetime_pipe);
  
  err = NULL;
  argv[0] = GNOME_KEYRING_DAEMON;
  argv[1] = NULL;
  g_spawn_sync (NULL, argv, NULL, G_SPAWN_LEAVE_DESCRIPTORS_OPEN,
		child_setup, NULL,
		&standard_out, NULL, &status, &err);
  
  close (keyring_lifetime_pipe[0]);
  /* We leave keyring_lifetime_pipe[1] open for the lifetime of the session,
     in order to slave the keyring daemon lifecycle to the session. */
  
  if (err != NULL)
    {
      g_printerr ("Failed to run gnome-keyring-daemon: %s\n",
                  err->message);
      g_error_free (err);
      /* who knows what's wrong, just continue */
    }
  else
    {
      if (WIFEXITED (status) &&
	  WEXITSTATUS (status) == 0 &&
	  standard_out != NULL)
        {
	  lines = g_strsplit (standard_out, "\n", 0);

	  for (l = lines; *l; ++l)
	    {
	      /* split the line into name=value */
	      t = strchr (*l, '=');
	      if (!t)
		continue;
              /* make *l be the name and t the value */
              *t = 0;
              t++;

	      /* everything that comes out should be an env var */
	      if (g_str_equal (*l, "GNOME_KEYRING_SOCKET"))
		g_setenv (*l, t, TRUE);

	      /* track the daemon's PID */
	      if (g_str_equal (*l, "GNOME_KEYRING_PID")) 
		{
		  pid = strtol (t, &end, 10);
		  if (end != t)
		      gnome_keyring_daemon_pid = pid;
		}
	    }

	  g_strfreev (lines);

	  gnome_keyring_daemon_prepare_environment_sync ();
        }
      else
	{
	  /* daemon failed for some reason */
	  g_printerr ("gnome-keyring-daemon failed to start correctly, exit code: %d\n",
		      WEXITSTATUS (status));
	}
      g_free (standard_out);
    }
}
