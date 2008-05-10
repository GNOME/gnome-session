#include <glib.h>
#include "session.h"

#define GSM_GCONF_DEFAULT_SESSION_KEY           "/desktop/gnome/session/default-session"
#define GSM_GCONF_REQUIRED_COMPONENTS_DIRECTORY "/desktop/gnome/session/required-components"

void gsm_initialization_error (gboolean fatal, const char *format, ...);

extern GsmSession *global_session;
