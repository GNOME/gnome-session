#include <config.h>

#include "gsm-sound.h"

#ifdef HAVE_ESD /* almost whole file */
#include <unistd.h>
#include <libgnome/libgnome.h>
#include <gconf/gconf-client.h>
#include <esd.h>

#define ENABLE_ESD_KEY    "/desktop/gnome/sound/enable_esd"
#define ENABLE_SOUNDS_KEY "/desktop/gnome/sound/event_sounds"

static gboolean
esd_enabled (void)
{
  GConfClient *client;
  GError      *error = NULL;
  gboolean     retval;

  client = gconf_client_get_default ();

  retval = gconf_client_get_bool (client, ENABLE_ESD_KEY, &error);
  if (error)
    {
      g_warning ("Error getting value of " ENABLE_ESD_KEY ": %s", error->message);
      g_error_free (error);
      return FALSE;  /* Fallback value */
    }

  g_object_unref (client);

  return retval;
}

static gboolean
sound_events_enabled (void)
{
  GConfClient *client;
  GError      *error = NULL;
  gboolean     retval;

  client = gconf_client_get_default ();

  retval = gconf_client_get_bool (client, ENABLE_SOUNDS_KEY, &error);
  if (error)
    {
      g_warning ("Error getting value of " ENABLE_SOUNDS_KEY ": %s", error->message);
      g_error_free (error);
      return FALSE;  /* Fallback value */
    }

  g_object_unref (client);

  return retval;
}

static void
start_esd (void) 
{
  GError *err = NULL;
  time_t starttime;
	
  if (!g_spawn_command_line_async (ESD_SERVER" -nobeeps", &err))
    {
      g_warning ("Could not start esd: %s\n", err->message);
      g_error_free (err);
      return;
    }

  starttime = time (NULL);
  gnome_sound_init (NULL);

  while (gnome_sound_connection_get () < 0
	 && ((time(NULL) - starttime) < 4)) 
    {
#ifdef HAVE_USLEEP
      usleep(200);
#endif
      gnome_sound_init(NULL);
    }  
}

static gboolean
load_login_sample_from (const char *file)
{
  char *key;
  char *sample_file;
  int sample_id;

  if (!file)
    return FALSE;

  key = g_strconcat ("=", file, "=login/file", NULL);
  sample_file = gnome_config_get_string (key);
  g_free (key);

  if (sample_file && *sample_file && *sample_file != '/')
    {
      char *tmp_sample_file;
      tmp_sample_file = gnome_program_locate_file (NULL, GNOME_FILE_DOMAIN_SOUND, sample_file, TRUE, NULL);
      g_free (sample_file);
      sample_file = tmp_sample_file;
    }

  if (!(sample_file && *sample_file))
    {
      g_free (sample_file);
      return FALSE;
    }

  sample_id = esd_sample_getid (gnome_sound_connection_get (), "gnome-2/login");
  if (sample_id >= 0)
    esd_sample_free (gnome_sound_connection_get (), sample_id);

  sample_id = gnome_sound_sample_load ("gnome-2/login", sample_file);

  if (sample_id < 0)
    {
      g_warning ("Couldn't load sound file %s\n", sample_file);
      return FALSE;
    }

  g_free (sample_file);

  return TRUE;
}

#define SOUND_EVENT_FILE "sound/events/gnome-2.soundlist"
static gboolean
load_login_sample (void)
{
  char *s;
  gboolean loaded;

  s = gnome_util_home_file (SOUND_EVENT_FILE);
  loaded = load_login_sample_from (s);
  g_free (s);

  if (loaded)
    return TRUE;

  s = gnome_program_locate_file (NULL, GNOME_FILE_DOMAIN_CONFIG, SOUND_EVENT_FILE, TRUE, NULL);
  loaded = load_login_sample_from (s);
  g_free (s);

  return loaded;
}

static gboolean
sound_init (void)
{

  if (!esd_enabled ())
    return FALSE;

  if (gnome_sound_connection_get () < 0)
    start_esd ();

  if (gnome_sound_connection_get () < 0)
    {
      g_warning ("Esound failed to start.\n");
      return FALSE;
    }

  if (!sound_events_enabled ())
    return FALSE;

  return load_login_sample ();
}

static void
play_trigger (const char *trigger)
{
  const char *supinfo[3] = { "gnome-2" };
  supinfo[1] = trigger;
  gnome_triggers_vdo ("", NULL, supinfo);
}
#endif /* HAVE_ESD */

void 
gsm_sound_login (void)
{
#ifdef HAVE_ESD
  if (sound_init ())
    play_trigger ("login");
#endif /* HAVE_ESD */
}

void 
gsm_sound_logout (void)
{
#ifdef HAVE_ESD
  if (sound_events_enabled ())
    play_trigger ("logout");
#endif
}
