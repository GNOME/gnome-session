/* session.h - Information shared between gnome-session and other programs.

   Copyright (C) 1998 Tom Tromey

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

#ifndef SESSION_H
#define SESSION_H

/* Name of config prefix used to store `preload' information.  A
   preload is a program that is (typically) not session-aware, and
   which is started when the session is started.  FIXME: really we
   should store preloads on a per-session basis.  */
#define PRELOAD_PREFIX "/session/preload/"

/* Name of key used to store number of preloads.  */
#define PRELOAD_COUNT_KEY "num_preloads"

#endif /* SESSION_H */
