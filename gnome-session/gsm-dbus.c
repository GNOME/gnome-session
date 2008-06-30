/* gsm-dbus.c - Handle the dbus-daemon process.
 *
 * Copyright (c) 2006 Julio M. Merino Vidal <jmmv@NetBSD.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gprintf.h>

#include "gsm-dbus.h"

static pid_t    dbus_daemon_pid = 0;

static gboolean have_dbus_daemon (void);
static gboolean have_running_instance (void);
static int      read_line (int, char *, ssize_t);
static void     start_child (int, int);
static void     start_parent (int, int, pid_t);

/* ---------------------------------------------------------------------
 * PUBLIC INTERFACE
 * --------------------------------------------------------------------- */

/*
 * Starts the dbus-daemon if not already running and attaches it to the
 * current environment by defining DBUS_SESSION_BUS_ADDRESS.  Returns
 * true if we launch a new dbus-daemon so that we know if we have to call
 * gsm_dbus_daemon_stop later on or not.
 *
 * This function can only be called if dbus-daemon is not already running
 * (i.e., if gnome-session is starting up or if gsm_dbus_daemon_stop was
 * previously called).
 */
gboolean
gsm_dbus_daemon_start (void)
{
  int address_pipe[2];
  int pid_pipe[2];
  pid_t tmp_pid;

  g_assert (dbus_daemon_pid == 0);

  if (have_running_instance ())
    return FALSE;
  if (! have_dbus_daemon ())
    return FALSE;

  /*
   * At this point, dbus-daemon is not running for the current session
   * and the binary exists.  Spawn it.
   */

  if (pipe (address_pipe) == -1)
    {
      g_printerr ("Cannot create address pipe for dbus-daemon\n");
      return FALSE;
    }

  if (pipe (pid_pipe) == -1)
    {
      close (address_pipe[0]);
      close (address_pipe[1]);
      g_printerr ("Cannot create pid pipe for dbus-daemon\n");
      return FALSE;
    }

  tmp_pid = fork ();
  if (tmp_pid == -1)
    {
      close (address_pipe[0]);
      close (address_pipe[1]);
      close (pid_pipe[0]);
      close (pid_pipe[1]);
      g_printerr ("Cannot create child process for dbus-daemon\n");
      return FALSE;
    }
  else if (tmp_pid == 0)
    {
      close (address_pipe[0]);
      close (pid_pipe[0]);
      start_child (address_pipe[1], pid_pipe[1]);
      /* NOTREACHED */
    }
  else
    {
      close (address_pipe[1]);
      close (pid_pipe[1]);
      start_parent (address_pipe[0], pid_pipe[0], tmp_pid);
    }

  g_assert (dbus_daemon_pid != 0);
  return TRUE;
}

/*
 * Stops the running dbus-daemon.  Can only be called if we own the process;
 * i.e., if gsm_dbus_daemon_start returned true.
 */
void
gsm_dbus_daemon_stop (void)
{
  g_assert (dbus_daemon_pid != 0);

  if (kill (dbus_daemon_pid, SIGTERM) == -1)
    g_printerr ("Failed to kill dbus-daemon (pid %d)\n",
                dbus_daemon_pid);
  else
    {
      dbus_daemon_pid = 0;
      g_unsetenv ("DBUS_SESSION_BUS_ADDRESS");
    }
}

/* ---------------------------------------------------------------------
 * PRIVATE FUNCTIONS
 * --------------------------------------------------------------------- */

/*
 * Check whether the dbus-daemon binary is in the path and raise an
 * appropriate error message if it is not.
 */
static gboolean
have_dbus_daemon (void)
{
  gboolean result;
  gchar *file_name;

  file_name = g_find_program_in_path ("dbus-daemon");
  if (file_name == NULL)
    g_printerr ("Cannot locate dbus-daemon\n");
  result = file_name != NULL;
  g_free (file_name);

  return result;
}

/*
 * Check whether there is a dbus-daemon session instance currently running
 * (not spawned by us).  If there is, do nothing and return TRUE.
 */
static gboolean
have_running_instance (void)
{
  const gchar *address_str;

  g_assert (dbus_daemon_pid == 0);

  address_str = g_getenv ("DBUS_SESSION_BUS_ADDRESS");
  return address_str != NULL;
}

/*
 * Reads a single line from the given file descriptor and stores it in the
 * buffer pointed to by 'buf'.
 *
 * After finding the first new line character, the function returns.  This
 * is to avoid reading dbus' pid multiple times from its file descriptor.
 */
