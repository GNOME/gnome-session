/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
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
#include "config.h"
#endif

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "gsm-xsmp-client.h"

#include "gsm-manager.h"

/* FIXME */
#define GsmDesktopFile "_Gsm_DesktopFile"

#define GSM_XSMP_CLIENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSM_TYPE_XSMP_CLIENT, GsmXSMPClientPrivate))

struct GsmXSMPClientPrivate
{

        SmsConn    conn;
        IceConn    ice_connection;

        guint      watch_id;
        guint      protocol_timeout;

        int        current_save_yourself;
        int        next_save_yourself;
        char      *id;
        char      *description;
        GPtrArray *props;
};

enum {
        PROP_0,
        PROP_ICE_CONNECTION,
};

G_DEFINE_TYPE (GsmXSMPClient, gsm_xsmp_client, GSM_TYPE_CLIENT)

static gboolean
client_iochannel_watch (GIOChannel    *channel,
                        GIOCondition   condition,
                        GsmXSMPClient *client)
{

        switch (IceProcessMessages (client->priv->ice_connection, NULL, NULL)) {
        case IceProcessMessagesSuccess:
                return TRUE;

        case IceProcessMessagesIOError:
                g_debug ("IceProcessMessagesIOError on '%s'", client->priv->description);
                gsm_client_disconnected (GSM_CLIENT (client));
                return FALSE;

        case IceProcessMessagesConnectionClosed:
                g_debug ("IceProcessMessagesConnectionClosed on '%s'",
                         client->priv->description);
                return FALSE;

        default:
                g_assert_not_reached ();
        }
}

/* Called if too much time passes between the initial connection and
 * the XSMP protocol setup.
 */
static gboolean
client_protocol_timeout (GsmXSMPClient *client)
{
        g_debug ("client_protocol_timeout for client '%s' in ICE status %d",
                 client->priv->description,
                 IceConnectionStatus (client->priv->ice_connection));

        gsm_client_disconnected (GSM_CLIENT (client));

        return FALSE;
}

static SmProp *
find_property (GsmXSMPClient *client,
               const char    *name,
               int           *index)
{
        SmProp *prop;
        int i;

        for (i = 0; i < client->priv->props->len; i++) {
                prop = client->priv->props->pdata[i];

                if (!strcmp (prop->name, name)) {
                        if (index) {
                                *index = i;
                        }
                        return prop;
                }
        }

        return NULL;
}

static void
set_description (GsmXSMPClient *client)
{
        SmProp     *prop;
        const char *id;

        prop = find_property (client, SmProgram, NULL);
        id = gsm_client_get_client_id (GSM_CLIENT (client));

        g_free (client->priv->description);
        if (prop) {
                client->priv->description = g_strdup_printf ("%p [%.*s %s]",
                                                             client,
                                                             prop->vals[0].length,
                                                             (char *)prop->vals[0].value,
                                                             id);
        } else if (id != NULL) {
                client->priv->description = g_strdup_printf ("%p [%s]", client, id);
        } else {
                client->priv->description = g_strdup_printf ("%p", client);
        }
}

static void
setup_connection (GsmXSMPClient *client)
{
        GIOChannel    *channel;
        int            fd;

        fd = IceConnectionNumber (client->priv->ice_connection);
        fcntl (fd, F_SETFD, fcntl (fd, F_GETFD, 0) | FD_CLOEXEC);
        channel = g_io_channel_unix_new (fd);
        client->priv->watch_id = g_io_add_watch (channel,
                                                 G_IO_IN | G_IO_ERR,
                                                 (GIOFunc)client_iochannel_watch,
                                                 client);
        g_io_channel_unref (channel);

        client->priv->protocol_timeout = g_timeout_add (5000,
                                                        (GSourceFunc)client_protocol_timeout,
                                                        client);

        set_description (client);
        g_debug ("New client '%s'", client->priv->description);
}

static GObject *
gsm_xsmp_client_constructor (GType                  type,
                             guint                  n_construct_properties,
                             GObjectConstructParam *construct_properties)
{
        GsmXSMPClient *client;

        client = GSM_XSMP_CLIENT (G_OBJECT_CLASS (gsm_xsmp_client_parent_class)->constructor (type,
                                                                                              n_construct_properties,
                                                                                              construct_properties));
        setup_connection (client);

        return G_OBJECT (client);
}

