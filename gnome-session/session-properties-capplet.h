#ifndef SESSIONPROPERTIESCAPPLET_H 
#define SESSIONPROPERTIESCAPPLET_H
#include <gtk/gtk.h>

#define DEFAULT_CONFIG_PREFIX "=" DEFAULTDIR "/default.session=/"
#define CONFIG_PREFIX "session/"
#define MANUAL_CONFIG_PREFIX "session-manual/"
#define DEFAULT_SESSION "Default"
#define GSM_OPTION_CONFIG_PREFIX "session-options/Options/"
#define CURRENT_SESSION_KEY "CurrentSession"
#define LOGOUT_SCREEN_KEY "LogoutPrompt"
#define LOGOUT_SCREEN_DEFAULT "TRUE"
#define SPLASH_SCREEN_KEY "SplashScreen"
#define SPLASH_SCREEN_DEFAULT "TRUE"
#define AUTOSAVE_MODE_KEY "AutoSave"
#define AUTOSAVE_MODE_DEFAULT "TRUE"

GSList	*startup_list_read (gchar *name);

void	startup_list_write (GSList *cl, 
			    const gchar *name);

void	session_list_write (GSList *sess_list,
			    GSList *sess_list_rev, 
			    GHashTable *hash);

GSList	*startup_list_duplicate (GSList *sl);

void	startup_list_free (GSList *sl);

void	startup_list_update_gui (GSList *sl, 
				 GtkWidget *clist);

void	startup_list_add_dialog (GSList **sl, 
				 GtkWidget *clist, 
				 GtkWidget **dialog);

void	startup_list_edit_dialog (GSList **sl, 
				  GtkWidget *clist, 
				  GtkWidget **dialog);

void	startup_list_delete (GSList **sl, 
			     GtkWidget *clist);

void	session_list_update_gui (GSList *sess_list, 
				 GtkWidget *clist, 
				 gchar *curr_sess);

void	session_list_add_dialog (GSList **sess_list, 
				 GtkWidget *clist,
				 GtkWidget **dialog);

void	session_list_delete (GSList **sess_list, 
			     GtkWidget *clist, 
			     gint row,
			     GtkWidget **dialog);

void	session_list_edit_dialog (GSList **sess_list, 
				  GtkWidget *clist, 
				  GHashTable **hash, 
				  gint row, 
				  GtkWidget **dialog);

GSList	*session_list_duplicate (GSList *list);

void	session_list_free (GSList *list);

void	deleted_session_list_free (void);

#endif
