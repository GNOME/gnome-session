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
#include <glib/gi18n.h>
#include <gtk/gtkicontheme.h>
#include <libgnome/gnome-program.h>
#include <gconf/gconf-client.h>

#include "headers.h"
#include "splash-widget.h"

G_DEFINE_TYPE (SplashWidget,
               splash_widget,
               GTK_TYPE_WINDOW);

static gboolean update_trans_effect (gpointer);

typedef struct {
	const char *human_name;
	const char *exe;
	const char *icon;
} SplashApp;

static const SplashApp splash_map_table[] = {
	{ N_("Sawfish Window Manager"),  "sawfish",               "gnome-window-manager" },
	{ N_("Metacity Window Manager"), "metacity",              "gnome-window-manager" },
	{ N_("Window Manager"),          "gnome-wm",              "gnome-window-manager" },
	{ N_("The Panel"),               "gnome-panel",           "gnome-panel" },
	{ N_("Nautilus"),                "nautilus",              "gnome-fs-desktop" },
	{ N_("Desktop Settings"),        "gnome-settings-daemon", "gnome-settings" }
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

typedef struct _TransParam TransParam;

typedef struct {
	GdkRectangle position;
	GdkPixbuf   *unscaled;
	GdkPixbuf   *scaled;
	GdkPixbuf   *scaled_copy;
	gint	     trans_count;
	TransParam  *transformation;
} SplashIcon;

struct _TransParam {
	SplashWidget *sw;
	int width;
	int height;
	int n_channels;
	int rowstride_trans;
	int rowstride_orig;
	guchar *pixels_trans;
	guchar *pixels_orig;
};

static void prepare_trans_effect (SplashIcon *si);
static void update_icon_with_effect (SplashIcon *si);

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

static void
calc_text_box (SplashWidget *sw)
{
	PangoRectangle pixel_rect;
	
	pango_layout_get_pixel_extents (sw->layout, NULL, &pixel_rect);
	
	sw->text_box.x = (sw->image_bounds.x + sw->image_bounds.width / 2 -
			  pixel_rect.width / 2);
	sw->text_box.y = (sw->image_bounds.y + sw->image_bounds.height -
			  pixel_rect.height - SPLASH_LABEL_V_OFFSET);
	sw->text_box.width = pixel_rect.width + 1;
	sw->text_box.height = pixel_rect.height + 1;
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

	for (l = sw->icons; l; l = l->next) {
		SplashIcon *si = l->data;

		if (gdk_rectangle_intersect (&event->area,
					     &si->position,
					     &exposed))
			gdk_draw_pixbuf (
			        widget->window,
				widget->style->black_gc,
				si->scaled,
				exposed.x - si->position.x,
				exposed.y - si->position.y,
				exposed.x, exposed.y,
				exposed.width, exposed.height,
				GDK_RGB_DITHER_MAX,
				exposed.x, exposed.y);
	}

	if (sw->layout) {
		calc_text_box (sw);
		if (gdk_rectangle_intersect (&event->area, &sw->text_box, &exposed)) {
			/* drop shadow */
			gdk_draw_layout (widget->window,
					 widget->style->black_gc,
					 sw->text_box.x + 1, sw->text_box.y + 1,
					 sw->layout);
			
			/* text */
			gdk_draw_layout (widget->window,
					 widget->style->white_gc,
					 sw->text_box.x, sw->text_box.y,
					 sw->layout);
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

	GTK_WIDGET_CLASS (splash_widget_parent_class)->size_allocate (widget, allocation);
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
	SplashWidget *sw = (SplashWidget *) widget;

	GTK_WIDGET_CLASS (splash_widget_parent_class)->realize (widget);

	if (sw->background && widget->window) {
		GdkPixmap *pixmap;
		GdkBitmap *mask;
		GdkColormap *colormap;

		pixmap = NULL;
		mask = NULL;

		colormap = gtk_widget_get_colormap (widget);
		pixmap = gdk_pixmap_new (widget->window, 
					 gdk_pixbuf_get_width (sw->background),
					 gdk_pixbuf_get_height (sw->background),
					 -1);

		if (pixmap) {
			GtkStyle *style;

			/* we want dither_max for 16-bits people */
			gdk_draw_pixbuf (pixmap, NULL, sw->background,
					 0, 0, 0, 0, -1, -1,
					 GDK_RGB_DITHER_MAX, 0, 0);

			style = gtk_style_copy (widget->style);
			style->bg_pixmap[GTK_STATE_NORMAL] = pixmap;

			gtk_widget_set_style (widget, style);
			g_object_unref (style);

			gdk_pixbuf_render_pixmap_and_mask_for_colormap (sw->background,
									colormap,
									NULL,
									&mask,
									125);

			if (mask) {
				gdk_window_shape_combine_mask (widget->window,
							       mask, 0, 0);

				g_object_unref (mask);
			}

			gtk_style_set_background (widget->style, widget->window,
						  GTK_STATE_NORMAL);
		}
	}
}

static void
splash_icon_destroy (SplashIcon *si)
{
	g_object_unref (si->unscaled);
	if (si->scaled)
		g_object_unref (si->scaled);
	if (si->scaled_copy)
		g_object_unref (si->scaled_copy);
	if (si->transformation)
		g_free (si->transformation);
	
	g_free (si);
}

static void
splash_widget_finalize (GObject *object)
{
	SplashWidget *sw = (SplashWidget *) object;

	g_list_foreach (sw->icons, (GFunc) splash_icon_destroy, NULL);
	g_list_free (sw->icons);

	if (sw->background)
		g_object_unref (sw->background);
	sw->background = NULL;

	g_object_unref (sw->layout);
	sw->layout = NULL;

	G_OBJECT_CLASS (splash_widget_parent_class)->finalize (object);
}

static void
splash_widget_class_init (SplashWidgetClass *klass)
{
	GObjectClass *gobject_class = (GObjectClass *) klass;
	GtkWidgetClass *widget_class = (GtkWidgetClass *) klass;

	gobject_class->finalize = splash_widget_finalize;

	widget_class->realize = splash_widget_realize;
	widget_class->expose_event = splash_widget_expose_event;
	widget_class->size_request = splash_widget_size_request;
	widget_class->size_allocate = splash_widget_size_allocate;
	widget_class->button_release_event = splash_widget_button_release_event;
}

static GdkPixbuf *
load_background_pixbuf (const char *filename)
{
	GdkPixbuf *retval;
	GError    *error = NULL;
	char      *path;

	if (!filename)
		return NULL;

	path = gnome_program_locate_file (NULL,
					  GNOME_FILE_DOMAIN_PIXMAP,
					  filename,
					  TRUE,
					  NULL);
	if (!path)
		return NULL;

	retval = gdk_pixbuf_new_from_file (path, &error);
	if (!retval) {
		g_warning ("Failed to load image from '%s': %s\n",
			   path, error->message);
		g_free (path);
		g_error_free (error);
		return NULL;
	}

	g_free (path);

	return retval;
}

static void
load_background (SplashWidget *sw)
{
	GConfClient *client;
	char        *filename;

	client = gconf_client_get_default ();

	filename = gconf_client_get_string (client,
					    "/apps/gnome-session/options/splash_image",
					    NULL);

	g_object_unref (client);

	sw->background = load_background_pixbuf (filename);
	g_free (filename);

	if (!sw->background)
		sw->background = load_background_pixbuf ("splash/gnome-splash.png");
}

static void
splash_widget_init (SplashWidget *sw)
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
	sw->font_size_attr = pango_attr_size_new (PANGO_SCALE * SPLASH_LABEL_FONT_SIZE);
	sw->font_size_attr->start_index = 0;
	sw->font_size_attr->end_index = 0;
	pango_attr_list_insert (attrs, sw->font_size_attr);
	pango_layout_set_attributes (sw->layout, attrs);
	pango_attr_list_unref (attrs);

	load_background (sw);
}

static GdkPixbuf *
get_splash_icon (SplashWidget *sw, const char *icon_name)
{
	GdkScreen *screen;
	GtkIconTheme *icon_theme;
	GdkPixbuf *pb;
	char *icon_no_extension;
	char *p;
	char *fname;

	icon_no_extension = g_strdup (icon_name);
	p = strrchr (icon_no_extension, '.');
	if (p &&
	    (strcmp (p, ".png") == 0 ||
	     strcmp (p, ".xpm") == 0 ||
	     strcmp (p, ".svg") == 0)) {
		*p = 0;
	}

        screen = gtk_widget_get_screen (GTK_WIDGET (sw));
        icon_theme = gtk_icon_theme_get_for_screen (screen);

        pb = gtk_icon_theme_load_icon (icon_theme,
                                       icon_no_extension,
                                       48, /* icon size */
                                       0, NULL);
	g_free (icon_no_extension);

	if (pb == NULL) {
		fname = g_build_filename (GNOME_ICONDIR, icon_name, NULL);
		pb = gdk_pixbuf_new_from_file (fname, NULL);
		g_free (fname);
	}
	
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

	if (gtk_widget_get_direction (GTK_WIDGET (sw)) == GTK_TEXT_DIR_RTL)
		si->position.x = GTK_WIDGET (sw)->allocation.width - si->position.x - si->position.width;

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

		layout_icon (sw, si, NULL);

		prepare_trans_effect (si);
	}
}

static void
prepare_trans_effect (SplashIcon *si)
{
	if (si->scaled_copy) {
		g_object_unref (si->scaled_copy);
		si->scaled_copy = NULL;
	}

	if ((si->trans_count < TRANS_TIMEOUT_PERIOD) &&
	    (gdk_pixbuf_get_colorspace (si->scaled) == GDK_COLORSPACE_RGB) &&
	    (gdk_pixbuf_get_bits_per_sample (si->scaled) == 8) &&
	    (gdk_pixbuf_get_n_channels (si->scaled))) {
		if (!gdk_pixbuf_get_has_alpha (si->scaled)) {
			si->scaled_copy = gdk_pixbuf_add_alpha (si->scaled, FALSE, 0, 0, 0);
			g_object_unref (si->scaled);
			si->scaled = gdk_pixbuf_copy (si->scaled_copy);
		} else 
			si->scaled_copy = gdk_pixbuf_copy (si->scaled);

		si->transformation->width  = gdk_pixbuf_get_width  (si->scaled);
		si->transformation->height = gdk_pixbuf_get_height (si->scaled);
		si->transformation->rowstride_trans = gdk_pixbuf_get_rowstride (si->scaled);
		si->transformation->rowstride_orig  = gdk_pixbuf_get_rowstride (si->scaled_copy);
		si->transformation->pixels_trans = gdk_pixbuf_get_pixels (si->scaled);
		si->transformation->pixels_orig  = gdk_pixbuf_get_pixels (si->scaled_copy);
		si->transformation->n_channels = gdk_pixbuf_get_n_channels (si->scaled);

		if (si->trans_count < 0)
			si->trans_count = 0;
		else
			update_icon_with_effect (si);
	} else
		si->trans_count = TRANS_TIMEOUT_PERIOD;
}

void
splash_widget_add_icon (SplashWidget *sw,
			const char   *executable_name)
{
	const SplashApp *app;
	char            *basename;
	char            *text;
	int              length;
	GdkPixbuf       *pb = NULL;

	g_return_if_fail (SPLASH_IS_WIDGET (sw));

	if (!executable_name || executable_name [0] == '\0')
		return;

	/* FIXME: we should allow arbitrary apps to install
         * stuff here, by passing the icon name to the SM
         * as a session property
         */

	basename = g_path_get_basename (executable_name);
	app = get_splash_app (basename);

	if (app)
		pb = get_splash_icon (sw, app->icon);

	if (!pb)
		pb = get_splash_icon (sw, basename);

	text = app && app->human_name ?  _(app->human_name) : basename;

	/* re-draw the old text extents */
	gtk_widget_queue_draw_area (GTK_WIDGET (sw),
				    sw->text_box.x, sw->text_box.y,
				    sw->text_box.width, sw->text_box.height);

	length = strlen (text);
	sw->font_size_attr->end_index = length;
	pango_layout_set_text (sw->layout, text, length);
	calc_text_box (sw);

	/* re-draw the new text extents */
	gtk_widget_queue_draw_area (GTK_WIDGET (sw),
				    sw->text_box.x, sw->text_box.y,
				    sw->text_box.width, sw->text_box.height);

	g_free (basename);

	if (pb) {
		SplashIcon   *si;
		GdkRectangle  area;

		si = g_new0 (SplashIcon, 1);
		si->unscaled = pb;
		si->transformation = g_new0(TransParam, 1);
		si->transformation->sw = sw;
		si->trans_count = -1;

		sw->icons = g_list_append (sw->icons, si);

		layout_icon (sw, si, &area);

		if (si->trans_count == -1)
			prepare_trans_effect (si);

		/* don't do this in prepare_trans_effect(), because
		 * prepare_trans_effect() might be called twice if re_laydown()
		 * is called twice (in case we have to resize the icon twice).
		 * And if this happens, then the copy of the scaled icon will
		 * be empty too because of the second call to
		 * prepare_trans_effect(), which will result in an
		 * all-transparent icon */
		gdk_pixbuf_fill (si->scaled, 0x00000000);

		if (si->trans_count < TRANS_TIMEOUT_PERIOD)
			g_timeout_add (TRANS_TIMEOUT, update_trans_effect, si);
		else
			gtk_widget_queue_draw_area (GTK_WIDGET (sw),
						    area.x, area.y,
						    area.width, area.height);
	}
}

static SplashWidget *global_splash = NULL;

static void
update_icon_with_effect (SplashIcon *si)
{
	guchar     *p_trans;
	guchar     *p_orig;
	gdouble    r_mul, g_mul, b_mul, a_mul;
	gint       x = 0;
	gint	   y = 0;

	a_mul = (gdouble) si->trans_count / TRANS_TIMEOUT_PERIOD;
	r_mul = 1;
	g_mul = 1;
	b_mul = 1;

	for (y = 0; y < si->transformation->height; y++) {
		for (x = 0; x < si->transformation->width; x++) {
			p_trans = si->transformation->pixels_trans + y * si->transformation->rowstride_trans + x * si->transformation->n_channels;
			p_orig  = si->transformation->pixels_orig  + y * si->transformation->rowstride_orig  + x * si->transformation->n_channels;

			/* we can add more effects here apart from alpha fading */
			p_trans[0] = r_mul * p_orig[0];
			p_trans[1] = g_mul * p_orig[1];
			p_trans[2] = b_mul * p_orig[2];
			p_trans[3] = a_mul * p_orig[3];
		}
	}
}

static gboolean
update_trans_effect (gpointer splash_icon)
{
	SplashIcon *si = (SplashIcon *) splash_icon;

	if (!global_splash)
		return FALSE;
	
	if (si->trans_count++ >= TRANS_TIMEOUT_PERIOD) {
		gtk_widget_queue_draw_area (GTK_WIDGET (si->transformation->sw),
					    si->position.x, si->position.y,
					    si->position.width, si->position.height);
		return FALSE;
	}

	update_icon_with_effect (si);

	gtk_widget_queue_draw_area (GTK_WIDGET (si->transformation->sw),
				    si->position.x, si->position.y,
				    si->position.width, si->position.height);
	return TRUE;
}

void
splash_start (void)
{
	if (global_splash)
		return;

	global_splash = g_object_new (SPLASH_TYPE_WIDGET,
                                      "type_hint",
                                      GDK_WINDOW_TYPE_HINT_SPLASHSCREEN,
                                      NULL);
        gtk_window_set_decorated (GTK_WINDOW (global_splash), FALSE);
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

