/* gsm-client-list.c - a clist listing the clients in the gnome-session.

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

#include <config.h>

#include <glib/gi18n.h>

#include "gsm-client-list.h"
#include "gsm-client-row.h"
#include "gsm-client-editor.h"

static void gsm_client_list_destroy  (GtkObject *o);
static void initialized_cb (GsmClientList* client_list);
static void changed_cb (GsmClientEditor* client_editor, 
			guint order, GsmStyle style);
static void dirty (GsmClientList* client_list);

enum {
  DIRTY,
  STARTED,
  INITIALIZED,
  NSIGNALS
};

struct gsm_client_row_data state_data[] = {
  { N_("Inactive"), N_("Waiting to start or already finished."),
    GTK_STOCK_DISCONNECT, NULL },
  { N_("Starting"), N_("Started but has not yet reported state."),
    GTK_STOCK_CONNECT, NULL },
  { N_("Running"), N_("A normal member of the session."),
    GTK_STOCK_EXECUTE, NULL },
  { N_("Saving"), N_("Saving session details."),
    GTK_STOCK_SAVE, NULL },
  //FIXME find better icon
  { N_("Unknown"), N_("State not reported within timeout."),
    GTK_STOCK_HELP, NULL },
  { NULL }
};

struct gsm_client_row_data style_data[] = {
  //FIXME find icon
  { N_("Normal"), N_("Unaffected by logouts but can die."),
    NULL, NULL },
  { N_("Restart"), N_("Never allowed to die."),
    GTK_STOCK_REFRESH, NULL },
  { N_("Trash"), N_("Discarded on logout and can die."),
    GTK_STOCK_DELETE, NULL },
  { N_("Settings"), N_("Always started on every login."),
    GTK_STOCK_PREFERENCES, NULL },
  { NULL }
};

static guint gsm_client_list_signals[NSIGNALS];

G_DEFINE_TYPE (GsmClientList, gsm_client_list, GTK_TYPE_TREE_VIEW);

static void
gsm_client_list_init (GsmClientList *list)
{
}

static void
gsm_client_list_class_init (GsmClientListClass *klass)
{
  GtkObjectClass *object_class = (GtkObjectClass*) klass;

  gsm_client_list_signals[DIRTY] =
    g_signal_new ("dirty",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (GsmClientListClass, dirty),
		  NULL, NULL,
		  g_cclosure_marshal_VOID__VOID,
		  G_TYPE_NONE, 0);
  gsm_client_list_signals[STARTED] =
    g_signal_new ("started",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (GsmClientListClass, started),
		  NULL, NULL,
		  g_cclosure_marshal_VOID__VOID,
		  G_TYPE_NONE, 0);
  gsm_client_list_signals[INITIALIZED] =
    g_signal_new ("initialized",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (GsmClientListClass, initialized),
		  NULL, NULL,
		  g_cclosure_marshal_VOID__VOID,
		  G_TYPE_NONE, 0);

  klass->dirty       = dirty;
  klass->started     = NULL;
  klass->initialized = NULL;

  object_class->destroy = gsm_client_list_destroy;
}

static void
selection_changed (GtkTreeSelection *selection,
		   GsmClientList *client_list)
{
  GtkTreeModel *model;
  GtkTreeIter iter;
  GsmClient *client = NULL;

  if (gtk_tree_selection_get_selected (selection, &model, &iter))
    gtk_tree_model_get (model, &iter,
			GSM_CLIENT_LIST_COL_CLIENT_ROW, &client,
			-1);

  gsm_client_editor_set_client (GSM_CLIENT_EDITOR (client_list->client_editor), client);
}

//FIXME: this is leaking memory since we never unref the pixbufs
static void
create_stock_menu_pixmaps (GsmClientList *client_list)
{
	gint i;

	for (i = 0; i < GSM_NSTATES; i++) {
	  if (state_data[i].image)
	    state_data[i].pixbuf = gtk_widget_render_icon (GTK_WIDGET (client_list),
						      state_data[i].image,
						      GTK_ICON_SIZE_MENU,
						      NULL);
	  else
	    state_data[i].pixbuf = NULL;
	}

	for (i = 0; i < GSM_NSTYLES; i++) {
	  if (style_data[i].image)
	    style_data[i].pixbuf = gtk_widget_render_icon (GTK_WIDGET (client_list),
						      style_data[i].image,
						      GTK_ICON_SIZE_MENU,
						      NULL);
	  else
	    style_data[i].pixbuf = NULL;
	}
}

GtkWidget* 
gsm_client_list_new (void)
{
  GsmClientList* client_list;
  GtkTreeView *view;
  GtkCellRenderer *text_cell, *pixbuf_cell;
  GtkTreeSelection *selection;
  GtkTreeViewColumn *column;

  client_list = g_object_new (gsm_client_list_get_type(), NULL);
  view = GTK_TREE_VIEW (client_list);

  gtk_tree_view_set_rules_hint (view, TRUE);

  client_list->model = gtk_list_store_new (GSM_CLIENT_LIST_COL_LAST,
					   G_TYPE_STRING,
					   G_TYPE_INT,
					   GDK_TYPE_PIXBUF,
					   G_TYPE_INT,
					   GDK_TYPE_PIXBUF,
					   G_TYPE_STRING,
					   gsm_client_row_get_type ());

  gtk_tree_view_set_model (view,
			   GTK_TREE_MODEL (client_list->model));
  
  text_cell = gtk_cell_renderer_text_new ();
  pixbuf_cell = gtk_cell_renderer_pixbuf_new ();

  gtk_tree_view_insert_column_with_attributes (view, -1,
					       _("Order"), text_cell,
					       "text", GSM_CLIENT_LIST_COL_ORDER,
					       NULL);
  column = gtk_tree_view_get_column (view, 0);
  gtk_tree_view_column_set_sort_column_id (column, GSM_CLIENT_LIST_COL_ORDER);

  gtk_tree_view_insert_column_with_attributes (view, -1,
					       _("Style"), pixbuf_cell,
					       "pixbuf", GSM_CLIENT_LIST_COL_STYLE_PB,
					       NULL);
  column = gtk_tree_view_get_column (view, 1);
  gtk_tree_view_column_set_sort_column_id (column, GSM_CLIENT_LIST_COL_STYLE);

  gtk_tree_view_insert_column_with_attributes (view, -1,
					       _("State"), pixbuf_cell,
					       "pixbuf", GSM_CLIENT_LIST_COL_STATE_PB,
					       NULL);
  column = gtk_tree_view_get_column (view, 2);
  gtk_tree_view_column_set_sort_column_id (column, GSM_CLIENT_LIST_COL_STATE);

  gtk_tree_view_insert_column_with_attributes (view, -1,
					       _("Program"), text_cell,
					       "text", GSM_CLIENT_LIST_COL_COMMAND,
					       NULL);
  column = gtk_tree_view_get_column (view, 3);
  gtk_tree_view_column_set_sort_column_id (column, GSM_CLIENT_LIST_COL_COMMAND);

  gtk_tree_view_set_search_column (view, GSM_CLIENT_LIST_COL_COMMAND);					      

  if (!state_data[0].pixbuf)
    create_stock_menu_pixmaps (client_list);
 
  client_list->client_editor = gsm_client_editor_new ();
  client_list->session   = NULL;
  client_list->committed = FALSE;
  client_list->dirty     = FALSE;
  client_list->changes   = NULL;

  selection = gtk_tree_view_get_selection (view);

  g_signal_connect (selection, "changed",
		    G_CALLBACK (selection_changed), client_list);

  g_signal_connect (client_list->client_editor, "changed",
		    G_CALLBACK (changed_cb), NULL);

  gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
#ifdef FIXME
  gtk_clist_set_auto_sort (clist, TRUE);
#endif

  return GTK_WIDGET (client_list);
}

static void 
gsm_client_list_destroy (GtkObject *o)
{
  GsmClientList* client_list = (GsmClientList*)o;

  g_return_if_fail(client_list != NULL);
  g_return_if_fail(GSM_IS_CLIENT_LIST(client_list));

  (*(GTK_OBJECT_CLASS (gsm_client_list_parent_class)->destroy))(o);
}

GtkWidget* 
gsm_client_list_get_editor (GsmClientList* client_list)
{
  g_return_val_if_fail(client_list != NULL, NULL);
  g_return_val_if_fail(GSM_IS_CLIENT_LIST(client_list), NULL);

  return client_list->client_editor;
}

void 
gsm_client_list_live_session (GsmClientList* client_list)
{
  if (client_list->session)
    {
      gtk_list_store_clear (client_list->model);
      g_object_unref (client_list->session);
    }

  client_list->session = 
    gsm_session_live ((GsmClientFactory)gsm_client_row_new, client_list);

  g_signal_connect_swapped (client_list->session, "initialized",
			    G_CALLBACK (initialized_cb), client_list);
}

void 
gsm_client_list_saved_session (GsmClientList* client_list, gchar* name)
{
  if (client_list->session)
    {
      gtk_list_store_clear (client_list->model);
      g_object_unref (client_list->session);
    }

  client_list->session = 
    gsm_session_new (name, (GsmClientFactory)gsm_client_row_new, client_list);

  g_signal_connect_swapped (client_list->session, "initialized",
			    G_CALLBACK (initialized_cb), client_list);
}

void 
gsm_client_list_start_session (GsmClientList* client_list)
{
  gsm_session_start (GSM_SESSION (client_list->session));
  client_list->pending = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (client_list->model), NULL);
}

static void 
dirty (GsmClientList* client_list)
{
  if (! client_list->dirty)
    {
      gsm_client_list_discard_changes (client_list);
      client_list->dirty = TRUE;
    }
}

static void 
register_change (GsmClientList* client_list, GsmClientRow* client_row,
		 GsmClientRowChange change)
{
  GsmClient *client;

  client = GSM_CLIENT (client_row);
    
  g_signal_emit (client_list, gsm_client_list_signals[DIRTY], 0);

  switch (client_row->change)
    {
    case GSM_CLIENT_ROW_NONE:
      if (change == GSM_CLIENT_ROW_EDIT)
	{
	  client_row->revert_style = client->style;
	  client_row->revert_order = client->order;
	}
      client_list->changes = g_slist_prepend (client_list->changes,
					      client_row);
      if (change != GSM_CLIENT_ROW_ADD)
	g_object_ref (client_row);
      client_row->change = change;
      break;

    case GSM_CLIENT_ROW_EDIT:
      if (change == GSM_CLIENT_ROW_REMOVE)
	{
	  client->style = client_row->revert_style;
	  client->order = client_row->revert_order;
	}
      client_row->change = change;
      break;

    case GSM_CLIENT_ROW_ADD:
      if (change == GSM_CLIENT_ROW_REMOVE)
	{
	  client_list->changes = g_slist_remove (client_list->changes,
						 client_row);
	  g_object_unref (client_row);
	}
      break;

    case GSM_CLIENT_ROW_REMOVE:
    default:
      /* impossible */
      g_assert_not_reached();
      break;
    }
}

