#include <config.h>

#include "util.h"

#include <sys/time.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static gboolean is_verbose = FALSE;
static int indent_level = 0;

void
gsm_set_verbose (gboolean setting)
{
  is_verbose = setting;
}

static void
timestamp (FILE *file)
{
  struct timeval tv;
  struct timezone tz;

  if (gettimeofday (&tv, &tz) != 0)
    fputs ("timestamp(): could not gettimeofday()\n", file);
  else
    fprintf (file, "%ld.%06ld ", tv.tv_sec, tv.tv_usec);
}

void
gsm_verbose_print_indent (FILE *file)
{
  int i;

  for (i = 0; i < indent_level; i++)
    fputc (' ', file);
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

  timestamp (stderr);
  gsm_verbose_print_indent (stderr);
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

  timestamp (stderr);
  gsm_verbose_print_indent (stderr);
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

  timestamp (stdout);
  gsm_verbose_print_indent (stdout);
/*   fputs ("gsm verbose: ", stdout); */
  fputs (str, stdout);

  fflush (stdout);
  
  g_free (str);
}

void
gsm_verbose_indent (gboolean indent)
{
  if (indent)
    indent_level += 4;
  else
    indent_level -= 4;

  if (indent_level < 0)
    {
      fprintf (stderr, "Indentation Error\n");
      indent_level = 0;
    }
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

GConfClient *
gsm_get_conf_client (void)
{
	static GConfClient *client = NULL;

	if (!client)
		client = gconf_client_get_default ();

	return client;
}
