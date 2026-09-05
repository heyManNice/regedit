/* 主窗口：菜单栏 + 左右分栏（文件树 | 配置项面板）+ 状态栏 */
#ifndef LR_UI_MAIN_WINDOW_H
#define LR_UI_MAIN_WINDOW_H

#include <gtk/gtk.h>
#include "ui/tree_pane.h"
#include "ui/value_pane.h"

typedef struct _LrWindowState LrWindowState;

/* 主窗口对象：布局与内部状态（收藏夹/导出等子模块需访问字段） */
typedef struct _LrMainWindow
{
    GtkWidget *window;
    LrTreePane *tree;
    LrValuePane *value;
    GtkWidget *location_entry;
    GtkWidget *location_bar;  /* 地址栏容器（查看→地址栏 切换） */
    char *current_path;       /* 当前打开/选中的路径 */
    char *pending_path;       /* 恢复状态时待定位的路径 */
    LrWindowState *win_state; /* 窗口几何与上次路径状态 */
    GtkWidget *find_dialog;   /* 查找对话框（Find…，单例） */
    guint reveal_idle;        /* 待执行的“恢复上次路径” idle 源 id */
    guint reopen_idle;        /* 待执行的“重开命令行文件” idle 源 id */
    gchar *reopen_path;       /* 重开目标（根目录外文件，避开首帧选中风暴） */
} LrMainWindow;

LrMainWindow *lr_main_window_new(void);
GtkWidget *lr_main_window_get_window(LrMainWindow *self);
void lr_main_window_free(LrMainWindow *self);

/* 从 /run 会话状态恢复窗口大小、位置与上次路径 */
void lr_main_window_restore_state(LrMainWindow *self);

/* 直接打开指定文件（或定位到目录）：供命令行 / 外部调用 */
void lr_main_window_open_file(LrMainWindow *self, const char *path);

#endif /* LR_UI_MAIN_WINDOW_H */
