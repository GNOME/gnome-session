#/* gsm-client-row.h - a gsm-client object for entry into a clist

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

#ifndef GSM_CLIENT_ROW_H
#define GSM_CLIENT_ROW_H

#include "gsm-protocol.h"
#include "gsm-client-list.h"

#define GSM_IS_CLIENT_ROW(obj)      GTK_CHECK_TYPE (obj, gsm_client_row_get_type ())
#define GSM_CLIENT_ROW(obj)         GTK_CHECK_CAST (obj, gsm_client_row_get_type (), GsmClientRow)
#define GSM_CLIENT_ROW_CLASS(klass) GTK_CHECK_CLASS_CAST (klass, gsm_client_row_get_type (), GsmClientRowClass)

typedef struct _GsmClientRow GsmClientRow;
typedef struct _GsmClientRowClass GsmClientRowClass;

typedef enum {
  GSM_CLIENT_ROW_ADD,
  GSM_CLIENT_ROW_REMOVE,
  GSM_CLIENT_ROW_EDIT,
  GSM_CLIENT_ROW_NONE
} GsmClientRowChange;

struct _GsmClientRow {
  GsmClient client;

  GsmClientList*     client_list;  /* where we are displayed (when visible) */
  gint               row;          /* row in the client_list */

  /* used in implementing reversion */
  GsmClientRowChange change;
  gint               revert_order;
  gint               revert_style;
};

struct _GsmClientRowClass {
  GsmClientClass parent_class;
};

guint gsm_client_row_get_type  (void);

/* creates a client row to appear in the GsmClientList */
GtkObject* gsm_client_row_new (GsmClientList* client_list);

/* removes the row from its GsmClientList */
void gsm_client_row_remove (GsmClientRow* client_row);

/* adds the row into its GsmClientList */
void gsm_client_row_add (GsmClientRow* client_row);

/* changes the restart style for the row */
void       gsm_client_row_set_style (GsmClientRow *client_row, GsmStyle style);

/* changes the start order for the row */
void       gsm_client_row_set_order (GsmClientRow *client_row, guint order);

/* menu data */
extern GnomeUIInfo state_data[GSM_NSTATES + 1];
extern GnomeUIInfo style_data[GSM_NSTYLES + 1];

#endif /* GSM_CLIENT_ROW_H */
