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
static void unselect_cb (GsmClientList* client_list);
static void select_cb (GsmClientList* client_list, gint row);
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

  parent_class = gtk_type_class (gtk_clist_get_type ());

  gsm_client_list_signals[DIRTY] =
    gtk_signal_new ("dirty",
		    GTK_RUN_LAST,
		    object_class->type,
		    GTK_SIGNAL_OFFSET (GsmClientListClass, dirty),
		    gtk_signal_default_marshaller,
		    GTK_TYPE_NONE, 0); 
  gsm_client_list_signals[STARTED] =
    gtk_signal_new ("started",
		    GTK_RUN_LAST,
		    object_class->type,
		    GTK_SIGNAL_OFFSET (GsmClientListClass, started),
		    gtk_signal_default_marshaller,
		    GTK_TYPE_NONE, 0); 
  gsm_client_list_signals[INITIALIZED] =
    gtk_signal_new ("initialized",
		    GTK_RUN_LAST,
		    object_class->type,
		    GTK_SIGNAL_OFFSET (GsmClientListClass, initialized),
		    gtk_signal_default_marshaller,
		    GTK_TYPE_NONE, 0); 

  gtk_object_class_add_signals (object_class, gsm_client_list_signals, 
				NSIGNALS);

  klass->dirty       = dirty;
  klass->started     = NULL;
  klass->initialized = NULL;

  object_class->destroy = gsm_client_list_destroy;
}

static GtkTypeInfo gsm_client_list_info = 
{
  "GsmClientList",
  sizeof (GsmClientList),
  sizeof (GsmClientListClass),
  (GtkClassInitFunc) gsm_client_list_class_init,
  (GtkObjectInitFunc) NULL,
  NULL,
  NULL,
  (GtkClassInitFunc) NULL
};

guint
gsm_client_list_get_type (void)
{
  static guint type = 0;

  if (!type)
    type = gtk_type_unique (gtk_clist_get_type (), &gsm_client_list_info);
  return type;
}

GtkWidget* 
gsm_client_list_new (void)
{
  GsmClientList* client_list = gtk_type_new(gsm_client_list_get_type());
  GtkCList* clist = (GtkCList*) client_list;
  GdkFont*  font;
  gchar*    titles[4];  
  int i;

/* Changes to build on IRIX */
  
  titles[0] = N_("Order"); 
  titles[1] = N_("Style"); 
  titles[2] = N_("State");
  titles[3] = N_("Program");

  for(i = 0; i < sizeof(titles)/sizeof(titles[0]); i++)
    titles[i] = gettext(titles[i]);

  client_list->client_editor = gsm_client_editor_new ();
  client_list->session   = NULL;
  client_list->committed = FALSE;
  client_list->dirty     = FALSE;
  client_list->changes   = NULL;

  gtk_clist_construct (clist, 4, titles);
  gtk_signal_connect(GTK_OBJECT(client_list), "select_row",
		     GTK_SIGNAL_FUNC (select_cb), NULL);
  gtk_signal_connect(GTK_OBJECT(client_list), "unselect_row",
		     GTK_SIGNAL_FUNC (unselect_cb), NULL);
  gtk_signal_connect(GTK_OBJECT(client_list->client_editor), "changed",
		     GTK_SIGNAL_FUNC (changed_cb), NULL);

  gtk_clist_set_selection_mode (clist, GTK_SELECTION_BROWSE);
  gtk_clist_set_auto_sort (clist, TRUE);

  gtk_clist_set_column_justification (clist, 0, GTK_JUSTIFY_CENTER);
  gtk_clist_set_column_justification (clist, 1, GTK_JUSTIFY_CENTER);
  gtk_clist_set_column_justification (clist, 2, GTK_JUSTIFY_CENTER);
  gtk_clist_column_titles_passive (clist);

  font = gtk_widget_get_style (GTK_WIDGET (client_list))->font;
  gtk_clist_set_column_min_width (clist, 3, 40 * gdk_string_width (font, "n"));
  gtk_clist_set_column_max_width (clist, 3, 50 * gdk_string_width (font, "n"));
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
  gtk_clist_freeze (GTK_CLIST (client_list));
  if (client_list->session)
    {
      gtk_clist_clear (GTK_CLIST (client_list));
      gtk_object_unref (client_list->session);
    }

  client_list->session = 
    gsm_session_live ((GsmClientFactory)gsm_client_row_new, client_list);

  gtk_signal_connect_object (GTK_OBJECT (client_list->session), "initialized",
			     GTK_SIGNAL_FUNC (initialized_cb), 
			     GTK_OBJECT (client_list));
}

