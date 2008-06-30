/*
 * gsm-atk.h: some atk sugar
 *
 * Copyright 2002 Sun Microsystems, Inc.
 *
 * Author: Jacob Berkman  <jacob@ximian.com>
 *
 */

#ifndef GSM_ATK_H
#define GSM_ATK_H

#include <gtk/gtkwidget.h>

void gsm_atk_set_name        (GtkWidget *w, const char *name);
void gsm_atk_set_description (GtkWidget *w, const char *desc);

#endif /* GSM_ATK_H */
