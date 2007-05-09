/* startup-programs.c

   Copyright 1999 Free Software Foundation, Inc.
   Copyright 2007 Vincent Untz

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
#include <string.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>

#include "session-properties-capplet.h"
#include "gsm-autostart.h"
#include "gsm-keyfile.h"
#include "gsm-protocol.h"
#include "headers.h"

/* Structure for storing information about one client */
typedef struct _ManualClient ManualClient;

struct _ManualClient
{
  char     *desktop_file;
  gboolean  enabled;
  char     *name;
  char     *command;
  char     *comment;
};

/* Free client record */
static void
client_free (ManualClient *client)
{
  g_free (client->desktop_file);
  g_free (client->name);
  g_free (client->command);
  g_free (client->comment);
  g_free (client);
}

static gboolean
system_desktop_entry_exists (const char  *basename,
                             char       **system_path)
{
  char *path;
  const char * const * system_dirs;
  int i;

  system_dirs = g_get_system_data_dirs ();
  for (i = 0; system_dirs[i] != NULL; i++)
    {
      path = g_build_filename (system_dirs[i], "gnome",
                               "autostart", basename, NULL);
      if (g_file_test (path, G_FILE_TEST_EXISTS))
        {
          if (system_path)
            *system_path = path;
          else
            g_free (path);

          return TRUE;
        }

      g_free (path);
    }

  /* support old place (/etc/xdg/autostart) */
  system_dirs = g_get_system_config_dirs ();
  for (i = 0; system_dirs[i] != NULL; i++)
    {
      path = g_build_filename (system_dirs[i], "autostart", basename, NULL);
      if (g_file_test (path, G_FILE_TEST_EXISTS))
        {
          if (system_path)
            *system_path = path;
          else
            g_free (path);

          return TRUE;
        }

      g_free (path);
    }

  return FALSE;
}

