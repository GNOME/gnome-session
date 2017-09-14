/*
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include "gnome-session/gsm-inhibitor-flag.h"

static GsmInhibitorFlag parse_flags (const gchar *arg)
{
  GsmInhibitorFlag flags;
  gchar **args;
  gint i;

  flags = 0;

  args = g_strsplit (arg, ":", 0);
  for (i = 0; args[i]; i++)
    {
      if (strcmp (args[i], "logout") == 0)
        flags |= GSM_INHIBITOR_FLAG_LOGOUT;
      else if (strcmp (args[i], "switch-user") == 0)
        flags |= GSM_INHIBITOR_FLAG_SWITCH_USER;
      else if (strcmp (args[i], "suspend") == 0)
        flags |= GSM_INHIBITOR_FLAG_SUSPEND;
      else if (strcmp (args[i], "idle") == 0)
        flags |= GSM_INHIBITOR_FLAG_IDLE;
      else if (strcmp (args[i], "automount") == 0)
        flags |= GSM_INHIBITOR_FLAG_AUTOMOUNT;
      else
        g_print ("Ignoring inhibit argument: %s\n", args[i]);
    }

  g_strfreev (args);

  return flags;
}

static gboolean inhibit (const gchar       *app_id,
                         const gchar       *reason,
                         GsmInhibitorFlag flags)
{
  GDBusConnection *bus;
  GVariant *ret;
  GError *error = NULL;

  bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);

  if (bus == NULL)
    {
      g_warning ("Failed to connect to session bus: %s", error->message);
      g_error_free (error);
      return FALSE;
    }

  ret = g_dbus_connection_call_sync (bus,
                               "org.gnome.SessionManager",
                               "/org/gnome/SessionManager",
                               "org.gnome.SessionManager",
                               "Inhibit",
                               g_variant_new ("(susu)",
                                              app_id, 0, reason, flags),
                               G_VARIANT_TYPE ("(u)"),
                               0,
                               G_MAXINT,
                               NULL,
                               &error);

  if (ret == NULL)
    {
      g_warning ("Failed to call Inhibit: %s\n", error->message);
      g_error_free (error);
      return FALSE;
    }

  g_variant_unref (ret);

  return TRUE;
}

static void usage (void)
{
  g_print (_("%s [OPTION...] COMMAND\n"
             "\n"
             "Execute COMMAND while inhibiting some session functionality.\n"
             "\n"
             "  -h, --help        Show this help\n"
             "  --version         Show program version\n"
             "  --app-id ID       The application id to use\n"
             "                    when inhibiting (optional)\n"
             "  --reason REASON   The reason for inhibiting (optional)\n"
             "  --inhibit ARG     Things to inhibit, colon-separated list of:\n"
             "                    logout, switch-user, suspend, idle, automount\n"
             "  --inhibit-only    Do not launch COMMAND and wait forever instead\n"
             "\n"
             "If no --inhibit option is specified, idle is assumed.\n"),
           g_get_prgname ());
}

static void version (void)
{
  g_print ("%s %s\n", g_get_prgname (), PACKAGE_VERSION);
}

static void
wait_for_child_app (char **argv)
{
  pid_t pid;
  int status;

  pid = fork ();
  if (pid < 0)
    {
      g_print ("fork failed\n");
      exit (1);
    }

  if (pid == 0)
    {
      execvp (*argv, argv);
      g_print (_("Failed to execute %s\n"), *argv);
      exit (1);
    }

  do
    {
      if (waitpid (pid, &status, 0) == -1)
        {
          g_print ("waitpid failed\n");
          exit (1);
        }

      if (WIFEXITED (status))
        exit (WEXITSTATUS (status));

    } while (!WIFEXITED (status) && ! WIFSIGNALED(status));
}

static void
wait_forever (void)
{
  g_print ("Inhibiting until Ctrl+C is pressed...\n");
  while (1)
    sleep (UINT32_MAX);
}

int main (int argc, char *argv[])
{
  gchar *prgname;
  GsmInhibitorFlag inhibit_flags = 0;
  gboolean show_help = FALSE;
  gboolean show_version = FALSE;
  gboolean no_launch = FALSE;
  gint i;
  const gchar *app_id = "unknown";
  const gchar *reason = "not specified";

  prgname = g_path_get_basename (argv[0]);
  g_set_prgname (prgname);
  g_free (prgname);

  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, LOCALE_DIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  for (i = 1; i < argc; i++)
    {
      if (strcmp (argv[i], "--help") == 0 ||
          strcmp (argv[i], "-h") == 0)
        show_help = TRUE;
      else if (strcmp (argv[i], "--version") == 0)
        show_version = TRUE;
      else if (strcmp (argv[i], "--inhibit-only") == 0)
        no_launch = TRUE;
      else if (strcmp (argv[i], "--app-id") == 0)
        {
          i++;
          if (i == argc)
            {
              g_print (_("%s requires an argument\n"), argv[i]);
              exit (1);
            }
          app_id = argv[i];
        }
      else if (strcmp (argv[i], "--reason") == 0)
        {
          i++;
          if (i == argc)
            {
              g_print (_("%s requires an argument\n"), argv[i]);
              exit (1);
            }
          reason = argv[i];
        }
      else if (strcmp (argv[i], "--inhibit") == 0)
        {
          i++;
          if (i == argc)
            {
              g_print (_("%s requires an argument\n"), argv[i]);
              exit (1);
            }
          inhibit_flags |= parse_flags (argv[i]);
        }
      else
        break;
    }

  if (show_version)
    {
      version ();
      return 0;
    }

  if (show_help || (i == argc && !no_launch))
    {
      usage ();
      return 0;
    }

  if (inhibit_flags == 0)
    inhibit_flags = GSM_INHIBITOR_FLAG_IDLE;

  if (inhibit (app_id, reason, inhibit_flags) == FALSE)
    return 1;

  if (!no_launch)
    wait_for_child_app (argv + i);
  else
    wait_forever ();

  return 0;
}
