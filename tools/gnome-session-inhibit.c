/*
 * Copyright (C) 2012,2025 Red Hat, Inc.
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

#if defined(__linux__)
#include <sys/prctl.h>
#endif

#include <glib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include "gnome-session/gsm-inhibitor-flag.h"

static gboolean
parse_flags (const gchar      *option_name,
             const gchar      *value,
             GsmInhibitorFlag *flags,
             GError          **error)
{
  /* Previous versions of gnome-session-inhibit suggested using a colon-separated
   * list as the --inhibit=ARG value. For backwards-compatibility, we should
   * continue to support that. */

  g_auto(GStrv) args = NULL;
  gint i;

  args = g_strsplit (value, ":", 0);
  for (i = 0; args[i]; i++)
    {
      if (strcmp (args[i], "logout") == 0)
        *flags |= GSM_INHIBITOR_FLAG_LOGOUT;
      else if (strcmp (args[i], "switch-user") == 0)
        *flags |= GSM_INHIBITOR_FLAG_SWITCH_USER;
      else if (strcmp (args[i], "suspend") == 0)
        *flags |= GSM_INHIBITOR_FLAG_SUSPEND;
      else if (strcmp (args[i], "idle") == 0)
        *flags |= GSM_INHIBITOR_FLAG_IDLE;
      else if (strcmp (args[i], "automount") == 0)
        *flags |= GSM_INHIBITOR_FLAG_AUTOMOUNT;
      else
        {
          g_set_error (error,
                       G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                       _("Unknown inhibit argument: %s"),
                       args [i]);
          return FALSE;
        }
    }

  return TRUE;
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

static GVariant *
get_inhibitor_prop (GDBusProxy *inhibitor,
                    const char *method_name)
{
  g_autoptr(GVariant) variant = NULL;
  g_autoptr(GError) error = NULL;

  variant = g_dbus_proxy_call_sync (inhibitor,
                                    method_name,
                                    NULL,
                                    G_DBUS_CALL_FLAGS_NONE,
                                    -1,
                                    NULL,
                                    &error);

  if (variant == NULL) {
    g_debug ("Failed to get property via '%s': %s",
             method_name, error->message);
    return NULL;
  }

  return g_variant_get_child_value (variant, 0);
}

static void
string_append_comma (GString *s,
                     const char *value)
{
  if (s->len != 0)
    g_string_append (s, ", ");
  g_string_append (s, value);
}

static char *
flags_to_str (guint32 flags)
{
  GString *s;

  s = g_string_new (NULL);
  if (flags & GSM_INHIBITOR_FLAG_LOGOUT)
    string_append_comma (s, "logout");
  if (flags & GSM_INHIBITOR_FLAG_SWITCH_USER)
    string_append_comma (s, "switch-user");
  if (flags & GSM_INHIBITOR_FLAG_SUSPEND)
    string_append_comma (s, "suspend");
  if (flags & GSM_INHIBITOR_FLAG_IDLE)
    string_append_comma (s, "idle");
  if (flags & GSM_INHIBITOR_FLAG_AUTOMOUNT)
    string_append_comma (s, "automount");

  return g_string_free (s, FALSE);
}

static void list (void)
{
  g_autoptr(GDBusConnection) bus = NULL;
  g_autoptr(GVariant) ret = NULL;
  g_autoptr(GVariant) array = NULL;
  g_autoptr(GError) error = NULL;
  guint num_children;
  g_autoptr(GVariantIter) iter = NULL;
  const char *inhibitor_path;

  bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);

  if (bus == NULL)
    {
      g_warning ("Failed to connect to session bus: %s", error->message);
      g_error_free (error);
      return;
    }

  ret = g_dbus_connection_call_sync (bus,
                               "org.gnome.SessionManager",
                               "/org/gnome/SessionManager",
                               "org.gnome.SessionManager",
                               "GetInhibitors",
                               NULL,
                               NULL,
                               0,
                               G_MAXINT,
                               NULL,
                               &error);

  if (ret == NULL)
    {
      g_warning ("Failed to call GetInhibitors: %s\n", error->message);
      g_error_free (error);
      return;
    }

  array = g_variant_get_child_value (ret, 0);

  num_children = g_variant_n_children (array);
  if (num_children == 0)
    {
      g_print ("No inhibitors\n");
      return;
    }

  g_variant_get (array, "ao", &iter);
  while (g_variant_iter_loop (iter, "&o", &inhibitor_path))
    {
      g_autoptr(GDBusProxy) inhibitor = NULL;
      g_autoptr(GVariant) app_id = NULL;
      g_autoptr(GVariant) reason = NULL;
      g_autoptr(GVariant) flags = NULL;
      g_autofree char *flags_str = NULL;

      inhibitor = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                 G_DBUS_PROXY_FLAGS_NONE,
                                                 NULL,
                                                 "org.gnome.SessionManager",
                                                 inhibitor_path,
                                                 "org.gnome.SessionManager.Inhibitor",
                                                 NULL,
                                                 &error);

      /* Skip inhibitors that might have disappeared */
      if (!inhibitor)
        {
          g_debug ("Failed to get proxy for %s: %s",
                   inhibitor_path,
                   error->message);
          continue;
        }

      app_id = get_inhibitor_prop (inhibitor, "GetAppId");
      reason = get_inhibitor_prop (inhibitor, "GetReason");
      flags = get_inhibitor_prop (inhibitor, "GetFlags");
      if (!app_id || !reason || !flags)
        continue;
      flags_str = flags_to_str (g_variant_get_uint32 (flags));

      g_print ("%s: %s (%s)\n",
               g_variant_get_string (app_id, NULL),
               g_variant_get_string (reason, NULL),
               flags_str);
    }
}

