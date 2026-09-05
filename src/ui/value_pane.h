/* 右侧配置项面板：解析后的配置项表格，或文本兜底视图 */
#ifndef LR_UI_VALUE_PANE_H
#define LR_UI_VALUE_PANE_H

#include <gtk/gtk.h>

typedef struct _LrValuePane LrValuePane;

/* 表格内容被内存编辑（不落盘）时回调，供主窗口显示未保存提示 */
typedef void (*LrValuePaneDirtyCb)(gboolean dirty, gpointer user_data);

LrValuePane *lr_value_pane_new(void);

void lr_value_pane_set_dirty_cb(LrValuePane *self, LrValuePaneDirtyCb cb,
                                gpointer user_data);

/* 把当前表格中可写回的变化安全保存（值/启用；其余变化返回错误）。 */
gboolean lr_value_pane_save_changes(LrValuePane *self, GError **error);

/* 获取面板顶层 widget（GtkStack） */
GtkWidget *lr_value_pane_get_widget(LrValuePane *self);

/* 加载并展示一个配置文件（自动检测格式；不支持则文本兜底） */
void lr_value_pane_load_file(LrValuePane *self, const char *path);

/* 清空为占位视图 */
void lr_value_pane_clear(LrValuePane *self);

/* 在当前表格中查找（大小写不敏感，匹配 名称/数据/备注）。
 * first：从头查找，*matches 返回总命中数并选中第一项；
 * next：从上次命中继续查找并在结尾处循环。 */
gboolean lr_value_pane_search_first(LrValuePane *self, const char *needle,
                                    guint *matches);
gboolean lr_value_pane_search_next(LrValuePane *self);
gboolean lr_value_pane_search_has_query(LrValuePane *self);

/* 在表格末尾追加一个配置项（仅内存，不写盘）；type ∈ Section/String/Boolean/Number */
void lr_value_pane_add_value(LrValuePane *self, const char *type);

void lr_value_pane_free(LrValuePane *self);

#endif /* LR_UI_VALUE_PANE_H */
