/* exec.c - Execute some command.
   Written by Tom Tromey <tromey@cygnus.com>.  */

#include <stdio.h>

void
execute_async (int argc, char *argv[])
{
  /* Dummy implementation.  */
  int i;
  printf ("execute_async:");
  for (i = 0; i < argc; ++i)
    printf (" %s", argv[i]);
  printf ("\n");
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
