/* manager.h - Definitions for session manager.

   Copyright (C) 1998, 1999 Tom Tromey

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

#ifndef MANAGER_H
#define MANAGER_H

#include <X11/SM/SMlib.h>
#include <time.h>

#include "glib.h"

/* Config prefix used to store the sysadmin's default sessions. */
#define DEFAULT_CONFIG_PREFIX "=" DEFAULTDIR "/default.session=/"

/* Config prefix used to store the users' sessions. */
#define CONFIG_PREFIX "session/"

/* Config prefix used to store the users' sessions. */
#define MANUAL_CONFIG_PREFIX "session-manual/"

/* Default session name. */
#define DEFAULT_SESSION "Default"

/* Chooser session name. */
#define CHOOSER_SESSION "Chooser"

/* Trash session name. */
#define WARNER_SESSION "Warner"

/* Trash session name. */
#define TRASH_SESSION "Trash"

/* Config section used for gnome-session's own config details. */
#define GSM_CONFIG_SECTION "__gsm__"

/* Config section used for session-related options */
#define GSM_OPTION_CONFIG_PREFIX "session-options/Options/"

/* Name of key used to store the current session name. */
#define CURRENT_SESSION_KEY "Current Session"

/* Name of key used to store number of clients in a session. */
#define CLIENT_COUNT_KEY "num_clients"

/* Name of key used to store whether show the splash screen */
#define SPLASH_SCREEN_KEY "SplashScreen"

#ifndef SPLASH_SCREEN_DEFAULT
#define SPLASH_SCREEN_DEFAULT "true"
#endif

/* Name of key used to store trash mode. */
#define TRASH_MODE_KEY "TrashMode"

#ifndef TRASH_MODE_DEFAULT
#define TRASH_MODE_DEFAULT "true"
#endif

/* Allow TCP connections to the session manager. */
#define ALLOW_TCP_KEY "AllowTCP"

#ifndef ALLOW_TCP_DEFAULT
#define ALLOW_TCP_DEFAULT "false"
#endif

/* Convenience macros: */
#define GSM_CONFIG_PREFIX CONFIG_PREFIX GSM_CONFIG_SECTION "/"

/* Rule used to match client: gets around need to specify proper client ids
 * when starting from sysadmin files or via the GsmAddClient protocol.
 * This is a big burden off end users and only creates the possibility of
 * confusion between PURGED SM aware clients started by GsmAddClient. */
typedef enum {
  MATCH_NONE,
  MATCH_ID,
  MATCH_FAKE_ID,
  MATCH_DONE,
  MATCH_PROP,
  MATCH_WARN
} MatchRule;

/* Additional details for clients that speak our command protocol */
typedef struct _CommandData CommandData;

/* Each client is represented by one of these.  Note that the client's
   state is not kept explicitly.  A client is on only one of several
   linked lists at a given time; the state is implicit in the list.  */
typedef struct
{
  /* Client's session id.  */
  char *id;

  /* Handle for the GsmCommand protocol */
  gchar* handle;

  /* Client's connection.  */
  SmsConn connection;

  /* List of all properties for this client.  Each element of the list
     is an `SmProp *'.  */
  GSList *properties;

  /* Used to detect clients which are dying quickly */
  guint  attempts;
  time_t connect_time;

  /* Used to determine order in which clients are started */
  guint priority;

  /* Used to avoid registering clients with ids from default.session */
  MatchRule match_rule;

  /* Used to suspend further warnings while user is responding to a warning */
  gboolean warning;

  /* Used to decouple SmsGetPropertiesProc and SmsReturnProperties
   * for the purpose of extending the protocol: */
  GSList* get_prop_replies;
  guint get_prop_requests;

  /* Additional details for clients that speak our command protocol */
  CommandData *command_data;
} Client;

typedef struct {
  /* Handle for the GsmCommand protocol */
  gchar*  handle;

  /* session name for user presentation */
  gchar*  name;

  /* The Client* members of the session */
  GSList *client_list;
} Session;


/* Milliseconds to wait for clients to register before assuming that
 * they have finished any initialisation needed by other clients. */
extern guint purge_delay;

/* Milliseconds to wait for clients to die before cutting our throat. */
extern guint suicide_delay;

/* Milliseconds to wait for clients to complete save before reporting error. */
extern guint warn_delay;

/* Waiting for the chooser to tell us which session to start. */ 
extern gboolean choosing;

/* Putting all saves into the "Trash" session. */
extern gboolean trashing;

/* Ignoring ~/.gnome/session as it is deemed to be unreliable. */
extern gboolean failsafe;

/* Vector of sockets we are listening on, and size of vector.  */
extern gint num_sockets;
extern IceListenObj *sockets;

/* List of auth entries.  */
extern GSList *auth_entries;

/*
 * logout.c
 */

gboolean maybe_display_gui (void);


/*
 * manager.c
 */

extern GSList **client_lists[];
/* List of clients which have been started but have yet to register */
extern gchar *events[];

/* Lists of clients which have been purged from the pending list: 
   The first list contains clients we throw away at the session end,
   the second contains clients the user wants us to keep. */
extern GSList *purge_drop_list, *purge_retain_list, *live_list, *zombie_list, *pending_list;

/* Start an individual client. */
void start_client (Client* client);

