/* $XConsortium: smproxy.c,v 1.29 95/04/04 15:17:40 mor Exp $ */
/******************************************************************************

Copyright (c) 1994  X Consortium

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
X CONSORTIUM BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of the X Consortium shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from the X Consortium.

Author:  Ralph Mor, X Consortium
******************************************************************************/

#include "smproxy.h"

/* This is probably nonportable.  But then so is the call to
   XmuClientWindow below.  */
#include <X11/Xmu/WinUtil.h>
#include <libgnome/libgnome.h>
#include <unistd.h>

XtAppContext appContext;
Display *disp;

Atom wmProtocolsAtom;
Atom wmSaveYourselfAtom;
Atom wmStateAtom;
Atom smClientIdAtom;
Atom wmClientLeaderAtom;

Bool debug = 0;

SmcConn proxy_smcConn;
XtInputId proxy_iceInputId;
char *proxy_clientId = NULL;

WinInfo *win_head = NULL;

int proxy_count = 0;
int die_count = 0;

Bool shutting_down = 0;

Bool caught_error = 0;

Bool sent_save_done = 0;

int Argc;
char **Argv;



static Bool LookupWindow (Window window, WinInfo **ptr_ret,
			  WinInfo **prev_ptr_ret);


static Bool
HasSaveYourself (window)

Window window;

{
    Atom *protocols;
    int numProtocols;
    int i, found;

    protocols = NULL;

    if (XGetWMProtocols (disp, window, &protocols, &numProtocols) != True)
	return (False);

    found = 0;

    if (protocols != NULL)
    {
	for (i = 0; i < numProtocols; i++)
	    if (protocols[i] == wmSaveYourselfAtom)
		found = 1;

	XFree (protocols);
    }

    return (found);
}



static Bool
HasXSMPsupport (window)

Window window;

{
    XTextProperty tp;
    Bool hasIt = 0;

    if (XGetTextProperty (disp, window, &tp, smClientIdAtom))
    {
	if (tp.encoding == XA_STRING && tp.format == 8 && tp.nitems != 0)
	    hasIt = 1;

	if (tp.value)
	    XFree ((char *) tp.value);
    }

    return (hasIt);
}



static WinInfo *
GetClientLeader (winptr)

WinInfo *winptr;

{
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytesafter;
    unsigned long *datap = NULL;
    WinInfo *leader_winptr = NULL;
    Bool failure = 0;

    if (XGetWindowProperty (disp, winptr->window, wmClientLeaderAtom,
	0L, 1L, False, AnyPropertyType,	&actual_type, &actual_format,
	&nitems, &bytesafter, (unsigned char **) &datap) == Success)
    {
	if (actual_type == XA_WINDOW && actual_format == 32 &&
	    nitems == 1 && bytesafter == 0)
	{
	    Window leader_win = *((Window *) datap);

	    if (!LookupWindow (leader_win, &leader_winptr, NULL))
		failure = 1;
	}

	if (datap)
	    XFree (datap);
    }

    if (failure)
    {
	/* The client leader was defined, but we couldn't find the window */

	return (NULL);
    }
    else if (leader_winptr)
    {
	/* We found the real client leader */

	return (leader_winptr);
    }
    else
    {
	/* There is no client leader defined, return this window */

	return (winptr);
    }
}



static char *
CheckFullyQuantifiedName (name, newstring)

char *name;
int *newstring;

{
    /*
     * Due to a bug in Xlib (for hpux in particular), some clients
     * will have a WM_CLIENT_MACHINE that is not fully quantified.
     * For example, we might get "excon" instead of "excon.x.org".
     * This really stinks.  The best we can do is tag on our own
     * domain name.
     */

    if (strchr (name, '.') != NULL)
    {
	*newstring = 0;
	return (name);
    }
    else
    {
	char hostnamebuf[80];
	char *firstDot;

	gethostname (hostnamebuf, sizeof hostnamebuf);
	firstDot = strchr (hostnamebuf, '.');

	if (!firstDot)
	{
	    *newstring = 0;
	    return (name);
	}
	else
	{
	    int bytes = strlen (name) + strlen (firstDot + 1) + 2;
	    char *newptr;

	    newptr = (char *) malloc (bytes);
	    sprintf (newptr, "%s.%s", name, firstDot + 1);

	    *newstring = 1;
	    return (newptr);
	}
    }
}



static void FinishSaveYourself (winInfo, has_WM_SAVEYOURSELF)

WinInfo *winInfo;
Bool has_WM_SAVEYOURSELF;

