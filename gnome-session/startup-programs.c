/* startup-programs.c

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

   Authors: Owen Taylor */

#include <config.h>
#include <ctype.h>
#include <stdlib.h>
#include <gnome.h>
#include "capplet-widget.h"
#include "session-properties-capplet.h"
#include "gsm-protocol.h"

/* Structure for storing information about one client */
typedef struct _ManualClient ManualClient;

struct _ManualClient
{
  int    priority;
  int    argc;
  char **argv;
};

/* Free client record */
static void
client_free (ManualClient *client)
{
  int i;
  
  for (i=0; i<client->argc; i++)
    g_free (client->argv[i]);
  
  g_free (client->argv);
  g_free (client);
}

/* Compare client records by priority */
static gint
client_compare (gconstpointer a, gconstpointer b)
{
  const ManualClient *client_a = a;
  const ManualClient *client_b = b;
  
  return client_a->priority - client_b->priority;
}

/* Read in the client record for a given session */
GSList *
startup_list_read (gchar *name)
{
  GSList *result = NULL;
  void *iterator;
  gchar *p;
  ManualClient *current = NULL;
  gchar *handle = NULL;
  
  gnome_config_push_prefix (MANUAL_CONFIG_PREFIX);
  
  iterator = gnome_config_init_iterator (name);
  while (iterator)
    {
      gchar *key;
      gchar *value;
      
      iterator = gnome_config_iterator_next (iterator, &key, &value);
      if (!iterator)
	      break;
      
      p = strchr (key, ',');
      if (p)
	{
	  *p = '\0';

	  if (!current || strcmp (handle, key) != 0) 
	    {
	      if (handle)
		g_free (handle);
	      
	      current = g_new0 (ManualClient, 1);
	      handle = g_strdup (key);

	      result = g_slist_prepend (result, current);
	    }

	  if (strcmp (p+1, "Priority") == 0)
	    current->priority = atoi (value);
	  else if (strcmp (p+1, "RestartCommand") == 0)
	    gnome_config_make_vector (value, &current->argc, &current->argv);
	}
      
      g_free (key);
      g_free (value);
  }
  
  gnome_config_pop_prefix ();
  
  return g_slist_sort (result, client_compare);
}

/* Write out a client list for under a given name */
void
startup_list_write (GSList *sl, const gchar *name)
{
  GSList *tmp_list;
  gchar *prefix;
  int i;

  gnome_config_push_prefix (MANUAL_CONFIG_PREFIX);
  gnome_config_clean_section (name);
  gnome_config_pop_prefix ();

  prefix = g_strconcat (MANUAL_CONFIG_PREFIX, name, "/", NULL);
  gnome_config_push_prefix (prefix);
  g_free (prefix);

  gnome_config_set_int ("num_clients", g_slist_length (sl));
  
  tmp_list = sl;
  i = 0;
  while (tmp_list)
    {
      ManualClient *client = tmp_list->data;
      gchar *key;

      key = g_strdup_printf ("%d,%s", i, "RestartStyleHint");
      gnome_config_set_int (key, 3);   /* RestartNever */
      g_free (key);

      key = g_strdup_printf ("%d,%s", i, "Priority");
      gnome_config_set_int (key, client->priority);
      g_free (key);

      key = g_strdup_printf ("%d,%s", i, "RestartCommand");
      gnome_config_set_vector (key, client->argc,
			       (const char * const *)client->argv);
      g_free (key);

      tmp_list = tmp_list->next;
      i++;
    }

  gnome_config_pop_prefix ();
  gnome_config_sync ();
}

/* Duplicate a client list */
GSList *
startup_list_duplicate (GSList *sl)
{
  GSList *tmp_list;
  GSList *result = NULL;
  int i;

  tmp_list = sl;
  while (tmp_list)
    {
      ManualClient *client = tmp_list->data;
      ManualClient *new_client = g_new (ManualClient, 1);

      new_client->priority = client->priority;
      new_client->argc = client->argc;
      new_client->argv = g_new (gchar *, client->argc);
      
      for (i=0; i<client->argc; i++)
	new_client->argv[i] = g_strdup (client->argv[i]);

      result = g_slist_prepend (result, new_client);
      
      tmp_list = tmp_list->next;
    }

  return g_slist_reverse (result);
}

/* Free a client list */
void
startup_list_free  (GSList *sl)
{
  GSList *tmp_list;

  tmp_list = sl;
  while (tmp_list)
    {
      ManualClient *client = tmp_list->data;

      client_free (client);
      tmp_list = tmp_list->next;
    }

  g_slist_free (sl);
}

/* Update the given clist to display the given client list */
void
startup_list_update_gui (GSList *sl, GtkWidget *clist)
{
  GSList *tmp_list;
  gint new_row;
  
  gtk_clist_clear (GTK_CLIST (clist));

  tmp_list = sl;
  while (tmp_list)
    {
      gchar *vals[2];
      ManualClient *client = tmp_list->data;
      
      vals[0] = g_strdup_printf ("%d", client->priority);
      vals[1] = gnome_config_assemble_vector (client->argc,
					      (const char * const *)client->argv);
      new_row = gtk_clist_append (GTK_CLIST (clist), vals);
      gtk_clist_set_row_data (GTK_CLIST (clist), new_row, client);
      
      g_free (vals[0]);
      g_free (vals[1]);

      tmp_list = tmp_list->next;
    }
}

/* Util function - check if a string is blank */
static gboolean
is_blank (gchar *str)
{
        while (*str) {
                if (!isspace(*str))
                        return FALSE;
                str++;
        }
        return TRUE;
}

/* Display a dialog for editing a client. The dialog parameter
 * is used to implement hiding the dialog when the user switches
 * away to another page of the control center */
