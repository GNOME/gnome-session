/* exec.c - Execute some command.
   Written by Tom Tromey <tromey@cygnus.com>.  */

#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>

void
execute_async (int argc, char *argv[])
{
  int i, pid, status;

  pid = fork ();

  /* FIXME: error handling.  */
  if (pid == -1)
    return;

  if (pid == 0)
    {
      /* Child.  Fork again so child won't become a zombie.  */
      pid = fork ();
      if (pid == -1)
	_exit (1);
      if (pid == 0)
	_exit (0);

      execvp (argv[0], argv);
      _exit (1);
    }

  /* Parent.  Wait for the child, since we know it will exit shortly.
     FIXME: check return value of waitpid and report an error.  */
  waitpid (pid, &status, 0);
}
