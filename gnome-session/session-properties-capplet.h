#ifndef SESSIONPROPERTIESCAPPLET_H 
#define SESSIONPROPERTIESCAPPLET_H
#include <gtk/gtk.h>

GSList	*startup_list_read (gchar *name);

void    spc_write_state (void);

void	startup_list_write (GSList *cl, 
			    const gchar *name);

void	session_list_write (GSList *sess_list,
			    GSList *sess_list_rev, 
			    GHashTable *hash);

GSList	*startup_list_duplicate (GSList *sl);

void	startup_list_free (GSList *sl);

void	startup_list_update_gui (GSList *sl, 
				 GtkTreeModel *model, GtkTreeSelection *sel);

void	startup_list_add_dialog (GSList **sl, 
				 GtkWidget **dialog,
				 GtkWidget *parent_dlg);

void	startup_list_edit_dialog (GSList **sl, 
				  GtkTreeModel *model, GtkTreeSelection *sel,
				  GtkWidget **dialog,
				  GtkWidget *parent_dlg);

void	startup_list_delete (GSList **sl, 
			     GtkTreeModel *model, GtkTreeSelection *sel);

void session_list_update_gui (GSList *sess_list,
			      GtkTreeModel *model,
			      GtkTreeSelection *sel,
			      gchar *curr_sess);

void	session_list_add_dialog (GSList **sess_list, 
				 GtkWidget **dialog,
				 GtkWidget *parent_dlg);

void session_list_delete (GSList **sess_list,
			  const gchar *old_session_name,
			  GtkWidget **dialog);

void	session_list_edit_dialog (GSList **sess_list, 
				  const gchar *old_session_name,
				  GHashTable **hash, 
				  GtkWidget **dialog,
				  GtkWidget *parent_dlg);

GSList	*session_list_duplicate (GSList *list);

void	session_list_free (GSList *list);

void	deleted_session_list_free (void);

void	apply (void);

#endif
