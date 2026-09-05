/* 文件级工具：备份当前文件 / 与另一文件对比（v0.6） */
#ifndef LR_UI_FILE_TOOLS_H
#define LR_UI_FILE_TOOLS_H

#include <gtk/gtk.h>

/* 把当前文件复制到用户数据目录的 backups/ 下（时间戳命名，自动防覆盖） */
void lr_backup_current_file(GtkWindow *parent, const char *path);

/* 选择另一文件并与当前文件做逐行对比（diff -u，缺失时退化为相同性检查） */
void lr_compare_with_file(GtkWindow *parent, const char *path);

#endif /* LR_UI_FILE_TOOLS_H */
