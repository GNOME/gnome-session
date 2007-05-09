/* ice.c - Handle session manager/ICE integration.

   Copyright (C) 1998, 1999, 2006 Tom Tromey

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
   02111-1307, USA.  */

#include <config.h>

#include <stdlib.h>

#include <X11/ICE/ICElib.h>
#include <X11/ICE/ICEutil.h>

#include <glib/gi18n.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>

#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#endif
#ifdef HAVE_TCPD_H
#include <tcpd.h>
#endif

#include <libgnomeui/gnome-ice.h>
#include <gconf/gconf-client.h>

#include "ice.h"
#include "session.h"
#include "manager.h"
#include "util.h"

/* Network ids.  */
static char* ids;

/* List of all the auth entries. */
GSList *auth_entries;

/* The "sockets" which we listen on */
static IceListenObj *sockets; 
static gint *input_id;
static gint num_sockets;

/* File containing the ICE authorization data (usually ~/.ICEauthority) */
static gchar* authfile;

static IceAuthFileEntry* file_entry_new (const gchar* protocol, 
					 const IceListenObj socket);
static GSList* read_authfile(gchar* filename, gboolean notify_user);
static void fatal (gchar *format, gchar *argument);
static void write_authfile(gchar* filename, GSList* entries,
			   gboolean notify_user);

static GSList* watch_list = NULL;

static gint original_umask;

/* This is called when a client tries to connect. 
 * We still accept connections during a shutdown so we can handle warnings. */

#ifdef HAVE_SYSLOG_H
int allow_severity = LOG_INFO, deny_severity = LOG_NOTICE;
#endif

static void
my_ice_error_handler(IceConn cnx,
	Bool swap,
	int offendingMinorOpcode,
	unsigned long offendingSequence,
	int errorClass,
	int severity,
	IcePointer values)
{
  IceCloseStatus close_status;
  const char *msg;

  gsm_verbose ("my_ice_error_handler (cnx %p, %s, %d, %lx, %d, %d)\n",
	       cnx, swap ? "TRUE" : "FALSE", offendingMinorOpcode, offendingSequence, errorClass, severity);

  close_status = IceCloseConnection(cnx);

  switch (close_status)
    {
    case IceClosedNow:
      msg = "IceClosedNow";
      break;
    case IceClosedASAP:
      msg = "IceClosedASAP";
      break;
    case IceConnectionInUse:
      msg = "IceConnectionInUse";
      break;
    case IceStartedShutdownNegotiation:
      msg = "IceStartedShutdownNegotiation";
      break;
    default:
      msg = "UNKNOWN";
      break;
    }

  gsm_verbose ("IceCloseConnection (cnx %p) returned %s\n", cnx, msg);
}

static void
accept_connection (GIOChannel   *source,
		   GIOCondition  condition,
		   IceListenObj  listener)
{
  IceConn connection;
  IceAcceptStatus status;
  IceConnectStatus status2 = IceConnectPending;
#if defined(HAVE_TCPD_H) && defined(HAVE_HOSTS_ACCESS) && defined(HAVE_SYSLOG_H)
  char *ctmp;
#endif

  gsm_verbose ("accept_connection() for listener %p...\n", listener);

  connection = IceAcceptConnection (listener, &status);
  if (status != IceAcceptSuccess)
    {
      gsm_verbose ("IceAcceptConnection (listener %p) returned %d\n", listener, status);
      return;
    }

#if defined(HAVE_TCPD_H) && defined(HAVE_HOSTS_ACCESS) && defined(HAVE_SYSLOG_H)
  ctmp = IceGetListenConnectionString (listener);
  if(!strncmp(ctmp, "tcp/", 4)) { /* It's a TCP connection */
    struct request_info request;
    
    request_init(&request, RQ_DAEMON, "gnome-session", RQ_FILE, IceConnectionNumber(connection), 0);
    
    fromhost(&request);
    if(!hosts_access(&request)) {
      syslog(deny_severity, "[gnome-session] refused connect from %s", eval_client(&request));
      IceCloseConnection(connection);
      return;
    } else
      syslog(allow_severity, "[gnome-session] connect from %s", eval_client(&request));
  }
  free(ctmp);
#endif

  /* Must wait until connection leaves pending state.  */
  /* FIXME: after a certain amount of time, just reject the
     connection.  */
  while (status2 == IceConnectPending)
    {
      /* Freeze ice while doing the gtk iteration. We don't want to recurse
         here */
      ice_frozen();
      gsm_verbose ("iterate...\n");
      g_main_context_iteration (NULL, TRUE);
      ice_thawed();
      status2 = IceConnectionStatus (connection);
    }
  gsm_verbose ("Done.\n");
}

