/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006 Novell, Inc.
 * Copyright (C) 2008 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <locale.h>

#include <glib/gi18n.h>
#include <glib.h>
#include <glib-unix.h>
#include <gio/gio.h>

#include "gsm-util.h"

#define INIT_WORKER_PATH (LIBEXECDIR "/gnome-session-init-worker")

static char *
shell_to_login_shell (const char *shell)
{
        gboolean in_etc_shells = FALSE;
        const char *candidate;
        g_autofree char *basename = NULL;

        if (IS_STRING_EMPTY (shell))
                return NULL;

        /* Check /etc/shells, to see if the shell is listed as a valid login
         * shell on this system */
        setusershell ();
        while ((candidate = getusershell ()) != NULL) {
                if (g_strcmp0 (shell, candidate) == 0) {
                        in_etc_shells = TRUE;
                        break;
                }
        }
        endusershell ();
        if (!in_etc_shells)
                return NULL;

        /* Sometimes false and nologin end up in /etc/shells, so let's double
         * check for them explicitly */
        basename = g_path_get_basename (shell);
        if (g_strcmp0 (basename, "false") == 0 ||
            g_strcmp0 (basename, "nologin") == 0)
                return NULL;

        /* It's a login shell! Let's do the UNIX magic of prepending '-' to
         * argv [0], and that will tell the shell to run as a login shell */
        return g_strdup_printf ("-%s", basename);
}

static void
maybe_reexec_with_login_shell (GStrv argv)
{
        const char *shell = NULL;
        g_autofree char *login_shell = NULL;
        GStrvBuilder *builder = NULL;
        g_auto (GStrv) inner_args = NULL;
        g_auto (GStrv) outer_args = NULL;

        /* No need to bother on the greeter (no user profile to execute) */
        if (g_strcmp0 (g_getenv ("XDG_SESSION_CLASS"), "greeter") == 0)
                return;

        /* Or with X (it has a mess of scripts that do this for us) */
        if (g_strcmp0 (g_getenv ("XDG_SESSION_TYPE"), "wayland") != 0)
                return;

        shell = g_getenv ("SHELL");
        login_shell = shell_to_login_shell (shell);

        /* Launching sessions with an invalid shell (i.e. nologin) is supported,
         * but these aren't real shells so we can't re-exec with them */
        if (login_shell == NULL)
                return;

        g_debug ("Relaunching with login shell %s (%s)", login_shell, shell);

        /* First, we construct the command executed by the login shell */
        builder = g_strv_builder_new ();
        g_strv_builder_add (builder, "exec");
        g_strv_builder_take (builder, g_shell_quote (argv [0]));
        g_strv_builder_add (builder, "--no-reexec");
        for (char **p = argv + 1; *p != NULL; p++)
                g_strv_builder_take (builder, g_shell_quote (*p));
        inner_args = g_strv_builder_unref_to_strv (builder);

        /* Next, we construct the invocation of the login shell itself */
        builder = g_strv_builder_new ();
        g_strv_builder_take (builder, g_steal_pointer (&login_shell));
        g_strv_builder_add (builder, "-c");
        g_strv_builder_take (builder, g_strjoinv (" ", inner_args));
        outer_args = g_strv_builder_unref_to_strv (builder);

        execv (shell, outer_args);
        g_warning ("Failed to re-execute with login shell (%s): %m", shell);
}

static void
set_up_environment (void)
{
        g_autofree char *dbus_address = NULL;
        g_autoptr (GSettings) locale_settings = NULL;
        g_autofree char *region = NULL;
        g_autofree char *ibus_path = NULL;

        dbus_address = g_dbus_address_get_for_bus_sync (G_BUS_TYPE_SESSION, NULL, NULL);
        if (!dbus_address)
                g_error ("No session bus running!");
        g_setenv ("DBUS_SESSION_BUS_ADDRESS", dbus_address, TRUE);

        locale_settings = g_settings_new ("org.gnome.system.locale");
        region = g_settings_get_string (locale_settings, "region");
        if (!IS_STRING_EMPTY (region)) {
                if (g_strcmp0 (g_getenv ("LANG"), region) == 0) {
                        // LC_CTYPE
                        g_unsetenv ("LC_NUMERIC");
                        g_unsetenv ("LC_TIME");
                        // LC_COLLATE
                        g_unsetenv ("LC_MONETARY");
                        // LC_MESSAGES
                        g_unsetenv ("LC_PAPER");
                        // LC_NAME
                        g_unsetenv ("LC_ADDRESS");
                        g_unsetenv ("LC_TELEPHONE");
                        g_unsetenv ("LC_MEASUREMENT");
                        // LC_IDENTIFICATION
                } else {
                        // LC_CTYPE
                        g_setenv ("LC_NUMERIC", region, TRUE);
                        g_setenv ("LC_TIME", region, TRUE);
                        // LC_COLLATE
                        g_setenv ("LC_MONETARY", region, TRUE);
                        // LC_MESSAGES
                        g_setenv ("LC_PAPER", region, TRUE);
                        // LC_NAME
                        g_setenv ("LC_ADDRESS", region, TRUE);
                        g_setenv ("LC_TELEPHONE", region, TRUE);
                        g_setenv ("LC_MEASUREMENT", region, TRUE);
                        // LC_IDENTIFICATION
                }
        }

        ibus_path = g_find_program_in_path ("ibus-daemon");
        if (ibus_path) {
                /* For Qt 6.8 and newer */
                if (IS_STRING_EMPTY (g_getenv ("QT_IM_MODULES")))
                        g_setenv ("QT_IM_MODULES", "wayland;ibus", TRUE);

                /* For Qt 6.7 and older */
                if (IS_STRING_EMPTY (g_getenv ("QT_IM_MODULE")))
                        g_setenv ("QT_IM_MODULE", "ibus", TRUE);

                if (IS_STRING_EMPTY (g_getenv ("XMODIFIERS")))
                        g_setenv ("XMODIFIERS", "@im=ibus", TRUE);
        }

        /* We want to use the GNOME menus which has the designed categories. */
        g_setenv ("XDG_MENU_PREFIX", "gnome-", TRUE);
}

