#ifndef __SESSION_PROPERTIES_COMMANDS_H__ 
#define __SESSION_PROPERTIES_COMMANDS_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

enum
{
  STORE_COL_ENABLED = 0,
  STORE_COL_ICON_NAME,
  STORE_COL_DESCRIPTION,
  STORE_COL_NAME,
  STORE_COL_COMMAND,
  STORE_COL_COMMENT,
  STORE_COL_DESKTOP_FILE,
  STORE_COL_ID,
  STORE_COL_ACTIVATABLE, 
  STORE_NUM_COLS              
};

GtkTreeModel*   spc_command_get_store            (void);

gboolean        spc_command_enable_app           (GtkListStore *store, 
                                                  GtkTreeIter  *iter);

gboolean        spc_command_disable_app          (GtkListStore *store, 
                                                  GtkTreeIter  *iter);

void            spc_command_add_app              (GtkListStore *store,
                                                  GtkTreeIter  *iter);

void            spc_command_delete_app           (GtkListStore *store, 
                                                  GtkTreeIter  *iter);

void            spc_command_update_app           (GtkListStore *store,
                                                  GtkTreeIter  *iter);

char*           spc_command_get_app_description  (const char *name,
                                                  const char *comment);

G_END_DECLS

#endif /* __SESSION_PROPERTIES_COMMANDS_H__ */
