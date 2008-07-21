/* util.h
 * Copyright (C) 2008 Lucas Rocha.
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

#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/goption.h>
#include <gtk/gtkmain.h>
#include <gtk/gtkmessagedialog.h>

#include "util.h"

gchar **
gsm_util_get_autostart_dirs ()
{
  GPtrArray *dirs;
  const char * const *system_config_dirs;
  const char * const *system_data_dirs;
  gint i;

  dirs = g_ptr_array_new ();

  g_ptr_array_add (dirs,
                   g_build_filename (g_get_user_config_dir (),
                                     "autostart", NULL));

  system_data_dirs = g_get_system_data_dirs ();
  for (i = 0; system_data_dirs[i]; i++)
    {
      g_ptr_array_add (dirs,
                       g_build_filename (system_data_dirs[i],
                                         "gnome", "autostart", NULL));

      g_ptr_array_add (dirs,
                       g_build_filename (system_data_dirs[i],
                                         "autostart", NULL));
    }

  system_config_dirs = g_get_system_config_dirs ();
  for (i = 0; system_config_dirs[i]; i++)
    {
      g_ptr_array_add (dirs,
                       g_build_filename (system_config_dirs[i],
                                         "autostart", NULL));
    }

  g_ptr_array_add (dirs, NULL);

  return (char **) g_ptr_array_free (dirs, FALSE);
}

gchar **
gsm_util_get_app_dirs ()
{
  GPtrArray *dirs;
  const char * const *system_data_dirs;
  gint i;

  dirs = g_ptr_array_new ();

  system_data_dirs = g_get_system_data_dirs ();
  for (i = 0; system_data_dirs[i]; i++)
    {
      g_ptr_array_add (dirs,
                       g_build_filename (system_data_dirs[i], "applications",
                                         NULL));

      g_ptr_array_add (dirs,
                       g_build_filename (system_data_dirs[i], "gnome", "wm-properties",
                                         NULL));
    }

  g_ptr_array_add (dirs, NULL);

  return (char **) g_ptr_array_free (dirs, FALSE);
}

gboolean
gsm_util_text_is_blank (const gchar *str)
{
  if (str == NULL)
    return TRUE;

  while (*str)
    {
      if (!isspace(*str))
        return FALSE;

      str++;
    }

  return TRUE;
}

/**
 * gsm_util_init_error:
 * @fatal: whether or not the error is fatal to the login session
 * @format: printf-style error message format
 * @...: error message args
 *
 * Displays the error message to the user. If @fatal is %TRUE, gsm
 * will exit after displaying the message.
 *
 * This should be called for major errors that occur before the
 * session is up and running. (Notably, it positions the dialog box
 * itself, since no window manager will be running yet.)
 **/
void
gsm_util_init_error (gboolean fatal, const char *format, ...)
{
  GtkWidget *dialog;
  char *msg;
  va_list args;

  va_start (args, format);
  msg = g_strdup_vprintf (format, args);
  va_end (args);

  /* If option parsing failed, Gtk won't have been initialized... */
  if (!gdk_display_get_default ())
    {
      if (!gtk_init_check (NULL, NULL))
	{
	  /* Oh well, no X for you! */
	  g_printerr (_("Unable to start login session (and unable connect to the X server)"));
	  g_printerr (msg);
	  exit (1);
	}
    }

  dialog = gtk_message_dialog_new (NULL, 0, GTK_MESSAGE_ERROR,
				   GTK_BUTTONS_CLOSE, "%s", msg);

  g_free (msg);

  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);
  gtk_dialog_run (GTK_DIALOG (dialog));

  gtk_widget_destroy (dialog);

  gtk_main_quit ();
}

/**
 * gsm_util_generate_startup_id:
 *
 * Generates a new SM client ID.
 *
 * Return value: an SM client ID.
 **/
char *
gsm_util_generate_startup_id (void)
{
  static int sequence = -1;
  static guint rand1 = 0, rand2 = 0;
  static pid_t pid = 0;
  struct timeval tv;

  /* The XSMP spec defines the ID as:
   *
   * Version: "1"
   * Address type and address:
   *   "1" + an IPv4 address as 8 hex digits
   *   "2" + a DECNET address as 12 hex digits
   *   "6" + an IPv6 address as 32 hex digits
   * Time stamp: milliseconds since UNIX epoch as 13 decimal digits
   * Process-ID type and process-ID:
   *   "1" + POSIX PID as 10 decimal digits
   * Sequence number as 4 decimal digits
   *
   * XSMP client IDs are supposed to be globally unique: if
   * SmsGenerateClientID() is unable to determine a network
   * address for the machine, it gives up and returns %NULL.
   * GNOME and KDE have traditionally used a fourth address
   * format in this case:
   *   "0" + 16 random hex digits
   *
   * We don't even bother trying SmsGenerateClientID(), since the
   * user's IP address is probably "192.168.1.*" anyway, so a random
   * number is actually more likely to be globally unique.
   */

  if (!rand1)
    {
      rand1 = g_random_int ();
      rand2 = g_random_int ();
      pid = getpid ();
    }

  sequence = (sequence + 1) % 10000;
  gettimeofday (&tv, NULL);
  return g_strdup_printf ("10%.04x%.04x%.10lu%.3u%.10lu%.4d",
			  rand1, rand2,
			  (unsigned long) tv.tv_sec,
			  (unsigned) tv.tv_usec,
			  (unsigned long) pid,
			  sequence);
}
