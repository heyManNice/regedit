/* 配置文件格式识别与解析分发 */
#ifndef LR_CORE_FORMAT_H
#define LR_CORE_FORMAT_H

#include <glib.h>
#include "core/value.h"

typedef enum
{
    LR_FORMAT_UNKNOWN = 0, /* 不支持：以文本编辑器兜底展示 */
    LR_FORMAT_INI,         /* INI：分节键值对 */
    LR_FORMAT_KV,          /* 扁平 key=value */
    LR_FORMAT_SYSTEMD,     /* systemd unit 文件 */
    LR_FORMAT_KEYWORD,     /* 关键字-参数（空白分隔），如 sshd_config */
    LR_FORMAT_JSON,        /* JSON（嵌套结构，树形展示） */
    LR_FORMAT_APT,         /* apt 配置（嵌套块 + :: 键，树形展示） */
    LR_FORMAT_XML,         /* XML（嵌套元素树，树形展示） */
    LR_FORMAT_TOML,        /* TOML（节 + 键值对） */
} LrConfigFormat;

/* 根据文件名与文件内容启发式判断格式 */
LrConfigFormat lr_format_detect(const char *path);

/* 基于已读取的内容检测格式（避免重复读文件） */
LrConfigFormat lr_format_detect_content(const char *path,
                                        const char *content, gsize len);

/* 格式的中文显示名 */
const char *lr_format_name(LrConfigFormat fmt);

/* 是否为受支持的格式 */
gboolean lr_format_supported(LrConfigFormat fmt);

/* 解析入口：检测格式并调用对应解析器，返回解析后的文件（调用方释放） */
LrConfigFile *lr_parse_config(const char *path);

/* 基于已读取的内容解析（避免重复读文件），调用方释放返回的 file */
LrConfigFile *lr_parse_config_content(const char *path,
                                      const char *content, gsize len);

/* 新建文件时可选的格式显示名（静态数组，NULL 结尾） */
const char *const *lr_format_new_file_names(void);

#endif /* LR_CORE_FORMAT_H */
