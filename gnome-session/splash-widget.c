/* splash-widget.c - splash screen rendering

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
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-macros.h>

#include "headers.h"
#include "splash-widget.h"

GNOME_CLASS_BOILERPLATE (SplashWidget,
			 splash_widget,
			 GObject,
			 GTK_TYPE_WINDOW);

typedef struct {
	char *human_name;
	char *exe;
	char *icon;
} SplashApp;

static const SplashApp splash_map_table[] = {
	{ N_("Audio Settings"),               "gnome-sound-properties",         "gnome-audio2.png" },
	{ N_("Screensaver"),                  "xscreensaver-demo",              "gnome-ccscreensaver.png" },
	{ N_("Sawfish Window Manager"),       "sawfish",                        "gnome-ccwindowmanager.png" },
	{ N_("Metacity Window Manager"),      "metacity",                       "gnome-ccwindowmanager.png" },
	{ N_("Window Manager"),               "gnome-wm",                       "gnome-ccwindowmanager.png" },
	{ N_("Background Settings"),          "gnome-background-properties",    "gnome-ccbackground.png" },
	{ N_("Mouse Settings"),               "gnome-mouse-properties",         "gnome-mouse.png" },
	{ N_("Keyboard Settings"),            "gnome-keyboard-properties",      "gnome-cckeyboard.png" },
	{ N_("The Panel"),                    "gnome-panel",                    "gnome-panel.png" },
	{ N_("Session Manager Proxy"),        "gnome-smproxy",                  "gnome-session.png" },
	{ N_("Nautilus"),                     "nautilus",                       "gnome-ccdesktop.png" },
};

static const SplashApp *
get_splash_app (const char *exe)
{
	int i;

	for (i = 0; i < G_N_ELEMENTS (splash_map_table); i++) {
		if (!strcmp (splash_map_table [i].exe, exe))
			return &splash_map_table [i];
	}

	return NULL;
}

typedef struct {
	GdkRectangle position;
	GdkPixbuf   *unscaled;
	GdkPixbuf   *scaled;
} SplashIcon;

static gboolean
re_scale (SplashWidget *sw)
{
	int i;

	static struct {
		int icon_size;
		int icon_spacing;
		int icon_rows;
	} scales[] = {
		{ SPLASH_BASE_ICON_SIZE,
		  SPLASH_ICON_SPACING,
		  SPLASH_BASE_ICON_ROWS },
		{ 24, 3, 1 },
		{ 24, 3, 2 },
		{ 16, 2, 2 },
		{ 16, 2, 3 },
		{ 12, 1, 3 },
		{ 8, 1, 4 },
		{ 4, 1, 5 },
		{ 4, 1, 4 }
	};

	for (i = 0; i < G_N_ELEMENTS (scales); i++) {
		if (scales [i].icon_size < sw->icon_size ||
		    scales [i].icon_rows > sw->icon_rows) {
			sw->icon_size    = scales [i].icon_size;
			sw->icon_spacing = scales [i].icon_spacing;
			sw->icon_rows    = scales [i].icon_rows;
			break;
		}
	}

	if (i == G_N_ELEMENTS (scales)) {
		g_warning ("Too many inits - overflowing");
		return FALSE;
	} else
		return TRUE;
}

static gboolean
splash_widget_expose_event (GtkWidget      *widget,
			    GdkEventExpose *event)
{
	GList *l;
	GdkRectangle exposed;
	SplashWidget *sw = SPLASH_WIDGET (widget);

	if (!GTK_WIDGET_DRAWABLE (widget))
		return FALSE;

	if (gdk_rectangle_intersect (
		&event->area, &sw->image_bounds, &exposed))
		gdk_draw_drawable  (
			GDK_DRAWABLE (widget->window),
			widget->style->black_gc,
			GDK_DRAWABLE (sw->bg_pixmap),
			exposed.x, exposed.y,
			exposed.x, exposed.y,
			exposed.width, exposed.height);

	for (l = sw->icons; l; l = l->next) {
		SplashIcon *si = l->data;

		if (gdk_rectangle_intersect (&event->area,
					     &si->position,
					     &exposed))
			gdk_pixbuf_render_to_drawable (
				si->scaled, widget->window,
				widget->style->black_gc,
				si->position.x - exposed.x,
				si->position.y - exposed.y,
				exposed.x, exposed.y,
				exposed.width, exposed.height,
				GDK_RGB_DITHER_NORMAL,
				exposed.x, exposed.y);
	}

	if (sw->layout) {
		GdkRectangle text;
		PangoRectangle pixel_rect;

		pango_layout_get_pixel_extents (sw->layout, NULL, &pixel_rect);

		text.x = (sw->text_box.x + sw->text_box.width / 2 - pixel_rect.width / 2);
		text.y = sw->text_box.y;
		text.width = pixel_rect.width;
		text.height = pixel_rect.height;

		if (gdk_rectangle_intersect (&event->area, &text, &exposed)) {
			/* drop shadow */
			gdk_draw_layout (widget->window,
					 widget->style->black_gc,
					 text.x + 1, text.y + 1, sw->layout);
			
			/* text */
			gdk_draw_layout (widget->window,
					 widget->style->white_gc,
					 text.x, text.y, sw->layout);
		}
	}

	return FALSE;
}