static ManualClient *
create_client_from_desktop_entry (const char *path,
                                  GKeyFile   *keyfile)
{
  ManualClient *client = NULL;

  client = g_new0 (ManualClient, 1);

  client->desktop_file = g_strdup (path);

  client->name = gsm_key_file_get_locale_string (keyfile, "Name");
  if (client->name == NULL)
    client->name = gsm_key_file_get_locale_string (keyfile, "GenericName");

  client->command = gsm_key_file_get_string (keyfile, "Exec");
  client->comment = gsm_key_file_get_locale_string (keyfile, "Comment");
  client->enabled = gsm_key_file_get_boolean (keyfile,
                                              "X-GNOME-Autostart-enabled",
                                              TRUE);

  return client;
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

/* Read in the client record for a given session */
GSList *
startup_list_read (void)
{
  return gsm_autostart_read_desktop_files ((GsmAutostartCreateFunc) create_client_from_desktop_entry,
                                           (GsmAutostartFreeFunc) client_free,
                                           FALSE);
}

static void
ensure_user_autostart_dir (void)
{
  char *dir;
  
  /* create autostart directory if not existing */
  dir = g_build_filename (g_get_user_config_dir (), "autostart", NULL);
  g_mkdir_with_parents (dir, S_IRWXU);
  g_free (dir);
}

static void
startup_client_delete (ManualClient *client)
{
  char *basename;

  basename = g_path_get_basename (client->desktop_file);

  if (g_str_has_prefix (client->desktop_file,
                        g_get_user_config_dir ())
      && !system_desktop_entry_exists (basename, NULL))
    {
      if (g_file_test (client->desktop_file, G_FILE_TEST_EXISTS))
        g_remove (client->desktop_file);
    }
  else
    {
      /* Two possible cases:
       * a) We want to remove a system wide startup desktop file.
       *    We can't do that, so we will create a user desktop file
       *    with the hidden flag set.
       * b) We want to remove a startup desktop file that is both
       *    system and user. So we have to mark it as hidden.
       */
      char     *path;
      GKeyFile *keyfile;
      GError   *err;

      ensure_user_autostart_dir ();

      path = g_build_filename (g_get_user_config_dir(),
                               "autostart", basename, NULL);

      keyfile = g_key_file_new ();

      err = NULL;
      g_key_file_load_from_file (keyfile, path,
                                 G_KEY_FILE_KEEP_COMMENTS|G_KEY_FILE_KEEP_TRANSLATIONS,
                                 &err);
      if (err)
        {
          g_error_free (err);
          g_key_file_free (keyfile);
          keyfile = gsm_key_file_new_desktop ();
        }

      gsm_key_file_set_boolean (keyfile, "Hidden", TRUE);

      if (!gsm_key_file_to_file (keyfile, path, NULL))
        g_warning ("Could not save %s file\n", path);

      g_key_file_free (keyfile);

      g_free (path);
    }

  g_free (basename);
}

static void
startup_client_write (ManualClient *client)
{
  GKeyFile *keyfile;
  GError   *err;
  char     *name;

  ensure_user_autostart_dir ();

  keyfile = g_key_file_new ();

  err = NULL;
  g_key_file_load_from_file (keyfile, client->desktop_file,
                             G_KEY_FILE_KEEP_COMMENTS|G_KEY_FILE_KEEP_TRANSLATIONS,
                             &err);
  if (err)
    {
      g_error_free (err);
      g_key_file_free (keyfile);
      keyfile = gsm_key_file_new_desktop ();
    }

  if (!g_str_has_prefix (client->desktop_file,
                         g_get_user_config_dir ()))
    {
      /* It's a system-wide file, save it to the user's home */

      char *basename;
      char *path;

      basename = g_path_get_basename (client->desktop_file);
      path = g_build_filename (g_get_user_config_dir (),
                               "autostart", basename, NULL);
      g_free (basename);

      g_free (client->desktop_file);
      client->desktop_file = path;
    }

  gsm_key_file_set_locale_string (keyfile, "Name", client->name);
  name = gsm_key_file_get_string (keyfile, "Name");
  if (name == NULL || name[0] == '\0')
    gsm_key_file_set_string (keyfile, "Name", client->name);
  g_free (name);

  if (client->comment && client->comment[0] != '\0')
    gsm_key_file_set_locale_string (keyfile, "Comment", client->comment);
  else
      gsm_key_file_remove_locale_key (keyfile, "Comment");
      //FIXME should we do this too?: gsm_key_file_remove_key (keyfile, "Comment"):

  gsm_key_file_set_string (keyfile,
                            "Exec", client->command);
  gsm_key_file_set_boolean (keyfile,
                            "X-GNOME-Autostart-enabled", client->enabled);

  if (!gsm_key_file_to_file (keyfile, client->desktop_file, NULL))
    g_warning ("Could not save %s file\n", client->desktop_file);

  g_key_file_free (keyfile);
}

static int
compare_clients (ManualClient *a,
		 ManualClient *b)
{
  const char *acmp;
  const char *bcmp;

  if (a->name && a->name[0] != '\0')
    acmp = a->name;
  else
    acmp = a->command;

  if (b->name && b->name[0] != '\0')
    bcmp = b->name;
  else
    bcmp = b->command;

  return g_ascii_strcasecmp (acmp, bcmp);
}

/* Update the given clist to display the given client list */
void
startup_list_update_gui (GSList **sl, GtkTreeModel *model, GtkTreeSelection *sel)
{
  GSList *tmp_list;
  GtkTreeIter iter;
  GtkTreeIter sel_iter;
  gboolean    selection_found;
  ManualClient *selected_client;

  if (gtk_tree_selection_get_selected (sel, NULL, &iter))
    gtk_tree_model_get (model, &iter, 0, &selected_client, -1);
  else
    selected_client = NULL;

  selection_found = FALSE;

  gtk_list_store_clear (GTK_LIST_STORE (model));

  *sl = g_slist_sort (*sl, (GCompareFunc) compare_clients);

  tmp_list = *sl;
  while (tmp_list)
    {
      char         *text;
      const char   *name;
      const char   *description;
      ManualClient *client = tmp_list->data;

      if (client->name && client->name[0] != '\0')
        name = client->name;
      else
        name = client->command;

      if (client->comment && client->comment[0] != '\0')
        description = client->comment;
      else
        description = _("No description");

      text = g_markup_printf_escaped ("<b>%s</b>\n%s", name, description);

      gtk_list_store_append (GTK_LIST_STORE (model), &iter);
      gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                          0, client,
                          1, client->enabled,
                          2, text,
                          3, TRUE, /* activatable */
                          -1);

      g_free (text);

      if (client == selected_client)
        {
          sel_iter = iter;
          selection_found = TRUE;
        }

      tmp_list = tmp_list->next;
    }

  if (selection_found)
    gtk_tree_selection_select_iter (sel, &sel_iter);
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

static void
entry_activate_callback (GtkEntry *entry, void *data)
{
  gtk_dialog_response (GTK_DIALOG (data), GTK_RESPONSE_OK);
}

static void
command_browse_button_clicked (GtkWidget *dialog)
{
  GtkWidget *chooser;
  int        response;

  chooser = gtk_file_chooser_dialog_new ("", GTK_WINDOW (dialog),
                                         GTK_FILE_CHOOSER_ACTION_OPEN,
                                         GTK_STOCK_CANCEL,
                                         GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_OPEN,
                                         GTK_RESPONSE_ACCEPT,
                                         NULL);
  gtk_window_set_transient_for (GTK_WINDOW (chooser),
                                GTK_WINDOW (dialog));
  gtk_window_set_destroy_with_parent (GTK_WINDOW (chooser), TRUE);
  gtk_widget_show (chooser);

  response = gtk_dialog_run (GTK_DIALOG (chooser));

  if (response == GTK_RESPONSE_ACCEPT)
    {
      GtkWidget *entry;
      char      *text;
      char      *uri;

      text = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (chooser));
      uri = gsm_key_file_make_exec_uri (text);
      g_free (text);

      entry = g_object_get_data (G_OBJECT (dialog), "CommandEntry");
      g_assert (entry != NULL);
      gtk_entry_set_text (GTK_ENTRY (entry), uri);

      g_free (uri);
    }

  gtk_widget_destroy (chooser);
}

