/* gsm-proxy.c - utility function for providing legacy http_proxy 
                 support

   Copyright (C) 2006 Red Hat, Inc.
   Written by Ray Strode <rstrode@redhat.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA.  */

#include <config.h>
#include "gsm-proxy.h"

#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gconf/gconf-client.h>

#include "manager.h"
#include "util.h"

/* Set up the environment such that apps run from the session get
 * gnome's http proxy settings when possible
 */ 
void
gsm_set_up_legacy_proxy_environment (void)
{
  static gboolean run_before = FALSE;
  gboolean use_proxy, use_authentication;
  char *host, *user_name, *password, *http_proxy;
  gint port;
  GConfClient *gconf_client;

  gconf_client = gsm_get_conf_client ();

  if (!run_before) 
    {
      gconf_client_add_dir (gconf_client, HTTP_PROXY_PREFIX, GCONF_CLIENT_PRELOAD_ONELEVEL, NULL); 
      gconf_client_notify_add (gconf_client,
                               HTTP_PROXY_PREFIX,
                               (GConfClientNotifyFunc) gsm_set_up_legacy_proxy_environment,
                               NULL, NULL, NULL);
      run_before = TRUE;
    }

  use_proxy = gconf_client_get_bool (gconf_client, HTTP_PROXY_KEY, NULL);

  if (!use_proxy)
    {
      g_unsetenv ("http_proxy"); 
      return;
    }

  host = gconf_client_get_string (gconf_client, HTTP_PROXY_HOST_KEY, NULL);

  if (host == NULL || host[0] == '\0')
    {
      g_unsetenv ("http_proxy");
      return;
    }

  port = gconf_client_get_int (gconf_client, HTTP_PROXY_PORT_KEY, NULL);

  use_authentication = gconf_client_get_bool (gconf_client, HTTP_PROXY_AUTHENTICATION_KEY, NULL);

  user_name = NULL;
  password = NULL;
  if (use_authentication)
    {
      user_name = gconf_client_get_string (gconf_client, HTTP_PROXY_USER_NAME_KEY, 
                                           NULL);
      if (user_name != NULL && user_name[0] == '\0')
        {
          g_free (user_name);
          user_name = NULL;
        }
      else 
        password = gconf_client_get_string (gconf_client, HTTP_PROXY_PASSWORD_KEY, 
                                          NULL);
    }

  if (port <= 0)
    http_proxy = g_strdup_printf ("http://%s%s%s%s%s",
                                  user_name != NULL? user_name : "",
                                  user_name != NULL && password != NULL? "@" : "",
                                  user_name != NULL && password != NULL? password : "",
                                  user_name != NULL? ":" : "",
                                  host); 
  else
    http_proxy = g_strdup_printf ("http://%s%s%s%s%s:%d",
                                  user_name != NULL? user_name : "",
                                  user_name != NULL && password != NULL? "@" : "",
                                  user_name != NULL && password != NULL? password : "",
                                  user_name != NULL? ":" : "",
                                  host, port); 
  g_setenv ("http_proxy", http_proxy, TRUE);

  g_free (http_proxy);
  g_free (password);
  g_free (user_name);
  g_free (host);
}
