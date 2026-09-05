#include "core/parsers/keyword.h"

#include <string.h>

/* 将参数中的连续空白（空格/Tab）压缩为单个空格 */
static char *
collapse_whitespace(const char *s)
{
    GString *out = g_string_new(NULL);
    gboolean last_ws = FALSE;
    const char *p;

    for (p = s; *p != '\0'; p++)
    {
        if (*p == ' ' || *p == '\t')
        {
            if (!last_ws && out->len > 0)
                g_string_append_c(out, ' ');
            last_ws = TRUE;
        }
        else
        {
            g_string_append_c(out, *p);
            last_ws = FALSE;
        }
    }
    return g_string_free(out, FALSE);
}

/* 是否含非 ASCII 字节（中文等说明文字特征） */
static gboolean
has_non_ascii(const char *s)
{
    for (; *s != '\0'; s++)
    {
        if ((guchar)*s >= 0x80)
            return TRUE;
    }
    return FALSE;
}

gboolean
lr_parse_keyword(const char *content, gsize length, LrConfigFile *file)
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
        char *content;
        char *space;
        char *key, *value;
        gboolean commented = FALSE;

        if (*line == '\0')
            continue;

        if (line[0] == '#')
        {
            char *p = line;
            char *sp;

            while (*p == '#')
                p++;
            content = g_strstrip(p);
            if (*content == '\0')
                continue;

            /* 被注释的配置：关键字 + 单 token 参数（如 #Port 22）。
             * 含非 ASCII（如中文说明）一律视为说明文字，避免误判 */
            if ((g_ascii_isalpha(content[0]) || content[0] == '_') &&
                !has_non_ascii(content))
            {
                sp = strchr(content, ' ');
                if (sp == NULL)
                    sp = strchr(content, '\t');
                if (sp != NULL && strchr(sp + 1, ' ') == NULL &&
                    strchr(sp + 1, '\t') == NULL)
                {
                    commented = TRUE; /* 是被注释的配置 */
                }
                else
                {
                    /* 说明文字：记录为「上方最近一条」注释 */
                    g_free(pending_comment);
                    pending_comment = g_strdup(content);
                    continue;
                }
            }
            else
            {
                /* 说明文字 */
                g_free(pending_comment);
                pending_comment = g_strdup(content);
                continue;
            }
        }
        else
        {
            content = line;
        }

        /* 第一个空白分隔：前为关键字，后为参数 */
        space = strchr(content, ' ');
        if (space == NULL)
            space = strchr(content, '\t');
        if (space == NULL)
            continue;

        *space = '\0';
        key = g_strstrip(content);
        value = g_strstrip(space + 1);
        /* 关键字不应含空白：若含说明分隔异常（如多值配置被误切），跳过 */
        if (*key == '\0' || *value == '\0' || strchr(key, ' ') != NULL ||
            strchr(key, '\t') != NULL)
            continue;

        {
            char *norm = collapse_whitespace(value);
            LrConfigItem *item = lr_config_item_new(
                key, norm, lr_value_detect_type(norm), NULL, pending_comment);
            item->enabled = !commented;
            item->source_line = line_idx;
            g_ptr_array_add(file->items, item);
            g_free(norm);
        }
        file->parsed = TRUE;
    }

    g_strfreev(lines);
    g_free(pending_comment);
    return file->parsed;
}
