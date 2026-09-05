#include "core/text_file.h"
#include "core/limits.h"

#include <glib/gstdio.h>

gboolean
lr_text_is_binary(const guchar *buf, gsize len)
{
    gsize i;

    if (buf == NULL)
        return FALSE;
    for (i = 0; i < len; i++)
        if (buf[i] == '\0')
            return TRUE;
    return FALSE;
}

LrTextReadStatus
lr_text_file_read(const char *path, gchar **content, gsize *len,
                  GError **error)
{
    GStatBuf st;
    gchar *data = NULL;
    gsize size = 0;

    *content = NULL;
    *len = 0;

    if (g_stat(path, &st) != 0)
    {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "%s", g_strerror(errno));
        return LR_TEXT_ERROR;
    }
    if (st.st_size > LR_MAX_FILE_SIZE)
        return LR_TEXT_TOO_LARGE;

    if (!g_file_get_contents(path, &data, &size, error))
        return LR_TEXT_ERROR;

    if (lr_text_is_binary((const guchar *)data, size))
    {
        g_free(data);
        return LR_TEXT_BINARY;
    }

    /* 非 UTF-8 文本（如 latin1）转成合法 UTF-8（无效字节替换为 U+FFFD），
     * 避免解析器与 GtkTextView 拿到非法序列 */
    if (!g_utf8_validate(data, size, NULL))
    {
        gchar *valid = g_utf8_make_valid(data, size);
        g_free(data);
        data = valid;
        size = strlen(valid);
    }

    *content = data;
    *len = size;
    return LR_TEXT_OK;
}
