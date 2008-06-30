/* gsm-client-editor.h - a widget to edit the details of a GsmClient.

   Copyright 1999 Free Software Foundation, Inc.

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
   02111-1307, USA. 

   Authors: Felix Bellaby */

#ifndef GSM_CLIENT_EDITOR_H
#define GSM_CLIENT_EDITOR_H

#include <gtk/gtk.h>
#include "gsm-protocol.h"

#define GSM_TYPE_CLIENT_EDITOR         (gsm_client_editor_get_type ())
#define GSM_CLIENT_EDITOR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GSM_TYPE_CLIENT_EDITOR, GsmClientEditor))
#define GSM_CLIENT_EDITOR_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GSM_TYPE_CLIENT_EDITOR, GsmClientEditorClass))
#define GSM_IS_CLIENT_EDITOR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GSM_TYPE_CLIENT_EDITOR))
#define GSM_IS_CLIENT_EDITOR_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GSM_TYPE_CLIENT_EDITOR))
#define GSM_CLIENT_EDITOR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GSM_TYPE_CLIENT_EDITOR, GsmClientEditorClass))

typedef struct _GsmClientEditor GsmClientEditor;
typedef struct _GsmClientEditorClass GsmClientEditorClass;

struct _GsmClientEditor {
  GtkHBox hbox;

  GsmClient* client;
  GtkWidget *spin_button;
  GtkWidget *style_menu;
};

struct _GsmClientEditorClass {
  GtkHBoxClass parent_class;

  void (* changed) (GsmClientEditor* client_editor, 
		    guint order, GsmStyle style);    /* user change */
};

GtkType gsm_client_editor_get_type  (void);

/* Creates a client_editor widget */
GtkWidget* gsm_client_editor_new (void);

/* Sets the client being edited */
void  gsm_client_editor_set_client (GsmClientEditor* client_editor, 
				    GsmClient* client);

#endif /* GSM_CLIENT_EDITOR_H */
