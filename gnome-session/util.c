#include <config.h>

#include "util.h"

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static gboolean is_verbose = FALSE;

void
gsm_set_verbose (gboolean setting)
{
  is_verbose = setting;
}

void
gsm_fatal (const char *format, ...)
{
  va_list args;
  gchar *str;
  
  g_return_if_fail (format != NULL);
  
  va_start (args, format);
  str = g_strdup_vprintf (format, args);
  va_end (args);

  fputs ("Session manager: ", stderr);
  fputs (str, stderr);

  fflush (stderr);
  
  g_free (str);

  exit (1);
}


void
gsm_warning (const char *format, ...)
{
  va_list args;
  gchar *str;
  
  g_return_if_fail (format != NULL);
  
  va_start (args, format);
  str = g_strdup_vprintf (format, args);
  va_end (args);

  fputs ("Session manager: ", stderr);
  fputs (str, stderr);

  fflush (stderr);
  
  g_free (str);
}

void
gsm_verbose (const char *format, ...)
{
  va_list args;
  gchar *str;

  if (!is_verbose)
    return;
  
  g_return_if_fail (format != NULL);
  
  va_start (args, format);
  str = g_strdup_vprintf (format, args);
  va_end (args);

  fputs ("gsm verbose: ", stdout);
  fputs (str, stdout);

  fflush (stdout);
  
  g_free (str);
}

gboolean
gsm_compare_commands (int argc1, char **argv1,
		      int argc2, char **argv2)
{
	int i;

	if (argc1 != argc2)
		return FALSE;

	for (i = 0; i < argc1; i++)
		if (strcmp (argv1 [i], argv2 [i]))
			return FALSE;

	return TRUE;
}
