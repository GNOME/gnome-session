/* gsm-multiscreen.c
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

#include <config.h>

#include <gdk/gdk.h>
#include <gdk/gdkx.h>

#include "gsm-multiscreen.h"

void
gsm_foreach_screen (GsmScreenForeachFunc callback)
{
	GdkDisplay *display;
	int         n_screens, i;

	display = gdk_display_get_default ();

	n_screens = gdk_display_get_n_screens (display);
	for (i = 0; i < n_screens; i++) {
		GdkScreen *screen;
		int        n_monitors, j;

		screen = gdk_display_get_screen (display, i);

		n_monitors = gdk_screen_get_n_monitors (screen);
		for (j = 0; j < n_monitors; j++)
			callback (screen, j);
	}
}

static gboolean
gsm_screen_contains_pointer (GdkScreen *screen,
			     int       *x,
			     int       *y)
{
	GdkWindow    *root_window;
	Window        root, child;
	Bool          retval;
	int           rootx, rooty;
	int           winx, winy;
	unsigned int  xmask;

	root_window = gdk_screen_get_root_window (screen);

	retval = XQueryPointer (gdk_display,
				gdk_x11_drawable_get_xid (GDK_DRAWABLE (root_window)),
				&root, &child, &rootx, &rooty, &winx, &winy, &xmask);

	if (x)
		*x = retval ? rootx : -1;
	if (y)
		*y = retval ? rooty : -1;

	return retval;
}

GdkScreen *
gsm_locate_screen_with_pointer (int *monitor_ret)
{
	GdkDisplay *display;
	int         n_screens, i;

	display = gdk_display_get_default ();

	n_screens = gdk_display_get_n_screens (display);
	for (i = 0; i < n_screens; i++) {
		GdkScreen  *screen;
		int         x, y;

		screen = gdk_display_get_screen (display, i);

		if (gsm_screen_contains_pointer (screen, &x, &y)) {
			if (monitor_ret)
				*monitor_ret = gdk_screen_get_monitor_at_point (screen, x, y);

			return screen;
		}
	}

	if (monitor_ret)
		*monitor_ret = 0;

	return NULL;
}

int
gsm_screen_get_width (GdkScreen *screen,
		      int        monitor)
{
	GdkRectangle geometry;

	gdk_screen_get_monitor_geometry (screen, monitor, &geometry);

	return geometry.width;
}

int
gsm_screen_get_height (GdkScreen *screen,
		       int        monitor)
{
	GdkRectangle geometry;

	gdk_screen_get_monitor_geometry (screen, monitor, &geometry);

	return geometry.height;
}

int
gsm_screen_get_x (GdkScreen *screen,
		  int        monitor)
{
	GdkRectangle geometry;

	gdk_screen_get_monitor_geometry (screen, monitor, &geometry);

	return geometry.x;
}

int
gsm_screen_get_y (GdkScreen *screen,
		  int        monitor)
{
	GdkRectangle geometry;

	gdk_screen_get_monitor_geometry (screen, monitor, &geometry);

	return geometry.y;
}

void
gsm_center_window_on_screen (GtkWindow *window,
			     GdkScreen *screen,
			     int        monitor)
{
	GtkRequisition requisition;
	GdkRectangle   geometry;
	int            x, y;

	gdk_screen_get_monitor_geometry (screen, monitor, &geometry);
	gtk_widget_size_request (GTK_WIDGET (window), &requisition);

	x = geometry.x + (geometry.width - requisition.width) / 2;
	y = geometry.y + (geometry.height - requisition.height) / 2;

	gtk_window_move (window, x, y);
}
