#include <glib.h>
#include "session.h"

void gsm_initialization_error (gboolean fatal, const char *format, ...);

#define GSM_GCONF_DEFAULT_SESSION_KEY           "/desktop/gnome/session/default-session"
#define GSM_GCONF_REQUIRED_COMPONENTS_DIRECTORY "/desktop/gnome/session/required-components"

extern GsmSession *global_session;
