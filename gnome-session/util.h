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

#endif /* UTIL_H */