static guint
ice_add_listener (IceListenObj listener)
{
  GIOChannel *channel;
  guint       retval;

  gsm_verbose ("Adding listener for %p\n", listener);

  channel = g_io_channel_unix_new (IceGetListenConnectionNumber (listener));

  retval = g_io_add_watch (
		channel, G_IO_IN | G_IO_HUP | G_IO_ERR,
		(GIOFunc) accept_connection, listener);

  g_io_channel_unref (channel);

  return retval;
}

static void
ice_watch (IceConn connection, IcePointer client_data, Bool opening,
	   IcePointer *watch_data)
{

  gsm_verbose ("ice_watch():\tconnection: %p\topening: %d\n", connection, opening);

  if (opening)
    {
      Watch *watch = g_new0 (Watch, 1); 
      watch->connection = connection;
      *watch_data = (IcePointer)watch;
      APPEND (watch_list, watch);
    }
  else 
    {
      Watch *watch = (Watch*)*watch_data;
      if (watch->clean_up)
	watch->clean_up (watch->data);
      REMOVE (watch_list, watch);
      g_free (watch);
    }
}

void 
ice_set_clean_up_handler (IceConn connection, 
			  GDestroyNotify clean_up, gpointer data)
{
  GSList* list;

  gsm_verbose ("setting clean up handler for connection %p\n", connection);

  for (list = watch_list; list; list = list->next)
    {
      Watch *watch = (Watch*)list->data;
      if (watch->connection == connection)
	{
	  watch->clean_up = clean_up;
	  watch->data     = data;
	  REMOVE (watch_list, watch);	  
	  break;
	}
    }
}

/* After an unclean shutdown clean_ice () may not have been called the last
 * time so there may be stale records left in the file which we need to remove
 * first.
 */

static GSList *
startup_clean_ice (void)
{
  guint i;
  GSList* entries;

  entries = read_authfile (authfile, TRUE);

  for (i = 0; i < num_sockets; i++)
    {
      char* network_id = IceGetListenConnectionString (sockets[i]);
      GSList* list = entries;
      
      while (list)
	{
	  IceAuthFileEntry *file_entry = (IceAuthFileEntry *)list->data;

	  list = list->next;
	  /* remove "bad" entries with no network_id */
	  if (!file_entry->network_id || !strcmp (file_entry->network_id, network_id))
	    {
	      REMOVE (entries, file_entry);
	      IceFreeAuthFileEntry (file_entry);
	    }
	}

      free (network_id);
    }

  return entries;
}

/*
 * Host Based Authentication Callback.
 * Refuse all connections that fail MIT-MAGIC-COOKIE-1 authentication.
 */
static Bool
auth_proc (char* hostname)
{
    return 0; /* always reject */
}

#if 0
/*
 * This routine tries to launch the ICE listen port without an open socket
 * to the world.  It loops from -1 to -256, as the socket might be in use
 */
static gboolean
init_ice_connections (void)
{
	char error [256];
	char buffer [32];
	int i;

	for (i = 1; i < 256; i++){
		g_snprintf (buffer, sizeof (buffer), "-%d", i); 
		if (IceListenForWellKnownConnections (buffer, &num_sockets, &sockets, sizeof(error), error))
			return TRUE;
	}
	return FALSE;
}
#endif

/* FIX warning, is this ever defined anywhere? */
#ifdef HAVE__ICETRANSNOLISTEN
void _IceTransNoListen (char *foo);
#endif

static int ice_depth = 0;

