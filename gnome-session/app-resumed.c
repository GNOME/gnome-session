/* app-resumed.c
 * Copyright (C) 2007 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <X11/SM/SMlib.h>

#include "app-resumed.h"

static pid_t launch (GsmApp *app, GError **error);

G_DEFINE_TYPE (GsmAppResumed, gsm_app_resumed, GSM_TYPE_APP)

static void
gsm_app_resumed_init (GsmAppResumed *app)
{
  ;
}

static void
gsm_app_resumed_class_init (GsmAppResumedClass *klass)
{
  GsmAppClass *app_class = GSM_APP_CLASS (klass);

  app_class->launch = launch;
}

/**
 * gsm_app_resumed_new_from_legacy_session:
 * @session_file: a session file (eg, ~/.gnome2/session) from the old
 * gnome-session
 * @n: the number of the client in @session_file to read
 *
 * Creates a new #GsmApp corresponding to the indicated client in
 * a legacy session file.
 *
 * Return value: the new #GsmApp, or %NULL if the @n'th entry in
 * @session_file is not actually a saved XSMP client.
 **/
GsmApp *
gsm_app_resumed_new_from_legacy_session (GKeyFile *session_file, int n)
{
  GsmAppResumed *app;
  char *key, *id, *val;

  key = g_strdup_printf ("%d,id", n);
  id = g_key_file_get_string (session_file, "Default", key, NULL);
  g_free (key);

  if (!id)
    {
      /* Not actually a saved app, just a redundantly-specified
       * autostart app; ignore.
       */
      return NULL;
    }

  app = g_object_new (GSM_TYPE_APP_RESUMED,
		      "client-id", id,
		      NULL);

  key = g_strdup_printf ("%d," SmProgram, n);
  val = g_key_file_get_string (session_file, "Default", key, NULL);
  g_free (key);
  if (val)
    app->program = val;

  key = g_strdup_printf ("%d," SmRestartCommand, n);
  val = g_key_file_get_string (session_file, "Default", key, NULL);
  g_free (key);
  if (val)
    app->restart_command = val;

  /* We ignore the discard_command on apps resumed from the legacy
   * session, so that the legacy session will still work if the user
   * reverts back to the old gnome-session.
   */
  app->discard_on_resume = FALSE;

  return (GsmApp *)app;
}

/**
 * gsm_app_resumed_new_from_session:
 * @session_file: a session file
 * @group: the group containing the client to read
 * @discard: whether or not the app's state should be discarded after
 * it resumes.
 *
 * Creates a new #GsmApp corresponding to the indicated client in a
 * session file.
 *
 * Return value: the new #GsmApp, or %NULL on error
 **/
GsmApp *
gsm_app_resumed_new_from_session (GKeyFile *session_file, const char *group,
				  gboolean discard)
{
  GsmAppResumed *app;
  char *desktop_file, *client_id, *val;

  desktop_file = g_key_file_get_string (session_file, group,
					"_GSM_DesktopFile", NULL);
  client_id = g_key_file_get_string (session_file, group, "id", NULL);

  app = g_object_new (GSM_TYPE_APP_RESUMED,
		      "desktop-file", desktop_file,
		      "client-id", client_id,
		      NULL);
  g_free (desktop_file);
  g_free (client_id);

  /* Replace Exec key with RestartCommand */
  val = g_key_file_get_string (session_file, group,
			       SmRestartCommand, NULL);
  gsm_app_set_exec (app, val);
  g_free (val);

  /* Use Program for the name if there's no localized name */
  if (!gsm_app_get_name (app))
    {
      val = g_key_file_get_string (session_file, group,
				   SmProgram, NULL);
      gsm_app_set_name (app, val);
      g_free (val);
    }

  app->discard_command = g_key_file_get_string (session_file, group,
						SmDiscardCommand, NULL);
  app->discard_on_resume = discard;

  return (GsmApp *)app;
}

static pid_t
launch (GsmApp *app, GError **err)
{
  const char *restart_command = GSM_APP_RESUMED (app)->restart_command;
  int argc;
  char **argv;
  gboolean success;
  pid_t pid;

  if (!restart_command)
    return (pid_t)-1;

  if (!g_shell_parse_argv (restart_command, &argc, &argv, err))
    return (pid_t)-1;

  success = g_spawn_async (NULL, argv, NULL,
			   G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
			   NULL, NULL, &pid, err);
  g_strfreev (argv);

  return success ? pid : (pid_t)-1;
}