/* Start a list of clients in priority order. */
void start_clients (GSList* client_list);

/* Remove a client from the session (completely).
 * Returns 1 on success, 0 when the client was not found in the current 
 * session and -1 when a save is in progress. */
gint remove_client (Client* client);

/* Free the memory used by a client. */
void free_client (Client* client);

/* Run the Discard, Resign or Shutdown command on a client.
 * Returns the pid or -1 if unsuccessful. */
gint run_command (Client* client, const gchar* command);

/* Call this to initiate a session save, and perhaps a shutdown.
   Save requests are queued internally. */
void save_session (int save_type, gboolean shutdown, int interact_style,
		   gboolean fast);

/* This is called via ICE when a new client first connects.  */
Status new_client (SmsConn connection, SmPointer data, unsigned long *maskp,
		   SmsCallbacks *callbacks, char **reasons);

/* Find a client from a list */
Client* find_client_by_id (const GSList *list, const char *id);

/* This is used to send properties to clients that use the _GSM_Command
 * protocol extension. */
void send_properties (Client* client, GSList* prop_list);

/*
 * save.c
 */

/* Attempts to set the session name (the requested name may be locked).
 * Returns the name that has been assigned to the session. */
gchar* set_session_name (const gchar *name);

/* Returns name of last session run (with a default) */
gchar* get_last_session (void);

/* Set the trash mode */
void set_trash_mode (gboolean new_mode);

/* Releases the lock on the session name when shutting down the session */
void unlock_session (void);

/* Write current session to the config file. */
void write_session (void);

/* Load a session from our configuration by name. */
Session* read_session (const char *name);

/* Starts the clients in a session and frees the session. */
void start_session (Session* session);

/* Frees the memory used by a session. */
void free_session (Session* session);

/* Delete a session from the config file and discard any stale
 * session info saved by clients that were in the session. */
void delete_session (const char *name);

/*
 * ice.c
 */

/* Initialize the ICE part of the session manager. */
void initialize_ice (void);

/* Set a clean up callback for when the ice_conn is closed. */
void ice_set_clean_up_handler (IceConn ice_conn, 
			       GDestroyNotify clean_up, gpointer data);

/* Clean up ICE when exiting.  */
void clean_ice (void);

/*
 * prop.c
 */

/* Call this to find the named property for a client.  Returns NULL if
   not found.  */
SmProp *find_property_by_name (const Client *client, const char *name);

/* Find property NAME attached to CLIENT.  If not found, or type is
   not CARD8, then return FALSE.  Otherwise set *RESULT to the value
   and return TRUE.  */
gboolean find_card8_property (const Client *client, const char *name,
			      int *result);

/* Find property NAME attached to CLIENT.  If not found, or type is
   not ARRAY8, then return FALSE.  Otherwise set *RESULT to the value
   and return TRUE.  *RESULT is g_malloc()d and must be freed by the
   caller.  */
gboolean find_string_property (const Client *client, const char *name,
			       char **result);

/* Find property NAME attached to CLIENT.  If not found, or type is
   not LISTofARRAY8, then return FALSE.  Otherwise set *ARGCP to the
   number of vector elements, *ARGVP to the elements themselves, and
   return TRUE.  Each element of *ARGVP is g_malloc()d, as is *ARGVP
   itself.  You can use `g_strfreev' to free the result.  */
gboolean find_vector_property (const Client *client, const char *name,
			       int *argcp, char ***argvp);

/*
 * command.c
 */

extern gint logout_command_argc;
extern gchar **logout_command_argv;

/* Log change in the state of a client with the client event selectors. */
void client_event (const gchar* handle, const gchar* event);

/* Log change in the properties of a client with the client event selectors. */
void client_property (const gchar* handle, int nprops, SmProp** props);

/* Log reasons for an event with the client event selectors. */
void client_reasons (Client* client, gboolean confirm, 
		     gint count, gchar** reasons);

/* Returns the client that prints up warnings for gnome-session (if any). */
Client* get_warner (void);

/* Process a _GSM_Command protocol message. */
void command (Client* client, int nprops, SmProp** props);

/* TRUE when the _GSM_Command protocol is enabled for this client. */
gboolean command_active (Client* client);

/* Clean up the _GSM_Command protocol for a client. */
void command_clean_up (Client* client);

/* Create an object handle for use in the _GSM_Command protocol. */
gchar* command_handle_new (gpointer object);

/* Move an object handle from one object to another. */ 
gchar* command_handle_reassign (gchar* handle, gpointer new_object);

/* Free an object handle */
void command_handle_free (gchar* handle);

/* If a logout command exits, exec() it and don't return */
void execute_logout (void);

/* Set the logout command.  */
void set_logout_command (char **command);

/* Convenience macros */
#define APPEND(List,Elt) ((List) = (g_slist_append ((List), (Elt))))
#define REMOVE(List,Elt) ((List) = (g_slist_remove ((List), (Elt))))
#define CONCAT(L1,L2) ((L1) = (g_slist_concat ((L1), (L2))))

/*
 * remote.c
 */

/* Try to start a program remotely, if appropriate, else locally.  */
gint remote_start (char *restart_info, int argc, char **argv,
		   char *cwd, int envpc, char **envp);

#endif /* MANAGER_H */
