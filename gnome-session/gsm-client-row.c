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

#include <glib/gi18n.h>

#include <gtk/gtkimage.h>

#include "gsm-client-row.h"
#include "gsm-client-editor.h"

static GsmClientClass *gsm_client_row_parent_class = NULL;

static void client_remove   (GsmClient* client);
static void client_command  (GsmClient* client, gchar* command);
static void client_state    (GsmClient* client, GsmState state);
static void client_style    (GsmClient* client, GsmStyle style);
static void client_order    (GsmClient* client, guint order);

static void 
gsm_client_row_finalize (GObject *object)
{
  GsmClientRow *client_row = (GsmClientRow *) object;

  g_return_if_fail (client_row != NULL);
  g_return_if_fail (GSM_IS_CLIENT_ROW (client_row));

  G_OBJECT_CLASS (gsm_client_row_parent_class)->finalize (object);
}

static void
gsm_client_row_class_init (GsmClientRowClass *klass)
{
  GsmClientClass *client_class = (GsmClientClass *) klass;
  GObjectClass   *gobject_class = (GObjectClass *) klass;

  client_class->remove   = client_remove;
  client_class->state    = client_state;
  client_class->command  = client_command;
  client_class->style    = client_style;
  client_class->order    = client_order;

  gobject_class->finalize = gsm_client_row_finalize;
}

static void
gsm_client_row_instance_init (GsmClientRow *client_row)
{
  client_row->state  = GSM_CLIENT_ROW_NEW;
  client_row->change = GSM_CLIENT_ROW_NONE;
}

GType
gsm_client_row_get_type (void)
{
	static GType retval = 0;

	if (!retval) {
		static const GTypeInfo info = {
			sizeof (GsmClientRowClass),
			(GBaseInitFunc)     NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc)    gsm_client_row_class_init,
			NULL,               /* class_finalize */
			NULL,               /* class_data */
			sizeof (GsmClientRow),
			0,                  /* n_preallocs */
			(GInstanceInitFunc) gsm_client_row_instance_init
		};

		retval = g_type_register_static (GSM_TYPE_CLIENT, "GsmClientRow", &info, 0);
		gsm_client_row_parent_class = g_type_class_ref (GSM_TYPE_CLIENT);
	}

	return retval;
}

GsmClientRow * 
gsm_client_row_new (GsmClientList *client_list)
{
  GsmClientRow *client_row;

  client_row = g_object_new (GSM_TYPE_CLIENT_ROW, NULL);

  client_row->client_list = client_list;
  client_row->model = gtk_tree_view_get_model (GTK_TREE_VIEW (client_list));

  return client_row;
}

void
gsm_client_row_add (GsmClientRow* client_row)
{
  GsmClient *client = (GsmClient*)client_row;
  char temp[3];

  g_return_if_fail (client_row != NULL);
  g_return_if_fail (GSM_IS_CLIENT_ROW (client_row));

  if (client_row->state == GSM_CLIENT_ROW_ADDED)
    return;

  gtk_list_store_append (GTK_LIST_STORE (client_row->model), &client_row->iter);

  snprintf (temp, 3, "%.02d", client->order);

  gtk_list_store_set (GTK_LIST_STORE (client_row->model), &client_row->iter,
		      GSM_CLIENT_LIST_COL_ORDER, temp,
		      GSM_CLIENT_LIST_COL_STYLE, client->style,
		      GSM_CLIENT_LIST_COL_STYLE_PB, style_data[client->style].pixbuf,
		      GSM_CLIENT_LIST_COL_STATE, client->state,
		      GSM_CLIENT_LIST_COL_STATE_PB, state_data[client->state].pixbuf,
		      GSM_CLIENT_LIST_COL_COMMAND, client->command,
		      GSM_CLIENT_LIST_COL_CLIENT_ROW, client_row,
		      -1);

  client_row->state = GSM_CLIENT_ROW_ADDED;
}

void
gsm_client_row_remove (GsmClientRow* client_row)
{
  g_return_if_fail (client_row != NULL);
  g_return_if_fail (GSM_IS_CLIENT_ROW (client_row));

  if (client_row->state == GSM_CLIENT_ROW_ADDED)
    {
      gtk_list_store_remove (GTK_LIST_STORE (client_row->model),
			     &client_row->iter);
      client_row->state = GSM_CLIENT_ROW_REMOVED;
    }
  else
    {
      client_row->state = GSM_CLIENT_ROW_NEW;
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
  char *old_command;

  g_return_if_fail (client_row != NULL);
  g_return_if_fail (GSM_IS_CLIENT_ROW (client_row));

  old_command = client->command;
  client->command = command;

  switch (client_row->state)
    {
    case GSM_CLIENT_ROW_ADDED:
      gtk_list_store_set (GTK_LIST_STORE (client_row->model),
			  &client_row->iter,
			  GSM_CLIENT_LIST_COL_COMMAND, command,
			  -1);
      break;
    case GSM_CLIENT_ROW_NEW:
      gsm_client_row_add (client_row);
      break;
    default:
      break;
    }

  client->command = old_command;
}

static void
client_state (GsmClient* client, GsmState state)
{
  GsmClientRow *client_row = (GsmClientRow*)client;

  g_return_if_fail (client_row != NULL);
  g_return_if_fail (GSM_IS_CLIENT_ROW (client_row));

  if (state == client->state)
    return;

  client->state = state;

  if (client_row->state != GSM_CLIENT_ROW_ADDED)
    return;
  
  gtk_list_store_set (GTK_LIST_STORE (client_row->model),
		      &client_row->iter,
		      GSM_CLIENT_LIST_COL_STATE, state,
		      GSM_CLIENT_LIST_COL_STATE_PB, state_data[state].pixbuf,
		      -1);
}

void
gsm_client_row_set_order (GsmClientRow* client_row, guint order)
{
  GsmClient *client = (GsmClient*)client_row;
  char temp[3];

  g_return_if_fail (client_row != NULL);
  g_return_if_fail (GSM_IS_CLIENT_ROW (client_row));

  if (order == client->order)
    return;

  client->order = order;

  if (client_row->state != GSM_CLIENT_ROW_ADDED)
    return;

  snprintf (temp, 3, "%.02d", client->order);

  gtk_list_store_set (GTK_LIST_STORE (client_row->model),
		      &client_row->iter,
		      GSM_CLIENT_LIST_COL_ORDER, temp,
		      -1);
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

  if (style == client->style)
    return;

  client->style = style;

  if (client_row->state != GSM_CLIENT_ROW_ADDED)
    return;

  gtk_list_store_set (GTK_LIST_STORE (client_row->model),
		      &client_row->iter,
		      GSM_CLIENT_LIST_COL_STYLE, style,
		      GSM_CLIENT_LIST_COL_STYLE_PB, style_data[style].pixbuf,
		      -1);
}

static void
client_style (GsmClient* client, GsmStyle style)
{
  gsm_client_row_set_style ((GsmClientRow*)client, style);
}
