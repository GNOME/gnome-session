/* gsm-client-list.h - a scrolled window listing the gnome-session clients.

   Copyright 1999 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. 

   Authors: Felix Bellaby */

#ifndef GSM_CLIENT_LIST_H
#define GSM_CLIENT_LIST_H

#include <gtk/gtk.h>

#include "gsm-protocol.h"

#define GSM_TYPE_CLIENT_LIST         (gsm_client_list_get_type ())
#define GSM_CLIENT_LIST(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GSM_TYPE_CLIENT_LIST, GsmClientList))
#define GSM_CLIENT_LIST_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GSM_TYPE_CLIENT_LIST, GsmClientListClass))
#define GSM_IS_CLIENT_LIST(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GSM_TYPE_CLIENT_LIST))
#define GSM_IS_CLIENT_LIST_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GSM_TYPE_CLIENT_LIST))
#define GSM_CLIENT_LIST_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GSM_TYPE_CLIENT_LIST, GsmClientListClass))

typedef enum {
	GSM_CLIENT_LIST_COL_ORDER,
	GSM_CLIENT_LIST_COL_STYLE,
	GSM_CLIENT_LIST_COL_STYLE_PB,
	GSM_CLIENT_LIST_COL_STATE,
	GSM_CLIENT_LIST_COL_STATE_PB,
	GSM_CLIENT_LIST_COL_COMMAND,
	GSM_CLIENT_LIST_COL_CLIENT_ROW,
	GSM_CLIENT_LIST_COL_LAST
} GsmClientListColumn;

typedef struct _GsmClientList GsmClientList;
typedef struct _GsmClientListClass GsmClientListClass;

struct _GsmClientList {
  GtkTreeView clist;

  GtkListStore *model;
  GtkWidget* client_editor;
  GsmSession *session;
  gboolean   committed;
  gboolean   dirty;
  guint      pending;
  GSList*    changes;
};

struct _GsmClientListClass {
  GtkTreeViewClass parent_class;

  void (* dirty)       (GsmClientList *client_list); /* user did something */
  void (* started)     (GsmClientList *client_list); /* all clients started */
  void (* initialized) (GsmClientList *client_list); /* list ready to show */
};

GType gsm_client_list_get_type  (void);

/* Creates an empty client list. */
GtkWidget* gsm_client_list_new (void);

/* Returns a widget which may be used to edit the selected client. */
GtkWidget* gsm_client_list_get_editor (GsmClientList* client_list);

/* Fills out the list with the clients in the live session. */
void gsm_client_list_live_session (GsmClientList* client_list);

/* Fills out the list with the clients in the session saved under "name". */
void gsm_client_list_saved_session (GsmClientList* client_list, gchar* name);

/* Starts the clients in a SAVED session. */
void gsm_client_list_start_session (GsmClientList* client_list);

/* Removes the currently selected client from the list. */
void gsm_client_list_remove_selection (GsmClientList* client_list);

/* Adds a client to the list. 
 * Returns FALSE when the command line contains unbalanced quotes. */
gboolean gsm_client_list_add_program (GsmClientList *client_list,
				      const char    *command);

/* Commits any changes made in the list over to gnome-session. */
void gsm_client_list_commit_changes (GsmClientList* client_list);

/* Reverts any changes made in the list. */
void gsm_client_list_revert_changes (GsmClientList* client_list);

/* Discards information about changes. */
void gsm_client_list_discard_changes (GsmClientList* client_list);

/* menu data */
struct gsm_client_row_data {
  const char *name;
  const char *tooltip;
  const char *image;
  GdkPixbuf  *pixbuf;
};

extern struct gsm_client_row_data state_data[GSM_NSTATES + 1];
extern struct gsm_client_row_data style_data[GSM_NSTYLES + 1];

#endif /* GSM_CLIENT_LIST_H */
