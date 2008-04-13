/* app-autostart.h
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

#ifndef __GSM_APP_AUTOSTART_H__
#define __GSM_APP_AUTOSTART_H__

#include "app.h"

#include <X11/SM/SMlib.h>

G_BEGIN_DECLS

#define GSM_TYPE_APP_AUTOSTART            (gsm_app_autostart_get_type ())
#define GSM_APP_AUTOSTART(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSM_TYPE_APP_AUTOSTART, GsmAppAutostart))
#define GSM_APP_AUTOSTART_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GSM_TYPE_APP_AUTOSTART, GsmAppAutostartClass))
#define GSM_IS_APP_AUTOSTART(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSM_TYPE_APP_AUTOSTART))
#define GSM_IS_APP_AUTOSTART_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GSM_TYPE_APP_AUTOSTART))
#define GSM_APP_AUTOSTART_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GSM_TYPE_APP_AUTOSTART, GsmAppAutostartClass))

typedef struct _GsmAppAutostart        GsmAppAutostart;
typedef struct _GsmAppAutostartClass   GsmAppAutostartClass;
typedef struct _GsmAppAutostartPrivate GsmAppAutostartPrivate;

struct _GsmAppAutostart
{
  GsmApp parent;

  GsmAppAutostartPrivate *priv;
};

struct _GsmAppAutostartClass
{
  GsmAppClass parent_class;

  /* signals */
  void     (*condition_changed)  (GsmApp *app, gboolean condition);
};

GType   gsm_app_autostart_get_type           (void) G_GNUC_CONST;

GsmApp *gsm_app_autostart_new                (const char *desktop_file,
					      const char *client_id);

G_END_DECLS

#endif /* __GSM_APP_AUTOSTART_H__ */
