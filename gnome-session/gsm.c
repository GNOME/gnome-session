#include "gsm.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>

#define SESSIONFILE_PREFIX ".XSM-"

/* TODO:
   - See if gsm_generate_session_list() should use
   the various gnome functions for getting directories.
   - Do the actual session stuff. (Right now this is just a skeleton)
   - Make the list_sessions have a vertical scrollbar.
*/

GtkWidget *mainwin, *list_sessions;
GList *avail_sessions;

/* GUI stuff */
GtkWidget *gsm_window_new(void);
void gsm_window_close_button(GtkWidget *w, gpointer calldata);
void gsm_window_load_button(GtkWidget *w, gpointer calldata);
void gsm_generate_session_list(GtkWidget *session_list);

int
main(int argc, char *argv[])
{
  int i;
  gchar *sessionname = NULL;

  /* We have to do this before gtk_init in order to get -display,
     which we will be passing on to programs that we run. */
  for(i = 1; i < argc; i++) {
    if(argv[i][0] == '-')
      {
	if(i <= argc - 1)
	  {
	    if(!strcmp(argv[i], "-display"))
	      {
		setenv("DISPLAY", argv[++i], TRUE);
	  }
	    else if(!strcmp(argv[i], "-session"))
	      {
		sessionname = g_strdup(argv[++i]);
	      }
	  }
	else
	  {
	    fprintf(stderr, "Missing parameter to %s\n", argv[i]);
	  }
      }
  }

  gtk_init(&argc, &argv);
  gsm_session_init();
  
  if(sessionname != NULL)
    gsm_session_run(sessionname);

  mainwin = gsm_window_new();
  gtk_widget_show(mainwin);
  gtk_main();

  return 0;
}

#define WS(w) gtk_widget_show(GTK_WIDGET(w))
#define P(b, w) wtmp = (w); WS(wtmp); \
gtk_box_pack_start_defaults(GTK_BOX(b), wtmp);

GtkWidget*
gsm_window_new(void)
{
  GtkWidget *retval, *vbox, *hbox, *btnbar, *wtmp, *sw,
    *btn_load, *btn_close;

  retval = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  vbox = gtk_vbox_new(FALSE, 5);
  WS(vbox);
  gtk_container_add(GTK_CONTAINER(retval), vbox);

  hbox = gtk_hbox_new(FALSE, 5);
  P(vbox, hbox);
  P(hbox, gtk_label_new("Sessions"));
  P(hbox, sw = gtk_scrolled_window_new(
				       GTK_ADJUSTMENT(gtk_adjustment_new(0, 0, 101, 0.1, 1, 1)),
				       GTK_ADJUSTMENT(gtk_adjustment_new(0, 0, 101, 0.1, 0.1, 1))
				       ));
  list_sessions = gtk_list_new();
  gtk_widget_show(list_sessions);
  gtk_container_add(GTK_CONTAINER(sw), list_sessions);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				 GTK_POLICY_AUTOMATIC,
				 GTK_POLICY_ALWAYS);
  P(vbox, btnbar = gtk_hbutton_box_new());
  P(btnbar, btn_load = gtk_button_new_with_label("Load Session"));
  P(btnbar, btn_close = gtk_button_new_with_label("Close"));

  gtk_signal_connect(GTK_OBJECT(btn_close), "clicked",
		     (GtkSignalFunc)gsm_window_close_button, NULL);

  gtk_signal_connect(GTK_OBJECT(btn_load), "clicked",
		     (GtkSignalFunc)gsm_window_load_button, NULL);

  gsm_generate_session_list(list_sessions);

  return retval;
}

void gsm_window_close_button(GtkWidget *w, gpointer calldata)
{
  gtk_main_quit();
}

void gsm_window_load_button(GtkWidget *w, gpointer calldata)
{
  gchar *newsession;

  newsession = gtk_object_get_user_data(GTK_OBJECT(w));

  gsm_session_end_current();
  gsm_session_run(newsession);
}

gchar*
get_sm_save_dir(void)
{
  gchar *retval;

  retval = getenv("SM_SAVE_DIR");
  if(retval == NULL)
    retval = getenv("HOME");
  if(retval == NULL)
    retval = ".";
#ifdef DEBUG
  g_print("sm save dir is %s\n", retval);
#endif
  return retval;
}

void
gsm_generate_session_list(GtkWidget *session_list)
{
  gchar asession_label[64], asession_fn[1024];
  DIR *smdir;
  struct dirent *entry;
  GtkWidget *wtmp;
  GList *lis = NULL;
  gchar *dn = get_sm_save_dir();
  gint nents = 0;

  g_return_if_fail(session_list != NULL);
  g_return_if_fail(GTK_IS_LIST(session_list));

  gtk_list_clear_items(GTK_LIST(session_list), 0, -1);

  smdir = opendir(dn);
  g_return_if_fail(smdir != NULL);

  while((entry = readdir(smdir)) != NULL) {
    if(!strncmp(entry->d_name, SESSIONFILE_PREFIX,
	       strlen(SESSIONFILE_PREFIX))) {
      snprintf(asession_label, sizeof(asession_label),
	       "%s", entry->d_name + strlen(SESSIONFILE_PREFIX));
      snprintf(asession_fn, sizeof(asession_fn),
	       "%s/%s", dn, entry->d_name);
      wtmp = gtk_list_item_new_with_label(asession_label);
      gtk_widget_show(wtmp);
      gtk_object_set_user_data(GTK_OBJECT(wtmp), strdup(asession_fn));
      lis = g_list_append(lis, wtmp);
      nents++;
    }
  }
  if(lis != NULL)
    gtk_list_append_items(GTK_LIST(session_list), lis);
}
