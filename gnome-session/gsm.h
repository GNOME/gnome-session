/* Licensed under the GPL */
#ifndef GSM_H
#define GSM_H 1
#include <gtk/gtk.h>
#define DEBUG 1

extern gchar *current_session;
extern gchar *smsavedir;

/* Back-end session stuff */
void gsm_session_init(void);
void gsm_session_run(gchar *session_name);
void gsm_session_end_current(void);
#endif