static void
gsm_xsmp_client_init (GsmXSMPClient *client)
{
        client->priv = GSM_XSMP_CLIENT_GET_PRIVATE (client);

        client->priv->props = g_ptr_array_new ();
        client->priv->current_save_yourself = -1;
        client->priv->next_save_yourself = -1;
}


static void
delete_property (GsmXSMPClient *client,
                 const char    *name)
{
        int     index;
        SmProp *prop;

        prop = find_property (client, name, &index);
        if (!prop) {
                return;
        }

#if 0
        /* This is wrong anyway; we can't unconditionally run the current
         * discard command; if this client corresponds to a GsmAppResumed,
         * and the current discard command is identical to the app's
         * discard_command, then we don't run the discard command now,
         * because that would delete a saved state we may want to resume
         * again later.
         */
        if (!strcmp (name, SmDiscardCommand)) {
                gsm_client_run_discard (GSM_CLIENT (client));
        }
#endif

        g_ptr_array_remove_index_fast (client->priv->props, index);
        SmFreeProperty (prop);
}


static void
debug_print_property (SmProp *prop)
{
        GString *tmp;
        int      i;

        switch (prop->type[0]) {
        case 'C': /* CARD8 */
                g_debug ("  %s = %d", prop->name, *(unsigned char *)prop->vals[0].value);
                break;

        case 'A': /* ARRAY8 */
                g_debug ("  %s = '%s'", prop->name, (char *)prop->vals[0].value);
                break;

        case 'L': /* LISTofARRAY8 */
                tmp = g_string_new (NULL);
                for (i = 0; i < prop->num_vals; i++) {
                        g_string_append_printf (tmp, "'%.*s' ", prop->vals[i].length,
                                                (char *)prop->vals[i].value);
                }
                g_debug ("  %s = %s", prop->name, tmp->str);
                g_string_free (tmp, TRUE);
                break;

        default:
                g_debug ("  %s = ??? (%s)", prop->name, prop->type);
                break;
        }
}


static void
set_properties_callback (SmsConn     conn,
                         SmPointer   manager_data,
                         int         num_props,
                         SmProp    **props)
{
        GsmXSMPClient *client = manager_data;
        int            i;

        g_debug ("Set properties from client '%s'", client->priv->description);

        for (i = 0; i < num_props; i++) {
                delete_property (client, props[i]->name);
                g_ptr_array_add (client->priv->props, props[i]);

                debug_print_property (props[i]);

                if (!strcmp (props[i]->name, SmProgram))
                        set_description (client);
        }

        free (props);

}

static void
delete_properties_callback (SmsConn     conn,
                            SmPointer   manager_data,
                            int         num_props,
                            char      **prop_names)
{
        GsmXSMPClient *client = manager_data;
        int i;

        g_debug ("Delete properties from '%s'", client->priv->description);

        for (i = 0; i < num_props; i++) {
                delete_property (client, prop_names[i]);

                g_debug ("  %s", prop_names[i]);
        }

        free (prop_names);
}

static void
get_properties_callback (SmsConn   conn,
                         SmPointer manager_data)
{
        GsmXSMPClient *client = manager_data;

        g_debug ("Get properties request from '%s'", client->priv->description);

        SmsReturnProperties (conn,
                             client->priv->props->len,
                             (SmProp **)client->priv->props->pdata);
}

static char *
prop_to_command (SmProp *prop)
{
        GString *str;
        int i, j;
        gboolean need_quotes;

        str = g_string_new (NULL);
        for (i = 0; i < prop->num_vals; i++) {
                char *val = prop->vals[i].value;

                need_quotes = FALSE;
                for (j = 0; j < prop->vals[i].length; j++) {
                        if (!g_ascii_isalnum (val[j]) && !strchr ("-_=:./", val[j])) {
                                need_quotes = TRUE;
                                break;
                        }
                }

                if (i > 0) {
                        g_string_append_c (str, ' ');
                }

                if (!need_quotes) {
                        g_string_append_printf (str,
                                                "%.*s",
                                                prop->vals[i].length,
                                                (char *)prop->vals[i].value);
                } else {
                        g_string_append_c (str, '\'');
                        while (val < (char *)prop->vals[i].value + prop->vals[i].length) {
                                if (*val == '\'') {
                                        g_string_append (str, "'\''");
                                } else {
                                        g_string_append_c (str, *val);
                                }
                                val++;
                        }
                        g_string_append_c (str, '\'');
                }
        }

        return g_string_free (str, FALSE);
}