void
initialize_ice (void)
{
  char error[256];
  guint i;
  GSList* entries;
  int saved_umask;
  gboolean allow_tcp;
  gboolean init_error = TRUE;

  gnome_ice_init ();

  IceSetErrorHandler(my_ice_error_handler);

  IceAddConnectionWatch (ice_watch, NULL);

  /* Some versions of IceListenForConnections have a bug which causes
     the umask to be set to 0 on certain types of failures.  So we
     work around this by saving and restoring the umask.  */
  saved_umask = umask (0);
  umask (saved_umask);

  allow_tcp = gconf_client_get_bool (gsm_get_conf_client (), ALLOW_TCP_KEY, NULL);
 
  if (SmsInitialize (GsmVendor, VERSION, new_client, NULL,
		     auth_proc, sizeof error, error)) 
    {
      if (!allow_tcp){
#ifdef HAVE__ICETRANSNOLISTEN
	_IceTransNoListen ("tcp");
#endif
      }
      if (IceListenForConnections (&num_sockets, &sockets,
				   sizeof(error), error))
	init_error = FALSE;
    }

  if (init_error)
    {
      /* At least try to tell the user something.  */
      fatal (_("The GNOME session manager cannot start properly.  "
	       "Please report this as a GNOME bug. "
	       "Please include this ICE failure message in the bug report:  "
	       "'%s'.  "
	       "Meanwhile you could try logging in using the failsafe session."),
	     error);
    }
  /* See above.  */
  umask (saved_umask);

  input_id = g_new (gint, num_sockets);

  authfile = IceAuthFileName ();

  entries = startup_clean_ice ();

  for (i = 0; i < num_sockets; i++)
    {
      PREPEND (entries, file_entry_new ("ICE", sockets[i]));
      PREPEND (entries, file_entry_new ("XSMP", sockets[i]));

      input_id [i] = ice_add_listener (sockets [i]);
      
      IceSetHostBasedAuthProc (sockets[i], auth_proc);
    }

  write_authfile (authfile, entries, TRUE);
  auth_entries = entries;

  ids = IceComposeNetworkIdList (num_sockets, sockets);
  g_setenv (ENVNAME, ids, TRUE);
  free (ids);
  
  ice_depth = 0;	/* We are live */
}


/*
 * Re-enable ice processing
 */
 
void ice_thawed (void)
{
	guint i;
	
	ice_depth--;
	if (ice_depth)
		return;
	
	/* Last disable cancelled so listen again */
	for (i = 0; i < num_sockets; i++)
		input_id [i] = ice_add_listener (sockets [i]);
}

void ice_frozen (void)
{
	guint i;
	
	if (++ice_depth == 1)
		/* First disable so turn off the events */
		for (i = 0; i < num_sockets; i++)
		  {
		    gsm_verbose ("Removing listener for %p\n", sockets[i]);
		    g_source_remove (input_id [i]);
		  }
}

void
clean_ice (void)
{
  guint i;
  GSList* entries;

  /* No point telling the user about a failure to read during
     shutdown.  */
  entries = read_authfile (authfile, FALSE);

  for (i = 0; i < num_sockets; i++)
    {
      char* network_id = IceGetListenConnectionString (sockets[i]);
      GSList* list = entries;
      
      while (list)
	{
	  IceAuthFileEntry *file_entry = (IceAuthFileEntry *)list->data;

	  list = list->next;
	  /* remove "bad" entries with no network_id */
	  if (!file_entry->network_id || !strcmp (file_entry->network_id, network_id))
	    {
	      REMOVE (entries, file_entry);
	      IceFreeAuthFileEntry (file_entry);
	    }
	}

      gsm_verbose ("Removing listener for %p\n", sockets[i]);
      g_source_remove (input_id [i]);
      free (network_id);
    }

  /* During shutdown we don't want to report errors via a dialog.  */
  write_authfile (authfile, entries, FALSE);

  g_slist_free (entries);

  g_free (input_id);
  IceFreeListenObjs (num_sockets, sockets);
  g_slist_free (auth_entries);
  
  ice_depth = ~0;	/* We are very frozen, like totally off */
}

/* Creates a new entry for inclusion in the ICE authority file and
 * tells ICElib that we are using it to vailidate connections. */
