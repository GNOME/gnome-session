#include "gsm.h"

#include <unistd.h>
#include <SM/SMlib.h>
#include <ICE/ICElib.h>

static void
gsm_session_new_client_proc(SmsConn smsConn,
			    SmPointer managerData,
			    unsigned long *maskRet,
			    SmsCallbacks *callbacksRet,
			    char **failureReasonRet);
static gboolean
check_if_is_wm(const gchar *progname);

gchar *smsavedir = NULL;
gchar *current_session = NULL;
static gchar errorStringRet[128];
static gchar *sm_id = NULL;

gint numTransports;

IceListenObj *listenObjs;
#ifdef ALL_TODOS_ARE_FINISHED
IceAuthDataEntry *authDataEntries = NULL;
#endif

#ifndef WE_HAVE_EXPORTED_THE_STATIC_STUFF_FROM_LIBGNOMEUI
static void
process_ice_messages (gpointer client_data, gint source,
                      GdkInputCondition condition)
{
  IceProcessMessagesStatus status;
  IceConn connection = (IceConn) client_data;

  status = IceProcessMessages (connection, NULL, NULL);
  /* FIXME: handle case when status==closed.  */
}

static void
new_ice_connection (IceConn connection,
		    IcePointer client_data,
		    Bool opening,
                    IcePointer *watch_data)
{
  if (opening)
    ice_tag = gdk_input_add (IceConnectionNumber (connection),
                             GDK_INPUT_READ, process_ice_messages,
                             (gpointer) connection);
  else
    gdk_input_remove (ice_tag);
}
#endif /* WE_HAVE_EXPORTED_THE_STATIC_STUFF_FROM_LIBGNOMEUI */

void gsm_session_init(GtkWidget *mainwin)
{
  errorStringRet[127] = '\0';
  if(SmsInitialize("gsm",
		   "1.0",
		   gsm_session_new_client_proc,
		   NULL,
		   gtk_false,
		   sizeof(errorStringRet) - 1,
		   errorStringRet) != True)
    g_error("SmsInitialize failed: %s\n", errorStringRet);

  IceAddConnectionWatch(new_ice_connection, NULL);
  if(IceListenForConnections(&numTransports, &listenObjs,
			     sizeof(errorStringRet) - 1,
			     errorStringRet)) {
    g_error("IceListenForConnections failed: %s\n", errorStringRet);
  }

#ifdef ALL_TODOS_ARE_FINISHED
  /* What is this supposed to do in xsm? */
  SetAuthentication (numTransports, listenObjs, &authDataEntries);
#endif
  sm_id = SmsGenerateClientID(NULL);

  gtk_widget_realize(widget);
  gdk_property_change(gdk_window_get_toplevel(widget->window),
		      gdk_atom_intern("SM_CLIENT_ID", FALSE),
		      gdk_atom_intern("STRING", FALSE),
		      8, GDK_PROP_MODE_REPLACE,
		      sm_id, strlen(sm_id));
}

void gsm_session_run(gchar *session_name)
{
  g_free(current_session);
  current_session = g_strdup(session_name);

  g_error("Not implemented yet\n");
}

void gsm_session_save_current(void)
{
  g_return_if_fail(current_session != NULL);
  
  g_error("Not implemented yet\n");
}

static void
gsm_session_new_client_proc(SmsConn smsConn,
			    SmPointer managerData,
			    unsigned long *maskRet,
			    SmsCallbacks *callbacksRet,
			    char **failureReasonRet)
{
  g_error("Not implemented yet\n");
}

/* There's no standard way to distinguish between regular and
   manager clients, so we just guess */
static gboolean
check_if_is_wm(const gchar *progname)
{
  const char *non_reg_wms[] = {
    "enlightenment",
    "afterstep",
    "windowmaker",
    NULL
  };
  char *realpn;

  realpn = strrchr(progname, "/");
  if(realpn == NULL)
    realpn = progname;
  else
    realpn++;

  if(strstr(realpn, "wm") != NULL)
    return TRUE;

  for(i = 0; non_reg_wms[i]; i++)
    if(!strcmp(realpn, non_reg_wms[i]))
      return TRUE;

  return FALSE;
}
