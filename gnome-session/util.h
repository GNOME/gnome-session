#ifndef UTIL_H
#define UTIL_H

#include <glib.h>
#include <stdio.h>
#include <gconf/gconf-client.h>

void gsm_warning    (const char *format,
                     ...) G_GNUC_PRINTF (1, 2);
void gsm_fatal      (const char *format,
                     ...) G_GNUC_PRINTF (1, 2);
void gsm_verbose    (const char *format,
                     ...) G_GNUC_PRINTF (1, 2);
void gsm_verbose_indent (gboolean indent);
void gsm_verbose_print_indent (FILE *file);

void gsm_set_verbose (gboolean setting);


gboolean gsm_compare_commands (int argc1, char **argv1,
			       int argc2, char **argv2);

GConfClient *gsm_get_conf_client (void);

gboolean gsm_exec_async (char    *cwd,
			 char   **argv,
			 char   **envp,
			 GPid    *child_pid,
			 GError **error);

gboolean gsm_exec_command_line_async (const gchar  *cmd_line,
				      GError      **error);

#endif /* UTIL_H */
