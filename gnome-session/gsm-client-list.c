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
#include "gsm-column.h"

#define GNOME_STOCK_MENU_HELP "Menu_Help"

static gchar *col_tip[] = { 
  N_("This button sets the start order of the selected programs.\n"), 
  N_("This button sets the restart style of the selected programs:\n"
     "Normal programs are unaffected by logouts but can die;\n"
     "Respawn programs are never allowed to die;\n"
     "Trash programs are discarded on logout and can die;\n"
     "Settings programs are always started on every login."), 
  N_("This button produces a key to the program states below:\n"
     "Inactive programs are waiting to start or have finished;\n"
     "Starting programs are being given time to get running;\n"
     "Running programs are normal members of the session;\n"
     "Saving programs are saving their session details;\n"
     "Programs which make no contact have Unknown states.\n"),
  N_("This column gives the command used to start a program.") 
};

static GnomeUIInfo style_data[] = {
  GNOMEUIINFO_ITEM_STOCK(N_("_Normal"), 
			 N_("Unaffected by logouts but can die."),
			 NULL, GNOME_STOCK_MENU_BLANK),
  GNOMEUIINFO_ITEM_STOCK(N_("_Respawn"), 
			 N_("Never allowed to die."),
			 NULL, GNOME_STOCK_MENU_REFRESH),
  GNOMEUIINFO_ITEM_STOCK(N_("_Trash"), 
			 N_("Discarded on logout and can die."),
			 NULL, GNOME_STOCK_MENU_TRASH),
  GNOMEUIINFO_ITEM_STOCK(N_("_Settings"), 
			 N_("Always started on every login."),
			 NULL, GNOME_STOCK_MENU_PREF),
  GNOMEUIINFO_END
};

static GnomeUIInfo state_data[] = {
  GNOMEUIINFO_ITEM_STOCK(N_("Inactive"), 
			 N_("Waiting to start or already finished."), 
			 NULL, GNOME_STOCK_MENU_BLANK),
  GNOMEUIINFO_ITEM_STOCK(N_("Starting"), 
			 N_("Started but has not yet reported state."), 
			 NULL, GNOME_STOCK_MENU_TIMER),
  GNOMEUIINFO_ITEM_STOCK(N_("Running"), 
			 N_("A normal member of the session."), 
			 NULL, GNOME_STOCK_MENU_EXEC),
  GNOMEUIINFO_ITEM_STOCK(N_("Saving"), 
			 N_("Saving session details."), 
			 NULL, GNOME_STOCK_MENU_SAVE),
  GNOMEUIINFO_ITEM_STOCK(N_("Unknown"), 
			 N_("State not reported within timeout."), 
			 NULL, GNOME_STOCK_MENU_HELP),
  GNOMEUIINFO_END
};

static void gsm_client_list_destroy  (GtkObject *o);
static gint create_stock_menu_pixmaps (void);
static gboolean order_value (guint *data);
static void initialized_cb (GsmClientList* client_list);
static void dirty (GsmClientList* client_list);
static void change_order (GtkObject* object, guint value);
static void change_style (GtkObject* object, guint value);

enum {
  DIRTY,
  INITIALIZED,
  NSIGNALS
};

static gint gsm_client_list_signals[NSIGNALS];
static GtkCListClass *parent_class = NULL;

