#include <config.h>

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <glib.h>

#include "gsm-keyring.h"

static pid_t gnome_keyring_daemon_pid = 0;

void
gsm_keyring_daemon_stop (void)
{
  if (gnome_keyring_daemon_pid != 0)
    {
      kill (gnome_keyring_daemon_pid, SIGTERM);
      gnome_keyring_daemon_pid = 0;
    }
  
}

void
gsm_keyring_daemon_start (void)
{
  GError *err;
  char *standard_out;
  char **lines;
  int status;
  long pid;
  char *pid_str, *end;
  const char *old_keyring;

  /* If there is already a working keyring, don't start a new daemon */
  old_keyring = g_getenv ("GNOME_KEYRING_SOCKET");
  if (old_keyring != NULL &&
      access (old_keyring, R_OK | W_OK) == 0)
    return;
  
  err = NULL;
  g_spawn_command_line_sync (GNOME_KEYRING_DAEMON, &standard_out, NULL, &status, &err);
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
	  lines = g_strsplit (standard_out, "\n", 3);

	  if (lines[0] != NULL &&
	      lines[1] != NULL &&
	      g_str_has_prefix (lines[1], "GNOME_KEYRING_PID="))
	    {
	      pid_str = lines[1] + strlen ("GNOME_KEYRING_PID=");
	      pid = strtol (pid_str, &end, 10);
	      if (end != pid_str)
		{
		  gnome_keyring_daemon_pid = pid;
		  putenv (g_strdup (lines[0]));
		}
	    }

	  g_strfreev (lines);
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
