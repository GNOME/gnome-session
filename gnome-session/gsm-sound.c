#include <config.h>

#include "gsm-sound.h"

#include <signal.h>
#include <unistd.h>
#include "util.h"

#include <libgnome/libgnome.h>

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

static gboolean
sound_init (void)
{

  if (!sound_enabled ())
    return FALSE;

  if (gnome_sound_connection_get () < 0)
    return FALSE;

  return TRUE;
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

  gnome_sound_shutdown ();
}

