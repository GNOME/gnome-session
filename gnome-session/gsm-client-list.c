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
#include <gnome.h>

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

static guint gsm_client_list_signals[NSIGNALS];
static GtkCListClass *parent_class = NULL;

static void
gsm_client_list_class_init (GsmClientListClass *klass)
{
  GtkObjectClass *object_class = (GtkObjectClass*) klass;

  parent_class = gtk_type_class (GTK_TYPE_TREE_VIEW);

  gsm_client_list_signals[DIRTY] =
    gtk_signal_new ("dirty",
		    GTK_RUN_LAST,
		    GTK_CLASS_TYPE (object_class),
		    GTK_SIGNAL_OFFSET (GsmClientListClass, dirty),
		    gtk_signal_default_marshaller,
		    GTK_TYPE_NONE, 0); 
  gsm_client_list_signals[STARTED] =
    gtk_signal_new ("started",
		    GTK_RUN_LAST,
		    GTK_CLASS_TYPE (object_class),
		    GTK_SIGNAL_OFFSET (GsmClientListClass, started),
		    gtk_signal_default_marshaller,
		    GTK_TYPE_NONE, 0); 
  gsm_client_list_signals[INITIALIZED] =
    gtk_signal_new ("initialized",
		    GTK_RUN_LAST,
		    GTK_CLASS_TYPE (object_class),
		    GTK_SIGNAL_OFFSET (GsmClientListClass, initialized),
		    gtk_signal_default_marshaller,
		    GTK_TYPE_NONE, 0); 

  klass->dirty       = dirty;
  klass->started     = NULL;
  klass->initialized = NULL;

  object_class->destroy = gsm_client_list_destroy;
}

GType
gsm_client_list_get_type (void)
{
  static GType type = 0;

  if (!type)
    {
      static GTypeInfo gsm_client_list_info = 
	{
	  sizeof (GsmClientListClass),
	  (GBaseInitFunc)     NULL,
	  (GBaseFinalizeFunc) NULL,
	  (GClassInitFunc) gsm_client_list_class_init,
	  NULL,
	  NULL,
	  sizeof (GsmClientList),
	  0,
	  (GInstanceInitFunc) NULL,
	};

      type = g_type_register_static (GTK_TYPE_TREE_VIEW, "GsmClientList", &gsm_client_list_info, 0);
    }

  return type;
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

GtkWidget* 
gsm_client_list_new (void)
{
  GsmClientList* client_list;
  GtkTreeView *view;
  GtkCellRenderer *text_cell, *pixbuf_cell;
  GtkTreeSelection *selection;

  client_list = g_object_new (gsm_client_list_get_type(), NULL);
  view = GTK_TREE_VIEW (client_list);

  gtk_tree_view_set_rules_hint (view, TRUE);

  client_list->model = gtk_list_store_new (GSM_CLIENT_LIST_COL_LAST,
					   G_TYPE_STRING,
					   GDK_TYPE_PIXBUF,
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

  gtk_tree_view_insert_column_with_attributes (view, -1,
					       _("Style"), pixbuf_cell,
					       "pixbuf", GSM_CLIENT_LIST_COL_STYLE,
					       NULL);

  gtk_tree_view_insert_column_with_attributes (view, -1,
					       _("State"), pixbuf_cell,
					       "pixbuf", GSM_CLIENT_LIST_COL_STATE,
					       NULL);

  gtk_tree_view_insert_column_with_attributes (view, -1,
					       _("Program"), text_cell,
					       "text", GSM_CLIENT_LIST_COL_COMMAND,
					       NULL);
					       
  client_list->client_editor = gsm_client_editor_new ();
  client_list->session   = NULL;
  client_list->committed = FALSE;
  client_list->dirty     = FALSE;
  client_list->changes   = NULL;

  selection = gtk_tree_view_get_selection (view);

  g_signal_connect (selection, "changed",
		    G_CALLBACK (selection_changed), client_list);

  gtk_signal_connect(GTK_OBJECT(client_list->client_editor), "changed",
		     GTK_SIGNAL_FUNC (changed_cb), NULL);

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

  (*(GTK_OBJECT_CLASS (parent_class)->destroy))(o);
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
    
  gtk_signal_emit ((GtkObject*)client_list, gsm_client_list_signals[DIRTY]); 

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
  gtk_signal_emit ((GtkObject*)client_list, 
		   gsm_client_list_signals[INITIALIZED]);
}