/* Display a dialog for editing a client. The dialog parameter
 * is used to implement hiding the dialog when the user switches
 * away to another page of the control center */
static gboolean
edit_client (gchar *title, ManualClient *client, GtkWidget *parent_dlg)
{
  GtkWidget *dialog;
  GtkWidget *name_entry;
  GtkWidget *cmd_hbox;
  GtkWidget *cmd_entry;
  GtkWidget *cmd_button;
  GtkWidget *comment_entry;
  GtkWidget *label;
  GtkWidget *table;
  char      *text;

  dialog = gtk_dialog_new_with_buttons (title,
					GTK_WINDOW (parent_dlg),
					GTK_DIALOG_MODAL,
					GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					GTK_STOCK_OK, GTK_RESPONSE_OK,
					NULL);
  gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);

  gtk_window_set_default_size (GTK_WINDOW (dialog), 400, -1);
 
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (parent_dlg));

  table = gtk_table_new (3, 3, FALSE);
  
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), table,
		      TRUE, TRUE, 0);

  label = gtk_label_new ("");
  text = g_strdup_printf ("<b>%s</b>", _("_Name:"));
  gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), text);
  g_free (text);
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1, GTK_FILL, GTK_FILL, 4, 4);

  name_entry = gtk_entry_new ();
  g_signal_connect (name_entry, "activate",
                    G_CALLBACK (entry_activate_callback),
		    (void *) dialog);
  gtk_table_attach (GTK_TABLE (table), name_entry, 1, 2, 0, 1, GTK_EXPAND|GTK_FILL, GTK_FILL, 4, 4);

  gtk_label_set_mnemonic_widget (GTK_LABEL (label), GTK_WIDGET (name_entry));

  if (client->name)
    gtk_entry_set_text (GTK_ENTRY (name_entry), client->name);

  label = gtk_label_new ("");
  text = g_strdup_printf ("<b>%s</b>", _("_Command:"));
  gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), text);
  g_free (text);
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2, GTK_FILL, GTK_FILL, 4, 4);

  cmd_hbox = gtk_hbox_new (FALSE, 12);
  gtk_table_attach (GTK_TABLE (table), cmd_hbox, 1, 2, 1, 2, GTK_EXPAND|GTK_FILL, GTK_FILL, 4, 4);

  cmd_entry = gtk_entry_new ();
  g_signal_connect (cmd_entry, "activate", G_CALLBACK (entry_activate_callback),
                    (void *) dialog);
  gtk_box_pack_start (GTK_BOX (cmd_hbox), cmd_entry, TRUE, TRUE, 0);

  cmd_button = gtk_button_new_with_mnemonic (_("_Browse..."));
  g_signal_connect_swapped (cmd_button, "clicked",
			    G_CALLBACK (command_browse_button_clicked),
			    dialog);
  g_object_set_data (G_OBJECT (dialog), "CommandEntry", cmd_entry);
  gtk_box_pack_start (GTK_BOX (cmd_hbox), cmd_button, FALSE, FALSE, 0);

  gtk_label_set_mnemonic_widget (GTK_LABEL (label), GTK_WIDGET (cmd_entry));

  if (client->command)
    gtk_entry_set_text (GTK_ENTRY (cmd_entry), client->command);

  label = gtk_label_new ("");
  text = g_strdup_printf ("<b>%s</b>", _("Co_mment:"));
  gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), text);
  g_free (text);
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 2, 3, GTK_FILL, GTK_FILL, 4, 4);

  comment_entry = gtk_entry_new ();
  g_signal_connect (comment_entry, "activate",
                    G_CALLBACK (entry_activate_callback),
		    (void *) dialog);
  gtk_table_attach (GTK_TABLE (table), comment_entry, 1, 2, 2, 3, GTK_EXPAND|GTK_FILL, GTK_FILL, 4, 4);

  gtk_label_set_mnemonic_widget (GTK_LABEL (label), GTK_WIDGET (comment_entry));

  if (client->comment)
    gtk_entry_set_text (GTK_ENTRY (comment_entry), client->comment);
  
  gtk_widget_show_all (table);

  while (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK)
    {
      const gchar  *name = gtk_entry_get_text (GTK_ENTRY (name_entry));
      const gchar  *comment = gtk_entry_get_text (GTK_ENTRY (comment_entry));
      const gchar  *tmp = gtk_entry_get_text (GTK_ENTRY (cmd_entry));
      char        **argv;
      int           argc;
      GError       *error = NULL;
      const char   *error_msg = NULL;

      if (is_blank (name))
        error_msg = _("The name of the startup program cannot be empty");
      if (is_blank (tmp))
        error_msg = _("The startup command cannot be empty");
      if (!g_shell_parse_argv (tmp, &argc, &argv, &error))
        {
          if (error)
            error_msg = error->message;
          else
            error_msg = _("The startup command is not valid");
        }

      if (error_msg != NULL)
	{
	  GtkWidget *msgbox;
	  
	  gtk_widget_show (dialog);
	  
	  msgbox = gtk_message_dialog_new (GTK_WINDOW (dialog),
					   GTK_DIALOG_MODAL,
					   GTK_MESSAGE_ERROR,
					   GTK_BUTTONS_OK,
                                           error_msg);

          if (error)
            g_error_free (error);

	  gtk_dialog_run (GTK_DIALOG (msgbox));
	  gtk_widget_destroy (msgbox);
	}
      else
	{
          g_strfreev (argv);

          if (client->name)
            g_free (client->name);
          client->name = g_strdup (name);

          if (client->command)
            g_free (client->command);
          client->command = g_strdup (tmp);

          if (client->comment)
            g_free (client->comment);
          client->comment = comment ? g_strdup (comment) : NULL;

	  gtk_widget_destroy (dialog);
	  return TRUE;
	}
    }

  gtk_widget_destroy (dialog);
  
  return FALSE;
}