static int
wait_for_child_app (char **argv)
{
  pid_t pid;
  int status;

  /* Fork off the child */
  pid = fork ();
  if (pid < 0)
    {
      g_printerr ("fork failed: %m\n");
      return 1;
    }

  if (pid == 0) /* Child process */
    {
      /* Give ourselves a new process group, so that we can handle Ctrl+C
       * separately from the parent */
      if (isatty (0))
        setpgid (0, 0);

#if defined(__linux__)
      /* Ensure that the child process goes away when the parent does */
      if (prctl(PR_SET_PDEATHSIG, SIGHUP) < 0)
        g_printerr ("Failed to configure PDEATHSIG, ignoring.\n");
#endif

      execvp (*argv, argv);
      g_printerr (_("Failed to execute %s: %m\n"), *argv);
      exit (1);
    }

  /* Parent process */

  /* Give the child control over the terminal. This allows the child to
   * handle Ctrl+C on its own. */
  if (isatty (0))
    tcsetpgrp (0, pid);

  /* Wait for the child to exit, either naturally or via signal */
  do
    {
      if (waitpid (pid, &status, 0) < 0)
        {
          g_printerr ("waitpid failed: %m\n");
          return 1;
        }
    } while (!WIFEXITED (status) && ! WIFSIGNALED(status));

  /* Take back control over the terminal */
  if (isatty (0))
    {
      sigset_t block, old;
      sigemptyset (&block);
      sigaddset (&block, SIGTTOU);

      /* If you don't currently have control and try to take it, the kernel will
       * send you SIGTTOU and your process will be stopped. By blocking it, we
       * allow ourselves to take control. */
      if (sigprocmask (SIG_BLOCK, &block, &old) < 0)
        {
          g_printerr ("sigprocmask(SIG_BLOCK, SIGTTOU) failed: %m\n");
          return 1;
        }

      tcsetpgrp (0, getpgid (0));

      /* Not strictly necessary, but good hygiene */
      if (sigprocmask (SIG_SETMASK, &old, NULL) < 0)
        {
          g_printerr ("sigprocmask(SIG_SETMASK, old) failed: %m\n");
          return 1;
        }
    }

  /* Propagate exit code */
  if (WIFEXITED (status))
    return WEXITSTATUS (status);
  else
    return 0;
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
  GsmInhibitorFlag inhibit_flags = 0;
  gboolean show_version = FALSE;
  gboolean show_list = FALSE;
  gboolean no_launch = FALSE;
  g_autofree gchar *app_id = NULL;
  g_autofree gchar *reason = NULL;

  GOptionEntry entries[] = {
    { "version", 0, 0, G_OPTION_ARG_NONE, &show_version, N_("Show program version"), NULL },
    { "app-id", 0, 0, G_OPTION_ARG_STRING, &app_id, N_("The application id to use when inhibiting (optional)"), N_("ID") },
    { "reason", 0, 0, G_OPTION_ARG_STRING, &reason, N_("The reason for inhibiting (optional)"), N_("REASON") },
    { "inhibit", 0, 0, G_OPTION_ARG_CALLBACK, &parse_flags, N_("Add action to inhibit (repeatable)"), N_("ACTION") },
    { "inhibit-only", 0, 0, G_OPTION_ARG_NONE, &no_launch, N_("Do not launch COMMAND and wait forever instead"), NULL },
    { "list", 'l', 0, G_OPTION_ARG_NONE, &show_list, N_("List the existing inhibitions, and exit"), NULL },
    { NULL, 0, 0, 0, NULL, NULL, NULL }
  };
  g_autoptr(GOptionContext) opts = NULL;
  GOptionGroup *main_opts;
  g_autoptr(GError) error = NULL;

  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, LOCALE_DIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  opts = g_option_context_new (N_("COMMAND"));
  g_option_context_set_translation_domain (opts, GETTEXT_PACKAGE);
  main_opts = g_option_group_new (NULL, NULL, NULL, &inhibit_flags, NULL);
  g_option_group_set_translation_domain (main_opts, GETTEXT_PACKAGE);
  g_option_group_add_entries (main_opts, entries);
  g_option_context_set_main_group (opts, main_opts);
  g_option_context_set_summary (opts, N_("Execute COMMAND while inhibiting some session functionality."));
  g_option_context_set_description (opts, N_("Valid ACTION values are: logout, switch-user, suspend, idle, and automount.\nIf no --inhibit options are specified, idle is assumed."));
  g_option_context_set_strict_posix (opts, TRUE);

  if (!g_option_context_parse (opts, &argc, &argv, &error))
    {
      g_printerr ("%s\n", error->message);
      return 1;
    }

  if (show_version)
    {
      g_print ("%s %s\n", g_get_prgname (), PACKAGE_VERSION);
      return 0;
    }

  if (show_list)
    {
      list ();
      return 0;
    }

  if (argc < 2 && !no_launch)
    {
      gchar *usage = g_option_context_get_help (opts, TRUE, NULL);
      g_printerr ("%s\n\n%s",
                  _("Neither COMMAND nor --inhibit-only were specified."),
                  usage);
      g_free(usage);
      return 1;
    }

  if (app_id == NULL)
    app_id = g_strdup ("unknown");

  if (reason == NULL)
    reason = g_strdup ("not specified");

  if (inhibit_flags == 0)
    inhibit_flags = GSM_INHIBITOR_FLAG_IDLE;

  if (!inhibit (app_id, reason, inhibit_flags))
    return 1;

  if (!no_launch)
    return wait_for_child_app (argv + 1);
  else
    wait_forever ();
  return 0;
}
