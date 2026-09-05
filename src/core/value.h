/*
 * linux-regedit — 配置值类型与数据模型
 *
 * 将配置文件中的每一行配置建模为一个 LrConfigItem（类比注册表的一个值）：
 *   - 名称 (key)
 *   - 类型 (type)：数字 / 字符串 / 布尔值（类比 REG_DWORD / REG_SZ）
 *   - 数据 (data)：原始值文本
 *   - 节 (section)：INI / systemd 等格式的 [Section]，可为 NULL
 *   - 备注 (comment)：来自配置文件中的注释（# ; //），可为 NULL
 */
#ifndef LR_CORE_VALUE_H
#define LR_CORE_VALUE_H

#include <glib.h>

typedef enum
{
    LR_VALUE_NUMBER = 0, /* 数字，类比 REG_DWORD / REG_QWORD */
    LR_VALUE_STRING,     /* 字符串，类比 REG_SZ */
    LR_VALUE_BOOL,       /* 布尔，类比 REG_DWORD (0/1) */
    LR_VALUE_SECTION,    /* 节/容器行（UI 模型用，非解析产物） */
} LrValueType;

typedef struct
{
    char *key;        /* 配置项名称 */
    LrValueType type; /* 识别出的类型 */
    char *data;       /* 原始值文本（去引号、去注释后的值） */
    char *section;    /* 所属节，无节为 NULL */
    char *comment;    /* 备注（注释内容），可为 NULL */
    gboolean enabled; /* 是否启用（被注释的配置为 FALSE） */
    guint source_line; /* 0 起原文行号；未知/非行格式为 G_MAXUINT */
} LrConfigItem;

/* 一个已解析的配置文件（类比一个注册表键下的值集合） */
typedef struct
{
    char *path;       /* 文件路径 */
    gboolean parsed;  /* 是否解析成功 */
    char *error;      /* 解析错误信息，可为 NULL */
    GPtrArray *items; /* LrConfigItem* 数组 */
} LrConfigFile;

/* 类型识别：根据值文本启发式判断其类型 */
LrValueType lr_value_detect_type(const char *value);

/* 类型的规范英文标识（Number/String/Boolean/Section），用于模型与表格 */
const char *lr_value_type_name(LrValueType type);

/* 由规范标识反查类型；未知名称一律回退为 LR_VALUE_STRING */
LrValueType lr_value_type_from_name(const char *name);

/* 表格“类型”列的全部取值（Section/String/Boolean/Number，NULL 结尾） */
const char *const *lr_value_type_names(void);

/* 构造 / 释放一个配置项 */
LrConfigItem *lr_config_item_new(const char *key, const char *data,
                                 LrValueType type, const char *section,
                                 const char *comment);
void lr_config_item_free(LrConfigItem *item);

/* 构造 / 释放一个解析后的配置文件 */
LrConfigFile *lr_config_file_new(const char *path);
void lr_config_file_free(LrConfigFile *file);

#endif /* LR_CORE_VALUE_H */
