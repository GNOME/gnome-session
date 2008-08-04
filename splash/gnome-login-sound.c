/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 * gnome-login-sound.c
 * Copyright (C) 1999, 2007 Novell, Inc.
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

#include <config.h>

#include <libgnome/libgnome.h>
#include <gconf/gconf-client.h>
#include <gtk/gtkmain.h>
#include "eggsmclient-libgnomeui.h"

/* FIXME: Move the sound file configuration into GConf and use
 * esound (or whatever) directly rather than gnome_sound_play().
 */

static char *
get_sound_file_from_config (const char *event,
                            const char *config_file,
                            gboolean   *was_set)
{
        char *key;
        char *sound_file;

        key = g_strdup_printf ("=%s=%s/file", config_file, event);
        sound_file = gnome_config_get_string (key);
        g_free (key);

        if (!sound_file) {
                *was_set = FALSE;
                return NULL;
        }

        *was_set = TRUE;
        if (!sound_file[0]) {
                return NULL;
        }

        if (!g_path_is_absolute (sound_file)) {
                char *tmp_sound_file;

                tmp_sound_file = gnome_program_locate_file (NULL, GNOME_FILE_DOMAIN_SOUND, sound_file, TRUE, NULL);
                g_free (sound_file);
                sound_file = tmp_sound_file;
        }

        return sound_file;
}

#define SOUND_EVENT_FILE "sound/events/gnome-2.soundlist"

static char *
get_sound_file (const char *event)
{
        char *config_file, *sound_file;
        gboolean was_set;

        config_file = gnome_util_home_file (SOUND_EVENT_FILE);
        if (config_file) {
                sound_file = get_sound_file_from_config (event, config_file, &was_set);
                g_free (config_file);

                if (was_set)
                        return sound_file;
        }

        config_file = gnome_program_locate_file (NULL, GNOME_FILE_DOMAIN_CONFIG, SOUND_EVENT_FILE, TRUE, NULL);
        if (config_file) {
                sound_file = get_sound_file_from_config (event, config_file, &was_set);
                g_free (config_file);

                return sound_file;
        }

        return NULL;
}

#define ENABLE_SOUNDS_KEY "/desktop/gnome/sound/event_sounds"

static void
maybe_play_sound (const char *event)
{
        GConfClient *gconf;
        GError      *error;
        gboolean     enable_sounds;
        char        *sound_file;

        gconf = gconf_client_get_default ();
        error = NULL;
        enable_sounds = gconf_client_get_bool (gconf, ENABLE_SOUNDS_KEY, &error);
        if (error != NULL) {
                g_warning ("Error getting value of " ENABLE_SOUNDS_KEY ": %s", error->message);
                g_error_free (error);
                return;  /* assume FALSE */
        }

        if (!enable_sounds) {
                return;
        }

        sound_file = get_sound_file (event);
        if (sound_file != NULL) {
                gnome_sound_play (sound_file);
                g_free (sound_file);
        }

        return;
}

static gboolean
idle_quit (gpointer user_data)
{
        gtk_main_quit ();
        return FALSE;
}

static gboolean logout = FALSE;

static const GOptionEntry options[] = {
        { "logout", 0, 0, G_OPTION_ARG_NONE, &logout, N_("Play logout sound instead of login"), NULL },

        { NULL }
};

int
main (int argc, char *argv[])
{
        GOptionContext *context;

        g_type_init ();

        context = g_option_context_new (_("- GNOME login/logout sound"));
        g_option_context_add_main_entries (context, options, NULL);

        gnome_program_init (PACKAGE, VERSION,
                            EGG_SM_CLIENT_LIBGNOMEUI_MODULE,
                            argc, argv,
                            GNOME_PARAM_GOPTION_CONTEXT, context,
                            NULL);

        maybe_play_sound (logout ? "logout" : "login");

        /* We need to start the main loop to force EggSMClient to register
         * so the session manager will know we started successfully.
         */
        g_idle_add (idle_quit, NULL);
        gtk_main ();

        return 0;
}
