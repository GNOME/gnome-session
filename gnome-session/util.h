#ifndef UTIL_H
#define UTIL_H

#include <glib.h>

void gsm_warning    (const char *format,
                     ...) G_GNUC_PRINTF (1, 2);
void gsm_fatal      (const char *format,
                     ...) G_GNUC_PRINTF (1, 2);
void gsm_verbose    (const char *format,
                     ...) G_GNUC_PRINTF (1, 2);

void gsm_set_verbose (gboolean setting);


gboolean gsm_compare_commands (int argc1, char **argv1,
			       int argc2, char **argv2);

#endif /* UTIL_H */
