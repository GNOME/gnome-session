#include "gsm.h"

gchar *smsavedir = NULL;
gchar *current_session = NULL;

void gsm_session_init(void)
{
  g_warning("not implemented yet\n");
}

void gsm_session_run(gchar *session_name)
{
  g_error("Not implemented yet\n");
}

void gsm_session_end_current(void)
{
  g_return_if_fail(current_session != NULL);

  g_error("Not implemented yet\n");
}
