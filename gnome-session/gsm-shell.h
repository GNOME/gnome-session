/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Red Hat, Inc.
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
 *
 * Authors:
 *    Ray Strode <rstrode@redhat.com>
 */

#ifndef __GSM_SHELL_H__
#define __GSM_SHELL_H__

#include <glib.h>
#include <glib-object.h>

#include "gsm-store.h"

G_BEGIN_DECLS

#define GSM_TYPE_SHELL             (gsm_shell_get_type ())
#define GSM_SHELL(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSM_TYPE_SHELL, GsmShell))
#define GSM_SHELL_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GSM_TYPE_SHELL, GsmShellClass))
#define GSM_IS_SHELL(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSM_TYPE_SHELL))
#define GSM_IS_SHELL_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GSM_TYPE_SHELL))
#define GSM_SHELL_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GSM_TYPE_SHELL, GsmShellClass))
#define GSM_SHELL_ERROR            (gsm_shell_error_quark ())

typedef struct _GsmShell        GsmShell;
typedef struct _GsmShellClass   GsmShellClass;
typedef struct _GsmShellPrivate GsmShellPrivate;

struct _GsmShell
{
        GObject               parent;

        GsmShellPrivate *priv;
};

struct _GsmShellClass
{
        GObjectClass parent_class;

};

GType            gsm_shell_get_type           (void);

GsmShell        *gsm_shell_new                (void);

GsmShell        *gsm_get_shell                (void);
gboolean         gsm_shell_is_running         (GsmShell *shell);

G_END_DECLS

#endif /* __GSM_SHELL_H__ */