static gboolean
edit_client (gchar *title, ManualClient *client, GtkWidget **dialog)
{
  GtkWidget *entry;
  GtkWidget *spinbutton;
  GtkWidget *label;
  GtkWidget *frame;
  GtkWidget *vbox;
  GtkWidget *util_vbox;
  GtkWidget *alignment;
  GtkWidget *gnome_entry;
  
  GtkObject *adjustment;

  gchar *tmp;

  *dialog = gnome_dialog_new (title,
			     GNOME_STOCK_BUTTON_OK,
			     GNOME_STOCK_BUTTON_CANCEL,
			     NULL);

  gnome_dialog_close_hides (GNOME_DIALOG (*dialog), TRUE);
  gtk_window_set_policy (GTK_WINDOW (*dialog), FALSE, TRUE, FALSE);
  gtk_window_set_default_size (GTK_WINDOW (*dialog), 400, -1);
  
  vbox = gtk_vbox_new (FALSE, GNOME_PAD_SMALL);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), GNOME_PAD_SMALL);
  
  gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (*dialog)->vbox), vbox,
		      TRUE, TRUE, 0);

  frame = gtk_frame_new (_("Startup Command"));
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 0);

  util_vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (util_vbox), GNOME_PAD_SMALL);
  gtk_container_add (GTK_CONTAINER (frame), util_vbox);

  gnome_entry = gnome_file_entry_new ("startup-commands", _("Startup Command"));
  entry = gnome_file_entry_gtk_entry (GNOME_FILE_ENTRY (gnome_entry));
  gtk_box_pack_start (GTK_BOX (util_vbox), gnome_entry, TRUE, TRUE, 0);

  tmp = gnome_config_assemble_vector (client->argc,
				      (const char * const *)client->argv);
  gtk_entry_set_text (GTK_ENTRY (entry), tmp);
  g_free (tmp);
  
  frame = gtk_frame_new (_("Priority"));
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 0);

  util_vbox = gtk_vbox_new (FALSE, GNOME_PAD_SMALL);
  gtk_container_set_border_width (GTK_CONTAINER (util_vbox), GNOME_PAD_SMALL);
  gtk_container_add (GTK_CONTAINER (frame), util_vbox);

  alignment = gtk_alignment_new (0.0, 0.5, 0.0, 0.0);
  gtk_box_pack_start (GTK_BOX (util_vbox), alignment, FALSE, FALSE, 0);

  adjustment = gtk_adjustment_new (client->priority,
				   -200.0, 200.0, 1.0, 10.0, 10.0); 
  spinbutton = gtk_spin_button_new (GTK_ADJUSTMENT (adjustment), 1.0, 0);
  gtk_container_add (GTK_CONTAINER (alignment), spinbutton);
  
  label = gtk_label_new (_("Programs with smaller values are started before programs with higher values. The default value should be OK"));
  gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_LEFT);
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  
  gtk_box_pack_start (GTK_BOX (util_vbox), label, FALSE, FALSE, 0);
  
  gtk_widget_show_all (*dialog);

  while (gnome_dialog_run (GNOME_DIALOG (*dialog)) == 0)
    {
      gchar *tmp = gtk_entry_get_text (GTK_ENTRY (entry));

      if (is_blank (tmp))
	{
	  GtkWidget *msgbox;
	  
	  gtk_widget_show (*dialog);
	  
	  msgbox = gnome_message_box_new (_("The startup command cannot be empty"),
					  GNOME_MESSAGE_BOX_ERROR,
					  GNOME_STOCK_BUTTON_OK,
					  NULL);
	  
	  gnome_dialog_set_parent (GNOME_DIALOG (msgbox), GTK_WINDOW (*dialog));
	  gnome_dialog_run (GNOME_DIALOG (msgbox));
	}
      else
	{
	  int i;

	  for (i = 0; i < client->argc; i++)
	    g_free (client->argv[i]);
	  g_free (client->argv);
	  
	  gnome_config_make_vector (tmp, &client->argc, &client->argv);
	  client->priority = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (spinbutton));

	  gtk_widget_destroy (*dialog);
	  *dialog = NULL;
	  return TRUE;
	}
    }

  gtk_widget_destroy (*dialog);
  *dialog = NULL;
  
  return FALSE;
}

/* Prompt the user with a dialog for adding a new client */
void
startup_list_add_dialog (GSList **sl, GtkWidget *clist, GtkWidget **dialog)
{
  ManualClient *client = g_new (ManualClient, 1);
  client->priority = 50;
  client->argc = 0;
  client->argv = NULL;

  if (edit_client (_("Add Startup Program"), client, dialog))
    {
      *sl = g_slist_prepend (*sl, client);
      *sl = g_slist_sort (*sl, client_compare);
    }
  else
    client_free (client);
}

/* Prompt the user with a dialog for editing the currently selected client */
void
startup_list_edit_dialog (GSList **sl, GtkWidget *clist, GtkWidget **dialog)
{
  if (GTK_CLIST (clist)->selection)
    {
      gint selected_row = GPOINTER_TO_UINT (GTK_CLIST (clist)->selection->data);
      ManualClient *client = gtk_clist_get_row_data (GTK_CLIST (clist), selected_row);

      if (edit_client (_("Edit Startup Program"), client, dialog))
	*sl = g_slist_sort (*sl, client_compare);
    }
}

/* Delete the currently selected client */
void
startup_list_delete (GSList **sl, GtkWidget *clist)
{
  if (GTK_CLIST (clist)->selection)
    {
      gint selected_row = GPOINTER_TO_UINT (GTK_CLIST (clist)->selection->data);
      ManualClient *client = gtk_clist_get_row_data (GTK_CLIST (clist), selected_row);

      *sl = g_slist_remove (*sl, client);
      client_free (client);
    }
}
