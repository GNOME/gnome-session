/* exec.c - Execute some command.
   Written by Tom Tromey <tromey@cygnus.com>.  */

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
execute_async (int argc, char *argv[])
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