static int
read_line (int fd, char *buf, ssize_t bufsize)
{
  gboolean discard, done;
  ssize_t bytes;

  bytes = 0;
  discard = FALSE;
  done = FALSE;
  do
    {
      ssize_t i, result;
      ssize_t oldbytes;

      oldbytes = bytes;

      result = read (fd, &buf[bytes], bufsize - bytes);
      if (result < 0)
        return -1;
      else if (result > 0 && !discard)
        {
          if (bytes + result < bufsize)
            bytes += result;
          else
            bytes = bufsize - 1;
        }
      else
        done = TRUE;

      for (i = oldbytes; !discard && i < bytes; i++)
        if (buf[i] == '\n')
          {
            buf[i] = '\0';
            discard = TRUE;
	    done = TRUE;
          }
    }
  while (!done);

  g_assert (bytes >= 0 && bytes < bufsize);
  buf[bytes] = '\0';

  return bytes;
}

/*
 * Code run by the child process after the fork to launch dbus-demon.
 *
 * As the child, this execs dbus-daemon, connecting it to the appropriate
 * file descriptors.
 */
static void
start_child (int address_fd, int pid_fd)
{
  gchar address_str[16];
  gchar pid_str[16];
  int fd;
  long open_max;

  open_max = sysconf (_SC_OPEN_MAX);
  for (fd = 0; fd < open_max; fd++)
    {
      if (fd != STDIN_FILENO && fd != STDOUT_FILENO && fd != STDERR_FILENO &&
          fd != address_fd && fd != pid_fd)
        fcntl (fd, F_SETFD, FD_CLOEXEC);
    }

  g_snprintf (address_str, sizeof (address_str), "%d", address_fd);
  g_snprintf (pid_str, sizeof (pid_str), "%d", pid_fd);

  execlp ("dbus-daemon",
          "dbus-daemon",
          "--fork",
          "--print-address", address_str,
          "--print-pid", pid_str,
          "--session",
          NULL);

  g_printerr ("Could not launch dbus-daemon\n");

  exit (EXIT_FAILURE);
}

/*
 * Code run by the parent process after the fork to launch dbus-demon.
 *
 * As the parent, this waits until dbus-daemon forks itself again and
 * fetches its address and pid to later take its ownership.
 */
static void
start_parent (int address_fd, int pid_fd, pid_t child)
{
  char address_str[256];
  char pid_str[256];
  char *tmp_ep;
  int exitstat;
  unsigned long tmp_num;
  ssize_t bytes;

  g_assert (child > 0);

  /*
   * dbus-daemon --fork causes our child process to exit prematurely
   * because it is not the real daemon.  See if it worked correctly
   * and clean it up to avoid a zombie.
   *
   * Life could be much easier if dbus-daemon had a --no-fork flag.
   * But, as it hasn't it, we cannot assume that it will not fork,
   * because we have no control over its configuration file.
   */
  if (waitpid (child, &exitstat, 0) == -1)
    {
      close (address_fd);
      close (pid_fd);

      g_printerr ("Failed to get dbus-daemon status\n");
      return;
    }
  if (!WIFEXITED (exitstat) || WEXITSTATUS (exitstat) != EXIT_SUCCESS)
    {
      close (address_fd);
      close (pid_fd);

      g_printerr ("dbus-daemon exited unexpectedly\n");
      return;
    }

  /*
   * Fetch dbus-daemon address.
   */
  bytes = read_line (address_fd, address_str, sizeof (address_str));
  if (bytes == -1 || bytes == 0)
    {
      close (address_fd);
      close (pid_fd);

      g_printerr ("Failed to get dbus-daemon's address\n");
      return;
    }

  /*
   * Fetch dbus-daemon pid.
   */
  bytes = read_line (pid_fd, pid_str, sizeof (pid_str));
  if (bytes == -1 || bytes == 0)
    {
      close (address_fd);
      close (pid_fd);

      g_printerr ("Failed to get dbus-daemon's pid\n");
      return;
    }

  close (address_fd);
  close (pid_fd);

  /*
   * Convert the string printed in pid_fd to a pid value.  Do the usual
   * strtoul dance to check for a valid number.
   */
  errno = 0;
  tmp_num = strtoul (pid_str, &tmp_ep, 10);
  if (pid_str[0] == '\0' || *tmp_ep != '\0')
    {
      g_printerr ("dbus-daemon pid invalid (not a number)\n");
      return;
    }
  if (errno == ERANGE && tmp_num == ULONG_MAX)
    {
      g_printerr ("dbus-daemon pid invalid (out of range)\n");
      return;
    }
  dbus_daemon_pid = tmp_num;

  /*
   * All right!  Tell our future children about the new born dbus-daemon.
   */
  g_setenv ("DBUS_SESSION_BUS_ADDRESS", address_str, TRUE);
}