static gboolean
splash_widget_button_release_event (GtkWidget      *widget,
				    GdkEventButton *event)
{
	gtk_widget_hide (widget);
	return TRUE;
}

static void
splash_widget_size_allocate (GtkWidget     *widget,
			     GtkAllocation *allocation)
{
	SplashWidget *sw = SPLASH_WIDGET (widget);

	sw->image_bounds = *allocation;

	sw->text_box.x = allocation->x;
	sw->text_box.y = (allocation->y + allocation->height -
			  SPLASH_LABEL_V_OFFSET - SPLASH_LABEL_V_HEIGHT);
	sw->text_box.width = allocation->width;
	sw->text_box.height = allocation->height - sw->text_box.y;

	GNOME_CALL_PARENT (GTK_WIDGET_CLASS, size_allocate, (widget, allocation));
}

static void
splash_widget_size_request (GtkWidget      *widget,
			    GtkRequisition *requisition)
{
	SplashWidget *sw = (SplashWidget *) widget;

	if (!sw->background) {
		requisition->width = SPLASH_BASE_WIDTH;
		requisition->height = SPLASH_BASE_HEIGHT;
	} else {
		requisition->width  = gdk_pixbuf_get_width  (sw->background);
		requisition->height = gdk_pixbuf_get_height (sw->background);
	}
}

static void
splash_widget_realize (GtkWidget *widget)
{
	GdkPixmap *pm;
	SplashWidget *sw = (SplashWidget *) widget;

	GNOME_CALL_PARENT (GTK_WIDGET_CLASS, realize, (widget));

	if (sw->background && widget->window) {
		int width, height;

		width = gdk_pixbuf_get_width  (sw->background);
		height = gdk_pixbuf_get_height (sw->background);

		pm = gdk_pixmap_new (
			widget->window,
			width, height,
			gdk_drawable_get_visual (widget->window)->depth);

		if (pm) {
			gdk_pixbuf_render_to_drawable (
				sw->background, GDK_DRAWABLE (pm),
				widget->style->black_gc,
				0, 0, 0, 0, width, height,
				GDK_RGB_DITHER_NORMAL,
				0, 0);
			
			gdk_window_set_back_pixmap (
				widget->window, pm, FALSE);
			sw->bg_pixmap = pm;
		}
	}
}

static void
splash_widget_unrealize (GtkWidget *widget)
{
	SplashWidget *sw = (SplashWidget *) widget; 

	if (sw->bg_pixmap) {
		g_object_unref (sw->bg_pixmap);
		sw->bg_pixmap = NULL;
	}

	GNOME_CALL_PARENT (GTK_WIDGET_CLASS, unrealize, (widget));
}

static void
splash_icon_destroy (SplashIcon *si)
{
	g_object_unref (si->unscaled);
	if (si->scaled)
		g_object_unref (si->scaled);

	g_free (si);
}

