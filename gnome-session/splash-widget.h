/* splash-widget.h - splash screen rendering

   Copyright (C) 1999-2002 Jacob Berkman, Ximian Inc.

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
*/
#ifndef SPLASH_WIDGET_H
#define SPLASH_WIDGET_H

#include <gtk/gtkwindow.h>

#define SPLASH_TYPE_WIDGET                  (splash_widget_get_type ())
#define SPLASH_WIDGET(obj)                  (GTK_CHECK_CAST ((obj), SPLASH_TYPE_WIDGET, SplashWidget))
#define SPLASH_WIDGET_CLASS(klass)          (GTK_CHECK_CLASS_CAST ((klass), SPLASH_TYPE_WIDGET, SplashWidgetClass))
#define SPLASH_IS_WIDGET(obj)               (GTK_CHECK_TYPE ((obj), SPLASH_TYPE_WIDGET))
#define SPLASH_IS_WIDGET_CLASS(klass)       (GTK_CHECK_CLASS_TYPE ((klass), SPLASH_TYPE_WIDGET))
#define SPLASH_WIDGET_GET_CLASS(obj)        (GTK_CHECK_GET_CLASS ((obj), SPLASH_TYPE_WIDGET, SplashWidgetClass))

typedef struct  {
	GtkWindow    window;

	GdkPixbuf   *background;
	GdkPixmap   *bg_pixmap;
	GList       *icons;
	PangoLayout *layout;

	/* current placement offsets */
	int          cur_x_offset;
	int          cur_y_row;

	/* Layout Measurements */
	int           icon_size;
	int           icon_spacing;
	int           icon_rows;
	GdkRectangle  image_bounds;
	GdkRectangle  text_box;
} SplashWidget;

typedef struct {
	GtkWindowClass parent_class;
} SplashWidgetClass;

GType splash_widget_get_type (void);
void  splash_widget_add_icon (SplashWidget *sw,
			      const char   *executable_name);

/* width / height if we have no image */
#define SPLASH_BASE_WIDTH 480
#define SPLASH_BASE_HEIGHT 220

/* offset from bottom of label & font */
#define SPLASH_LABEL_V_OFFSET 3
#define SPLASH_LABEL_FONT_SIZE 8

/* icon border, spacing, offset from bottom and initial size */
#define SPLASH_ICON_BORDER  8
#define SPLASH_ICON_SPACING 4
#define SPLASH_ICON_V_OFFSET 14
#define SPLASH_BASE_ICON_SIZE 36
#define SPLASH_BASE_ICON_ROWS 1

/* The global API */
void splash_start  (void);
void splash_update (const gchar *text);
void splash_stop   (void);

#endif /* SPLASH_WIDGET_H */