{
    SmProp prop1, prop2, prop3, *props[3];
    SmPropValue prop1val, prop2val, prop3val;
    int i;

    if (!winInfo->got_first_save_yourself)
    {
	char userId[20], restartService[80];
	char *fullyQuantifiedName;
	int newstring;

	prop1.name = SmProgram;
	prop1.type = SmARRAY8;
	prop1.num_vals = 1;
	prop1.vals = &prop1val;
	prop1val.value = (SmPointer) winInfo->wm_command[0];
	prop1val.length = strlen (winInfo->wm_command[0]);
    
	sprintf (userId, "%d", getuid());
	prop2.name = SmUserID;
	prop2.type = SmARRAY8;
	prop2.num_vals = 1;
	prop2.vals = &prop2val;
	prop2val.value = (SmPointer) userId;
	prop2val.length = strlen (userId);
    
	fullyQuantifiedName = CheckFullyQuantifiedName (
	    (char *) winInfo->wm_client_machine.value, &newstring);
	sprintf (restartService, "rstart-rsh/%s", fullyQuantifiedName);
	if (newstring)
	    free (fullyQuantifiedName);

	prop3.name = "_XC_RestartService";
	prop3.type = SmLISTofARRAY8;
	prop3.num_vals = 1;
	prop3.vals = &prop3val;
	prop3val.value = (SmPointer) restartService;
	prop3val.length = strlen (restartService);

	props[0] = &prop1;
	props[1] = &prop2;
	props[2] = &prop3;
	
	SmcSetProperties (winInfo->smc_conn, 3, props);

	winInfo->got_first_save_yourself = 1;
    }

    prop1.name = SmRestartCommand;
    prop1.type = SmLISTofARRAY8;
    prop1.num_vals = winInfo->wm_command_count;
    
    prop1.vals = (SmPropValue *) malloc (
	winInfo->wm_command_count * sizeof (SmPropValue));
    
    if (!prop1.vals)
    {
	SmcSaveYourselfDone (winInfo->smc_conn, False);
	return;
    }

    for (i = 0; i < winInfo->wm_command_count; i++)
    {
	prop1.vals[i].value = (SmPointer) winInfo->wm_command[i];
	prop1.vals[i].length = strlen (winInfo->wm_command[i]);
    }
    
    prop2.name = SmCloneCommand;
    prop2.type = SmLISTofARRAY8;
    prop2.num_vals = winInfo->wm_command_count;
    prop2.vals = prop1.vals;
    
    props[0] = &prop1;
    props[1] = &prop2;
    
    SmcSetProperties (winInfo->smc_conn, 2, props);
    
    free ((char *) prop1.vals);
    
    /*
     * If the client doesn't support WM_SAVE_YOURSELF, we should
     * return failure for the save, since we really don't know if
     * the application needed to save state.
     */

    SmcSaveYourselfDone (winInfo->smc_conn, has_WM_SAVEYOURSELF);
}



static void
SaveYourselfCB (smcConn, clientData, saveType, shutdown, interactStyle, fast)

SmcConn smcConn;
SmPointer clientData;
int saveType;
Bool shutdown;
int interactStyle;
Bool fast;

{
    WinInfo *winInfo = (WinInfo *) clientData;

    if (!winInfo->has_save_yourself)
    {
	FinishSaveYourself (winInfo, False);
    }
    else
    {
	XClientMessageEvent saveYourselfMessage;


	/* Send WM_SAVE_YOURSELF */

	saveYourselfMessage.type = ClientMessage;
	saveYourselfMessage.window = winInfo->window;
	saveYourselfMessage.message_type = wmProtocolsAtom;
	saveYourselfMessage.format = 32;
	saveYourselfMessage.data.l[0] = wmSaveYourselfAtom;
	saveYourselfMessage.data.l[1] = CurrentTime;

	if (XSendEvent (disp, winInfo->window, False, NoEventMask,
	    (XEvent *) &saveYourselfMessage))
	{
	    winInfo->waiting_for_update = 1;

	    if (debug)
	    {
		printf ("Sent SAVE YOURSELF to 0x%x\n", (unsigned int) winInfo->window);
		printf ("\n");
	    }
	}
	else
	{
	    if (debug)
	    {
		printf ("Failed to send SAVE YOURSELF to 0x%x\n",
		    (unsigned int) winInfo->window);
		printf ("\n");
	    }
	}
    }
}



static void
DieCB (smcConn, clientData)

SmcConn smcConn;
SmPointer clientData;

