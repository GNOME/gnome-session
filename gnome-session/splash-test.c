#include <config.h>

#include <splash-widget.h>

#include <gtk/gtkmain.h>
#include <libgnomeui/gnome-ui-init.h>

#define LAST_SPLASH 45

static gboolean
time_cb (gpointer data)
{
	static int i = 0;
	char *s;

	g_print ("(splash)\n");

	switch (i) {
	case LAST_SPLASH:
		splash_stop ();
		gtk_main_quit ();
		return FALSE;
	case 0:
		splash_start ();
		/* FALL THROUGH */
	case 1:
		splash_update ("metacity");
		break;
	case 2:
		splash_update ("gnome-wm");
		break;
	case 3:
		splash_update ("gnome-panel");
		break;

	default:
		s = g_strdup_printf ("Item %d", i);
		splash_update (s);
		g_free (s);
		break;
	}

	i++;

	return TRUE;
}

int
main (int argc, char *argv[])
{
	gnome_program_init ("splash-test", "0.1",
			    LIBGNOMEUI_MODULE,
			    argc, argv, NULL);

	g_timeout_add (500, time_cb, NULL);

	gtk_main ();

	return 0;
}
