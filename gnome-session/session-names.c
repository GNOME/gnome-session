/* Copyright 2001 Sun Microsystems Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA.

   Authors: Glynn Foster <glynn.foster@sun.com> */

#include <config.h>
#include <ctype.h>
#include <stdlib.h>
#include <gnome.h>
#include "capplet-widget.h"
#include "session-properties-capplet.h"
#include "headers.h"
#include "gsm-protocol.h"

extern GSList *deleted_sessions;

typedef struct _ClientObject ClientObject;
typedef struct _SessionObject SessionObject;

struct _ClientObject {
  gchar *id;
  gint style;
  gint priority;
  gchar *program;
  gchar *dir;
  gchar *clone;
  gchar *discard;
  gchar *env;
  int argc;
  char **argv;
};

struct _SessionObject {
  gchar *name;
  GSList *clients;
};

static gboolean checkduplication (gchar *sessionname, GSList **sess_list);
static gboolean is_blank (gchar *str);
static gboolean edit_session_name (gchar *title, gchar **editsession, 
			GSList **sess_list, GtkWidget **dialog);
static gint session_compare (gconstpointer a, gconstpointer b);
static gint sessionfile_compare (gconstpointer a, gconstpointer b);
static void delete_sessions (GSList *sess_list); 
static void change_session_section_name (const gchar *sess_name, 
			const gchar *sess_name_old); 
static void add_session_section_name (const gchar *sess_name);
static void read_session_clients (SessionObject **session_object);
static void write_session_clients (SessionObject *session_object);

void
session_list_update_gui (GSList *sess_list, GtkWidget *clist, gchar *curr_sess) 
{
  GSList *temp_list = NULL; 
  gtk_clist_clear (GTK_CLIST (clist));
  for(temp_list = sess_list; temp_list; temp_list = temp_list->next) {
    gint row;
    gchar *name = (gchar*)temp_list->data;
    row = gtk_clist_append (GTK_CLIST (clist), &name);
    if(!strcmp (curr_sess, name)) {
      gtk_clist_select_row (GTK_CLIST (clist), row, 0);
    }
  }
}

static gboolean
is_blank (gchar *str) 
{
  while(*str) {
    if( !isspace(*str))
      return FALSE;
    str++;
  }
  return TRUE;
}

static gboolean
edit_session_name (gchar *title, gchar **editsession, 
			GSList **sess_list, GtkWidget **dialog)
{
  GtkWidget *vbox;
  GtkWidget *entry;
  GtkWidget *frame;
  *dialog = gnome_dialog_new (title, GNOME_STOCK_BUTTON_OK,
				GNOME_STOCK_BUTTON_CANCEL,
				NULL);
  gnome_dialog_close_hides (GNOME_DIALOG (*dialog), TRUE);
  gtk_window_set_policy (GTK_WINDOW (*dialog), FALSE, TRUE, FALSE);
  gtk_window_set_default_size (GTK_WINDOW (*dialog), 400, -1);

  vbox = gtk_vbox_new (FALSE, GNOME_PAD_SMALL);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), GNOME_PAD_SMALL);

  gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (*dialog)->vbox), vbox,
			TRUE, TRUE, 0);
  frame = gtk_frame_new (title);
  gtk_container_add (GTK_CONTAINER(vbox),frame);
  entry = gtk_entry_new();
  gtk_container_add (GTK_CONTAINER(frame),entry);

  if(*editsession) {
    gtk_entry_set_text(GTK_ENTRY(entry),*editsession);
  }

  gtk_widget_show_all (*dialog); 

  while (gnome_dialog_run (GNOME_DIALOG (*dialog)) == 0) {  
    *editsession = gtk_editable_get_chars (GTK_EDITABLE (entry),0,-1);
    if (is_blank (*editsession)) {
      GtkWidget *msgbox;
      gtk_widget_show (*dialog);
      msgbox = gnome_message_box_new (_("The session name cannot be empty"),
 			GNOME_MESSAGE_BOX_ERROR,
			GNOME_STOCK_BUTTON_OK,
			NULL);
      gnome_dialog_set_parent (GNOME_DIALOG (msgbox), GTK_WINDOW (*dialog));
      gnome_dialog_run (GNOME_DIALOG (msgbox));
    } 
    else if(checkduplication(*editsession, sess_list)) {
      GtkWidget *msgbox;
      gtk_widget_show (*dialog);
      msgbox = gnome_message_box_new (_("The session name already exists"),
 			GNOME_MESSAGE_BOX_ERROR,
			GNOME_STOCK_BUTTON_OK,
			NULL);
      gnome_dialog_set_parent (GNOME_DIALOG (msgbox), GTK_WINDOW (*dialog));
      gnome_dialog_run (GNOME_DIALOG (msgbox));
    }
    else {
      gtk_widget_destroy (*dialog);
      *dialog = NULL;
      return TRUE;
    }
  }
  
  gtk_widget_destroy (*dialog);
  *dialog = NULL;
  return FALSE;
}

