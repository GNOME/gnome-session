/* gsm-protocol.h - gnome interface to gnome-session protocol extensions.

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

   Authors: Felix Bellaby */

/* This is under development and probably pitched at a lower level than
 * anyone would want to use... */

#ifndef GSM_PROTOCOL_H
#define GSM_PROTOCOL_H

#include <gnome.h>

typedef enum {
  GSM_INACTIVE,     /* unstarted or finished */
  GSM_START,        /* started but not connected */
  GSM_CONNECTED,    /* connected to gnome-session */
  GSM_SAVE,         /* saving session details */
  GSM_UNKNOWN,      /* still unconnected after timeout.*/
  GSM_NSTATES
} GsmState;

typedef enum {
  GSM_NORMAL,       /* dropped from the session when it dies */
  GSM_RESPAWN,      /* respawned when it dies */
  GSM_TRASH,        /* dropped from the session on logout */
  GSM_SETTINGS,     /* started on every login */
  GSM_NSTYLES
} GsmStyle;

typedef GObject *(*GsmClientFactory) ();

/* GSM_PROTOCOL object */

#define GSM_TYPE_PROTOCOL         (gsm_protocol_get_type ())
#define GSM_PROTOCOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GSM_TYPE_PROTOCOL, GsmProtocol))
#define GSM_PROTOCOL_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GSM_TYPE_PROTOCOL, GsmProtocolClass))
#define GSM_IS_PROTOCOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GSM_TYPE_PROTOCOL))
#define GSM_IS_PROTOCOL_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GSM_TYPE_PROTOCOL))
#define GSM_PROTOCOL_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GSM_TYPE_PROTOCOL, GsmProtocolClass))

typedef struct {
  GObject   object;

  gpointer  smc_conn; /* connection used for protocol */
} GsmProtocol;

typedef struct {
  GObjectClass parent_class;

  /* signals emitted when gnome-session returns a value. */
  void (* saved_sessions)  (GsmProtocol *protocol, GSList* saved_sessions); 
  void (* current_session) (GsmProtocol *protocol, gchar* current_session);
} GsmProtocolClass;

GType gsm_protocol_get_type  (void);

/* Creating a GsmProtocol turns on the protocol over the connection used by
 * the specified GnomeClient (the gnome_master_client is the best choice).
 * NB: only ONE GsmProtocol may be created and it must be created before
 * creating any GsmSession or GsmClient derived objects. */
GsmProtocol *gsm_protocol_new (GnomeClient *gnome_client);

/* Requests gnome-session to list its saved sessions. Emits "saved_sessions" */
void gsm_protocol_get_saved_sessions (GsmProtocol* protocol);

/* Requests gnome-session for the name of the current session loaded (the one
 * which it would load by default). Emits "current_session" */
void gsm_protocol_get_current_session (GsmProtocol* protocol);

/* Sets the autosave mode for the session manager */
void gsm_protocol_set_autosave_mode (GsmProtocol *protocol,
				  gboolean     auto_save);

/* Sets the sessionsave mode for the session manager */
void gsm_protocol_set_sessionsave_mode (GsmProtocol *protocol,
				  gboolean     sessionsave);

/* Sets the current session for the session manager */
void gsm_protocol_set_session_name (GsmProtocol *protocol,
				    const char  *session_name);

/* GSM_SESSION object */

#define GSM_TYPE_SESSION         (gsm_session_get_type ())
#define GSM_SESSION(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GSM_TYPE_SESSION, GsmSession))
#define GSM_SESSION_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GSM_TYPE_SESSION, GsmSessionClass))
#define GSM_IS_SESSION(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GSM_TYPE_SESSION))
#define GSM_IS_SESSION_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GSM_TYPE_SESSION))
#define GSM_SESSION_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GSM_TYPE_SESSION, GsmSessionClass))

typedef struct {
  GObject          object;

  gchar*           handle;             /* internal use */
  gint             waiting;            /* internal use */

  gchar*           name;               /* session name used by user */
  GsmClientFactory client_factory;     /* creates new GsmClient objects */
  gpointer         data;               /* data passed to client_factory */
} GsmSession;

typedef struct {
  GObjectClass parent_class;

  void (* initialized)    (GsmSession *session); /* all clients ready */
  void (* session_name)   (GsmSession *session, gchar* name); 
                                                 /* session name change */
} GsmSessionClass;

GType gsm_session_get_type  (void);

/* Creates a session object for the clients in the session saved under name.
 * The client_factory is used to create new GsmClient derived objects
 * for each client that is in the session and is passed the "data" pointer.
 * The details of the client are recorded in its GsmClient object and this
 * object emits signals when those details change. */
GsmSession *gsm_session_new  (char             *name, 
			      GsmClientFactory  client_factory,
			      gpointer          data);

/* Creates a session object for the clients in the live session. */
GsmSession *gsm_session_live (GsmClientFactory  client_factory,
			      gpointer          data);

/* Starts a saved session. Makes it into the live session when no live
 * session has been created. */
void       gsm_session_start (GsmSession *session);

/* Sets the name of the live session. */
void       gsm_session_set_name (GsmSession *session, gchar* name);

/* GSM_CLIENT object */

#define GSM_TYPE_CLIENT         (gsm_client_get_type ())
#define GSM_CLIENT(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GSM_TYPE_CLIENT, GsmClient))
#define GSM_CLIENT_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GSM_TYPE_CLIENT, GsmClientClass))
#define GSM_IS_CLIENT(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GSM_TYPE_CLIENT))
#define GSM_IS_CLIENT_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GSM_TYPE_CLIENT))
#define GSM_CLIENT_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GSM_TYPE_CLIENT, GsmClientClass))

typedef struct {
  GObject     object;

  gchar      *handle;                       /* internal use */
  GsmSession *session;                      /* session containing client */

  guint       order;                        /* startup order */ 
  GsmStyle    style;                        /* restart style */
  GsmState    state;                        /* state of client */
  gchar*      command;                      /* command (SmCloneCommand) */ 
} GsmClient;

typedef struct {
  GObjectClass parent_class;

  /* Signals emitted when gnome-session notifies a change: */
  void (* remove)    (GsmClient *client);
  void (* order)     (GsmClient *client, guint new_order);
  void (* style)     (GsmClient *client, GsmStyle new_style);
  void (* state)     (GsmClient *client, GsmState new_state);
  void (* command)   (GsmClient *client, gchar* new_command);
  /* Signal emitted when gnome-session encounters an client error:
   * Confirm is TRUE when the client must be removed explicitly. */
  void (* reasons)   (GsmClient *client, gboolean confirm, GSList* reasons);
} GsmClientClass;

GType gsm_client_get_type  (void);

/* Creates a client (without adding it to the live session) */
GsmClient *gsm_client_new (void);

/* Adds the client to the live session. 
 * Increases the ref count by 1 immediately. */
void       gsm_client_commit_add (GsmClient *client);
/* Removes the client from the live session.
 * Reduces the ref count by 1 when removal is confirmed by gnome-session. */
void       gsm_client_commit_remove (GsmClient *client);

/* Commits the current restart style of the client */
void       gsm_client_commit_style (GsmClient *client);
/* Commits the current start order of the client */
void       gsm_client_commit_order (GsmClient *client);

/* UTILITIES */

gboolean   gsm_sh_quotes_balance (const char* command);
/* Returns true when "command" has balanced quotes under the sh syntax and
 * contains some non-whitespace characters (i.e. it might be a commmand).  
 * Does NOT interpret sh metacharacters like "|", ">", etc. */

#endif /* GSM_PROTOCOL_H */
