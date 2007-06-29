/* xsmp.c
 * Copyright (C) 2007 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gi18n.h>

#include "client-xsmp.h"
#include "gsm.h"
#include "xsmp.h"

#include <X11/ICE/ICElib.h>
#include <X11/ICE/ICEutil.h>
#include <X11/ICE/ICEconn.h>
#include <X11/SM/SMlib.h>

#ifdef HAVE_X11_XTRANS_XTRANS_H
/* Get the proto for _IceTransNoListen */
#define ICE_t
#define TRANS_SERVER
#include <X11/Xtrans/Xtrans.h>
#undef  ICE_t
#undef TRANS_SERVER
#endif /* HAVE_X11_XTRANS_XTRANS_H */

static IceListenObj *xsmp_sockets;
static char *xsmp_network_id;
static guint ice_connection_watch;

static gboolean update_iceauthority    (gboolean      adding,
					const char   *our_network_id);

static gboolean accept_ice_connection  (GIOChannel     *source,
					GIOCondition    condition,
					gpointer        data);
static Status   accept_xsmp_connection (SmsConn         conn,
					SmPointer       manager_data,
					unsigned long  *mask_ret,
					SmsCallbacks   *callbacks_ret,
					char          **failure_reason_ret);

static void     ice_error_handler      (IceConn       conn,
					Bool          swap,
					int           offending_minor_opcode,
					unsigned long offending_sequence_num,
					int           error_class,
					int           severity,
					IcePointer    values);
static void     ice_io_error_handler   (IceConn       conn);
static void     sms_error_handler      (SmsConn       sms_conn,
					Bool          swap,
					int           offending_minor_opcode,
					unsigned long offending_sequence_num,
					int           error_class,
					int           severity,
					IcePointer    values);
/**
 * gsm_xsmp_init:
 *
 * Initializes XSMP. Notably, it creates the XSMP listening socket and
 * sets the SESSION_MANAGER environment variable to point to it.
 **/
void
gsm_xsmp_init (void)
{
  char error[256];
  mode_t saved_umask;
  IceListenObj *listeners;
  int num_listeners;
  int i, local_listener;

  /* Set up sane error handlers */
  IceSetErrorHandler (ice_error_handler);
  IceSetIOErrorHandler (ice_io_error_handler);
  SmsSetErrorHandler (sms_error_handler);

  /* Initialize libSM */
  if (!SmsInitialize (PACKAGE, VERSION, accept_xsmp_connection,
		      NULL, NULL, sizeof (error), error))
    gsm_initialization_error (TRUE, "Could not initialize libSM: %s", error);

#ifdef HAVE_X11_XTRANS_XTRANS_H
  /* Disable ICE over TCP. */
  _IceTransNoListen ("tcp");
#endif

  /* Create the XSMP socket. Older versions of IceListenForConnections
   * have a bug which causes the umask to be set to 0 on certain types
   * of failures. Probably not an issue on any modern systems, but
   * we'll play it safe.
   */
  saved_umask = umask (0);
  umask (saved_umask);
  if (!IceListenForConnections (&num_listeners, &listeners,
				sizeof (error), error))
    gsm_initialization_error (TRUE, _("Could not create ICE listening socket: %s"), error);
  umask (saved_umask);

  /* Find the local socket in the returned socket list. */
  local_listener = -1;
  for (i = 0; i < num_listeners; i++)
    {
      char *id = IceGetListenConnectionString (listeners[i]);

      if (!strncmp (id, "local/", sizeof ("local/") - 1))
	{
	  local_listener = i;
	  xsmp_network_id = g_strdup (id);
	  break;
	}
    }

  if (local_listener == -1)
    gsm_initialization_error (TRUE, "IceListenForConnections did not return a local listener!");

  if (num_listeners == 1)
    xsmp_sockets = listeners;
  else
    {
      /* Xtrans was compiled with support for some non-local transport
       * besides TCP. We want to close those sockets. (There's no way
       * we could have figured this out before calling
       * IceListenForConnections.) There's no API for closing a subset
       * of the returned connections, so we have to cheat...
       *
       * If the g_warning below is triggering for you and you want to
       * stop it, the fix is to add additional _IceTransNoListen()
       * calls above.
       */
      IceListenObj local_listener_socket = listeners[local_listener];

      listeners[local_listener] = listeners[num_listeners - 1];
#ifdef HAVE_X11_XTRANS_XTRANS_H
      g_warning ("IceListenForConnections returned %d non-local listeners: %s",
		 num_listeners - 1,
		 IceComposeNetworkIdList (num_listeners - 1, listeners));
#endif
      IceFreeListenObjs (num_listeners - 1, listeners);

      xsmp_sockets = malloc (sizeof (IceListenObj));
      xsmp_sockets[0] = local_listener_socket;
    }

  /* Update .ICEauthority with new auth entries for our socket */
  if (!update_iceauthority (TRUE, xsmp_network_id))
    {
      /* FIXME: is this really fatal? Hm... */
      gsm_initialization_error (TRUE, "Could not update ICEauthority file %s",
				IceAuthFileName ());
    }

  g_setenv ("SESSION_MANAGER", xsmp_network_id, TRUE);
  printf ("SESSION_MANAGER=%s\n", xsmp_network_id);
}