/* Prompt the user with a dialog for adding a new client */
void
startup_list_add_dialog (GSList **sl, GtkWidget *parent_dlg)
{
  ManualClient *client = g_new0 (ManualClient, 1);
  client->enabled = TRUE;
  client->name = NULL;
  client->command = NULL;
  client->comment = NULL;

  if (edit_client (_("New Startup Program"), client, parent_dlg))
    {
      char **argv;
      int    argc;
      char  *basename, *orig_filename, *filename;
      int    i = 2;

      g_shell_parse_argv (client->command, &argc, &argv, NULL);
      basename = g_path_get_basename (argv[0]);
      orig_filename = g_build_filename (g_get_user_config_dir (), "autostart", basename, NULL);
      g_strfreev (argv);
      g_free (basename);

      filename = g_strdup_printf ("%s.desktop", orig_filename);
      while (g_file_test (filename, G_FILE_TEST_EXISTS))
        {
	  char *tmp = g_strdup_printf ("%s-%d.desktop", orig_filename, i);

	  g_free (filename);
	  filename = tmp;
	  i++;
	}

      g_free (orig_filename);
      client->desktop_file = filename;

      *sl = g_slist_prepend (*sl, client);
      startup_client_write (client);
    }
  else
    client_free (client);
}

/* Prompt the user with a dialog for editing the currently selected client */
void
startup_list_edit_dialog (GSList **sl, GtkTreeModel *model, GtkTreeSelection *sel, GtkWidget *parent_dlg)
{
  ManualClient *client;
  GtkTreeIter iter;

  if (!gtk_tree_selection_get_selected (sel, NULL, &iter)) return;

  gtk_tree_model_get (model, &iter, 0, &client, -1);

  if (edit_client (_("Edit Startup Program"), client, parent_dlg))
    startup_client_write (client);
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

  startup_client_delete (client);

  *sl = g_slist_remove (*sl, client);
  client_free (client);
}