static char *
xsmp_get_restart_command (GsmClient *client)
{
        SmProp *prop;

        prop = find_property (GSM_XSMP_CLIENT (client), SmRestartCommand, NULL);

        if (!prop || strcmp (prop->type, SmLISTofARRAY8) != 0) {
                return NULL;
        }

        return prop_to_command (prop);
}

static char *
xsmp_get_discard_command (GsmClient *client)
{
        SmProp *prop;

        prop = find_property (GSM_XSMP_CLIENT (client), SmDiscardCommand, NULL);

        if (!prop || strcmp (prop->type, SmLISTofARRAY8) != 0) {
                return NULL;
        }

        return prop_to_command (prop);
}

static gboolean
xsmp_get_autorestart (GsmClient *client)
{
        SmProp *prop;

        prop = find_property (GSM_XSMP_CLIENT (client), SmRestartStyleHint, NULL);

        if (!prop || strcmp (prop->type, SmCARD8) != 0) {
                return FALSE;
        }

        return ((unsigned char *)prop->vals[0].value)[0] == SmRestartImmediately;
}

static void
xsmp_restart (GsmClient *client,
              GError   **error)
{
        char *restart_cmd;

        restart_cmd = gsm_client_get_restart_command (client);

        g_spawn_command_line_async (restart_cmd, error);

        g_free (restart_cmd);
}

static void
do_save_yourself (GsmXSMPClient *client,
                  int            save_type)
{
        if (client->priv->next_save_yourself != -1) {
                /* Either we're currently doing a shutdown and there's a checkpoint
                 * queued after it, or vice versa. Either way, the new SaveYourself
                 * is redundant.
                 */
                g_debug ("  skipping redundant SaveYourself for '%s'",
                         client->priv->description);
        } else if (client->priv->current_save_yourself != -1) {
                g_debug ("  queuing new SaveYourself for '%s'",
                         client->priv->description);
                client->priv->next_save_yourself = save_type;
        } else {
                client->priv->current_save_yourself = save_type;

                switch (save_type) {
                case SmSaveLocal:
                        /* Save state */
                        SmsSaveYourself (client->priv->conn,
                                         SmSaveLocal,
                                         FALSE,
                                         SmInteractStyleNone,
                                         FALSE);
                        break;

                default:
                        /* Logout */
                        SmsSaveYourself (client->priv->conn,
                                         save_type,
                                         TRUE,
                                         SmInteractStyleAny,
                                         FALSE);
                        break;
                }
        }
}

static void
xsmp_save_yourself (GsmClient *client,
                    gboolean   save_state)
{
        GsmXSMPClient *xsmp = (GsmXSMPClient *) client;

        g_debug ("xsmp_save_yourself ('%s', %s)",
                 xsmp->priv->description,
                 save_state ? "True" : "False");

        do_save_yourself (xsmp, save_state ? SmSaveBoth : SmSaveGlobal);
}

static void
xsmp_save_yourself_phase2 (GsmClient *client)
{
        GsmXSMPClient *xsmp = (GsmXSMPClient *) client;

        g_debug ("xsmp_save_yourself_phase2 ('%s')", xsmp->priv->description);

        SmsSaveYourselfPhase2 (xsmp->priv->conn);
}

static void
xsmp_interact (GsmClient *client)
{
        GsmXSMPClient *xsmp = (GsmXSMPClient *) client;

        g_debug ("xsmp_interact ('%s')", xsmp->priv->description);

        SmsInteract (xsmp->priv->conn);
}


static void
xsmp_shutdown_cancelled (GsmClient *client)
{
        GsmXSMPClient *xsmp = (GsmXSMPClient *) client;

        g_debug ("xsmp_shutdown_cancelled ('%s')", xsmp->priv->description);

        SmsShutdownCancelled (xsmp->priv->conn);
}

