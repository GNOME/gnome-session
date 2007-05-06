#include <config.h>

#include "gsm-sound.h"

#include <signal.h>
#include <unistd.h>
#ifdef HAVE_ESD
#include <esd.h>
#endif
#include "util.h"

#include <libgnome/gnome-sound.h>
#include <libgnome/gnome-util.h>
#include <libgnome/gnome-triggers.h>

#include <libgnome/gnome-config.h>

#define ENABLE_SOUND_KEY        "/desktop/gnome/sound/enable_esd"
#define ENABLE_EVENT_SOUNDS_KEY "/desktop/gnome/sound/event_sounds"

static gboolean
sound_enabled (void)
{
  GConfClient *client;
  GError      *error = NULL;
  gboolean     retval;

  client = gsm_get_conf_client ();

  retval = gconf_client_get_bool (client, ENABLE_SOUND_KEY, &error);
  if (error)
    {
      g_warning ("Error getting value of " ENABLE_SOUND_KEY ": %s", error->message);
      g_error_free (error);
      return FALSE;  /* Fallback value */
    }

  return retval;
}

static gboolean
sound_events_enabled (void)
{
  GConfClient *client;
  GError      *error = NULL;
  gboolean     retval;

  client = gsm_get_conf_client ();

  retval = gconf_client_get_bool (client, ENABLE_EVENT_SOUNDS_KEY, &error);
  if (error)
    {
      g_warning ("Error getting value of " ENABLE_EVENT_SOUNDS_KEY ": %s", error->message);
      g_error_free (error);
      return FALSE;  /* Fallback value */
    }

  return retval;
}

#ifdef HAVE_ESD
static GPid esd_pid = 0;

static void
reset_esd_pid (GPid     pid,
	       gint     status,
	       gpointer ignore)
{
  if (pid == esd_pid)
    esd_pid = 0;
}

static void
start_esd (void) 
{
  gchar  *argv[] = {ESD_SERVER, "-nobeeps", NULL};
  GError *err = NULL;
  time_t  starttime;

  /* FIXME: once esd is fixed to chdir(), we should remove this, IMHO */
  if (!gsm_exec_async ("/", argv, NULL, &esd_pid, &err))
    {
      g_warning ("Could not start esd: %s\n", err->message);
      g_error_free (err);
      return;
    }

  g_child_watch_add (esd_pid, reset_esd_pid, NULL);

  starttime = time (NULL);
  gnome_sound_init (NULL);

  while (gnome_sound_connection_get () < 0
	 && ((time (NULL) - starttime) < 4))
    {
      g_usleep (200);
      gnome_sound_init (NULL);
    }
}

static void
stop_esd (void)
{
  gnome_sound_shutdown ();

  if (esd_pid)
    {
      if (kill (esd_pid, SIGTERM) == -1)
        g_printerr ("Failed to kill esd (pid %d)\n", esd_pid);
      else
        esd_pid = 0;
    }
}
#endif /* HAVE_ESD */

static gboolean
sound_init (void)
{

  if (!sound_enabled ())
    return FALSE;

#ifdef HAVE_ESD
  if (gnome_sound_connection_get () < 0)
    start_esd ();
#endif

  if (gnome_sound_connection_get () < 0)
    {
      g_warning ("Failed to start sound.\n");
      return FALSE;
    }

  return TRUE;
}

static void 
sound_shutdown (void)
{
#ifdef HAVE_ESD
  stop_esd ();
#endif
}

static void
play_sound_event (const char *name)
{
  gnome_triggers_do (NULL, NULL, "gnome-2", name, NULL);
}

void 
gsm_sound_login (void)
{
  if (!sound_init ())
    return;

  if (sound_events_enabled ())
    play_sound_event ("login");
}

void 
gsm_sound_logout (void)
{
  if (sound_events_enabled ())
    play_sound_event ("logout");

  sound_shutdown ();
}
