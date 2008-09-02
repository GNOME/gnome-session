/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * gnome-keyring-daemon-wrapper: a wrapper to make
 * gnome-keyring-daemon conform to the startup item interfaces. (This
 * should go away when gnome-keyring-daemon supports session
 * management directly.)
 *
 * Most of this code comes from the old gnome-session/gsm-keyring.c.
 *
 * Copyright (C) 2003 Red Hat, Inc.
 * Copyright (C) 2007 Novell, Inc.
 */

#include <config.h>

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <dbus/dbus-glib.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gnome-keyring.h>

#include "eggdesktopfile.h"
#include "eggsmclient.h"

static pid_t gnome_keyring_daemon_pid = 0;
static int   keyring_lifetime_pipe[2];

static void
keyring_daemon_stop (void)
{
        if (gnome_keyring_daemon_pid != 0) {
                kill (gnome_keyring_daemon_pid, SIGTERM);
                gnome_keyring_daemon_pid = 0;
        }
}

static void
child_setup (gpointer user_data)
{
        int   open_max;
        int   fd;
        char *fd_str;

        open_max = sysconf (_SC_OPEN_MAX);
        for (fd = 3; fd < open_max; fd++) {
                if (fd != keyring_lifetime_pipe[0]) {
                        fcntl (fd, F_SETFD, FD_CLOEXEC);
                }
        }

        fd_str = g_strdup_printf ("%d", keyring_lifetime_pipe[0]);
        g_setenv ("GNOME_KEYRING_LIFETIME_FD",
                  fd_str,
                  TRUE);
}

static GHashTable *
keyring_env_to_hashtable (void)
{
        GHashTable  *env_hash;
        char       **envp;
        char       **env;

        env_hash = g_hash_table_new_full (g_str_hash, g_str_equal,
                                          g_free, g_free);

        envp = g_listenv ();
        for (env = envp; *env; env++) {
                g_hash_table_insert (env_hash,
                                     g_strdup (*env),
                                     g_strdup (g_getenv (*env)));
        }
        g_strfreev (envp);

        return env_hash;
}

struct keyring_environment_foreach {
        GHashTable *other_env;
        DBusGProxy *gsm;
};

static void
keyring_environment_updated_from_new (const char *name,
                                      const char *value,
                                      struct keyring_environment_foreach *data)
{
        const char *old_value;
        GError     *err;

        old_value = g_hash_table_lookup (data->other_env, name);

        /* If we didn't have a value for this environment variable before,
         * or if it changed, then we want to update the session-wide
         * environment variable. */
        if (!old_value || strcmp (old_value, value) != 0) {
                err = NULL;
                if (!dbus_g_proxy_call (data->gsm, "Setenv", &err,
                                        G_TYPE_STRING, name,
                                        G_TYPE_STRING, value,
                                        G_TYPE_INVALID,
                                        G_TYPE_INVALID)) {
                        g_warning ("Could not set %s: %s", name, err->message);
                        g_clear_error (&err);
                }
        }
}

static void
keyring_environment_removed_from_old (const char *name,
                                      const char *value,
                                      struct keyring_environment_foreach *data)
{
        const char *new_value;
        GError     *err;

        new_value = g_hash_table_lookup (data->other_env, name);

        /* If we don't have a value anymore for this environment variable,
         * then we want to remove it from the session-wide environment. */
        if (!new_value) {
                /* There's no proper Unsetenv, so just make it empty */
                err = NULL;
                if (!dbus_g_proxy_call (data->gsm, "Setenv", &err,
                                        G_TYPE_STRING, name,
                                        G_TYPE_STRING, "",
                                        G_TYPE_INVALID,
                                        G_TYPE_INVALID)) {
                        g_warning ("Could not set %s: %s", name, err->message);
                        g_clear_error (&err);
                }
        }
}

static void
keyring_export_environment (DBusGProxy *gsm)
{
        GHashTable  *old_env;
        GHashTable  *new_env;
        struct keyring_environment_foreach data;

        old_env = keyring_env_to_hashtable ();
        gnome_keyring_daemon_prepare_environment_sync ();
        new_env = keyring_env_to_hashtable ();

        data.gsm = gsm;

        data.other_env = old_env;
        g_hash_table_foreach (new_env,
                              (GHFunc) keyring_environment_updated_from_new,
                              &data);

        data.other_env = new_env;
        g_hash_table_foreach (old_env,
                              (GHFunc) keyring_environment_removed_from_old,
                              &data);

        g_hash_table_destroy (old_env);
        g_hash_table_destroy (new_env);
}

