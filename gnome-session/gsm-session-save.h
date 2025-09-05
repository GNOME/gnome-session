/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
        GSM_RESTORE_REASON_PRISTINE,
        GSM_RESTORE_REASON_LAUNCH,
        GSM_RESTORE_REASON_RECOVER,
        GSM_RESTORE_REASON_RESTORE,
} GsmRestoreReason;

#define GSM_TYPE_SESSION_SAVE (gsm_session_save_get_type ())
G_DECLARE_FINAL_TYPE (GsmSessionSave, gsm_session_save, GSM, SESSION_SAVE, GObject)

GsmSessionSave *gsm_session_save_new         (const char         *session_id);
gboolean        gsm_session_save_restore     (GsmSessionSave     *save);
void            gsm_session_save_seal        (GsmSessionSave     *save);
void            gsm_session_save_unseal      (GsmSessionSave     *save);
gboolean        gsm_session_save_register    (GsmSessionSave     *save,
                                              const char         *app_id,
                                              const char         *dbus_name,
                                              GsmRestoreReason   *out_restore_reason,
                                              char              **out_instance_id,
                                              GStrv              *out_cleanup_ids);
void            gsm_session_save_deleted_ids (GsmSessionSave     *save,
                                              const char         *app_id,
                                              const char *const  *ids);
gboolean        gsm_session_save_unregister  (GsmSessionSave     *save,
                                              const char         *app_id,
                                              const char         *instance_id);

G_END_DECLS
