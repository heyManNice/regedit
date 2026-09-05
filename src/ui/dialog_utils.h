/* 通用对话框工具：独立子窗口居中、响应即销毁 */
#ifndef LR_UI_DIALOG_UTILS_H
#define LR_UI_DIALOG_UTILS_H

#include <gtk/gtk.h>

/* 独立子窗口：显示并把位置定到主窗口中心（不与父窗口联动） */
void lr_dialog_center_on(GtkWidget *dialog, GtkWindow *parent);

/* 对话框任何响应（含关闭）都销毁自身 */
void lr_dialog_destroy_on_response(GtkDialog *dialog, gint response_id,
                                   gpointer user_data);

#endif /* LR_UI_DIALOG_UTILS_H */
