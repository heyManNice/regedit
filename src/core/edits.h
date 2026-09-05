/* 行级安全写回：对原文做最小化补丁，而不是整文件重新序列化 */
#ifndef LR_CORE_EDITS_H
#define LR_CORE_EDITS_H

#include <glib.h>

typedef enum
{
    LR_EDIT_SET_VALUE, /* 只替换 value 区段（保留引号/行内注释/前缀） */
    LR_EDIT_ENABLE,    /* 去掉行首注释标记 */
    LR_EDIT_DISABLE,   /* 在行首加注释标记 */
} LrEditType;

typedef struct
{
    LrEditType type;
    guint line;        /* 0 起原文行号（来自 source_line） */
    const char *key;   /* 该行应包含的键（防错位），可为 NULL 跳过校验 */
    const char *value; /* 仅 LR_EDIT_SET_VALUE 使用 */
} LrEdit;

/* UI 表格一行当前状态的快照（用于与原文模型对比生成补丁） */
typedef struct
{
    guint line;         /* 0 起原文行号；G_MAXUINT = 新增行（暂不支持） */
    const char *key;    /* 当前键名 */
    const char *data;   /* 当前值 */
    const char *enabled; /* "true" / "false" */
    const char *comment; /* 当前备注（可为 NULL/""） */
    const char *type;    /* 当前类型显示名（Number/...） */
} LrRowState;

/* 由行状态与原文生成编辑补丁：值/启用的变化可保存；
 * 重命名、类型覆盖、备注修改、新增行等暂不支持并报错。 */
gboolean lr_build_edits_from_rows(const char *path,
                                  const char *source_content,
                                  const LrRowState *rows, gsize n_rows,
                                  LrEdit **edits_out, gsize *n_edits_out,
                                  GError **error);

/* 依次应用 edits 到 content，返回新文本（成功）或 FALSE + error。
 * 未编辑的行逐字保留；行尾/注释/引号风格尽量不变。 */
gboolean lr_apply_edits(const char *content, const LrEdit *edits,
                        gsize n_edits, gchar **out, GError **error);

#endif /* LR_CORE_EDITS_H */
