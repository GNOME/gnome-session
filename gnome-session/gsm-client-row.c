/* gsm-client-row.c - a gsm-client object for entry into a clist

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

#include "gsm-client-row.h"
#include "gsm-column.h"

static GsmClientClass *parent_class = NULL;

static void gsm_client_row_destroy  (GtkObject *o);
static void client_remove   (GsmClient* client);
static void client_reasons  (GsmClient* client, GSList* reasons);
static void client_command  (GsmClient* client, gchar* command);
static void client_state    (GsmClient* client, GsmState state);
static void client_style    (GsmClient* client, GsmStyle style);
static void client_order    (GsmClient* client, guint order);

static void
gsm_client_row_class_init (GsmClientRowClass *klass)
{
  GtkObjectClass *object_class = (GtkObjectClass*) klass;
  GsmClientClass *client_class = (GsmClientClass*) klass;

  parent_class = gtk_type_class (gsm_client_get_type ());

  client_class->remove   = client_remove;
  client_class->state    = client_state;
  client_class->command  = client_command;
  client_class->style    = client_style;
  client_class->order    = client_order;
  client_class->reasons  = client_reasons;

  object_class->destroy = gsm_client_row_destroy;
}

GtkTypeInfo gsm_client_row_info = 
{
  "GsmClientRow",
  sizeof (GsmClientRow),
  sizeof (GsmClientRowClass),
  (GtkClassInitFunc) gsm_client_row_class_init,
  (GtkObjectInitFunc) NULL,
  (GtkArgSetFunc) NULL,
  (GtkArgGetFunc) NULL,
  (GtkClassInitFunc) NULL
};

guint
gsm_client_row_get_type (void)
{
  static guint type = 0;

  if (!type)
    type = gtk_type_unique (gsm_client_get_type (), &gsm_client_row_info);
  return type;
}

GtkObject* 
gsm_client_row_new (GsmClientList* client_list)
{
  GsmClientRow* client_row;

  client_row = gtk_type_new(gsm_client_row_get_type());
  client_row->client_list = client_list;
  client_row->row = -1;
  client_row->change = GSM_CLIENT_ROW_NONE;
  return GTK_OBJECT (client_row);
}

static void 
gsm_client_row_destroy (GtkObject *o)
{
  GsmClientRow* client_row = (GsmClientRow*)o;

  g_return_if_fail(client_row != NULL);
  g_return_if_fail(GSM_IS_CLIENT_ROW(client_row));

  (*(GTK_OBJECT_CLASS (parent_class)->destroy))(o);
}

void
gsm_client_row_add (GsmClientRow* client_row)
{
  GsmClient *client = (GsmClient*)client_row;

  g_return_if_fail (client_row != NULL);
  g_return_if_fail (GSM_IS_CLIENT_ROW (client_row));

  if (client_row->row < 0)
    {
      GsmClientList* client_list = client_row->client_list;
      GtkCList* clist = (GtkCList*)client_list;
      guint row;
      char temp[3];
      char* text[] = { temp, NULL, NULL, NULL };  

      snprintf (temp, 3, "%.02d", client->order);
      text[3] = client->command;
      client_row->row = gtk_clist_append (clist, text);
      gtk_clist_set_row_data (clist, client_row->row, client_row);
      for (row = client_row->row; row < clist->rows; row++)
	{
	  gpointer data = gtk_clist_get_row_data (clist, row);
	  GSM_CLIENT_ROW (data)->row = row;
	}
      gsm_column_set_pixmap (GSM_COLUMN (client_row->client_list->style_col),
			     client_row->row, client->style);
      gsm_column_set_pixmap (GSM_COLUMN (client_row->client_list->state_col),
			     client_row->row, client->state);
    }
}

void gsm_client_row_remove (GsmClientRow* client_row)
{
  GsmClient *client = (GsmClient*)client_row;

  g_return_if_fail (client_row != NULL);
  g_return_if_fail (GSM_IS_CLIENT_ROW (client_row));

  if (client_row->row > -1)
    {
      GsmClientList* client_list = client_row->client_list;
      GtkCList* clist = (GtkCList*)client_list;
      guint row;
      
      gtk_clist_remove (clist, client_row->row);
      for (row = client_row->row; row < clist->rows; row++)
	{
	  gpointer data = gtk_clist_get_row_data (clist, row);
	  GSM_CLIENT_ROW (data)->row = row;
	}
      client_row->row = -2;
    }
  else
    {
      /* removal completed */
      client_row->row = -1;
    }
}

