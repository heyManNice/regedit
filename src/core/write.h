/* 配置写回安全管线：冲突检测 → 备份 → 原子写 → 校验 → 回滚 */
#ifndef LR_CORE_WRITE_H
#define LR_CORE_WRITE_H

#include <glib.h>
#include "core/edits.h"

/* 把 edits 安全写回 path。
 * source_content 必须是打开文件时的原文快照；磁盘内容与快照不一致即中止。
 * 备份写入 $XDG_DATA_HOME/linux-regedit/backups。 */
gboolean lr_save_config_file(const char *path, const char *source_content,
                             const LrEdit *edits, gsize n_edits,
                             GError **error);

#endif /* LR_CORE_WRITE_H */