{
    WinInfo *winInfo = (WinInfo *) clientData;

    SmcCloseConnection (winInfo->smc_conn, 0, NULL);
    winInfo->smc_conn = NULL;
    XtRemoveInput (winInfo->input_id);

    /* Now tell the client to die */

    if (debug)
	printf ("Trying to kill 0x%x\n", (unsigned int) winInfo->window);

    XSync (disp, 0);
    XKillClient (disp, winInfo->window);
    XSync (disp, 0);


    /*
     * Proxy must exit when all clients die, and the proxy itself
     * must have received a Die.
     */

    die_count++;

    if (die_count == proxy_count + 1)
    {
	exit (0);
    }
}



static void
SaveCompleteCB (smcConn, clientData)

SmcConn smcConn;
SmPointer clientData;

{
    /*
     * Nothing to do here.
     */
}



static void
ShutdownCancelledCB (smcConn, clientData)

SmcConn smcConn;
SmPointer clientData;

{
    /*
     * Since we did not request to interact or request save yourself
     * phase 2, we know we already sent the save yourself done, so
     * there is nothing to do here.
     */
}



static void
ProcessIceMsgProc (client_data, source, id)

XtPointer	client_data;
int 		*source;
XtInputId	*id;

{
    IceConn	ice_conn = (IceConn) client_data;

    IceProcessMessages (ice_conn, NULL, NULL);
}



static void
NullIceErrorHandler (iceConn, swap,
    offendingMinorOpcode, offendingSequence, errorClass, severity, values)

IceConn		iceConn;
Bool		swap;
int		offendingMinorOpcode;
unsigned long	offendingSequence;
int 		errorClass;
int		severity;
IcePointer	values;

{
    return;
}


static void
ConnectClientToSM (winInfo)

WinInfo *winInfo;

{
    char errorMsg[256];
    unsigned long mask;
    SmcCallbacks callbacks;
    IceConn ice_conn;
    char *prevId;

    mask = SmcSaveYourselfProcMask | SmcDieProcMask |
	SmcSaveCompleteProcMask | SmcShutdownCancelledProcMask;

    callbacks.save_yourself.callback = SaveYourselfCB;
    callbacks.save_yourself.client_data = (SmPointer) winInfo;

    callbacks.die.callback = DieCB;
    callbacks.die.client_data = (SmPointer) winInfo;

    callbacks.save_complete.callback = SaveCompleteCB;
    callbacks.save_complete.client_data = (SmPointer) winInfo;

    callbacks.shutdown_cancelled.callback = ShutdownCancelledCB;
    callbacks.shutdown_cancelled.client_data = (SmPointer) winInfo;

    prevId = LookupClientID (winInfo);

    /*
     * In case a protocol error occurs when opening the connection,
     * (e.g. an authentication error), we set a null error handler
     * before the open, then restore the default handler after the open.
     */

    IceSetErrorHandler (NullIceErrorHandler);

    winInfo->smc_conn = SmcOpenConnection (
	NULL, 			/* use SESSION_MANAGER env */
	(SmPointer) winInfo,	/* force a new connection */
	SmProtoMajor,
	SmProtoMinor,
	mask,
	&callbacks,
	prevId,
	&winInfo->client_id,
	256, errorMsg);

    IceSetErrorHandler (NULL);

    if (winInfo->smc_conn == NULL)
	return;

    ice_conn = SmcGetIceConnection (winInfo->smc_conn);

    winInfo->input_id = XtAppAddInput (
	    appContext,
	    IceConnectionNumber (ice_conn),
            (XtPointer) XtInputReadMask,
	    ProcessIceMsgProc,
	    (XtPointer) ice_conn);

    XChangeProperty(disp, winInfo->window,
                   smClientIdAtom, XA_STRING, 8, PropModeReplace,
                   winInfo->client_id, strlen(winInfo->client_id) + 1);

    if (debug)
    {
	printf ("Connected to SM, window = 0x%x\n", (unsigned int) winInfo->window);
	printf ("\n");
    }

    proxy_count++;
}



static int
MyErrorHandler (display, event)

Display *display;
XErrorEvent *event;

{
    caught_error = 1;
    /* Placate compiler.  */
    return 0;
}



Bool
LookupWindow (window, ptr_ret, prev_ptr_ret)

Window window;
WinInfo **ptr_ret;
WinInfo **prev_ptr_ret;

