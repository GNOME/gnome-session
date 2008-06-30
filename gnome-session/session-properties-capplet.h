#ifndef SESSIONPROPERTIESCAPPLET_H 
#define SESSIONPROPERTIESCAPPLET_H
#include <gtk/gtk.h>

GSList	*startup_list_read (void);

void    mark_dirty (void);

void	startup_list_write (GSList *cl, 
			    const gchar *name);

void	startup_list_free (GSList *sl);

void	startup_list_update_gui (GSList **sl, 
				 GtkTreeModel *model, GtkTreeSelection *sel);

void	startup_list_add_dialog (GSList **sl, 
				 GtkWidget *parent_dlg);

void	startup_list_edit_dialog (GSList **sl, 
				  GtkTreeModel *model, GtkTreeSelection *sel,
				  GtkWidget *parent_dlg);

void	startup_list_delete (GSList **sl, 
			     GtkTreeModel *model, GtkTreeSelection *sel);

void     startup_list_enable (GSList **sl, GtkTreeModel *model, GtkTreeIter *iter);
void     startup_list_disable (GSList **sl, GtkTreeModel *model, GtkTreeIter *iter);

void session_list_update_gui (GSList *sess_list,
			      GtkTreeModel *model,
			      GtkTreeSelection *sel,
			      gchar *curr_sess);

void	apply (void);

#endif
