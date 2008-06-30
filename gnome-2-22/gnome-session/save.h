/* save.h - Code to save session.

   Copyright (C) 1998, 1999 Tom Tromey

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
   02111-1307, USA.  */

#ifndef SAVE_H
#define SAVE_H

#include "headers.h"

/* Used to save xsm style string valued SmDiscardCommand properties. */
#define XsmDiscardCommand SmDiscardCommand "[string]"

typedef enum
{
  VECTOR,
  STRING,
  NUMBER
} prop_type;

/* This is used to hold a table of all properties we care to save.  */
typedef struct
{
  const char *name;		/* Name of property per protocol	*/
  prop_type type;
  gboolean required;		/* TRUE if required (by us, not by the spec). */
  const char *save_name;	/* Name of property in config file */
} propsave;

/* Write current session to the config file. */
void write_session (void);

/* Releases the lock on the session name when shutting down the session */
void unlock_session (void);

/* Returns name of last session run (with a default) */
gchar* get_current_session(void);

/* Set the autosave key in the session-options file */
void set_autosave_mode (gboolean autosave);

/* Set the session_save variable to indicate that an 
 independant session is being saved */
void set_sessionsave_mode (gboolean sessionsave);

/* Set the session name key in the session-options file */
void set_session_name (const gchar *name);

/* Load a session from our configuration by name. */
Session* read_session (const char *name);

/* Starts the clients in a session and frees the session. */
void start_session (Session* session);

/* Frees the memory used by a session. */
void free_session (Session* session);

/* Delete a session from the config file and discard any stale
 * session info saved by clients that were in the session. */
void delete_session (const char *name);

#endif /* SAVE_H */
