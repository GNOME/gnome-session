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
#include <glib/gstdio.h>
#include <gnome.h>
#include <libgnome/gnome-desktop-item.h>
#include "session-properties-capplet.h"
#include "gsm-protocol.h"
#include "headers.h"

/* Structure for storing information about one client */
typedef struct _ManualClient ManualClient;

struct _ManualClient
{
  char    *desktop_file;
  gboolean to_remove;
  gboolean enabled;
  int      argc;
  char   **argv;
};

/* Free client record */
static void
client_free (ManualClient *client)
{
  int i;
  
  for (i=0; i<client->argc; i++)
    g_free (client->argv[i]);

  g_free (client->desktop_file);
  g_free (client->argv);
  g_free (client);
}

static void
search_desktop_entries_in_dir (GHashTable *clients, const char *path)
{
  ManualClient *current = NULL;
  const char *file;
  GDir *dir;

  dir = g_dir_open (path, 0, NULL);
  if (!dir)
    return;

  while ((file = g_dir_read_name (dir)))
    {
      char *desktop_file, *hash_key;
      GnomeDesktopItem *ditem;
      Client *hash_client;

      if (!g_str_has_suffix (file, ".desktop"))
	continue;

      desktop_file = g_build_filename (path, file, NULL);
      ditem = gnome_desktop_item_new_from_file (desktop_file, GNOME_DESKTOP_ITEM_LOAD_NO_TRANSLATIONS, NULL);
      if (ditem)
        {
	  current = g_new0 (ManualClient, 1);
	  current->desktop_file = desktop_file;
	  current->to_remove = FALSE;
	  g_shell_parse_argv (gnome_desktop_item_get_string (ditem, GNOME_DESKTOP_ITEM_EXEC), &current->argc, &current->argv, NULL);
	  if (gnome_desktop_item_attr_exists (ditem, "X-GNOME-Autostart-enabled"))
	    current->enabled = gnome_desktop_item_get_boolean (ditem, "X-GNOME-Autostart-enabled");
	  else
	    current->enabled = TRUE;

	  if (g_hash_table_lookup_extended (clients, file, &hash_key, &hash_client))
	    {
  	      g_hash_table_remove (clients, file);
	      g_free (hash_key);
	      client_free (hash_client);
	    }
	  g_hash_table_insert (clients, g_strdup (file), current);

	  gnome_desktop_item_unref (ditem);
	}
      else
        g_free (desktop_file);
    }

  g_dir_close (dir);

}

static gboolean
hash_table_remove_cb (gpointer key, gpointer value, gpointer user_data)
{
  GSList **result = (GSList **) user_data;

  *result = g_slist_prepend (*result, value);
  g_free (key);

  return TRUE;
}

/* Read in the client record for a given session */
GSList *
startup_list_read (gchar *name)
{
  GHashTable *clients;
  char *path;
  GSList *result = NULL;

  /* create temporary hash table */
  clients = g_hash_table_new (g_str_hash, g_str_equal);

  path = g_build_filename (PREFIX, "share", "autostart", NULL);
  search_desktop_entries_in_dir (clients, path);
  g_free (path);

  /* read directories */
  path = g_build_filename (g_get_home_dir (), ".config", "autostart", NULL);
  search_desktop_entries_in_dir (clients, path);
  g_free (path);

  /* convert the hash table into a GSList */
  g_hash_table_foreach_remove (clients, (GHRFunc) hash_table_remove_cb, &result);
  g_hash_table_destroy (clients);

  return result;
}