static void
gsm_client_list_class_init (GsmClientListClass *klass)
{
  GtkObjectClass *object_class = (GtkObjectClass*) klass;
  GtkCListClass *clist_class = (GtkCListClass*) klass;

  parent_class = gtk_type_class (gtk_clist_get_type ());

  gsm_client_list_signals[DIRTY] =
    gtk_signal_new ("dirty",
		    GTK_RUN_LAST,
		    object_class->type,
		    GTK_SIGNAL_OFFSET (GsmClientListClass, dirty),
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
  klass->initialized = NULL;

  object_class->destroy = gsm_client_list_destroy;
}

GtkTypeInfo gsm_client_list_info = 
{
  "GsmClientList",
  sizeof (GsmClientList),
  sizeof (GsmClientListClass),
  (GtkClassInitFunc) gsm_client_list_class_init,
  (GtkObjectInitFunc) NULL,
  (GtkArgSetFunc) NULL,
  (GtkArgGetFunc) NULL,
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
  GtkObject*     selector;
  GdkFont*       font;
  gint           height, i;

  client_list->order_col = NULL;
  client_list->style_col = NULL;
  client_list->state_col = NULL;
  client_list->committed = FALSE;
  client_list->dirty     = FALSE;
  client_list->changes   = NULL;

  gtk_clist_construct (clist, 4, NULL);
  selector = gsm_selector_new (gnome_master_client(),
			       (GsmClientFactory)gsm_client_row_new, 
			       client_list);
  if (!selector)
    {
      gtk_widget_unref (GTK_WIDGET (client_list));
      return NULL;
    }
  client_list->selector = selector;
  client_list->tooltips = gtk_tooltips_new ();

  height = create_stock_menu_pixmaps ();

  gtk_clist_set_selection_mode (clist, GTK_SELECTION_MULTIPLE);
  gtk_clist_set_auto_sort (clist, TRUE);

  client_list->order_col = 
    gsm_column_new (_("Order"), NULL, order_value, change_order);
  client_list->style_col = 
    gsm_column_new (_("Style"), style_data, NULL, change_style);
  client_list->state_col = 
    gsm_column_new (_("State"), state_data, NULL, NULL);
  gtk_clist_set_column_widget (clist, 0, client_list->order_col);
  gtk_clist_set_column_widget (clist, 1, client_list->style_col);
  gtk_clist_set_column_widget (clist, 2, client_list->state_col);
  gtk_clist_set_column_title  (clist, 3, _("Program"));
  gtk_clist_set_column_auto_resize (clist, 3, TRUE);
  gtk_clist_column_titles_show (clist);

  for (i = 0; i < 4; i++)
    gtk_tooltips_set_tip (client_list->tooltips, 
			  gtk_clist_get_column_widget (clist, i)->parent,
			  col_tip[i], NULL);

  font = gtk_widget_get_style (GTK_WIDGET (client_list))->font;
  height = MAX (height, gdk_string_height (font, "N"));
  gtk_clist_set_row_height (clist, height);
  gtk_clist_set_column_width (clist, 3, 40 * gdk_string_width (font, "n"));
  
  gtk_signal_connect_object (GTK_OBJECT (selector), "initialized",
			     GTK_SIGNAL_FUNC (initialized_cb), 
			     GTK_OBJECT (client_list));
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
      /* impossible */
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
	}
      
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
  while (clist->selection)
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
change_style (GtkObject *o, guint style)
{
  GsmClientRow *client_row = GSM_CLIENT_ROW (o);
  GsmClientList* client_list = client_row->client_list;

  register_change (client_list, client_row, GSM_CLIENT_ROW_EDIT);
  gsm_client_row_set_style (client_row, style);
}

static void
change_order (GtkObject *o, guint order)
{
  GsmClientRow *client_row = GSM_CLIENT_ROW (o);
  GsmClientList* client_list = client_row->client_list;

  register_change (client_list, client_row, GSM_CLIENT_ROW_EDIT);
  gsm_client_row_set_order (client_row, order);
}

/* This is called when the order button is pressed. */
static gboolean
order_value (guint *value)
{
  static GnomeDialog *dialog = NULL;
  static GtkWidget *spin_button;
  gboolean do_rows;

  if (! dialog)
    {
      GtkAdjustment *adjustment;
      dialog = 
	GNOME_DIALOG (gnome_message_box_new (_("Position in start order:"), 
					     GNOME_MESSAGE_BOX_QUESTION,
					     GNOME_STOCK_BUTTON_OK, 
					     GNOME_STOCK_BUTTON_CANCEL,
					     NULL));
      gnome_dialog_set_default (dialog, 0);
      
      adjustment = GTK_ADJUSTMENT (gtk_adjustment_new (50.0, 0.0, 99.0, 
						       1.0, 10.0, 0.0));
      spin_button = gtk_spin_button_new (adjustment, 1.0, 0);
      gtk_box_pack_end (GTK_BOX (dialog->vbox), spin_button, 
			FALSE, FALSE, GNOME_PAD_SMALL );
      gtk_widget_show (spin_button);
      gnome_dialog_editable_enters(dialog, (GtkEditable*)spin_button);
      gnome_dialog_close_hides (dialog, TRUE);
    }
  gtk_widget_grab_focus (spin_button);
  do_rows = (gnome_dialog_run (dialog) == 0);
  if (do_rows)
    *value = gtk_spin_button_get_value_as_int ((GtkSpinButton*)spin_button);
  return do_rows;
}

static gint
create_stock_menu_pixmaps (void)
{
  GnomeStockPixmapEntry *pixmap_help; 
  GnomeStockPixmapEntry *menu_blank; 
  GnomeStockPixmapEntry *menu_help;

  menu_blank = gnome_stock_pixmap_checkfor (GNOME_STOCK_MENU_BLANK,
					    GNOME_STOCK_PIXMAP_REGULAR);
  pixmap_help = gnome_stock_pixmap_checkfor (GNOME_STOCK_PIXMAP_HELP,
					     GNOME_STOCK_PIXMAP_REGULAR);
  menu_help = g_malloc(sizeof(GnomeStockPixmapEntry));
  memcpy (menu_help, pixmap_help, sizeof (GnomeStockPixmapEntry));
  menu_help->imlib_s.scaled_width = menu_blank->imlib_s.scaled_width;
  menu_help->imlib_s.scaled_height = menu_blank->imlib_s.scaled_height;
  gnome_stock_pixmap_register (GNOME_STOCK_MENU_HELP,
			       GNOME_STOCK_PIXMAP_REGULAR, menu_help);

  return menu_blank->imlib_s.scaled_height;
}

static void 
initialized_cb (GsmClientList* client_list)
{
  gtk_signal_emit ((GtkObject*)client_list, 
		   gsm_client_list_signals[INITIALIZED]);
}
