/* headers.h - Definitions for session manager.

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

#ifndef HEADER_H
#define HEADER_H

#include <X11/SM/SMlib.h>
#include <time.h>

#include <glib.h>


/* Config prefix used to store the sysadmin's default sessions. */
#define DEFAULT_CONFIG_PREFIX "=" DEFAULTDIR "/default.session=/"

/* Config prefix used to store the users' sessions. */
#define CONFIG_PREFIX "session/"

/* Config prefix used to store the users' sessions. */
#define MANUAL_CONFIG_PREFIX "session-manual/"

/* Default session name. */
#define DEFAULT_SESSION "Default"

#define FAILSAFE_SESSION "Failsafe"

/* Config section used for session-related options */
#define GSM_OPTION_CONFIG_PREFIX "session-options/Options/"

/* Name of key used to store the current session name. 
   Now to be found in $HOME/.gnome/session-options */
#define CURRENT_SESSION_KEY "CurrentSession"

/* Name of key used to store number of clients in a session. */
#define CLIENT_COUNT_KEY "num_clients"

/* 
 * These are stored in GConf
 *
 * autosave and logout prompt are auto-notified, the others loaded the
 * one time they are needed.
 */

#define GSM_GCONF_CONFIG_PREFIX    "/apps/gnome-session/options"
/* Name of key used to store whether show the splash screen */
#define SPLASH_SCREEN_KEY          GSM_GCONF_CONFIG_PREFIX "/show_splash_screen"

/* Name of key used to store autosave mode. */
#define AUTOSAVE_MODE_KEY          GSM_GCONF_CONFIG_PREFIX "/auto_save_session"

/* Prompt on logout */
#define LOGOUT_PROMPT_KEY          GSM_GCONF_CONFIG_PREFIX "/logout_prompt"

/* Allow TCP connections to the session manager. */
#define ALLOW_TCP_KEY              GSM_GCONF_CONFIG_PREFIX "/allow_tcp_connections"

/* Assistive Technology support is turned on */
#define ACCESSIBILITY_KEY         "/desktop/gnome/interface/accessibility"

/* Convenience macros: */
#define APPEND(List,Elt) ((List) = (g_slist_append ((List), (Elt))))
#define REMOVE(List,Elt) ((List) = (g_slist_remove ((List), (Elt))))
#define CONCAT(L1,L2) ((L1) = (g_slist_concat ((L1), (L2))))

#define CLIENT_MAGIC 0xFEED50DA
#define CLIENT_DEAD  0xBADC0DE5

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

  /* The last DiscardCommand we received */
  SmProp *last_discard;

  /* If the client has been loaded from, or saved to, disk */
  gboolean session_saved;

  /* Purge timeout ID; see start_client() */
  guint purge_timeout_id;

  guint magic;
} Client;

typedef struct {
  /* Handle for the GsmCommand protocol */
  gchar*  handle;

  /* session name for user presentation */
  gchar*  name;

  /* The Client* members of the session */
  GSList *client_list;
} Session;

/* Name of the session to load up */
extern gchar *session_name;

/* Milliseconds to wait for clients to register before assuming that
 * they have finished any initialisation needed by other clients. */
extern guint purge_delay;

/* Milliseconds to wait for clients to die before cutting our throat. */
extern guint suicide_delay;

/* Milliseconds to wait for clients to complete save before reporting error. */
extern guint warn_delay;

/* Prompting for save session on logout. If true, session will automatically
 * save, if false, user will be prompted to have option of saving session on
 * logout */
extern gboolean autosave;

/* This is just a temporary variable that is set to true when the user 
 * manually saves session and set to false otherwise. Dirty hack to avoid
 * saving on logout */
extern gboolean session_save;

/*
 * whether to prompt the user on logout
 */
extern gboolean logout_prompt;

/* When prompted to save session on logout, this flag indicates to save or 
 * not to save */
extern gboolean save_selected;

/* Ignoring ~/.gnome/session as it is deemed to be unreliable. */
extern gboolean failsafe;

/* List of auth entries.  */
extern GSList *auth_entries;

extern GSList **client_lists[];

/* List of clients which have been started but have yet to register */
extern gchar *events[];

/* Lists of clients which have been purged from the pending list: 
   The first list contains clients we throw away at the session end,
   the second contains clients the user wants us to keep. */
extern GSList *purge_drop_list, *purge_retain_list, *live_list, *zombie_list, *pending_list;

#endif /* HEADERS_H */
