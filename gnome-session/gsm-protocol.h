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

/* This is under development and probably pitched at a lowere level than
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

typedef GtkObject* (*GsmClientFactory) ();

/* GSM_CLIENT object */

#define GSM_IS_CLIENT(obj)      GTK_CHECK_TYPE (obj, gsm_client_get_type ())
#define GSM_CLIENT(obj)         GTK_CHECK_CAST (obj, gsm_client_get_type (), GsmClient)
#define GSM_CLIENT_CLASS(klass) GTK_CHECK_CLASS_CAST (klass, gsm_client_get_type (), GsmClientClass)

typedef struct _GsmClient GsmClient;
typedef struct _GsmClientClass GsmClientClass;

struct _GsmClient {
  GtkObject object;

  gchar*    handle;                       /* internal use */

  guint     order;                        /* startup order */ 
  GsmStyle  style;                        /* restart style */
  GsmState  state;                        /* state of client */
  gchar*    command;                      /* command (SmCloneCommand) */ 
};

struct _GsmClientClass {
  GtkObjectClass parent_class;

  /* Signals emitted when gnome-session notifies a change: */
  void (* remove)    (GsmClient *client);
  void (* order)     (GsmClient *client, guint new_order);
  void (* style)     (GsmClient *client, GsmStyle new_style);
  void (* state)     (GsmClient *client, GsmState new_state);
  void (* command)   (GsmClient *client, gchar* new_command);
  /* Signal emitted when gnome-session encounters an client error:
   * the list of strings argument is freed internally. */
  void (* reasons)   (GsmClient *client, GSList* reasons);
};

guint gsm_client_get_type  (void);

/* Creates a client (without adding it to the current session) */
GtkObject* gsm_client_new (void);

/* Adds the client to the current session. 
 * Increases the ref count by 1 immediately. */
void       gsm_client_commit_add (GsmClient *client);
/* Removes the client from the current session.
 * Reduces the ref count by 1 when removal is confirmed by gnome-session. */
void       gsm_client_commit_remove (GsmClient *client);

/* Commits the current restart style of the client */
void       gsm_client_commit_style (GsmClient *client);
/* Commits the current start order of the client */
void       gsm_client_commit_order (GsmClient *client);

/* GSM_SELECTOR object */

#define GSM_IS_SELECTOR(obj)      GTK_CHECK_TYPE (obj, gsm_selector_get_type ())
#define GSM_SELECTOR(obj)         GTK_CHECK_CAST (obj, gsm_selector_get_type (), GsmSelector)
#define GSM_SELECTOR_CLASS(klass) GTK_CHECK_CLASS_CAST (klass, gsm_selector_get_type (), GsmSelectorClass)

typedef struct _GsmSelector GsmSelector;
typedef struct _GsmSelectorClass GsmSelectorClass;

struct _GsmSelector {
  GtkObject object;

  gchar*           handle;                /* future internal use */
  gint             waiting;               /* internal use */

  GnomeClient*     gnome_client;          /* connection used for protocol */
  GsmClientFactory client_factory;        /* creates new GsmClient objects */
  gpointer         data;                  /* data passed to client_factory */
};

struct _GsmSelectorClass {
  GtkObjectClass parent_class;

  void (* initialized) (GsmSelector *selector); /* client details complete */
};

guint gsm_selector_get_type  (void);

/* Creating a selector turns on the protocol over the connection used by
 * the specified GnomeClient (the gnome_master_client is the best choice).
 * The client_factory is used to create a new GsmClient derived object
 * when gnome-session informs us that a new client has entered the session.
 * The details of the client are recorded in this GsmClient object and it
 * emits signals when the client details change. */
/* NB: the current implementation only allows the creation of ONE selector
 * object and connection is limited to one instance of gnome-session. */
GtkObject* gsm_selector_new (GnomeClient *gnome_client,
			     GsmClientFactory client_factory, gpointer data);

/* UTILITIES */

gboolean   gsm_sh_quotes_balance (gchar* command);
/* Returns true when "command" has balanced quotes under the sh syntax and
 * contains some non-whitespace characters (i.e. it might be a commmand).  
 * Does NOT interpret sh metacharacters like "|", ">", etc. */

#endif /* GSM_PROTOCOL_H */
