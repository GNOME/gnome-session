/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define GSM_TYPE_SESSION_SAVE (gsm_session_save_get_type ())
G_DECLARE_FINAL_TYPE (GsmSessionSave, gsm_session_save, GSM, SESSION_SAVE, GObject)

GsmSessionSave *gsm_session_save_new           (const char     *session_id);
gboolean        gsm_session_save_restore       (GsmSessionSave *save);
void            gsm_session_save_seal          (GsmSessionSave *save);
void            gsm_session_save_unseal        (GsmSessionSave *save);
void            gsm_session_save_discard       (GsmSessionSave *save);

G_END_DECLS