/**
 * gsm_xsmp_run:
 *
 * Sets the XSMP server to start accepting connections.
 **/
void
gsm_xsmp_run (void)
{
  GIOChannel *channel;

  channel = g_io_channel_unix_new (IceGetListenConnectionNumber (xsmp_sockets[0]));
  ice_connection_watch =
    g_io_add_watch (channel, G_IO_IN | G_IO_HUP | G_IO_ERR,
		    accept_ice_connection, xsmp_sockets[0]);
  g_io_channel_unref (channel);
}

/**
 * gsm_xsmp_shutdown:
 *
 * Shuts down the XSMP server and closes the ICE listening socket
 **/
void
gsm_xsmp_shutdown (void)
{
  update_iceauthority (FALSE, xsmp_network_id);
  g_free (xsmp_network_id);
  xsmp_network_id = NULL;

  IceFreeListenObjs (1, xsmp_sockets);
  xsmp_sockets = NULL;
}

/**
 * gsm_xsmp_generate_client_id:
 *
 * Generates a new XSMP client ID.
 *
 * Return value: an XSMP client ID.
 **/
char *
gsm_xsmp_generate_client_id (void)
{
  static int sequence = -1;
  static guint rand1 = 0, rand2 = 0;
  static pid_t pid = 0;
  struct timeval tv;

  /* The XSMP spec defines the ID as:
   *
   * Version: "1"
   * Address type and address:
   *   "1" + an IPv4 address as 8 hex digits
   *   "2" + a DECNET address as 12 hex digits
   *   "6" + an IPv6 address as 32 hex digits
   * Time stamp: milliseconds since UNIX epoch as 13 decimal digits
   * Process-ID type and process-ID:
   *   "1" + POSIX PID as 10 decimal digits
   * Sequence number as 4 decimal digits
   *
   * XSMP client IDs are supposed to be globally unique: if
   * SmsGenerateClientID() is unable to determine a network
   * address for the machine, it gives up and returns %NULL.
   * GNOME and KDE have traditionally used a fourth address
   * format in this case:
   *   "0" + 16 random hex digits
   *
   * We don't even bother trying SmsGenerateClientID(), since the
   * user's IP address is probably "192.168.1.*" anyway, so a random
   * number is actually more likely to be globally unique.
   */

  if (!rand1)
    {
      rand1 = g_random_int ();
      rand2 = g_random_int ();
      pid = getpid ();
    }

  sequence = (sequence + 1) % 10000;
  gettimeofday (&tv, NULL);
  return g_strdup_printf ("10%.04x%.04x%.10lu%.3u%.10lu%.4d",
			  rand1, rand2,
			  (unsigned long) tv.tv_sec,
			  (unsigned) tv.tv_usec,
			  (unsigned long) pid,
			  sequence);
}

/* This is called (by glib via xsmp->ice_connection_watch) when
 * a connection is first received on the ICE listening socket.
 */
static gboolean
accept_ice_connection (GIOChannel   *source,
		       GIOCondition  condition,
		       gpointer      data)
{
  IceListenObj listener = data;
  IceConn ice_conn;
  IceAcceptStatus status;
  GsmClientXSMP *client;

  g_debug ("accept_ice_connection()");

  ice_conn = IceAcceptConnection (listener, &status);
  if (status != IceAcceptSuccess)
    {
      g_debug ("IceAcceptConnection returned %d", status);
      return TRUE;
    }

  client = gsm_client_xsmp_new (ice_conn);
  ice_conn->context = client;
  return TRUE;
}

/* This is called (by libSM) when XSMP is initiated on an ICE
 * connection that was already accepted by accept_ice_connection.
 */
static Status
accept_xsmp_connection (SmsConn sms_conn, SmPointer manager_data,
			unsigned long *mask_ret, SmsCallbacks *callbacks_ret,
			char **failure_reason_ret)
{
  IceConn ice_conn;
  GsmClientXSMP *client;

  /* FIXME: what about during shutdown but before gsm_xsmp_shutdown? */
  if (!xsmp_sockets)
    {
      g_debug ("In shutdown, rejecting new client");

      *failure_reason_ret =
	strdup (_("Refusing new client connection because the session is currently being shut down\n"));
      return FALSE;
    }

  ice_conn = SmsGetIceConnection (sms_conn);
  client = ice_conn->context;

  g_return_val_if_fail (client != NULL, TRUE);

  gsm_client_xsmp_connect (client, sms_conn, mask_ret, callbacks_ret);
  return TRUE;
}

/* ICEauthority stuff */

/* Various magic numbers stolen from iceauth.c */
#define GSM_ICE_AUTH_RETRIES      10
#define GSM_ICE_AUTH_INTERVAL     2   /* 2 seconds */
#define GSM_ICE_AUTH_LOCK_TIMEOUT 600 /* 10 minutes */

#define GSM_ICE_MAGIC_COOKIE_AUTH_NAME "MIT-MAGIC-COOKIE-1"
#define GSM_ICE_MAGIC_COOKIE_LEN       16