void
startup_list_enable (GSList **sl, GtkTreeModel *model, GtkTreeIter *iter)
{
  ManualClient *client;
  char *basename;
  char *system_path;

  gtk_tree_model_get (model, iter, 0, &client, -1);
  if (client->enabled)
    return;

  client->enabled = TRUE;

  basename = g_path_get_basename (client->desktop_file);

  if (system_desktop_entry_exists (basename, &system_path))
    {
      ManualClient *system_client;

      system_client = gsm_autostart_read_desktop_file (system_path,
                                                       (GsmAutostartCreateFunc) create_client_from_desktop_entry,
                                                       FALSE);
      g_free (system_path);

      if (system_client
          && !strcmp (system_client->name, client->name)
          && !strcmp (system_client->command, client->command)
          && !strcmp (system_client->comment, client->comment)
          && system_client->enabled)
        {
          /* remove user entry and use system entry */
          if (g_file_test (client->desktop_file, G_FILE_TEST_EXISTS))
            g_remove (client->desktop_file);

	  g_free (client->desktop_file);
	  client->desktop_file = g_strdup (system_client->desktop_file);

          client_free (system_client);
        }
      else
        {
          /* else, the user and the system desktop files are not the same or
           * the system one is disabled, so enable the user's one (ie,
           * nothing special to do :-)) */

          if (system_client)
            client_free (system_client);

          startup_client_write (client);
        }
    }
  else
    {
      startup_client_write (client);
    }

  g_free (basename);
}

void
startup_list_disable (GSList **sl, GtkTreeModel *model, GtkTreeIter *iter)
{
  ManualClient *client;

  gtk_tree_model_get (model, iter, 0, &client, -1);
  if (!client->enabled)
    return;

  client->enabled = FALSE;
  startup_client_write (client);
}