{
    WinInfo *ptr, *prev;

    ptr = win_head;
    prev = NULL;

    while (ptr)
    {
	if (ptr->window == window)
	    break;
	else
	{
	    prev = ptr;
	    ptr = ptr->next;
	}
    }

    if (ptr)
    {
	if (ptr_ret)
	    *ptr_ret = ptr;
	if (prev_ptr_ret)
	    *prev_ptr_ret = prev;
	return (1);
    }
    else
	return (0);
}



static WinInfo *
AddNewWindow (window)

Window window;

{
    WinInfo *newptr;

    if (LookupWindow (window, NULL, NULL))
	return (NULL);

    newptr = (WinInfo *) malloc (sizeof (WinInfo));

    if (newptr == NULL)
	return (NULL);

    newptr->next = win_head;
    win_head = newptr;

    newptr->window = window;
    newptr->smc_conn = NULL;
    newptr->tested_for_sm_client_id = 0;
    newptr->client_id = NULL;
    newptr->wm_command = NULL;
    newptr->wm_command_count = 0;
    newptr->class.res_name = NULL;
    newptr->class.res_class = NULL;
    newptr->wm_name = NULL;
    newptr->wm_client_machine.value = NULL;
    newptr->wm_client_machine.nitems = 0;
    newptr->has_save_yourself = 0;
    newptr->waiting_for_update = 0;
    newptr->got_first_save_yourself = 0;

    return (newptr);
}



static void
RemoveWindow (winptr)

WinInfo *winptr;

{
    WinInfo *ptr, *prev;

    if (LookupWindow (winptr->window, &ptr, &prev))
    {
	if (prev == NULL)
	    win_head = ptr->next;
	else
	    prev->next = ptr->next;

	if (ptr->client_id)
	    free (ptr->client_id);
	
	if (ptr->wm_command)
	    XFreeStringList (ptr->wm_command);
	
	if (ptr->wm_name)
	    XFree (ptr->wm_name);
	
	if (ptr->wm_client_machine.value)
	    XFree (ptr->wm_client_machine.value);
	
	if (ptr->class.res_name)
	    XFree (ptr->class.res_name);
	
	if (ptr->class.res_class)
	    XFree (ptr->class.res_class);
	
	free ((char *) ptr);
    }
}



static void
Got_WM_STATE (winptr)

WinInfo *winptr;

{
    WinInfo *leader_winptr;

    /*
     * If we already got WM_STATE and tested for SM_CLIENT_ID, we
     * shouldn't do it again.
     */

    if (winptr->tested_for_sm_client_id)
    {
	return;
    }


    /*
     * Set a null error handler, in case this window goes away
     * behind our back.
     */

    caught_error = 0;
    XSetErrorHandler (MyErrorHandler);


    /*
     * Get the client leader window.
     */

    leader_winptr = GetClientLeader (winptr);

    if (caught_error)
    {
	caught_error = 0;
	RemoveWindow (winptr);
	XSetErrorHandler (NULL);
	return;
    }


    /*
     * If we already checked for SM_CLIENT_ID on the client leader
     * window, don't do it again.
     */

    if (!leader_winptr || leader_winptr->tested_for_sm_client_id)
    {
	caught_error = 0;
	XSetErrorHandler (NULL);
	return;
    }

    leader_winptr->tested_for_sm_client_id = 1;

    if (!HasXSMPsupport (leader_winptr->window))
    {
	XFetchName (disp, leader_winptr->window, &leader_winptr->wm_name);

	XGetCommand (disp, leader_winptr->window,
	    &leader_winptr->wm_command,
	    &leader_winptr->wm_command_count);

	XGetClassHint (disp, leader_winptr->window, &leader_winptr->class);

	XGetWMClientMachine (disp, leader_winptr->window,
	    &leader_winptr->wm_client_machine);

	if (leader_winptr->wm_name != NULL &&
	    leader_winptr->wm_command != NULL &&
	    leader_winptr->wm_command_count > 0 &&
	    leader_winptr->class.res_name != NULL &&
	    leader_winptr->class.res_class != NULL &&
	    leader_winptr->wm_client_machine.value != NULL &&
	    leader_winptr->wm_client_machine.nitems != 0)
	{
	    leader_winptr->has_save_yourself =
		HasSaveYourself (leader_winptr->window);

	    ConnectClientToSM (leader_winptr);
	}
    }

    XSync (disp, 0);
    XSetErrorHandler (NULL);

    if (caught_error)
    {
	caught_error = 0;
	RemoveWindow (leader_winptr);
    }
}



static void
HandleCreate (event)

XCreateWindowEvent *event;