static gboolean
checkduplication (gchar *sessionname, GSList **sess_list) 
{
  GSList *temp = NULL;
  for(temp = *sess_list; temp; temp = temp->next) 
    if(!strcmp((gchar *)temp->data, sessionname)) 
      return TRUE;
  return FALSE;
}
void 
session_list_add_dialog (GSList **sess_list, GtkWidget *clist,
						GtkWidget **dialog)
{
  gchar *new_session_name = NULL;
    
  if(edit_session_name (_("Add a new session"), &new_session_name, sess_list, dialog)) {
    *sess_list = g_slist_prepend (*sess_list,g_strdup(new_session_name));
    *sess_list = g_slist_sort (*sess_list, (GCompareFunc)session_compare);
  }
  g_free(new_session_name);
}

void 
session_list_delete (GSList **sess_list, GtkWidget *clist, gint row, 
			GtkWidget **dialog)
{	
  GSList *temp = NULL;
  gchar *old_session_name = NULL; 
  gtk_clist_get_text (GTK_CLIST(clist), row, 0, &old_session_name);
  for(temp = *sess_list; temp; temp=temp->next) {
    if(!strcmp((gchar *)temp->data, old_session_name)) {
      *sess_list = g_slist_remove(*sess_list,(gpointer)temp->data);
      break;
    }
  }
  *sess_list = g_slist_sort (*sess_list, (GCompareFunc)session_compare);
}

void
session_list_edit_dialog (GSList **sess_list, GtkWidget *clist, 
			  GHashTable **hash, gint row, GtkWidget **dialog)
{
  GSList *temp = NULL;
  gchar *edited_session_name = NULL;
  gchar *old_session_name = NULL;
  gtk_clist_get_text (GTK_CLIST(clist), row, 0, &old_session_name);
  edited_session_name = g_strdup(old_session_name);
  if(edit_session_name (_("Edit session name"), &edited_session_name, 
                          sess_list, dialog)) {
    for(temp = *sess_list; temp; temp=temp->next) {
      if(!strcmp((gchar *)temp->data, old_session_name)) {
        temp->data = g_strdup(edited_session_name);
      }
    } 
    g_free(edited_session_name); 
    *sess_list = g_slist_sort (*sess_list, (GCompareFunc)session_compare);
    g_hash_table_insert(*hash, g_strdup(edited_session_name), g_strdup(old_session_name));
    g_hash_table_insert(*hash, g_strdup(old_session_name), g_strdup(edited_session_name)); 
  }
}

GSList *
session_list_duplicate (GSList *list)
{
  GSList *temp;
  GSList *result = NULL;
  temp = list;
  while(temp) {
    gchar *name = g_strdup(temp->data);
    result = g_slist_prepend(result, name);
    temp = temp->next;
  }
  result =  g_slist_sort(result,session_compare);
  return result;
}

static gint 
session_compare (gconstpointer a, gconstpointer b)
{
  const gchar *session_a = a;
  const gchar *session_b = b;
  return toupper(session_a[0]) - toupper(session_b[0]);
}

static gint 
sessionfile_compare (gconstpointer a, gconstpointer b)
{
  const ClientObject *client_a = a;
  const ClientObject *client_b = b;
  return client_a->priority - client_b->priority;
}

