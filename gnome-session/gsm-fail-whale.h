/* gsm-fail-whale.h
 * Copyright (C) 2012 Red Hat, Inc.
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
 */

#ifndef __GSM_FAIL_WHALE_H__
#define __GSM_FAIL_WHALE_H__

#include <glib.h>

G_BEGIN_DECLS

void        gsm_fail_whale_dialog_we_failed  (gboolean            debug_mode,
                                              gboolean            allow_logout);

G_END_DECLS

#endif /* __GSM_FAIL_WHALE_H__ */