{
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytesafter;
    unsigned long *datap = NULL;
    WinInfo *winptr;
    Bool got_wm_state = 0;

    /*
     * We are waiting for all proxy connections to close so we can die.
     * Don't handle new connections.
     */

    if (shutting_down)
	return;


    /*
     * Add the new window
     */

    if ((winptr = AddNewWindow (event->window)) == NULL)
	return;


    /*
     * Right after the window was created, it might have been destroyed,
     * so the following Xlib calls might fail.  Need to catch the error
     * by installing an error handler.
     */

    caught_error = 0;
    XSetErrorHandler (MyErrorHandler);


    /*
     * Select for Property Notify on the window so we can determine
     * when WM_STATE is defined.  To avoid a race condition, we must
     * do this _before_ we check for WM_STATE right now.
     *
     * Select for Substructure Notify so we can determine when the
     * window is destroyed.
     */

    XSelectInput (disp, event->window,
	StructureNotifyMask | PropertyChangeMask);	


    /*
     * WM_STATE may already be there.  Check now.
     */

    if (XGetWindowProperty (disp, event->window, wmStateAtom,
	0L, 2L, False, AnyPropertyType,
	&actual_type, &actual_format, &nitems, &bytesafter,
	(unsigned char **) &datap) == Success && datap)
    {
	if (nitems > 0)
	    got_wm_state = 1;

	if (datap)
	    XFree ((char *) datap);
    }

    XSync (disp, 0);
    XSetErrorHandler (NULL);

    if (caught_error)
    {
	caught_error = 0;
	RemoveWindow (winptr);
    }
    else if (got_wm_state)
    {
	Got_WM_STATE (winptr);
    }
}



static void
HandleDestroy (event)

XDestroyWindowEvent *event;

{
    WinInfo *winptr;

    if (LookupWindow (event->window, &winptr, NULL))
    {
	if (winptr->smc_conn)
	{
	    SmcCloseConnection (winptr->smc_conn, 0, NULL);
	    XtRemoveInput (winptr->input_id);
	    proxy_count--;
	}

	if (debug)
	{
	    printf ("Removed window (window = 0x%x)\n", (unsigned int) winptr->window);
	    printf ("\n");
	}

	RemoveWindow (winptr);
    }
}



static void
HandleUpdate (event)

XPropertyEvent *event;

{
    Window window = event->window;
    WinInfo *winptr;

    if (!LookupWindow (window, &winptr, NULL))
	return;

    if (event->atom == wmStateAtom)
    {
	Got_WM_STATE (winptr);
    }
    else if (event->atom == XA_WM_COMMAND && winptr->waiting_for_update)
    {
	/* Finish off the Save Yourself */

        char ** argv;
        int argc;

      	/* Some particularly horrid clients (mozilla!) sometimes 
	   delete the value of the WM_COMMAND property rather 
	   than changing it. The only feasible response that
           I can see to this behavior is too ignore that the
	   WM_COMMAND has been changed at all... */

	if (XGetCommand (disp, window, &argv, &argc) &&
	    argv && argc > 0)
	{
	    if (winptr->wm_command)
	      XFreeStringList (winptr->wm_command);

	    winptr->wm_command = argv;
	    winptr->wm_command_count = argc;
	}

	winptr->waiting_for_update = 0;
	FinishSaveYourself (winptr, True);
    }
}



static void
ProxySaveYourselfPhase2CB (smcConn, clientData)

SmcConn smcConn;
SmPointer clientData;