static void
keyring_daemon_start (DBusGProxy *gsm)
{
        GError     *err;
        char       *standard_out;
        char      **lines;
        char      **env;
        int         status;
        long        pid;
        char       *pid_str;
        char       *end;
        const char *old_keyring;
        char       *argv[2];

        /* If there is already a working keyring, don't start a new daemon */
        old_keyring = g_getenv ("GNOME_KEYRING_SOCKET");
        if (old_keyring != NULL &&
            access (old_keyring, R_OK | W_OK) == 0) {
                keyring_export_environment (gsm);
                return;
        }

        /* Pipe to slave keyring lifetime to */
        pipe (keyring_lifetime_pipe);

        err = NULL;
        argv[0] = GNOME_KEYRING_DAEMON;
        argv[1] = NULL;
        g_spawn_sync (NULL, argv, NULL, G_SPAWN_LEAVE_DESCRIPTORS_OPEN,
                      child_setup, NULL,
                      &standard_out, NULL, &status, &err);

        close (keyring_lifetime_pipe[0]);
        /* We leave keyring_lifetime_pipe[1] open for the lifetime of the session,
           in order to slave the keyring daemon lifecycle to the session. */

        if (err != NULL) {
                g_printerr ("Failed to run gnome-keyring-daemon: %s\n",
                            err->message);
                g_error_free (err);
                exit (1);
        } else {
                if (WIFEXITED (status) &&
                    WEXITSTATUS (status) == 0 &&
                    standard_out != NULL) {
                        lines = g_strsplit (standard_out, "\n", 3);

                        if (lines[0] != NULL &&
                            lines[1] != NULL &&
                            lines[2] != NULL &&
                            g_str_has_prefix (lines[1], "SSH_AUTH_SOCK=") &&
                            g_str_has_prefix (lines[2], "GNOME_KEYRING_PID=")) {
                                env = g_strsplit (lines[1], "=", 2);
                                if (!dbus_g_proxy_call (gsm, "Setenv", &err,
                                                        G_TYPE_STRING, env[0],
                                                        G_TYPE_STRING, env[1],
                                                        G_TYPE_INVALID,
                                                        G_TYPE_INVALID)) {
                                        g_warning ("Could not set %s: %s", env[0], err->message);
                                        g_clear_error (&err);
                                }
                                g_strfreev (env);

                                pid_str = lines[2] + strlen ("GNOME_KEYRING_PID=");

                                pid = strtol (pid_str, &end, 10);
                                if (end != pid_str) {
                                        gnome_keyring_daemon_pid = pid;

                                        env = g_strsplit (lines[2], "=", 2);
                                        if (!dbus_g_proxy_call (gsm, "Setenv", &err,
                                                                G_TYPE_STRING, env[0],
                                                                G_TYPE_STRING, env[1],
                                                                G_TYPE_INVALID,
                                                                G_TYPE_INVALID)) {
                                                g_warning ("Could not set %s: %s", env[0], err->message);
                                                g_clear_error (&err);
                                        }
                                        g_strfreev (env);
                                }
                        }

                        g_strfreev (lines);

                        keyring_export_environment (gsm);
                } else {
                        /* daemon failed for some reason */
                        g_printerr ("gnome-keyring-daemon failed to start correctly, exit code: %d\n",
                                    WEXITSTATUS (status));
                        exit (1);
                }
                g_free (standard_out);
        }
}

static void
quit (EggSMClient *smclient,
      gpointer     user_data)
{
        gtk_main_quit ();
}

int
main (int argc, char **argv)
{
        EggSMClient     *client;
        GOptionContext  *goption_context;
        GError          *err;
        DBusGConnection *connection;
        DBusGProxy      *gsm;

        g_type_init ();

        goption_context = g_option_context_new (NULL);
        g_option_context_add_group (goption_context, gtk_get_option_group (FALSE));
        g_option_context_add_group (goption_context, egg_sm_client_get_option_group ());

        err = NULL;
        if (!g_option_context_parse (goption_context, &argc, &argv, &err)) {
                g_printerr ("Could not parse arguments: %s\n", err->message);
                g_error_free (err);
                g_option_context_free (goption_context);
                return 1;
        }

        connection = dbus_g_bus_get (DBUS_BUS_SESSION, &err);
        if (connection == NULL) {
                g_error ("couldn't get D-Bus connection: %s", err->message);
        }
        gsm = dbus_g_proxy_new_for_name (connection,
                                         "org.gnome.SessionManager",
                                         "/org/gnome/SessionManager",
                                         "org.gnome.SessionManager");

        egg_set_desktop_file (DEFAULT_SESSION_DIR "/gnome-keyring-daemon-wrapper.desktop");

        client = egg_sm_client_get ();
        g_signal_connect (client, "quit", G_CALLBACK (quit), NULL);

        keyring_daemon_start (gsm);
        gtk_main ();
        keyring_daemon_stop ();
        g_option_context_free (goption_context);

        return 0;
}
