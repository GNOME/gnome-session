#ifndef __SESSION_PROPERTIES_CAPPLET_H__ 
#define __SESSION_PROPERTIES_CAPPLET_H__

#include <gtk/gtk.h>

#include <gconf/gconf-client.h>

G_BEGIN_DECLS

#define SPC_GCONF_CONFIG_PREFIX   "/apps/gnome-session/options"
#define SPC_GCONF_AUTOSAVE_KEY    SPC_GCONF_CONFIG_PREFIX "/auto_save_session" 

void   spc_ui_build   (GConfClient *client);

G_END_DECLS

#endif /* __SESSION_PROPERTIES_CAPPLET_H__ */