void
session_list_write (GSList *sess_list, GSList *sess_list_rev, GHashTable *hash)
{
  GSList *tmp_sess;
  GSList *tmp_sess_revert;

  gchar *sess_name = NULL;
  gchar *sess_name_old = NULL;
  gboolean existed_before = FALSE;
  
  for(tmp_sess=sess_list; tmp_sess; tmp_sess=tmp_sess->next) {
    existed_before = FALSE;
    sess_name = (gchar *)tmp_sess->data;
   
    for(tmp_sess_revert=sess_list_rev; tmp_sess_revert; tmp_sess_revert=tmp_sess_revert->next) {
      /* If the session had existed before we set the flag existed_before */ 
      if(!strcmp((gchar *)tmp_sess_revert->data,sess_name)) 
         existed_before = TRUE;    
    }
      
    /* We either have a new added session or a session that has been edited */ 
    if(!existed_before) {
      sess_name_old = g_strdup(g_hash_table_lookup (hash, sess_name));
      /* The session's name has been edited and we need to update the session file */ 
      if(sess_name_old) {
        /* So we copy then delete section and paste with new section header */       
        change_session_section_name (sess_name, sess_name_old); 
	sess_name_old = NULL;
      }
      else {  
        /* We may possibly need to restore a session */
        if(deleted_sessions) {
          GSList *temp_del;
          gchar *path;
          for(temp_del = deleted_sessions; temp_del; temp_del = temp_del->next) {
            SessionObject *session_object = (SessionObject *)temp_del->data;
            if(!strcmp((gchar *)tmp_sess->data, (gchar *)session_object->name)) {
              /* The session has previously been deleted so recreate */
              path = g_strconcat (CONFIG_PREFIX, (gchar *)session_object->name, "/", NULL);
              gnome_config_push_prefix (path);
              write_session_clients (session_object);
              gnome_config_pop_prefix ();
            }
          }
        deleted_sessions = NULL; 
        } 
        else { 
          /* We have a new session name */
          add_session_section_name (sess_name);
        }
      } 
    } 
  } 
  /* Now we just have to delete the sessions that aren't listed */ 
  gnome_config_sync();
  delete_sessions (sess_list);
}

static void
delete_sessions (GSList *sess_list) 
{
  GSList *temp = NULL;
  gpointer iterator = NULL;
  gchar *name = NULL;
  iterator = gnome_config_init_iterator_sections (CONFIG_PREFIX);
  while ((iterator = gnome_config_iterator_next (iterator, &name, NULL))) {
    gboolean section_found = FALSE;
   
    for(temp=sess_list; temp; temp=temp->next) {
	if(!strcmp((gchar *)temp->data,name)) 
          section_found = TRUE;
    }
    if(!section_found) {
      SessionObject *session_object;
      session_object = g_new0 (SessionObject, 1);
      session_object->name = g_strdup(name);
      /* We need to copy the session first */
      read_session_clients (&session_object);
      deleted_sessions = g_slist_prepend(deleted_sessions, session_object);
      gnome_config_push_prefix(CONFIG_PREFIX);
      gnome_config_clean_section(g_strdup(name));
      gnome_config_pop_prefix ();
    }
  }
  gnome_config_sync ();
}

static void
change_session_section_name (const gchar *sess_name, const gchar *sess_name_old) 
{
  gchar *path = NULL;
  SessionObject *copied_session;
  copied_session = g_new0 (SessionObject, 1);
  copied_session->name = g_strdup(sess_name_old);
  copied_session->clients = NULL;
  /* Make a copy of the session */
  read_session_clients (&copied_session); 
  gnome_config_push_prefix (CONFIG_PREFIX);
  /* Clean the old session */
  gnome_config_clean_section (sess_name_old);
  gnome_config_pop_prefix ();
  path = g_strconcat (CONFIG_PREFIX, sess_name, "/", NULL);
  gnome_config_push_prefix (path);
  /* Write the new session */
  write_session_clients (copied_session);
  gnome_config_pop_prefix ();
  g_free(copied_session);
}