{
    char *filename;
    char *prefix, *ps;
    Bool success = True;
    SmProp prop1, prop2, prop3, prop4, *props[4];
    SmPropValue prop1val, prop2val, prop3val, prop4val;
    char discardCommand[80];
    int numVals, i;
    static int first_time = 1;

    if (first_time)
    {
	char userId[20];
	char hint = SmRestartImmediately;
	char priority = (char)0;

	prop1.name = SmProgram;
	prop1.type = SmARRAY8;
	prop1.num_vals = 1;
	prop1.vals = &prop1val;
	prop1val.value = Argv[0];
	prop1val.length = strlen (Argv[0]);

	sprintf (userId, "%d", getuid());
	prop2.name = SmUserID;
	prop2.type = SmARRAY8;
	prop2.num_vals = 1;
	prop2.vals = &prop2val;
	prop2val.value = (SmPointer) userId;
	prop2val.length = strlen (userId);
	
	prop3.name = SmRestartStyleHint;
	prop3.type = SmCARD8;
	prop3.num_vals = 1;
	prop3.vals = &prop3val;
	prop3val.value = (SmPointer) &hint;
	prop3val.length = 1;
	
	prop4.name = "_GSM_Priority";
	prop4.type = SmCARD8;
	prop4.num_vals = 1;
	prop4.vals = &prop4val;
	prop4val.value = (SmPointer) &priority;
	prop4val.length = 1;

	props[0] = &prop1;
	props[1] = &prop2;
	props[2] = &prop3;
	props[3] = &prop4;

	SmcSetProperties (smcConn, 4, props);

	first_time = 0;
    }

    if ((filename = WriteProxyFile ()) == NULL)
    {
	success = False;
	goto finishUp;
    }

    prop1.name = SmRestartCommand;
    prop1.type = SmLISTofARRAY8;

    prop1.vals = (SmPropValue *) malloc (
	(Argc + 4) * sizeof (SmPropValue));

    if (!prop1.vals)
    {
	success = False;
	goto finishUp;
    }

    numVals = 0;

    for (i = 0; i < Argc; i++)
    {
	if (strcmp (Argv[i], "--sm-client-id") == 0 ||
	    strcmp (Argv[i], "--sm-config-prefix") == 0 ||
	    strcmp (Argv[i], "-clientId") == 0 ||
	    strcmp (Argv[i], "-restore") == 0)
	{
	    i++;
	}
	else
	{
	    prop1.vals[numVals].value = (SmPointer) Argv[i];
	    prop1.vals[numVals++].length = strlen (Argv[i]);
	}
    }

    prop1.vals[numVals].value = (SmPointer) "--sm-config-prefix";
    prop1.vals[numVals++].length = 19;

    ps = strrchr (filename, '/');
    prefix = (char*)malloc (strlen (ps)+2);
    prefix = strcat (strcpy (prefix, ps), "/");

    prop1.vals[numVals].value = (SmPointer) prefix;
    prop1.vals[numVals++].length = strlen (prefix);

    prop1.vals[numVals].value = (SmPointer) "--sm-client-id";
    prop1.vals[numVals++].length = 14;

    prop1.vals[numVals].value = (SmPointer) proxy_clientId;
    prop1.vals[numVals++].length = strlen (proxy_clientId);

    prop1.num_vals = numVals;

    prop2.name = SmCloneCommand;
    prop2.type = SmLISTofARRAY8;
    prop2.vals = prop1.vals;
    prop2.num_vals = prop1.num_vals - 2;

    prop3.name = SmDiscardCommand;
    prop3.type = SmLISTofARRAY8;
    prop3.vals = (SmPropValue *) malloc (2 * sizeof (SmPropValue));

    if (!prop3.vals)
    {
	success = False;
	goto finishUp;
    }

    prop3.vals[0].value = (SmPointer) "rm";
    prop3.vals[0].length = 2;

    prop3.vals[1].value = (SmPointer) filename;
    prop3.vals[1].length = strlen (filename);

    prop3.num_vals = 2;

    props[0] = &prop1;
    props[1] = &prop2;
    props[2] = &prop3;

    SmcSetProperties (smcConn, 3, props);
    free ((char *) prop1.vals);
    free (prefix);

 finishUp:

    SmcSaveYourselfDone (smcConn, success);
    sent_save_done = 1;

    if (filename)
	free (filename);
}



static void
ProxySaveYourselfCB (smcConn, clientData, saveType,
    shutdown, interactStyle, fast)

SmcConn smcConn;
SmPointer clientData;
int saveType;
Bool shutdown;
int interactStyle;
Bool fast;

{
    /*
     * We want the proxy to respond to the Save Yourself after all
     * the regular XSMP clients have finished with the save (and possibly
     * interacted with the user).
     */

    shutting_down = shutdown;

    if (!SmcRequestSaveYourselfPhase2 (smcConn,
	ProxySaveYourselfPhase2CB, NULL))
    {
	SmcSaveYourselfDone (smcConn, False);
	sent_save_done = 1;
    }
    else
	sent_save_done = 0;
}



static void
ProxyDieCB (smcConn, clientData)

SmcConn smcConn;
SmPointer clientData;

{
    SmcCloseConnection (proxy_smcConn, 0, NULL);
    XtRemoveInput (proxy_iceInputId);

    die_count++;

    if (! shutting_down || die_count == proxy_count + 1)
	exit (0);
}



static void
ProxySaveCompleteCB (smcConn, clientData)

SmcConn smcConn;
SmPointer clientData;

{
    ;
}



static void
ProxyShutdownCancelledCB (smcConn, clientData)

SmcConn smcConn;
SmPointer clientData;

{
    shutting_down = 0;

    if (!sent_save_done)
    {
	SmcSaveYourselfDone (smcConn, False);
	sent_save_done = 1;
    }
}