static IceAuthFileEntry *
file_entry_new (const gchar* protocol, const IceListenObj socket)
{
  IceAuthFileEntry *file_entry;
  IceAuthDataEntry data_entry;
  
  file_entry = (IceAuthFileEntry *) malloc (sizeof (IceAuthFileEntry));

  file_entry->protocol_name = strdup (protocol);
  file_entry->protocol_data = NULL;
  file_entry->protocol_data_length = 0;
  file_entry->network_id = IceGetListenConnectionString (socket);
  file_entry->auth_name = strdup ("MIT-MAGIC-COOKIE-1");
  file_entry->auth_data = IceGenerateMagicCookie (MAGIC_COOKIE_LEN);
  file_entry->auth_data_length = MAGIC_COOKIE_LEN;

  data_entry.protocol_name = file_entry->protocol_name;
  data_entry.network_id = file_entry->network_id;
  data_entry.auth_name = file_entry->auth_name;
  data_entry.auth_data = file_entry->auth_data;
  data_entry.auth_data_length = file_entry->auth_data_length;

  IceSetPaAuthData (1, &data_entry);

  return file_entry;
}


/* Reads a file containing ICE authority data */
static GSList* 
read_authfile (gchar* filename, gboolean notify_user)
{
  GSList* entries = NULL;
  FILE *fp;

  original_umask = umask (0077);

  if (IceLockAuthFile (filename, 10, 2, 600) != IceAuthLockSuccess)
    {
      IceFreeListenObjs (num_sockets, sockets);

      if (notify_user)
	fatal (_("The GNOME session manager was unable to lock the file '%s'.  "
		 "Please report this as a GNOME bug.  "
		 "Sometimes this error may occur if the file's directory is unwritable, "
		 "you could try logging in via the failsafe session and ensuring that it is."),
	       filename);
      g_warning ("Unable to lock ICE authority file: %s", filename);
      exit (1);
    }

  fp = fopen (filename, "rb");

  if (fp) {
    IceAuthFileEntry *file_entry;
    
    while ((file_entry = IceReadAuthFileEntry (fp)) != NULL)
      PREPEND (entries, file_entry);

    fclose (fp);
  } else {
    struct stat st;

    if (stat (filename, &st) == 0)
      {
	IceUnlockAuthFile (filename);
	IceFreeListenObjs (num_sockets, sockets);

	if (notify_user)
	  fatal (_("The GNOME session manager was unable to read the file: '%s'.  "
		   "If this file exists it must be readable by you for GNOME to work properly.  "
		   "Try logging in with the failsafe session and removing this file."),
		 filename);

	g_warning ("Unable to read ICE authority file: %s", filename);
	exit (1);
      }
  }
  return entries;
}

static void
fatal (gchar *format, gchar *argument)
{
  GtkWidget *msgbox = gtk_message_dialog_new (NULL, 0,
					      GTK_MESSAGE_ERROR,
					      GTK_BUTTONS_OK,
					      format, argument);
  gtk_window_set_position (GTK_WINDOW (msgbox), GTK_WIN_POS_CENTER);
  gtk_dialog_run (GTK_DIALOG (msgbox));
  exit (1);
}

/* Writes a file of ICE authority data */
static void 
write_authfile (gchar* filename, GSList* entries, gboolean notify_user)
{
  GSList *list = entries; 
  FILE *fp;

  fp = fopen (filename, "wb");
  if (fp) {
    while (list)
      {
	IceAuthFileEntry *file_entry = (IceAuthFileEntry *)list->data;
	IceWriteAuthFileEntry (fp, file_entry);	
	list = list->next;
      }
    fclose (fp);
  } else {
    IceUnlockAuthFile (filename);
    IceFreeListenObjs (num_sockets, sockets);

    if (notify_user)
      fatal (_("Could not write to file '%s'.  "
	       "This file must be writable in order for GNOME to function properly.  "
	       "Try logging in with the failsafe session and removing this file.  "
	       "Also make sure that the file's directory is writable."),
	     filename);

    g_warning ("Unable to write to ICE authority file: %s", filename);
    exit (1);
  }
  IceUnlockAuthFile (filename);

  umask (original_umask);
}
