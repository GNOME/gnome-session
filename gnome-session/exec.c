/* exec.c - Execute some command.

   Copyright (C) 1998 Tom Tromey

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

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <errno.h>
#ifndef errno
extern int errno;
#endif

static void
report_errno (int fd)
{
  int n = errno;

  /* fprintf (stderr, "failure %d\n", errno); */
  /* We don't care if it fails.  */
  write (fd, &n, sizeof n);
  _exit (1);
}

void
execute_async (const char *dir, int argc, char *argv[])
{
  pid_t pid;
  int status, count;
  int p[2];

  /* FIXME: error handling.  */
  if (pipe (p) == -1)
    return;

  /* FIXME: error handling.  */
  pid = fork ();
  if (pid == (pid_t) -1)
    return;

  if (pid == 0)
    {
      /* Child.  Fork again so child won't become a zombie.  */
      close (p[0]);
      pid = fork ();
      if (pid == (pid_t) -1)
	report_errno (p[1]);
      if (pid != 0)
	_exit (0);

      /* Child of the child.  */
      fcntl (p[1], F_SETFD, 1);

      /* Try to go to the right directory.  If we fail, hopefully it
	 will still be ok.  */
      if (dir)
	chdir (dir);

      execvp (argv[0], argv);
      report_errno (p[1]);
    }

  close (p[1]);

  /* Ignore errors.  FIXME: maybe only ignore EOFs.  FIXME: if the
     read succeeds, that means the child died.  Do something with the
     error.  */
  count = read (p[0], &status, sizeof status);

  /* Parent.  Wait for the child, since we know it will exit shortly.
     FIXME: check return value of waitpid and report an error.  */
  waitpid (pid, &status, 0);
}