static Status
ConnectProxyToSM (previous_id)

char *previous_id;

{
    char errorMsg[256];
    unsigned long mask;
    SmcCallbacks callbacks;
    IceConn iceConn;

    mask = SmcSaveYourselfProcMask | SmcDieProcMask |
	SmcSaveCompleteProcMask | SmcShutdownCancelledProcMask;

    callbacks.save_yourself.callback = ProxySaveYourselfCB;
    callbacks.save_yourself.client_data = (SmPointer) NULL;

    callbacks.die.callback = ProxyDieCB;
    callbacks.die.client_data = (SmPointer) NULL;

    callbacks.save_complete.callback = ProxySaveCompleteCB;
    callbacks.save_complete.client_data = (SmPointer) NULL;

    callbacks.shutdown_cancelled.callback = ProxyShutdownCancelledCB;
    callbacks.shutdown_cancelled.client_data = (SmPointer) NULL;

    proxy_smcConn = SmcOpenConnection (
	NULL, 			/* use SESSION_MANAGER env */
	(SmPointer) appContext,
	SmProtoMajor,
	SmProtoMinor,
	mask,
	&callbacks,
	previous_id,
	&proxy_clientId,
	256, errorMsg);

    if (proxy_smcConn == NULL)
	return (0);

    iceConn = SmcGetIceConnection (proxy_smcConn);

    proxy_iceInputId = XtAppAddInput (
	    appContext,
	    IceConnectionNumber (iceConn),
            (XtPointer) XtInputReadMask,
	    ProcessIceMsgProc,
	    (XtPointer) iceConn);

    return (1);
}



static void
CheckForExistingWindows (root)

Window root;

{
    Window dontCare1, dontCare2, *children, client_window;
    unsigned int nchildren, i;
    XCreateWindowEvent event;

    /*
     * We query the root tree for all windows created thus far.
     * Note that at any moment after XQueryTree is called, a
     * window may be deleted.  So we must take extra care to make
     * sure a window really exists.
     */

    XQueryTree (disp, root, &dontCare1, &dontCare2, &children, &nchildren);

    for (i = 0; i < nchildren; i++)
    {
	event.window = children[i];

	HandleCreate (&event);

	caught_error = 0;
	XSetErrorHandler (MyErrorHandler);

	client_window = XmuClientWindow (disp, children[i]);

	XSetErrorHandler (NULL);

	if (!caught_error && client_window != children[i])
	{
	    event.window = client_window;
	    HandleCreate (&event);
	}
    }
}


int
main (argc, argv)

int argc;
char **argv;