static void
client_remove (GsmClient* client)
{
  gsm_client_row_remove ((GsmClientRow*)client);      
}

/* External clients are added into the list when they set their command 
 * unless a removal is pending on the client. */
static void
client_command (GsmClient* client, gchar* command)
{
  GsmClientRow *client_row = (GsmClientRow*)client;

  g_return_if_fail (client_row != NULL);
  g_return_if_fail (GSM_IS_CLIENT_ROW (client_row));

  if (client_row->row > -2)
    {
      gchar* old_command = client->command;
      GtkCList* clist = (GtkCList*)client_row->client_list;

      client->command = command;
      gtk_clist_freeze (clist);
      if (client_row->row > -1)
	gtk_clist_set_text (clist, client_row->row, 3, command);
      else 
	gsm_client_row_add (client_row);
      gtk_clist_thaw (clist);
      client->command = old_command;
    }
}

static void
client_state (GsmClient* client, GsmState state)
{
  GsmClientRow *client_row = (GsmClientRow*)client;

  g_return_if_fail (client_row != NULL);
  g_return_if_fail (GSM_IS_CLIENT_ROW (client_row));

  if (client_row->row > -1 && client->state != state)
    gsm_column_set_pixmap (GSM_COLUMN (client_row->client_list->state_col),
			   client_row->row, state);
}

void
gsm_client_row_set_order (GsmClientRow* client_row, guint order)
{
  GsmClient *client = (GsmClient*)client_row;

  g_return_if_fail (client_row != NULL);
  g_return_if_fail (GSM_IS_CLIENT_ROW (client_row));

  if (order != client->order)
    {	  
      gint row = client_row->row;
      client->order = order;

      if (row > -1)
	{
	  GtkCList* clist = (GtkCList*)client_row->client_list;
	  
	  gtk_clist_freeze (clist);
	  gtk_clist_remove (clist, client_row->row);
	  client_row->row = -1;
	  gsm_client_row_add (client_row);
	  for (; row < client_row->row; row++)
	    {
	      gpointer data = gtk_clist_get_row_data (clist, row);
	      GSM_CLIENT_ROW (data)->row = row;
	    }
	  gtk_clist_thaw (clist);
	}
    }
}

static void
client_order (GsmClient* client, guint order)
{
  gsm_client_row_set_order ((GsmClientRow*)client, order);
}

void
gsm_client_row_set_style (GsmClientRow* client_row, GsmStyle style)
{
  GsmClient *client = (GsmClient*)client_row;

  g_return_if_fail (client_row != NULL);
  g_return_if_fail (GSM_IS_CLIENT_ROW (client_row));

  if (style != client->style)
    {
      client->style = style;

      if (client_row->row > -1)
	gsm_column_set_pixmap (GSM_COLUMN (client_row->client_list->style_col),
			       client_row->row, style);
    }
}

static void
client_style (GsmClient* client, GsmStyle style)
{
  gsm_client_row_set_style ((GsmClientRow*)client, style);
}

/* FIXME: this could almost certainly be improved ... */
static void
client_reasons (GsmClient* client, GSList* reasons)
{
  GtkWidget* dialog;
  gchar* message = client->command;
  
  for (;reasons; reasons = reasons->next)
    message = g_strjoin ("\n", message, (gchar*)reasons->data, NULL);
  
  dialog = gnome_warning_dialog (message);
  g_free (message);
}
