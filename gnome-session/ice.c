/* ice.c - Handle session manager/ICE integration.

   Copyright (C) 1998 Tom Tromey

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
#include <gdk/gdk.h>
#include <gtk/gtk.h>

#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#include <libgnomeui/gnome-ice.h>
#include <libgnome/libgnome.h>

#include "manager.h"
#include "session.h"

#define MAGIC_COOKIE_LEN 16
#define ENVNAME "SESSION_MANAGER"

/* Network ids.  */
char* ids;

/* The "sockets" which we listen on */
static IceListenObj *sockets;
static gint *input_id;
static guint num_sockets;

/* File containing the ICE authorization data (usually ~/.ICEauthority) */
static gchar* authfile;

static IceAuthFileEntry* file_entry_new (const gchar* protocol, 
					 const IceListenObj socket);
static GSList* read_authfile(gchar* filename);
static void write_authfile(gchar* filename, GSList* entries);

/* This is called when a client tries to connect. 
 * We still accept connections during a shutdown so we can handle warnings. */
static void
accept_connection (gpointer client_data, gint source,
		   GdkInputCondition conditon)
{
  IceConn connection;
  IceAcceptStatus status;
  IceConnectStatus status2 = IceConnectPending;

  connection = IceAcceptConnection ((IceListenObj) client_data, &status);
  if (status != IceAcceptSuccess)
    return;

  /* Must wait until connection leaves pending state.  */
  /* FIXME: after a certain amount of time, just reject the
     connection.  */
  while (status2 == IceConnectPending)
    {
      gtk_main_iteration ();
      status2 = IceConnectionStatus (connection);
    }
}

typedef struct {
  IceConn        connection;
  GDestroyNotify clean_up;
  gpointer       data;
} Watch;

static GSList* watch_list = NULL;

static void
ice_watch (IceConn connection, IcePointer client_data, Bool opening,
	   IcePointer *watch_data)
{
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
      g_free (watch);
    }
}

void 
ice_set_clean_up_handler (IceConn connection, 
			  GDestroyNotify clean_up, gpointer data)
{
  GSList* list;
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

static void
startup_clean_ice (void)
{
  guint i;
  GSList* entries;

  entries = read_authfile (authfile);

  for (i = 0; i < num_sockets; i++)
    {
      char* network_id = IceGetListenConnectionString (sockets[i]);
      GSList* list = entries;
      
      while (list)
	{
	  IceAuthFileEntry *file_entry = (IceAuthFileEntry *)list->data;

	  list = list->next;
	  if (!strcmp (file_entry->network_id, network_id))
	    {
	      REMOVE (entries, file_entry);
	      IceFreeAuthFileEntry (file_entry);
	    }
	}

    }

  write_authfile (authfile, entries);
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

void
initialize_ice (void)
{
  char error[256];
  IceListenObj *listeners;
  char *p;
  guint i;
  GSList* entries;

  gnome_ice_init ();

  IceAddConnectionWatch (ice_watch, NULL);

  if (! SmsInitialize (GsmVendor, VERSION, new_client, NULL,
		       auth_proc, sizeof error, error) ||
      ! IceListenForConnections (&num_sockets, &sockets,
				 sizeof error, error))
    {
      g_warning (error);
      exit (1);
    }

  input_id = g_new (gint, num_sockets);

  authfile = IceAuthFileName ();

  startup_clean_ice ();

  entries = read_authfile (authfile);

  for (i = 0; i < num_sockets; i++)
    {
      APPEND (entries, file_entry_new ("ICE", sockets[i]));
      APPEND (entries, file_entry_new ("XSMP", sockets[i]));
      
      input_id[i] = gdk_input_add (IceGetListenConnectionNumber (sockets[i]),
				   GDK_INPUT_READ, accept_connection,
				   (gpointer) sockets[i]);

      IceSetHostBasedAuthProc (sockets[i], auth_proc);
    }

  write_authfile (authfile, entries);

  ids = IceComposeNetworkIdList (num_sockets, sockets);

  p = g_strconcat (ENVNAME "=", ids, NULL);
  putenv (p);
}

void
clean_ice (void)
{
  guint i;
  GSList* entries;

  entries = read_authfile (authfile);

  for (i = 0; i < num_sockets; i++)
    {
      char* network_id = IceGetListenConnectionString (sockets[i]);
      GSList* list = entries;
      
      while (list)
	{
	  IceAuthFileEntry *file_entry = (IceAuthFileEntry *)list->data;

	  list = list->next;
	  if (!strcmp (file_entry->network_id, network_id))
	    {
	      REMOVE (entries, file_entry);
	      IceFreeAuthFileEntry (file_entry);
	    }
	}

      gtk_input_remove (input_id[i]);      
      free (network_id);
    }

  write_authfile (authfile, entries);

  g_free (input_id);
  free (ids);
  IceFreeListenObjs (num_sockets, sockets);
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

static gint original_umask;

/* Reads a file containing ICE authority data */
static GSList* 
read_authfile(gchar* filename)
{
  GSList* entries = NULL;
  FILE *fp;

  original_umask = umask (0077);

  if (IceLockAuthFile (filename, 10, 2, 600) != IceAuthLockSuccess)
    {
      g_warning ("Unable to lock ICE authority file: %s", filename);
      IceFreeListenObjs (num_sockets, sockets);
      exit (1);
    }

  fp = fopen (filename, "rb");

  if (fp) {
    IceAuthFileEntry *file_entry;
    
    while ((file_entry = IceReadAuthFileEntry (fp)) != NULL)
      APPEND (entries, file_entry);

    fclose (fp);
  } else {
    struct stat st;

    if (stat (filename, &st) == 0)
      {
	g_warning ("Unable to read ICE authority file: %s", filename);
	IceUnlockAuthFile (filename);
	IceFreeListenObjs (num_sockets, sockets);
	exit (1);
      }
  }
  return entries;
}

/* Writes a file of ICE authority data */
static void 
write_authfile (gchar* filename, GSList* entries)
{
  GSList *list = entries; 
  FILE *fp;

  fp = fopen (filename, "wb");
  if (fp) {
    while (list)
      {
	IceAuthFileEntry *file_entry = (IceAuthFileEntry *)list->data;

	REMOVE (list, file_entry);

	IceWriteAuthFileEntry (fp, file_entry);	
	IceFreeAuthFileEntry (file_entry);
      }
    fclose (fp);
  } else {
    g_warning ("Unable to write to ICE authority file: %s", filename);
    IceUnlockAuthFile (filename);
    IceFreeListenObjs (num_sockets, sockets);
    exit (1);
  }
  IceUnlockAuthFile (filename);

  umask (original_umask);
}