int
main (int argc, char **argv)
{
        g_autoptr (GError) error = NULL;
        g_autofree char *session_name = NULL;
        gboolean debug = FALSE;
        gboolean show_version = FALSE;
        gboolean disable_acceleration_check = FALSE;
        gboolean no_reexec = FALSE;
        g_autoptr (GOptionContext) options = NULL;
        GStrvBuilder *argv_dup_builder = NULL;
        g_auto (GStrv) argv_dup = NULL;

        GOptionEntry entries[] = {
                { "session", 0, 0, G_OPTION_ARG_STRING, &session_name, N_("Session to use"), N_("SESSION_NAME") },
                { "debug", 0, 0, G_OPTION_ARG_NONE, &debug, N_("Enable debugging code"), NULL },
                { "version", 0, 0, G_OPTION_ARG_NONE, &show_version, N_("Version of this application"), NULL },
                { "disable-acceleration-check", 0, 0, G_OPTION_ARG_NONE, &disable_acceleration_check, N_("This option is ignored"), NULL },
                { "no-reexec", 0, 0, G_OPTION_ARG_NONE, &no_reexec, N_("Don't re-exec into a login shell"), NULL },
                { NULL, 0, 0, 0, NULL, NULL, NULL }
        };

        setlocale (LC_ALL, "");
        bindtextdomain (GETTEXT_PACKAGE, LOCALE_DIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        argv_dup_builder = g_strv_builder_new ();
        for (size_t i = 0; i < argc; i++)
                g_strv_builder_add (argv_dup_builder, argv [i]);
        argv_dup = g_strv_builder_unref_to_strv (argv_dup_builder);

        options = g_option_context_new (_(" — the GNOME session manager"));
        g_option_context_add_main_entries (options, entries, GETTEXT_PACKAGE);
        if (!g_option_context_parse (options, &argc, &argv, &error))
                g_error ("%s", error->message);

        if (!debug) {
                const char *debug_string = g_getenv ("GNOME_SESSION_DEBUG");
                if (debug_string != NULL)
                        debug = atoi (debug_string) == 1;
        }
        g_log_set_debug_enabled (debug);

        if (show_version) {
                g_print ("%s %s\n", argv [0], VERSION);
                return 0;
        }

        if (!g_getenv ("XDG_SESSION_TYPE"))
                g_error ("XDG_SESSION_TYPE= is unset!");

        /* Make sure that we were launched through a login shell. This ensures
         * that the user's shell profiles are sourced when logging into GNOME */
        if (!no_reexec)
                maybe_reexec_with_login_shell (argv_dup);

        if (disable_acceleration_check)
                g_warning ("Wayland assumes that acceleration works, so --disable-acceleration-check is deprecated!");

        set_up_environment ();

        gsm_util_export_activation_environment (&error);
        if (error)
                g_warning ("Failed to upload environment to DBus: %s", error->message);
        g_clear_error (&error);

        if (IS_STRING_EMPTY (session_name)) {
                g_autoptr (GSettings) settings = NULL;
                g_free (session_name); /* In case it's empty but not NULL */
                settings = g_settings_new ("org.gnome.desktop.session");
                session_name = g_settings_get_string (settings, "session-name");
        }

        /* We've set up the environment, so now it's time for the init system to take over.
         * The init system is responsible for starting up the rest of GNOME. Upstream we
         * only support systemd, but to make it possible to integrate GNOME with other inits
         * we've factored out the systemd-specific code into a separate init-worker binary. */
        if (execl(INIT_WORKER_PATH, INIT_WORKER_PATH, session_name, NULL) < 0)
            g_error ("Failed to exec gnome-session-init-worker: %m");

        g_assert_not_reached ();
        return 1;
}