static void
add_session_section_name (const gchar *sess_name) 
{
  gchar *path;
  path = g_strconcat (CONFIG_PREFIX, sess_name, "/", NULL);
  gnome_config_push_prefix (path);
  gnome_config_set_int ("num_clients", 0);
  gnome_config_pop_prefix ();
}

static void 
read_session_clients (SessionObject **session_object) 
{
  gpointer iterator;
  ClientObject *current = NULL;
  gchar *handle = NULL;
  gchar *p;
  gnome_config_push_prefix (CONFIG_PREFIX); 
  iterator = gnome_config_init_iterator (g_strdup((*session_object)->name));
  while (iterator) {
    gchar *key;
    gchar *value;
    iterator = gnome_config_iterator_next (iterator, &key, &value);
    
    if(!iterator)
      break;
    
    p = strchr (key, ',');
    if(p) {
      *p = '\0';
      if(!current || strcmp (handle, key) !=0) {
        if(handle) 
          g_free (handle);
        current = g_new0 (ClientObject, 1);
        handle = g_strdup(key);
       
        (*session_object)->clients = APPEND((*session_object)->clients, current);
      }
      if(strcmp(p+1, "id") == 0)
        current->id = g_strdup(value); 
      else if(strcmp(p+1, "RestartStyleHint") == 0)
        current->style = atoi(value);
      else if(strcmp(p+1, "Priority") == 0)
        current->priority = atoi(value);
      else if(strcmp(p+1, "Program") == 0)
        current->program = g_strdup(value);
      else if(strcmp(p+1, "CurrentDirectory") == 0)
        current->dir = g_strdup(value);
      else if(strcmp(p+1, "CloneCommand") == 0)
        current->clone = g_strdup(value);
      else if(strcmp(p+1, "Environment") == 0)
        current->env = g_strdup(value);
      else if(strcmp(p+1, "DiscardCommand") == 0)
        current->discard = g_strdup(value);
      else if(strcmp(p+1, "RestartCommand") == 0)
        gnome_config_make_vector (value, &current->argc, &current->argv);
    }
  g_free(key);
  g_free(value);
  }
  (*session_object)->clients =  g_slist_sort ((*session_object)->clients, sessionfile_compare);
  gnome_config_pop_prefix();
}

static void 
write_session_clients (SessionObject *session_object) 
{
  GSList *temp = NULL;
  gint i = 0;
  
  for(temp = session_object->clients; temp; temp=temp->next) {
    ClientObject *obj = (ClientObject *)(temp->data);
    gchar *key;

    key = g_strdup_printf("%d,%s", i, "id");
    gnome_config_set_string (key, obj->id);
    g_free(key);

    key = g_strdup_printf("%d,%s", i, "RestartStyleHint");
    gnome_config_set_int (key, obj->style);
    g_free(key);

    key = g_strdup_printf("%d,%s", i, "Priority");
    gnome_config_set_int (key, obj->priority);
    g_free(key);
 
    key = g_strdup_printf("%d,%s", i, "Program");
    gnome_config_set_string (key, obj->program);
    g_free(key);

    key = g_strdup_printf("%d,%s", i, "CurrentDirectory");
    gnome_config_set_string (key, obj->dir);
    g_free(key);

    key = g_strdup_printf("%d,%s", i, "DiscardCommand");
    gnome_config_set_string (key, obj->discard);
    g_free(key);

    key = g_strdup_printf("%d,%s", i, "Environment");
    gnome_config_set_string (key, obj->env);
    g_free(key);
    
    key = g_strdup_printf("%d,%s", i, "CloneCommand");
    gnome_config_set_string (key, obj->clone);
    g_free(key);

    key = g_strdup_printf("%d,%s", i, "RestartCommand");
    gnome_config_set_vector (key, obj->argc, (const char* const *)obj->argv);
    g_free(key);
   
    i++;
    g_free(obj);
  }
  gnome_config_set_int ("num_clients", i);
}

void
session_list_free (GSList *sess_list) 
{
  g_slist_free (sess_list);
}

void
deleted_session_list_free ()
{
  if(deleted_sessions) {
    GSList *temp = NULL;
    for(temp=deleted_sessions; temp; temp=temp->next) {
      g_free(temp->data);
    }
  } 
  deleted_sessions = NULL;
}