static void
splash_widget_finalize (GObject *object)
{
	SplashWidget *sw = (SplashWidget *) object;

	g_list_foreach (sw->icons, (GFunc) splash_icon_destroy, NULL);
	g_list_free (sw->icons);

	g_object_unref (sw->layout);

	GNOME_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static void
splash_widget_class_init (SplashWidgetClass *klass)
{
	GObjectClass *gobject_class = (GObjectClass *) klass;
	GtkWidgetClass *widget_class = (GtkWidgetClass *) klass;

	gobject_class->finalize = splash_widget_finalize;

	widget_class->realize = splash_widget_realize;
	widget_class->unrealize = splash_widget_unrealize;
	widget_class->expose_event = splash_widget_expose_event;
	widget_class->size_request = splash_widget_size_request;
	widget_class->size_allocate = splash_widget_size_allocate;
	widget_class->button_release_event = splash_widget_button_release_event;
}

static void
read_background (SplashWidget *sw)
{
	char *filename;
	GdkPixbuf *pb;
	int maj, minor, pl;

	/* Find a splash screen by looking at "gnome-splash-major.minor.pl.png",
	   "gnome-splash-major.minor.png" and "gnome-splash.png" */
	if (sscanf (VERSION, "%d.%d.%d", &maj, &minor, &pl) != 3) {
		pl = 0;
		if (sscanf (VERSION, "%d.%d", &maj, &minor) != 2) {
			maj = minor = pl = 0; /* sort of illegal version */
		}
	}
	
	filename = g_strdup_printf (
		"%s/splash/gnome-splash-%d.%d.%d.png",
		GNOME_ICONDIR, maj, minor, pl);

	pb = gdk_pixbuf_new_from_file (filename, NULL);
	g_free (filename);
	if (!pb) {
		filename = g_strdup_printf (
			"%s/splash/gnome-splash-%d.%d.png",
			GNOME_ICONDIR, maj, minor);

		pb = gdk_pixbuf_new_from_file (filename, NULL);
		g_free (filename);
	}
	
	if (!pb)
		pb = gdk_pixbuf_new_from_file (
			GNOME_ICONDIR "/splash/gnome-splash.png", NULL);

	sw->background = pb;

	fprintf (stderr, "Loaded background '%p\n", pb);
}

static void
splash_widget_instance_init (SplashWidget *sw)
{
	GtkWindow *window;
	PangoAttrList *attrs;

	window = &sw->window;

	/* window->type clobbered by default properties on GtkWindow */
	gtk_window_set_position (window, GTK_WIN_POS_CENTER);
	g_object_set (window, "allow_shrink", FALSE,
		      "allow_grow", FALSE, NULL);

	sw->icon_size = SPLASH_BASE_ICON_SIZE;
	sw->icon_spacing = SPLASH_ICON_SPACING;
	sw->cur_y_row = SPLASH_BASE_ICON_ROWS;

	gtk_widget_add_events (GTK_WIDGET (window),
			       GDK_BUTTON_RELEASE_MASK);

	sw->layout = gtk_widget_create_pango_layout (GTK_WIDGET (sw), "");
	pango_layout_set_alignment (sw->layout, PANGO_ALIGN_CENTER);

	attrs = pango_attr_list_new ();
	pango_attr_list_insert (attrs,
				pango_attr_size_new (SPLASH_LABEL_FONT_SIZE));
	pango_layout_set_attributes (sw->layout, attrs);
	pango_attr_list_unref (attrs);

	read_background (sw);
}

static GdkPixbuf *
get_splash_icon (SplashWidget *sw, const char *icon_name)
{
	char *fname;
	GdkPixbuf *pb;

	fname = g_build_filename (GNOME_ICONDIR, icon_name, NULL);

	if (g_file_test (fname, G_FILE_TEST_EXISTS))
		pb = gdk_pixbuf_new_from_file (fname, NULL);
	else
		pb = NULL;

	g_free (fname);

	return pb;
}

static void re_laydown (SplashWidget *sw);

static void
layout_icon (SplashWidget *sw, SplashIcon *si, GdkRectangle *area)
{
	g_return_if_fail (si != NULL);

	si->position.x = sw->image_bounds.x + sw->cur_x_offset + SPLASH_ICON_BORDER;
	si->position.y = (sw->image_bounds.y + sw->image_bounds.height -
			  SPLASH_ICON_V_OFFSET -
			  (sw->icon_size + sw->icon_spacing) * sw->cur_y_row);
	si->position.width = si->position.height = sw->icon_size;

	sw->cur_x_offset += sw->icon_size + sw->icon_spacing;

	if (area)
		*area = si->position;

	if (!si->scaled) {
		if (gdk_pixbuf_get_width (si->unscaled) == sw->icon_size &&
		    gdk_pixbuf_get_height (si->unscaled) == sw->icon_size)
			si->scaled = g_object_ref (si->unscaled);
		else
			si->scaled = gdk_pixbuf_scale_simple (
				si->unscaled, sw->icon_size,
				sw->icon_size, GDK_INTERP_BILINEAR);
	}

	if (sw->cur_x_offset >= (sw->image_bounds.width - SPLASH_ICON_BORDER * 2 -
				 sw->icon_size)) {
		if (--sw->cur_y_row > 0)
			sw->cur_x_offset = 0;

		else {
			if (re_scale (sw)) {
				re_laydown (sw);
				gtk_widget_queue_draw (GTK_WIDGET (sw));
			}
		}
	}
}

static void
re_laydown (SplashWidget *sw)
{
	GList *l;

	sw->cur_x_offset = 0;
	sw->cur_y_row = sw->icon_rows;

	for (l = sw->icons; l; l = l->next) {
		SplashIcon *si = l->data;

		if (si->scaled) {
			g_object_unref (si->scaled);
			si->scaled = NULL;
		}
		layout_icon (sw, l->data, NULL);
	}
}
 
void
splash_widget_add_icon (SplashWidget *sw,
			const char   *executable_name)
{
	char *text;
	char *basename;
	GdkPixbuf *pb;
	const SplashApp *app;

	g_return_if_fail (SPLASH_IS_WIDGET (sw));

	if (!executable_name || executable_name [0] == '\0')
		return;

	basename = g_path_get_basename (executable_name);

	pb = NULL;
	text = NULL;
	app = get_splash_app (basename);

	if (app) {
		pb = get_splash_icon (sw, app->icon);
		text = _(app->human_name);
	}

	/* FIXME: we should allow arbitrary apps to install
	   stuff here, by mangling the app name to a pixmap
	   name */

	if (!pb)
		pb = get_splash_icon (sw, "gnome-unknown.png");

	if (!text)
		text = basename;

	pango_layout_set_text (sw->layout, text, -1);

	if (pb) {
		SplashIcon *si;
		GdkRectangle area;

		si = g_new0 (SplashIcon, 1);
		si->unscaled = pb;

		sw->icons = g_list_append (sw->icons, si);

		layout_icon (sw, si, &area);
		
		gtk_widget_queue_draw_area (
			GTK_WIDGET (sw),
			area.x, area.y,
			area.width, area.height);
	}

	gtk_widget_queue_draw_area (
		GTK_WIDGET (sw),
		sw->text_box.x, sw->text_box.y,
		sw->text_box.width, sw->text_box.height);
}

static SplashWidget *global_splash = NULL;

void
splash_start (void)
{
	if (global_splash)
		return;

	global_splash = g_object_new (SPLASH_TYPE_WIDGET,
				      "type", GTK_WINDOW_POPUP, NULL);
	gtk_widget_show_now (GTK_WIDGET (global_splash));
}

void
splash_update (const gchar *text)
{
	if (!global_splash || !text || !text[0])
		return;

	if (!strcmp (text, "rm"))
		return;

	if (!strcmp (text, "done")) {
		splash_stop ();
		return;
	}

	splash_widget_add_icon (global_splash, text);
}

void
splash_stop (void)
{
	if (global_splash) {
		gtk_widget_destroy (GTK_WIDGET (global_splash));
		global_splash = NULL;
	}
}

