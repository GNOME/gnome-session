/*
 * gnome-libice-check.c: crash if libice is broken
 *
 * Based on a small program by:
 * Andy Reitz <areitz@uiuc.edu> 
 *
 * Adapted from `gnome-core-1.2.1/gsm/ice.c',
 * the original license of which follows:
 *
 ****************************************************************************
 * ice.c - Handle session manager/ICE integration.
 *
 * Copyright (C) 1998, 1999 Tom Tromey
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <stdlib.h>

#include <X11/ICE/ICElib.h>
#include <X11/ICE/ICEutil.h>

#include <sys/stat.h>
#include <unistd.h>
#include <string.h>


int main (void)
{
	IceListenObj *sockets;
	int num_sockets;
	char error[256];

	IceListenForConnections (&num_sockets, &sockets,
				 sizeof(error), error);

	return 0;
}
