/* gsm-multiscreen.h
 *
 * Copyright (C) 2002  Sun Microsystems Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: Mark McLoughlin <mark@skynet.ie>
 */

#ifndef __GSM_MULTISCREEN_H__
#define __GSM_MULTISCREEN_H__

#include <glib/gmacros.h>
#include <gdk/gdk.h>
#include <gtk/gtkwindow.h>

G_BEGIN_DECLS

typedef void (*GsmScreenForeachFunc) (GdkScreen *screen,
				      int        monitor);

void       gsm_foreach_screen             (GsmScreenForeachFunc callback);

GdkScreen *gsm_locate_screen_with_pointer (int       *monitor_ret);

int        gsm_screen_get_width           (GdkScreen *screen,
					   int        monitor);
int        gsm_screen_get_height          (GdkScreen *screen,
					   int        monitor);
int        gsm_screen_get_x               (GdkScreen *screen,
					   int        monitor);
int        gsm_screen_get_y               (GdkScreen *screen,
					   int        monitor);

void       gsm_center_window_on_screen    (GtkWindow *window,
					   GdkScreen *screen,
					   int        monitor);

G_END_DECLS

#endif /* __GSM_MULTISCREEN_H__ */
