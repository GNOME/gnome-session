#include <config.h>

#include <gtk/gtk.h>
#include <libgnomeui/gnome-ui-init.h>

#define ICE_H
#define ice_frozen()
#define ice_thawed()

#define COMMAND_H
static void set_logout_command (char **command) {}

static gboolean autosave = FALSE;
static gboolean save_selected = FALSE;
static gboolean logout_prompt = TRUE;

#include "logout.c"

int
main (int argc, char *argv[])
{
	gnome_program_init ("logout-test", "0.1",
			    LIBGNOMEUI_MODULE,
			    argc, argv, NULL);

	maybe_display_gui ();

	return 0;
}
