/* exec.c - Execute some command.
   Written by Tom Tromey <tromey@cygnus.com>.  */

#include <stdio.h>

void
execute_async (int argc, char *argv[])
{
  int i, pid;

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
}

void
execute_once (int argc, char *argv[])
{
  /* Dummy implementation.  */
  int i;
  printf ("execute_once:");
  for (i = 0; i < argc; ++i)
    printf (" %s", argv[i]);
  printf ("\n");
}
