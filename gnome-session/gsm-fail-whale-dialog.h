/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Red Hat, Inc.
 * Copyright (C) 2019 Canonical Ltd.
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
 *	Colin Walters <walters@verbum.org>
 *      Marco Trevisan <marco@ubuntu.com>
 */

#ifndef __GSM_FAIL_WHALE_DIALOG_H__
#define __GSM_FAIL_WHALE_DIALOG_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GSM_TYPE_FAIL_WHALE_DIALOG         (gsm_fail_whale_dialog_get_type ())
G_DECLARE_FINAL_TYPE (GsmFailWhaleDialog, gsm_fail_whale_dialog, GSM, FAIL_WHALE_DIALOG, GtkWindow);

G_END_DECLS

#endif /* __GSM_FAIL_WHALE_DIALOG_H__ */