static void
xsmp_stop (GsmClient *client)
{
        GsmXSMPClient *xsmp = (GsmXSMPClient *) client;

        g_debug ("xsmp_die ('%s')", xsmp->priv->description);

        SmsDie (xsmp->priv->conn);
}

static void
gsm_client_set_ice_connection (GsmXSMPClient *client,
                               gpointer       conn)
{
        client->priv->ice_connection = conn;
}

static void
gsm_xsmp_client_set_property (GObject       *object,
                              guint          prop_id,
                              const GValue  *value,
                              GParamSpec    *pspec)
{
        GsmXSMPClient *self;

        self = GSM_XSMP_CLIENT (object);

        switch (prop_id) {
        case PROP_ICE_CONNECTION:
                gsm_client_set_ice_connection (self, g_value_get_pointer (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_xsmp_client_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
        GsmXSMPClient *self;

        self = GSM_XSMP_CLIENT (object);

        switch (prop_id) {
        case PROP_ICE_CONNECTION:
                g_value_set_pointer (value, self->priv->ice_connection);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_xsmp_client_finalize (GObject *object)
{
        GsmXSMPClient *client = (GsmXSMPClient *) object;

        g_debug ("xsmp_finalize (%s)", client->priv->description);

        if (client->priv->watch_id) {
                g_source_remove (client->priv->watch_id);
        }

        if (client->priv->conn) {
                SmsCleanUp (client->priv->conn);
        } else {
                IceCloseConnection (client->priv->ice_connection);
        }

        if (client->priv->protocol_timeout) {
                g_source_remove (client->priv->protocol_timeout);
        }

        G_OBJECT_CLASS (gsm_xsmp_client_parent_class)->finalize (object);
}

static void
gsm_xsmp_client_class_init (GsmXSMPClientClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        GsmClientClass *client_class = GSM_CLIENT_CLASS (klass);

        object_class->finalize             = gsm_xsmp_client_finalize;
        object_class->constructor          = gsm_xsmp_client_constructor;
        object_class->get_property         = gsm_xsmp_client_get_property;
        object_class->set_property         = gsm_xsmp_client_set_property;

        g_object_class_install_property (object_class,
                                         PROP_ICE_CONNECTION,
                                         g_param_spec_pointer ("ice-connection",
                                                               "ice-connection",
                                                               "ice-connection",
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

        client_class->get_restart_command  = xsmp_get_restart_command;
        client_class->get_discard_command  = xsmp_get_discard_command;
        client_class->get_autorestart      = xsmp_get_autorestart;

        client_class->stop                 = xsmp_stop;
        client_class->restart              = xsmp_restart;
        client_class->save_yourself        = xsmp_save_yourself;
        client_class->save_yourself_phase2 = xsmp_save_yourself_phase2;
        client_class->interact             = xsmp_interact;
        client_class->shutdown_cancelled   = xsmp_shutdown_cancelled;
}

GsmClient *
gsm_xsmp_client_new (IceConn ice_conn)
{
        GsmXSMPClient *xsmp;

        xsmp = g_object_new (GSM_TYPE_XSMP_CLIENT,
                             "ice-connection", ice_conn,
                             NULL);

        return GSM_CLIENT (xsmp);
}

static Status
register_client_callback (SmsConn    conn,
                          SmPointer  manager_data,
                          char      *previous_id)
{
        GsmXSMPClient *client = manager_data;
        gboolean       handled;
        char          *id;

        g_debug ("Client '%s' received RegisterClient(%s)",
                 client->priv->description,
                 previous_id ? previous_id : "NULL");


        /* There are three cases:
         * 1. id is NULL - we'll use a new one
         * 2. id is known - we'll use known one
         * 3. id is unknown - this is an error
         */
        id = g_strdup (previous_id);
        handled = gsm_client_manage_request (GSM_CLIENT (client), &id);
        if (! handled) {
                g_debug (" RegisterClient not handled!");
                g_free (id);
                free (previous_id);
                return FALSE;
        }

        if (id == NULL) {
                g_debug ("  rejected: invalid previous_id");
                free (previous_id);
                return FALSE;
        }

        g_object_set (client, "client-id", id, NULL);

        set_description (client);

        g_debug ("Sending RegisterClientReply to '%s'", client->priv->description);

        SmsRegisterClientReply (conn, id);

        if (previous_id == NULL) {
                /* Send the initial SaveYourself. */
                g_debug ("Sending initial SaveYourself");
                SmsSaveYourself (conn, SmSaveLocal, False, SmInteractStyleNone, False);
                client->priv->current_save_yourself = SmSaveLocal;

                free (previous_id);
        }

        gsm_client_set_status (GSM_CLIENT (client), GSM_CLIENT_MANAGED);

        return TRUE;
}


static void
save_yourself_request_callback (SmsConn   conn,
                                SmPointer manager_data,
                                int       save_type,
                                Bool      shutdown,
                                int       interact_style,
                                Bool      fast,
                                Bool      global)
{
        GsmXSMPClient *client = manager_data;

        g_debug ("Client '%s' received SaveYourselfRequest(%s, %s, %s, %s, %s)",
                 client->priv->description,
                 save_type == SmSaveLocal ? "SmSaveLocal" :
                 save_type == SmSaveGlobal ? "SmSaveGlobal" : "SmSaveBoth",
                 shutdown ? "Shutdown" : "!Shutdown",
                 interact_style == SmInteractStyleAny ? "SmInteractStyleAny" :
                 interact_style == SmInteractStyleErrors ? "SmInteractStyleErrors" :
                 "SmInteractStyleNone", fast ? "Fast" : "!Fast",
                 global ? "Global" : "!Global");

        /* Examining the g_debug above, you can see that there are a total
         * of 72 different combinations of options that this could have been
         * called with. However, most of them are stupid.
         *
         * If @shutdown and @global are both TRUE, that means the caller is
         * requesting that a logout message be sent to all clients, so we do
         * that. We use @fast to decide whether or not to show a
         * confirmation dialog. (This isn't really what @fast is for, but
         * the old gnome-session and ksmserver both interpret it that way,
         * so we do too.) We ignore @save_type because we pick the correct
         * save_type ourselves later based on user prefs, dialog choices,
         * etc, and we ignore @interact_style, because clients have not used
         * it correctly consistently enough to make it worth honoring.
         *
         * If @shutdown is TRUE and @global is FALSE, the caller is
         * confused, so we ignore the request.
         *
         * If @shutdown is FALSE and @save_type is SmSaveGlobal or
         * SmSaveBoth, then the client wants us to ask some or all open
         * applications to save open files to disk, but NOT quit. This is
         * silly and so we ignore the request.
         *
         * If @shutdown is FALSE and @save_type is SmSaveLocal, then the
         * client wants us to ask some or all open applications to update
         * their current saved state, but not log out. At the moment, the
         * code only supports this for the !global case (ie, a client
         * requesting that it be allowed to update *its own* saved state,
         * but not having everyone else update their saved state).
         */

        if (shutdown && global) {
                g_debug ("  initiating shutdown");
                gsm_client_logout_request (GSM_CLIENT (client),
                                           !fast);
        } else if (!shutdown && !global) {
                g_debug ("  initiating checkpoint");
                do_save_yourself (client, SmSaveLocal);
        } else {
                g_debug ("  ignoring");
        }
}

static void
save_yourself_phase2_request_callback (SmsConn   conn,
                                       SmPointer manager_data)
{
        GsmXSMPClient *client = manager_data;

        g_debug ("Client '%s' received SaveYourselfPhase2Request",
                 client->priv->description);

        if (client->priv->current_save_yourself == SmSaveLocal) {
                /* WTF? Anyway, if it's checkpointing, it doesn't have to wait
                 * for anyone else.
                 */
                SmsSaveYourselfPhase2 (client->priv->conn);
        } else {
                gsm_client_request_phase2 (GSM_CLIENT (client));
        }
}

static void
interact_request_callback (SmsConn   conn,
                           SmPointer manager_data,
                           int       dialog_type)
{
        GsmXSMPClient *client = manager_data;

        g_debug ("Client '%s' received InteractRequest(%s)",
                 client->priv->description,
                 dialog_type == SmInteractStyleAny ? "Any" : "Errors");

        gsm_client_request_interaction (GSM_CLIENT (client));
}

static void
interact_done_callback (SmsConn   conn,
                        SmPointer manager_data,
                        Bool      cancel_shutdown)
{
        GsmXSMPClient *client = manager_data;

        g_debug ("Client '%s' received InteractDone(cancel_shutdown = %s)",
                 client->priv->description,
                 cancel_shutdown ? "True" : "False");

        gsm_client_interaction_done (GSM_CLIENT (client), cancel_shutdown);
}

static void
save_yourself_done_callback (SmsConn   conn,
                             SmPointer manager_data,
                             Bool      success)
{
        GsmXSMPClient *client = manager_data;

        g_debug ("Client '%s' received SaveYourselfDone(success = %s)",
                 client->priv->description,
                 success ? "True" : "False");

        if (client->priv->current_save_yourself == SmSaveLocal) {
                client->priv->current_save_yourself = -1;
                SmsSaveComplete (client->priv->conn);
                gsm_client_saved_state (GSM_CLIENT (client));
        } else {
                client->priv->current_save_yourself = -1;
                gsm_client_save_yourself_done (GSM_CLIENT (client));
        }

        if (client->priv->next_save_yourself) {
                int save_type = client->priv->next_save_yourself;

                client->priv->next_save_yourself = -1;
                do_save_yourself (client, save_type);
        }
}

static void
close_connection_callback (SmsConn     conn,
                           SmPointer   manager_data,
                           int         count,
                           char      **reason_msgs)
{
        GsmXSMPClient *client = manager_data;
        int            i;

        g_debug ("Client '%s' received CloseConnection", client->priv->description);
        for (i = 0; i < count; i++) {
                g_debug (" close reason: '%s'", reason_msgs[i]);
        }
        SmFreeReasons (count, reason_msgs);

        gsm_client_disconnected (GSM_CLIENT (client));
}

void
gsm_xsmp_client_connect (GsmXSMPClient *client,
                         SmsConn        conn,
                         unsigned long *mask_ret,
                         SmsCallbacks  *callbacks_ret)
{
        client->priv->conn = conn;

        if (client->priv->protocol_timeout) {
                g_source_remove (client->priv->protocol_timeout);
                client->priv->protocol_timeout = 0;
        }

        g_debug ("Initializing client %s", client->priv->description);

        *mask_ret = 0;

        *mask_ret |= SmsRegisterClientProcMask;
        callbacks_ret->register_client.callback = register_client_callback;
        callbacks_ret->register_client.manager_data  = client;

        *mask_ret |= SmsInteractRequestProcMask;
        callbacks_ret->interact_request.callback = interact_request_callback;
        callbacks_ret->interact_request.manager_data = client;

        *mask_ret |= SmsInteractDoneProcMask;
        callbacks_ret->interact_done.callback = interact_done_callback;
        callbacks_ret->interact_done.manager_data = client;

        *mask_ret |= SmsSaveYourselfRequestProcMask;
        callbacks_ret->save_yourself_request.callback = save_yourself_request_callback;
        callbacks_ret->save_yourself_request.manager_data = client;

        *mask_ret |= SmsSaveYourselfP2RequestProcMask;
        callbacks_ret->save_yourself_phase2_request.callback = save_yourself_phase2_request_callback;
        callbacks_ret->save_yourself_phase2_request.manager_data = client;

        *mask_ret |= SmsSaveYourselfDoneProcMask;
        callbacks_ret->save_yourself_done.callback = save_yourself_done_callback;
        callbacks_ret->save_yourself_done.manager_data = client;

        *mask_ret |= SmsCloseConnectionProcMask;
        callbacks_ret->close_connection.callback = close_connection_callback;
        callbacks_ret->close_connection.manager_data  = client;

        *mask_ret |= SmsSetPropertiesProcMask;
        callbacks_ret->set_properties.callback = set_properties_callback;
        callbacks_ret->set_properties.manager_data = client;

        *mask_ret |= SmsDeletePropertiesProcMask;
        callbacks_ret->delete_properties.callback = delete_properties_callback;
        callbacks_ret->delete_properties.manager_data = client;

        *mask_ret |= SmsGetPropertiesProcMask;
        callbacks_ret->get_properties.callback = get_properties_callback;
        callbacks_ret->get_properties.manager_data = client;
}