void 
gsm_client_list_commit_changes (GsmClientList* client_list)
{
  g_return_if_fail(client_list != NULL);
  g_return_if_fail(GSM_IS_CLIENT_LIST(client_list));

  if (! client_list->committed)
    {
      GSList *list;
      GSList *rlist = NULL;
      
      for (list = client_list->changes; list; list = list->next)
	{
	  GsmClientRow *client_row = list->data;
	  GsmClient    *client;

	  client = GSM_CLIENT (client_row);

	  switch (client_row->change)
	    {
	    case GSM_CLIENT_ROW_ADD:
	      gsm_client_commit_add (client);
	      break;
	    case GSM_CLIENT_ROW_REMOVE:
	      gsm_client_commit_remove (client);
	      break;
	    default:
	      gsm_client_commit_style (client);
	      gsm_client_commit_order (client);
	      break;
	    }
	  rlist = g_slist_prepend (rlist, client_row);
	}
      g_slist_free (client_list->changes);
      client_list->changes = rlist;
      
      client_list->committed = TRUE;
      client_list->dirty = FALSE;
    }
}

void 
gsm_client_list_discard_changes (GsmClientList* client_list)
{
  GSList *list;
  
  g_return_if_fail(client_list != NULL);
  g_return_if_fail(GSM_IS_CLIENT_LIST(client_list));

  for (list = client_list->changes; list; list = list->next)
    {
      GsmClientRow *client_row = list->data;

      g_object_unref (client_row);
      client_row->change = GSM_CLIENT_ROW_NONE;
    }
  g_slist_free (client_list->changes);

  client_list->changes = NULL;  
  client_list->committed = FALSE;
  client_list->dirty = FALSE;
}

