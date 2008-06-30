/* session.h - Information shared between gnome-session and other programs.

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

#ifndef SESSION_H
#define SESSION_H

/* THE GSM VENDOR:
 * This is the value that gnome-session returns when SmcVendor is called */
#define GsmVendor                "GnomeSM"

/* THE GSM STARTUP PRIORITY:
 * SmCARD8 value between 0 and 99 giving priority level in which
 * the client is started up (as in a SysV start up). */
#define GsmPriority              "_GSM_Priority"

/* Default priority.  */
#define DEFAULT_PRIORITY 50

/* THE RESTART SERVICE STRING
 * type=SmLISTofARRAY8,
 * The format is "remote_start_protocol/remote_start_data".  An
 * example is "rstart-rsh/hostname".  This is a non-standard property,
 * which is the whole reason we need this function in order to determine
 * the restart method.  The proxy uses this property to over-ride the
 * 'client_host_name' from the ICE connection (the proxy is connected to
 * the SM via a local connection, but the proxy may be acting as a proxy
 * for a remote client).
 * This property was first introduced by xsm.
 */
#define GsmRestartService	"_XC_RestartService"

/* THE GSM CLIENT EVENT PROTOCOL:
 * Invoke the GsmSelectClientEvents command (see below) and then you can 
 * retrieve the stream of events occuring to gnome-session clients using 
 * repeated calls to SmcGetProperties (do not nest this call in its callback).
 * A client event is a property with 
 * name=GsmClientEvent, 
 * type=SmListofARRAY8,
 * val[0].value=event name from list below,
 * val[1].value=handle (an unique identifier for each client)  */
#define GsmClientEvent           "_GSM_ClientEvent"

/* Event names: */
#define GsmConnected             "Connected" /* connected to SM */
#define GsmInactive              "Inactive"  /* not yet started or finished */
#define GsmSave                  "Save"      /* save started */
#define GsmStart                 "Start"     /* being started */
#define GsmUnknown               "Unknown"   /* slow (or unable) to connect */
#define GsmRemove                "Remove"    /* exit from session */
#define GsmProperty              "Property"  /* change to properties */
/* The GsmProperty event is followed by the new values of the changed 
 * properties for the client with the specified handle */
#define GsmReasons               "Reasons"   /* explanation for next event */
/* These are sent when gnome-session has information about why a client
 * is being removed or might need to be removed from the session: 
 * val[2].value=confirm = "0" removed by gnome-session automatically.
 *                      = "1" removed iff a GsmRemoveClient is sent in reply.
 * val[3].value, val[4].value, ...=the reasons(array of strings). */

/* THE GSM COMMAND PROTOCOL:
 * Call SmcSetProperty with the FIRST property having:
 * name=GsmCommand, 
 * type=SmLISTofARRAY8,
 * val[0].value=a command name from the list below,
 * val[1].value, val[2].value, ...=the command arguments. 
 * Properties specified AFTER the first property are usually IGNORED.
 * However, the GsmChangeProperties and GsmAddSilentClient commands treat
 * them as properties to set on specified clients. */
#define GsmCommand               "_GSM_Command"

/* Command names: */
#define GsmSuspend               "Suspend"  
/* Suspends the GsmCommand protocol extension. Any other GsmCommand restores 
 * its operation: */

#define GsmSelectClientEvents    "SelectClientEvents"
/* Selects events occuring to gsm clients */

#define GsmDeselectClientEvents  "DeselectClientEvents"
/* Deselects events occuring to gsm clients */

#define GsmHandleWarnings        "HandleWarnings"
/* This flags that you are able to handle the warning messages that
 * gnome-session sometimes needs to generate when clients are unresponsive.
 * gnome-session will rely on ONE of the clients that sets this flag to handle
 * the warnings. Only send this command when your warning handler works!!! */

#define GsmListClients           "ListClients"
/* Returns the clients that are currently running as an array of events. 
 * Each client is represented by the last event that it generated (ignoring 
 * any GsmProperty events). */

#define GsmListProperties        "ListProperties"
/* Returns the properties for the client with the given GsmClientEvent handle.
 * Client handles obtained from GsmListClients, a selected client event 
 * or a session which has been started expire when the client is removed 
 * from the current session. Client handles obtained from a GsmReadSession
 * expire when GsmFreeSession is called (unless the session is started). 
 * Returns a GsmProperty event listing all the properties. */

#define GsmAddClient             "AddClient"
/* Attempts to adds the client to the current session. Returns a (bogus) 
 * GsmAddClient command which has the new client handle as an argument and 
 * simultaneoulsy generates a GsmUnstarted event. 
 * The remaining properties in the SmcSetProperties call give initial 
 * properties for the new client and generate a GsmProperty event. You must
 * specify a SmRestartCommand (which should not specify a client id).
 *
 * gnome-session executes the restart command and then waits for a client
 * to connect with an SmProgram property matching the executed argv[0].
 * This matching technique is less accurate than matching on client ids but
 * it is impracticable to use client ids when ADDING clients. */

#define GsmRemoveClient          "RemoveClient"
/* Forcibly removes a client from the current session using an SmDie 
 * message on connected clients and the SmResignCommand on disconnected 
 * or silent clients. Any SmDiscardCommand will be called when the session 
 * is next saved. */

#define GsmClearClientWarning    "ClearClientWarning"
/* Instructs gnome-session that a GsmReasons event with confirm=TRUE has been
 * displayed to the user and any action chosen by the user has been taken.
 * gnome-session suspends further warnings on the client until this command 
 * has been received. */

#define GsmChangeProperties      "ChangeProperties"
/* Changes some properties for the client with the given GsmClientEvent handle.
 * The remaining properties in the SmcSetProperties call give the changed 
 * properties for the client. This generates a GsmProperty event. */

#define GsmListSessions          "ListSessions"
/* Returns an array of GsmReadSession commands to read the clients in each
 * of the sessions that gnome-session has saved in the past. */ 

#define GsmGetCurrentSession	 "GetCurrentSession"
/* Returns a GsmCurrentSession command which has the name of the 
 * session which gnome-session would load with no command line arguments and
 * no chooser as its only argument. */

#define GsmSetSessionName        "SetSessionName"
/* Changes the name of the current session. When the requested session name
 * in in use then a unique suffix will be added. When no session name is
 * specified then just returns the current name.
 * Returns the GsmSetSessionName command that was applied to set the
 * current session name. */

#define GsmReadSession           "ReadSession"
/* Returns the clients that would be loaded under the named session.
 * The first property returned is the GsmStartSession command needed to start 
 * the session clients. The second property is the GsmFreeSession Command 
 * which frees the gnome-session memory used to store an unstarted session.
 * The remaining properties comprise one GsmUnstarted event per client.
 * NB: Calling GsmReadSession on the name obtained using GsmSetSessionName
 * will return the SAVED details of the session which will not match the 
 * current details returned by GsmListClients */

#define GsmStartSession          "StartSession"
/* The opaque command returned by GsmReadSession to start the session.
 * The clients in the session retain their handles (see GsmClientEvents). */

#define GsmFreeSession           "FreeSession"
/* The opaque command returned by GsmReadSession to free the session.
 * Sessions are also freed when GsmStartSession is called or when the client
 * which read the session disconnects or dies. */

#define GsmAutoSaveMode		"AutoSaveMode"
/* Set the autosave mode of the session manager. */

#define GsmSessionSaveMode	"SessionSaveMode"
/* Set the sessionsave mode of the session manager. */

#endif /* SESSION_H */
