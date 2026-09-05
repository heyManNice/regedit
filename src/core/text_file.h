/* 受保护的文件文本读取：树浏览与“直接打开”共用同一套安全规则 */
#ifndef LR_CORE_TEXT_FILE_H
#define LR_CORE_TEXT_FILE_H

#include <glib.h>

typedef enum
{
    LR_TEXT_OK = 0,     /* 读取成功 */
    LR_TEXT_TOO_LARGE,  /* 超过 LR_MAX_FILE_SIZE */
    LR_TEXT_BINARY,     /* 内容含 NUL，判为非文本 */
    LR_TEXT_ERROR,      /* 打开/读取失败（*error 携带系统错误） */
} LrTextReadStatus;

/* 判断缓冲区是否含 NUL（非文本特征） */
gboolean lr_text_is_binary(const guchar *buf, gsize len);

/* 读取一个“可展示文本文件”：
 * 先按大小上限过滤，再整读，最后全量 NUL 检查。
 * 仅 LR_TEXT_OK 时输出 content 与 len 有效，调用方负责释放 content。 */
LrTextReadStatus lr_text_file_read(const char *path, gchar **content,
                                   gsize *len, GError **error);

#endif /* LR_CORE_TEXT_FILE_H */