void 
gsm_client_list_revert_changes (GsmClientList* client_list)
{
  GSList *list;

  g_return_if_fail(client_list != NULL);
  g_return_if_fail(GSM_IS_CLIENT_LIST(client_list));

  for (list = client_list->changes; list;)
    {
      GsmClientRow *client_row = list->data;

      list = list->next;
      switch (client_row->change)
	{
	case GSM_CLIENT_ROW_ADD:
	  gsm_client_row_remove (client_row);
	  break;
	case GSM_CLIENT_ROW_REMOVE:
	  gsm_client_row_add (client_row);
	  break;
	default:
	  gsm_client_row_set_style (client_row, client_row->revert_style);
	  gsm_client_row_set_order (client_row, client_row->revert_order);
	  break;
	}
    }
  if (client_list->committed)
    {
      for (list = client_list->changes; list;)
	{
	  GsmClientRow *client_row = list->data;
	  GsmClient    *client;

	  client = GSM_CLIENT (client_row);

	  list = list->next;
	  switch (client_row->change)
	    {
	    case GSM_CLIENT_ROW_ADD:
	      gsm_client_commit_remove (client);
	      break;
	    case GSM_CLIENT_ROW_REMOVE:
	      gsm_client_commit_add (client);
	      break;
	    default:
	      gsm_client_commit_style (client);
	      gsm_client_commit_order (client);
	      break;
	    }
	}
    }
  gsm_client_list_discard_changes (client_list);
}

