/*
 * gsm-atk.c: atk util functions
 *
 * Copyright 2002 Sun Microsystems, Inc.
 *
 * Author: Jacob Berkman  <jacob@ximian.com>
 *
 */

#include <config.h>
#include "gsm-atk.h"

#include <atk/atkobject.h>

void
gsm_atk_set_name (GtkWidget *w, const char *name)
{
  AtkObject *atk = gtk_widget_get_accessible (w);
  if (atk) atk_object_set_name (atk, name);
}

void
gsm_atk_set_description (GtkWidget *w, const char *desc)
{
  AtkObject *atk = gtk_widget_get_accessible (w);
  if (atk) atk_object_set_description (atk, desc);
}