void 
gsm_client_list_saved_session (GsmClientList* client_list, gchar* name)
{
  gtk_clist_freeze (GTK_CLIST (client_list));
  if (client_list->session)
    {
      gtk_clist_clear (GTK_CLIST (client_list));
      gtk_object_unref (client_list->session);
    }

  client_list->session = 
    gsm_session_new (name, (GsmClientFactory)gsm_client_row_new, client_list);

  gtk_signal_connect_object (GTK_OBJECT (client_list->session), "initialized",
			     GTK_SIGNAL_FUNC (initialized_cb), 
			     GTK_OBJECT (client_list));
}

void 
gsm_client_list_start_session (GsmClientList* client_list)
{
  gsm_session_start (GSM_SESSION (client_list->session));
  client_list->pending = ((GtkCList*)client_list)->rows;
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
  GsmClient* client = (GsmClient*)client_row;
    
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
	gtk_object_ref ((GtkObject*)client_row);
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
	  gtk_object_unref ((GtkObject*)client_row);
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
	  GsmClientRow* client_row = (GsmClientRow*)list->data;
	  GsmClient* client = (GsmClient*)client_row;
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
      GsmClientRow* client_row = (GsmClientRow*)list->data;

      gtk_object_unref ((GtkObject*)client_row);
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
  GtkCList* clist = (GtkCList*)client_list;

  g_return_if_fail(client_list != NULL);
  g_return_if_fail(GSM_IS_CLIENT_LIST(client_list));

  gtk_clist_freeze (clist);
  for (list = client_list->changes; list;)
    {
      GsmClientRow* client_row = (GsmClientRow*)list->data;
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
	  GsmClientRow* client_row = (GsmClientRow*)list->data;
	  GsmClient* client = (GsmClient*)client_row;
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
  gtk_clist_thaw (clist);
  gsm_client_list_discard_changes (client_list);
}

gboolean 
gsm_client_list_add_program (GsmClientList* client_list, gchar* command)
{
  GsmClientRow* client_row;
  GtkCList* clist = (GtkCList*)client_list;
  gboolean command_complete;

  g_return_val_if_fail(client_list != NULL, FALSE);
  g_return_val_if_fail(GSM_IS_CLIENT_LIST(client_list), FALSE);

  if ((command_complete = gsm_sh_quotes_balance(command)))
    {
      gtk_clist_freeze (clist);
      client_row = GSM_CLIENT_ROW (gsm_client_row_new (client_list));
      GSM_CLIENT (client_row)->command = g_strdup(command);
      gsm_client_row_add (client_row);
      register_change (client_list, client_row, GSM_CLIENT_ROW_ADD);
      gtk_clist_thaw (clist);
    }
  return command_complete;
}

void
gsm_client_list_remove_selection (GsmClientList* client_list)
{
  GtkCList* clist = (GtkCList*)client_list;

  g_return_if_fail(client_list != NULL);
  g_return_if_fail(GSM_IS_CLIENT_LIST(client_list));

  gtk_clist_freeze (clist);
  {
    gint row = GPOINTER_TO_INT (clist->selection->data);
    gpointer data = gtk_clist_get_row_data (clist, row);
    GsmClientRow* client_row = GSM_CLIENT_ROW (data);
    
    gsm_client_row_remove (client_row);
    register_change (client_list, client_row, GSM_CLIENT_ROW_REMOVE);
  }
  gtk_clist_thaw (clist);
}

static void
changed_cb (GsmClientEditor *client_editor, guint order, GsmStyle style)
{
  GsmClientRow *client_row = (GsmClientRow*)client_editor->client;
  GsmClientList *client_list = client_row->client_list;

  register_change (client_list, client_row, GSM_CLIENT_ROW_EDIT);
  gsm_client_row_set_order (client_row, order);
  gsm_client_row_set_style (client_row, style);
}

static void 
initialized_cb (GsmClientList* client_list)
{
  GtkCList *clist = (GtkCList*) client_list;

  gtk_clist_set_column_width (clist, 3,
			      gtk_clist_optimal_column_width (clist, 3));
  if (clist->rows > 0)
    select_cb (client_list, clist->rows - 1);
  gtk_clist_thaw (clist);
  gtk_signal_emit ((GtkObject*)client_list, 
		   gsm_client_list_signals[INITIALIZED]);
  gtk_clist_set_column_max_width (clist, 3, -1);
  gtk_clist_set_column_auto_resize (clist, 3, TRUE);
}

static void 
select_cb (GsmClientList* client_list, gint row)
{
  GsmClientEditor *client_editor=(GsmClientEditor*)client_list->client_editor;
  GtkCList *clist = (GtkCList*)client_list;
  gpointer data = gtk_clist_get_row_data (clist, row);

  if (data)
    gsm_client_editor_set_client (client_editor, data);  
}

static void 
unselect_cb (GsmClientList* client_list)
{
  GsmClientEditor *client_editor=(GsmClientEditor*)client_list->client_editor;

  gsm_client_editor_set_client (client_editor, NULL);
}