gboolean 
gsm_client_list_add_program (GsmClientList *client_list,
			     const char    *command)
{
  GsmClientRow *client_row;
  gboolean      command_complete;

  g_return_val_if_fail(client_list != NULL, FALSE);
  g_return_val_if_fail(GSM_IS_CLIENT_LIST(client_list), FALSE);

  if ((command_complete = gsm_sh_quotes_balance(command)))
    {
      client_row = gsm_client_row_new (client_list);
      GSM_CLIENT (client_row)->command = g_strdup(command);
      gsm_client_row_add (client_row);
      register_change (client_list, client_row, GSM_CLIENT_ROW_ADD);
    }
  return command_complete;
}

void
gsm_client_list_remove_selection (GsmClientList* client_list)
{
  GtkTreeSelection *selection;
  GtkTreeIter iter;
  GtkTreeModel *model;
  GsmClientRow *client_row;

  g_return_if_fail(client_list != NULL);
  g_return_if_fail(GSM_IS_CLIENT_LIST(client_list));

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (client_list));

  if (!gtk_tree_selection_get_selected (selection, &model, &iter))
    return;

  gtk_tree_model_get (model, &iter,
		      GSM_CLIENT_LIST_COL_CLIENT_ROW, &client_row,
		      -1);
    
  gsm_client_row_remove (client_row);
  register_change (client_list, client_row, GSM_CLIENT_ROW_REMOVE);

#ifdef FIXME
  gtk_clist_select_row (clist, row - 1, 0);
#endif
}

static void
changed_cb (GsmClientEditor *client_editor, guint order, GsmStyle style)
{
  GsmClientRow  *client_row;
  GsmClientList *client_list;

  client_row = GSM_CLIENT_ROW (client_editor->client);
  client_list = client_row->client_list;

  register_change (client_list, client_row, GSM_CLIENT_ROW_EDIT);
  gsm_client_row_set_order (client_row, order);
  gsm_client_row_set_style (client_row, style);
}

static void 
initialized_cb (GsmClientList* client_list)
{
#ifdef FIXME
  if (clist->rows > 0)
    select_cb (client_list, clist->rows - 1);
#endif
  g_signal_emit ((GtkObject*)client_list,
		 gsm_client_list_signals[INITIALIZED], 0);
}