{
    char *restore_filename = NULL;
    char *client_id = NULL;
    int i, zero = 0;
    Atom gnomeSmProxyAtom; 
    Window gnomeSmProxyWindow;
    Atom r_type;
    int r_format;
    unsigned long r_count, r_remaining;
    unsigned char *r_prop, *r_prop2;

    Argc = argc;
    Argv = argv;

    for (i = 1; i < argc; i++)
    {
      if (!strcmp("--sm-client-id", argv[i]) ||
	  !strcmp("-clientId", argv[i]))
	{
	  if (++i >= argc) goto usage;
	  client_id = argv[i];
	  continue;
	}
      else if (!strcmp("--sm-config-prefix", argv[i]))
	{
	  char* path;
	  int must_free_path = 0;
	  
	  if (++i >= argc) goto usage;

	  path = getenv ("SM_SAVE_DIR");
	  if (!path){
	    path = gnome_util_home_file (NULL);
	    must_free_path = 1;
	  }
	  if (!path)
	    path = getenv ("HOME");
	  if (!path)
	    path = ".";
	  
	  restore_filename = malloc (strlen(path)+strlen(argv[i])+1);
	  restore_filename = strcat (strcpy (restore_filename, path),argv[i]); 
	  restore_filename[strlen (restore_filename) - 1] ='\0';
	  if (must_free_path)
	    free (path);
	  continue;
	}
      else if(!strcmp("-restore", argv[i]))
	{
	  if (++i >= argc) goto usage;
	  restore_filename = argv[i];
	  continue;
	}
      else if (!strcmp("--debug", argv[i]))
	{
	  debug = 1;
	  continue;
	}
      
    usage:

	fprintf (stderr, "usage:  %s [--sm-config-prefix prefix] [--sm-client-id id] [--debug]\n", argv[0]);
	exit (1);
    }


    XtToolkitInitialize ();
    appContext = XtCreateApplicationContext ();

    if (!(disp = XtOpenDisplay (appContext, NULL, "SM-PROXY", "SM-PROXY",
	NULL, 0, &zero, NULL)))
    {
	fprintf (stderr, "%s: unable to open display %s\n", 
		 argv[0], XDisplayName(NULL));
	exit (1);
    }

    gnomeSmProxyAtom = XInternAtom (disp, "GNOME_SM_PROXY", False);
    gnomeSmProxyWindow = XCreateSimpleWindow (disp, RootWindow (disp, 0), 
					      10, 10, 10, 10, 0, 0, 0);
    XChangeProperty (disp, gnomeSmProxyWindow, gnomeSmProxyAtom, 
		     XA_CARDINAL, 32, PropModeReplace, 
		     (guchar*)&gnomeSmProxyWindow, 1); 
    XChangeProperty (disp, RootWindow (disp, 0), gnomeSmProxyAtom, 
		     XA_CARDINAL, 32, PropModeAppend, 
		     (guchar*)&gnomeSmProxyWindow, 1); 

    while (XGetWindowProperty(disp, RootWindow (disp, 0), gnomeSmProxyAtom, 
			   0, 100, False, XA_CARDINAL, 
			   &r_type, &r_format, &r_count, &r_remaining, 
			   &r_prop) != Success ||
	!r_prop || r_type != XA_CARDINAL || r_format != 32) 
      {
	/* Some joker has set the GNOME_SM_PROXY property to another format.
	 * We grab it back for use by gnome-smproxy. */
	XChangeProperty (disp, RootWindow (disp, 0), gnomeSmProxyAtom, 
			 XA_CARDINAL, 32, PropModeReplace, 
			 (guchar*)&gnomeSmProxyWindow, 1); 
      }

    /* Avoid choking on X errors when testing stale locks */
    XSetErrorHandler (MyErrorHandler);      
    for (i = 0; i < r_count; i++)
      {
	Window lockWin = ((Window *)r_prop)[i];

	/* Do we hold the lock ? */
	if (gnomeSmProxyWindow == lockWin)
	  break;

    	/* Only exit when the lock is still current */
	caught_error = 0;
	if (XGetWindowProperty(disp, lockWin, gnomeSmProxyAtom, 
			       0, 1, False, XA_CARDINAL, 
			       &r_type, &r_format, &r_count, &r_remaining, 
			       &r_prop2) == Success && ! caught_error &&
	    r_prop2 && r_type == XA_CARDINAL && r_format == 32)
	  {
	    if(lockWin == *(Window *)r_prop2)
	      {
		fprintf (stderr, "%s: already running on display %s\n", 
			 argv[0], DisplayString (disp));
		exit(1);
	      }
	    XFree (r_prop2);
	  }

      }
    XFree(r_prop);
    XSetErrorHandler (NULL);

    /* Clean up the lock property */
    XChangeProperty (disp, RootWindow (disp, 0), gnomeSmProxyAtom, 
		     XA_CARDINAL, 32, PropModeReplace, 
		     (guchar*)&gnomeSmProxyWindow, 1);

    if (restore_filename)
	ReadProxyFile (restore_filename);

    if (!ConnectProxyToSM (client_id))
    {
	fprintf (stderr, "%s: unable to connect to session manager\n", 
		 argv[0]);
	exit (1);
    }

    wmProtocolsAtom = XInternAtom (disp, "WM_PROTOCOLS", False);
    wmSaveYourselfAtom = XInternAtom (disp, "WM_SAVE_YOURSELF", False);
    wmStateAtom = XInternAtom (disp, "WM_STATE", False);
    smClientIdAtom = XInternAtom (disp, "SM_CLIENT_ID", False);
    wmClientLeaderAtom = XInternAtom (disp, "WM_CLIENT_LEADER", False);

    for (i = 0; i < ScreenCount (disp); i++)
    {
	Window root = RootWindow (disp, i);
	XSelectInput (disp, root, SubstructureNotifyMask | PropertyChangeMask);
	CheckForExistingWindows (root);
    }

    while (1)
    {
	XEvent event;

	XtAppNextEvent (appContext, &event);

	switch (event.type)
	{
	case CreateNotify:
	    HandleCreate (&event.xcreatewindow);
	    break;

	case DestroyNotify:
	    HandleDestroy (&event.xdestroywindow);
	    break;

	case PropertyNotify:
	    HandleUpdate (&event.xproperty);
	    break;

	default:
	    XtDispatchEvent (&event);
	    break;
	}
    }

    return 0;
}