static IceAuthFileEntry *
auth_entry_new (const char *protocol, const char *network_id)
{
  IceAuthFileEntry *file_entry;
  IceAuthDataEntry data_entry;

  file_entry = malloc (sizeof (IceAuthFileEntry));

  file_entry->protocol_name = strdup (protocol);
  file_entry->protocol_data = NULL;
  file_entry->protocol_data_length = 0;
  file_entry->network_id = strdup (network_id);
  file_entry->auth_name = strdup (GSM_ICE_MAGIC_COOKIE_AUTH_NAME);
  file_entry->auth_data = IceGenerateMagicCookie (GSM_ICE_MAGIC_COOKIE_LEN);
  file_entry->auth_data_length = GSM_ICE_MAGIC_COOKIE_LEN;

  /* Also create an in-memory copy, which is what the server will
   * actually use for checking client auth.
   */
  data_entry.protocol_name = file_entry->protocol_name;
  data_entry.network_id = file_entry->network_id;
  data_entry.auth_name = file_entry->auth_name;
  data_entry.auth_data = file_entry->auth_data;
  data_entry.auth_data_length = file_entry->auth_data_length;
  IceSetPaAuthData (1, &data_entry);

  return file_entry;
}

static gboolean
update_iceauthority (gboolean adding, const char *our_network_id)
{
  char *filename = IceAuthFileName ();
  FILE *fp;
  IceAuthFileEntry *auth_entry;
  GSList *entries, *e;

  if (IceLockAuthFile (filename, GSM_ICE_AUTH_RETRIES, GSM_ICE_AUTH_INTERVAL,
		       GSM_ICE_AUTH_LOCK_TIMEOUT) != IceAuthLockSuccess)
    return FALSE;

  entries = NULL;

  fp = fopen (filename, "r+");
  if (fp)
    {
      while ((auth_entry = IceReadAuthFileEntry (fp)) != NULL)
	{
	  /* Skip/delete entries with no network ID (invalid), or with
	   * our network ID; if we're starting up, an entry with our
	   * ID must be a stale entry left behind by an old process,
	   * and if we're shutting down, it won't be valid in the
	   * future, so either way we want to remove it from the list.
	   */
	  if (!auth_entry->network_id ||
	      !strcmp (auth_entry->network_id, our_network_id))
	    IceFreeAuthFileEntry (auth_entry);
	  else
	    entries = g_slist_prepend (entries, auth_entry);
	}

      rewind (fp);
    }
  else
    {
      int fd;

      if (g_file_test (filename, G_FILE_TEST_EXISTS))
	{
	  g_warning ("Unable to read ICE authority file: %s", filename);
	  return FALSE;
	}

      fd = open (filename, O_CREAT | O_WRONLY, 0600);
      fp = fdopen (fd, "w");
      if (!fp)
	{
	  g_warning ("Unable to write to ICE authority file: %s", filename);
	  if (fd != -1)
	    close (fd);
	  return FALSE;
	}
    }

  if (adding)
    {
      entries = g_slist_append (entries,
				auth_entry_new ("ICE", our_network_id));
      entries = g_slist_prepend (entries,
				 auth_entry_new ("XSMP", our_network_id));
    }

  for (e = entries; e; e = e->next)
    {
      IceAuthFileEntry *auth_entry = e->data;
      IceWriteAuthFileEntry (fp, auth_entry);
      IceFreeAuthFileEntry (auth_entry);
    }
  g_slist_free (entries);

  fclose (fp);
  IceUnlockAuthFile (filename);

  return TRUE;
}

/* Error handlers */

static void
ice_error_handler (IceConn conn, Bool swap, int offending_minor_opcode,
		   unsigned long offending_sequence, int error_class,
		   int severity, IcePointer values)
{
  g_debug ("ice_error_handler (%p, %s, %d, %lx, %d, %d)",
	   conn, swap ? "TRUE" : "FALSE", offending_minor_opcode,
	   offending_sequence, error_class, severity);

  if (severity == IceCanContinue)
    return;

  /* FIXME: the ICElib docs are completely vague about what we're
   * supposed to do in this case. Need to verify that calling
   * IceCloseConnection() here is guaranteed to cause neither
   * free-memory-reads nor leaks.
   */
  IceCloseConnection (conn);
}

static void
ice_io_error_handler (IceConn conn)
{
  g_debug ("ice_io_error_handler (%p)", conn);

  /* We don't need to do anything here; the next call to
   * IceProcessMessages() for this connection will receive
   * IceProcessMessagesIOError and we can handle the error there.
   */
}

static void
sms_error_handler (SmsConn conn, Bool swap, int offending_minor_opcode,
		   unsigned long offending_sequence_num, int error_class,
		   int severity, IcePointer values)
{
  g_debug ("sms_error_handler (%p, %s, %d, %lx, %d, %d)",
	   conn, swap ? "TRUE" : "FALSE", offending_minor_opcode,
	   offending_sequence_num, error_class, severity);

  /* We don't need to do anything here; if the connection needs to be
   * closed, libSM will do that itself.
   */
}