/* Write out a client list for under a given name */
void
startup_list_write (GSList *sl, const gchar *name)
{
  GSList *tmp_list;
  char *dir;
  
  /* create autostart directory if not existing */
  dir = g_build_filename (g_get_home_dir (), ".config/autostart", NULL);
  g_mkdir_with_parents (dir, S_IRWXU);

  /* write list of autostart clients */
  tmp_list = sl;
  while (tmp_list)
    {
      GnomeDesktopItem *ditem;
      char *cmd_line;
      ManualClient *client;

      client = tmp_list->data;

      if (client->to_remove)
        {
	  g_remove (client->desktop_file);
	}
      else
        {
          ditem = gnome_desktop_item_new ();

	  cmd_line = g_strjoinv (" ", client->argv);
	  gnome_desktop_item_set_string (ditem, GNOME_DESKTOP_ITEM_EXEC, cmd_line);
	  g_free (cmd_line);

	  if (client->enabled)
	    gnome_desktop_item_set_boolean (ditem, "X-GNOME-Autostart-enabled", TRUE);
	  else
	    gnome_desktop_item_set_boolean (ditem, "X-GNOME-Autostart-enabled", FALSE);

	  if (!gnome_desktop_item_save (ditem, client->desktop_file, TRUE, NULL))
	    g_warning ("Could not save %s file\n", client->desktop_file);

	  gnome_desktop_item_unref (ditem);

	  tmp_list = tmp_list->next;
	}
    }
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

      new_client->desktop_file = g_strdup (client->desktop_file);
      new_client->to_remove = client->to_remove;
      new_client->enabled = client->enabled;
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
startup_list_update_gui (GSList *sl, GtkTreeModel *model, GtkTreeSelection *sel)
{
  GSList *tmp_list;
  GtkTreeIter iter;

  gtk_list_store_clear (GTK_LIST_STORE (model));

  tmp_list = sl;
  while (tmp_list)
    {
      ManualClient *client = tmp_list->data;
      char         *name;

      if (!client->to_remove)
        {
          name = g_strjoinv (" ", client->argv);

	  gtk_list_store_append (GTK_LIST_STORE (model), &iter);
	  gtk_list_store_set (GTK_LIST_STORE (model), &iter, 0, client, 1, "", 2, name, -1);

	  g_free (name);
	}

      tmp_list = tmp_list->next;
    }
}

/* Util function - check if a string is blank */
static gboolean
is_blank (const gchar *str)
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
edit_client (gchar *title, ManualClient *client, GtkWidget **dialog, GtkWidget *parent_dlg)
{
  GtkWidget *entry;
  GtkWidget *label;
  GtkWidget *a;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *gnome_entry;

  gchar *tmp;

  *dialog = gtk_dialog_new_with_buttons (title,
					 GTK_WINDOW (parent_dlg),
					 GTK_DIALOG_MODAL,
					 GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					 GTK_STOCK_OK, GTK_RESPONSE_OK,
					 NULL);

  gtk_window_set_default_size (GTK_WINDOW (*dialog), 400, -1);
 
  gtk_window_set_transient_for (GTK_WINDOW (*dialog), GTK_WINDOW (parent_dlg));
  vbox = gtk_vbox_new (FALSE, GNOME_PAD_SMALL);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), GNOME_PAD_SMALL);
  
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (*dialog)->vbox), vbox,
		      TRUE, TRUE, 0);

  hbox = gtk_hbox_new (FALSE, GNOME_PAD_SMALL);
  a = gtk_alignment_new (0.0, 0.5, 0.0, 0.0);
  gtk_box_pack_start (GTK_BOX (hbox), a, FALSE, FALSE, 0);
  label = gtk_label_new_with_mnemonic (_("_Startup Command:"));
  gtk_container_add (GTK_CONTAINER (a), label);

  gtk_container_set_border_width (GTK_CONTAINER (hbox), GNOME_PAD_SMALL);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 0);

  gnome_entry = gnome_file_entry_new ("startup-commands", _("Startup Command"));
  g_object_set (G_OBJECT (gnome_entry), "use_filechooser", TRUE, NULL);
  gnome_file_entry_set_modal (GNOME_FILE_ENTRY (gnome_entry), TRUE);
  entry = gnome_file_entry_gtk_entry (GNOME_FILE_ENTRY (gnome_entry));
  gtk_box_pack_start (GTK_BOX (hbox), gnome_entry, TRUE, TRUE, 0);

  gtk_label_set_mnemonic_widget (GTK_LABEL (label), GTK_WIDGET (entry));

  tmp = gnome_config_assemble_vector (client->argc,
				      (const char * const *)client->argv);
  gtk_entry_set_text (GTK_ENTRY (entry), tmp);
  g_free (tmp);
  
  gtk_widget_show_all (vbox);

  while (gtk_dialog_run (GTK_DIALOG (*dialog)) == GTK_RESPONSE_OK)
    {
      const gchar *tmp = gtk_entry_get_text (GTK_ENTRY (entry));

      if (is_blank (tmp))
	{
	  GtkWidget *msgbox;
	  
	  gtk_widget_show (*dialog);
	  
	  msgbox = gtk_message_dialog_new (GTK_WINDOW (parent_dlg),
					   GTK_DIALOG_MODAL,
					   GTK_MESSAGE_ERROR,
					   GTK_BUTTONS_OK,
					   _("The startup command cannot be empty"));
	  gtk_dialog_run (GTK_DIALOG (msgbox));
	  gtk_widget_destroy (msgbox);
	}
      else
	{
	  int i;

	  for (i = 0; i < client->argc; i++)
	    g_free (client->argv[i]);
	  g_free (client->argv);
	  
	  mark_dirty ();
	  g_shell_parse_argv (tmp, &client->argc, &client->argv, NULL);

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
startup_list_add_dialog (GSList **sl, GtkWidget **dialog, GtkWidget *parent_dlg)
{
  ManualClient *client = g_new0 (ManualClient, 1);
  client->to_remove = FALSE;
  client->enabled = TRUE;
  client->argc = 0;
  client->argv = NULL;

  if (edit_client (_("Add Startup Program"), client, dialog, parent_dlg))
    {
      char *basename, *orig_filename, *filename;
      int i = 2;

      basename = g_path_get_basename (client->argv[0]);
      orig_filename = g_strdup_printf ("%s/.config/autostart/%s.desktop", g_get_home_dir (), basename);
      filename = g_strdup (orig_filename);
      while (g_file_exists (filename))
        {
	  char *tmp = g_strdup_printf ("%s-%d", orig_filename, i);

	  g_free (filename);
	  filename = tmp;
	  i++;
	}

      g_free (orig_filename);
      client->desktop_file = filename;

      *sl = g_slist_prepend (*sl, client);
      spc_write_state ();
    }
  else
    client_free (client);
}

/* Prompt the user with a dialog for editing the currently selected client */
void
startup_list_edit_dialog (GSList **sl, GtkTreeModel *model, GtkTreeSelection *sel, GtkWidget **dialog, GtkWidget *parent_dlg)
{
  ManualClient *client;
  GtkTreeIter iter;

  if (!gtk_tree_selection_get_selected (sel, NULL, &iter)) return;

  gtk_tree_model_get (model, &iter, 0, &client, -1);

  if (edit_client (_("Edit Startup Program"), client, dialog, parent_dlg))
    spc_write_state ();
}

/* Delete the currently selected client */
void
startup_list_delete (GSList **sl, GtkTreeModel *model, GtkTreeSelection *sel)
{
  ManualClient *client;
  GtkTreeIter iter;

  if (!gtk_tree_selection_get_selected (sel, NULL, &iter)) return;

  gtk_tree_model_get (model, &iter, 0, &client, -1);
  gtk_list_store_remove (GTK_LIST_STORE (model), &iter);

  client->to_remove = TRUE;
  spc_write_state ();
}

/* Check if the selected client can be enabled */
gboolean
startup_list_can_enable (GSList **sl, GtkTreeModel *model, GtkTreeSelection *sel)
{
  ManualClient *client;
  GtkTreeIter iter;

  if (!gtk_tree_selection_get_selected (sel, NULL, &iter)) return FALSE;

  gtk_tree_model_get (model, &iter, 0, &client, -1);
  return !client->enabled;
}

void
startup_list_enable (GSList **sl, GtkTreeModel *model, GtkTreeSelection *sel)
{
  ManualClient *client;
  GtkTreeIter iter;

  if (!gtk_tree_selection_get_selected (sel, NULL, &iter)) return;

  gtk_tree_model_get (model, &iter, 0, &client, -1);
  if (!client->enabled)
    {
     char *path, *basename;

     basename = g_path_get_basename (client->desktop_file);

      /* if the desktop file is in the user's home and there is one file with the same
	 name in the system-wide dir, just remove it */
     path = g_build_filename (PREFIX, "share", "autostart", basename, NULL);
     if (g_str_has_prefix (client->desktop_file, g_get_home_dir ())
	 && g_file_test (path, G_FILE_TEST_EXISTS))
       client->to_remove = TRUE;

     client->enabled = TRUE;

     spc_write_state ();

     g_free (basename);
     g_free (path);
    }
}

void
startup_list_disable (GSList **sl, GtkTreeModel *model, GtkTreeSelection *sel)
{
  ManualClient *client;
  GtkTreeIter iter;

  if (!gtk_tree_selection_get_selected (sel, NULL, &iter)) return;

  gtk_tree_model_get (model, &iter, 0, &client, -1);
  if (client->enabled)
    {
      char *path, *basename;

      basename = g_path_get_basename (client->desktop_file);

      /* if the desktop file is in the system-wide dir, create another one on
       the user's home */
      if (g_str_has_prefix (client->desktop_file, PREFIX))
        {
          path = g_build_filename (g_get_home_dir (), ".config", "autostart", basename, NULL);
	  g_free (client->desktop_file);
	  client->desktop_file = path;
	}

      client->enabled = FALSE;
      spc_write_state ();

      g_free (basename);
    }
}
