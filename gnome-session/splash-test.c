#include <config.h>

#include <splash.h>

#include <gtk/gtkmain.h>
#include <libgnomeui/gnome-ui-init.h>

#define LAST_SPLASH 7

static gboolean
time_cb (gpointer data)
{
	static int i = 0;
	char *s;

	g_print ("(splash)\n");

	switch (i) {
	case LAST_SPLASH:
		stop_splash ();
		gtk_main_quit ();
		return FALSE;
	case 0:
		start_splash (LAST_SPLASH);
		/* FALL THROUGH */
	default:
		s = g_strdup_printf ("%d", i);
		update_splash (s, (gfloat)i);
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

	g_timeout_add (1000, time_cb, NULL);

	gtk_main ();

	return 0;
}
