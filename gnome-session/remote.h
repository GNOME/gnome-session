/* $XConsortium: remote.h,v 1.15 95/01/03 17:26:52 mor Exp $ */
/******************************************************************************

Copyright (c) 1993, 1999  X Consortium

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
******************************************************************************/


#ifndef REMOTE_H
#define REMOTE_H

#include "headers.h"

/* Name of protocol we understand.  */
#define RSTART_RSH "rstart-rsh"

/* For now, we only support rsh.  FIXME.  */
#define RSHCMD "rsh"

/* Try to start a program remotely, if appropriate, else locally.  */
gint remote_start (char *restart_info, int argc, char **argv,
                   char *cwd, int envpc, char **envp);

#endif /* REMOTE_H */
