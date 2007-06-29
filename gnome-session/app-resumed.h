/* app-sm.h
 * Copyright (C) 2006 Novell, Inc.
 *
 */

#ifndef __GSM_APP_RESUMED_H__
#define __GSM_APP_RESUMED_H__

#include "app.h"

G_BEGIN_DECLS

#define GSM_TYPE_APP_RESUMED            (gsm_app_resumed_get_type ())
#define GSM_APP_RESUMED(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSM_TYPE_APP_RESUMED, GsmAppResumed))
#define GSM_APP_RESUMED_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GSM_TYPE_APP_RESUMED, GsmAppResumedClass))
#define GSM_IS_APP_RESUMED(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSM_TYPE_APP_RESUMED))
#define GSM_IS_APP_RESUMED_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GSM_TYPE_APP_RESUMED))
#define GSM_APP_RESUMED_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GSM_TYPE_APP_RESUMED, GsmAppResumedClass))

typedef struct _GsmAppResumed        GsmAppResumed;
typedef struct _GsmAppResumedClass   GsmAppResumedClass;
typedef struct _GsmAppResumedPrivate GsmAppResumedPrivate;

struct _GsmAppResumed
{
	GsmApp parent;

	char *program, *restart_command, *discard_command;
	gboolean discard_after_resume;
};

struct _GsmAppResumedClass
{
	GsmAppClass parent_class;

	/* signals */

	/* virtual methods */

};

GType   gsm_app_resumed_get_type (void) G_GNUC_CONST;


GsmApp *gsm_app_resumed_new_from_session        (GKeyFile   *session_file,
						 const char *group,
						 gboolean    discard);
GsmApp *gsm_app_resumed_new_from_legacy_session (GKeyFile   *session_file,
						 int         n);

G_END_DECLS

#endif /* __GSM_APP_RESUMED_H__ */
