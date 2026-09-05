#include "core/parsers/kv.h"
#include "core/parsers/common.h"

gboolean
lr_parse_kv(const char *content, gsize length, LrConfigFile *file)
{
    gchar **lines = NULL;
    gchar **linep;
    char *pending_comment = NULL;
    guint line_idx = 0;

    (void)length;
    if (content == NULL)
    {
        file->parsed = FALSE;
        return FALSE;
    }

    lines = g_strsplit(content, "\n", -1);

    for (line_idx = 0; lines[line_idx] != NULL; line_idx++)
    {
        linep = &lines[line_idx];
        char *line = g_strstrip(*linep);

        if (*line == '\0')
            continue;

        /* 注释行：说明文字（# / ;） */
        if (lr_capture_comment(line, &pending_comment))
            continue;

        {
            char *key = NULL, *raw_value = NULL;
            /* 扁平格式允许 = 或 : 或空白分隔 */
            if (!lr_split_key_value(line, "=: \t", &key, &raw_value))
                continue;

            char *inline_comment = NULL;
            char *value = lr_strip_inline_comment(raw_value, &inline_comment);

            LrConfigItem *item = lr_config_item_new(
                key, value, lr_value_detect_type(value), NULL,
                pending_comment != NULL ? pending_comment : inline_comment);
            item->source_line = line_idx;
            if (pending_comment != NULL && inline_comment != NULL)
            {
                char *merged = g_strconcat(pending_comment, "\n",
                                           inline_comment, NULL);
                g_free(item->comment);
                item->comment = merged;
            }

            g_ptr_array_add(file->items, item);
            file->parsed = TRUE;

            g_free(key);
            g_free(raw_value);
            g_free(value);
            g_free(inline_comment);
        }
    }

    g_strfreev(lines);
    g_free(pending_comment);
    return file->parsed;
}
