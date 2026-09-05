#include "core/edits.h"
#include "core/format.h"

#include <gio/gio.h>
#include <string.h>

typedef struct
{
    gchar *text; /* 不含行尾 */
    gchar *eol;  /* "\n" / "\r\n" / "" */
} LrLine;

static void
line_free(gpointer p)
{
    LrLine *l = p;
    g_free(l->text);
    g_free(l->eol);
    g_free(l);
}

/* 拆成“文本+行尾”，保留 CRLF 与末尾无换行等细节 */
static GPtrArray *
split_lines(const char *content, gsize len)
{
    GPtrArray *lines = g_ptr_array_new_with_free_func(line_free);
    gsize start = 0;

    if (content == NULL || len == 0)
        return lines;

    while (start < len)
    {
        const char *nl = memchr(content + start, '\n', len - start);
        gsize seg_len;
        gboolean crlf = FALSE;
        LrLine *l = g_new0(LrLine, 1);

        if (nl == NULL)
        {
            l->text = g_strndup(content + start, len - start);
            l->eol = g_strdup("");
            g_ptr_array_add(lines, l);
            break;
        }

        seg_len = (gsize)(nl - (content + start));
        if (seg_len > 0 && content[start + seg_len - 1] == '\r')
        {
            crlf = TRUE;
            seg_len--;
        }
        l->text = g_strndup(content + start, seg_len);
        l->eol = g_strdup(crlf ? "\r\n" : "\n");
        g_ptr_array_add(lines, l);
        start = (gsize)(nl - content) + 1;
    }
    return lines;
}

static char *
rebuild_lines(GPtrArray *lines)
{
    GString *out = g_string_new(NULL);
    guint i;

    for (i = 0; i < lines->len; i++)
    {
        LrLine *l = g_ptr_array_index(lines, i);
        g_string_append(out, l->text);
        g_string_append(out, l->eol);
    }
    return g_string_free(out, FALSE);
}

static gsize
skip_spaces_from(const char *s, gsize i)
{
    while (s[i] == ' ' || s[i] == '\t')
        i++;
    return i;
}

/* 解析一行键值结构（支持行首注释前缀的“禁用行”风格）：
 * 找到键、分隔符、值区段与行内注释；返回键指针/长度与各位置。 */
static gboolean
parse_kv_line(const char *line, gsize *key_start, gsize *key_len,
              gsize *sep, gsize *value_begin, gsize *value_end,
              gsize *comment_start)
{
    gsize i, k;
    char quote = 0;

    i = skip_spaces_from(line, 0);
    /* 行首注释标记（#/; 或 //），跳到标记后的键 */
    while (line[i] == '#' || line[i] == ';' ||
           (line[i] == '/' && line[i + 1] == '/'))
    {
        if (line[i] == '/' )
            i += 2;
        else
            i++;
        i = skip_spaces_from(line, i);
    }

    *key_start = i;
    while (line[i] != '\0' && line[i] != '=' && line[i] != ':' &&
           line[i] != ' ' && line[i] != '\t')
        i++;
    *key_len = i - *key_start;
    if (*key_len == 0)
        return FALSE;

    {
        gsize ws_sep = i;
        while (line[i] == ' ' || line[i] == '\t')
            i++;
        if (line[i] == '=' || line[i] == ':')
        {
            *sep = i;
            i++;
            *value_begin = skip_spaces_from(line, i);
        }
        else if (ws_sep > *key_start && line[ws_sep] != '\0')
        {
            /* 空白分隔风格（keyword/扁平 KV）：ws_sep 即分隔位置，
             * i 已越过空白，直接作为 value 起点 */
            *sep = ws_sep;
            *value_begin = i;
        }
        else
        {
            return FALSE;
        }
    }

    /* 找行内注释起点（引号外）与值结束 */
    *comment_start = strlen(line);
    for (k = *value_begin; line[k] != '\0'; k++)
    {
        char c = line[k];
        if (quote != 0)
        {
            if (c == quote)
                quote = 0;
            continue;
        }
        if (c == '"' || c == '\'')
        {
            quote = c;
            continue;
        }
        if (c == '#' || c == ';')
        {
            *comment_start = k;
            break;
        }
    }

    *value_end = *comment_start;
    while (*value_end > *value_begin &&
           (line[*value_end - 1] == ' ' || line[*value_end - 1] == '\t'))
        (*value_end)--;
    return TRUE;
}

static gboolean
line_has_key(const char *line, const char *key)
{
    gsize ks, kl, sep, vb, ve, cs;
    gchar *found;
    gboolean ok;

    if (!parse_kv_line(line, &ks, &kl, &sep, &vb, &ve, &cs))
        return FALSE;
    found = g_strndup(line + ks, kl);
    ok = g_strcmp0(found, key) == 0;
    g_free(found);
    return ok;
}

static gboolean
apply_set_value(LrLine *l, const LrEdit *e, GError **error)
{
    gsize ks, kl, sep, vb, ve, cs;
    GString *out;
    gchar quote = 0;

    if (!parse_kv_line(l->text, &ks, &kl, &sep, &vb, &ve, &cs))
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "target line is not a key-value line");
        return FALSE;
    }
    if (e->key != NULL && !line_has_key(l->text, e->key))
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "target line key does not match edit");
        return FALSE;
    }

    if (ve > vb)
    {
        char first = l->text[vb];
        char last = l->text[ve - 1];
        if ((first == '"' || first == '\'') && first == last)
            quote = first;
    }

    out = g_string_new(NULL);
    g_string_append_len(out, l->text, vb);
    if (quote != 0)
    {
        g_string_append_c(out, quote);
        g_string_append(out, e->value);
        g_string_append_c(out, quote);
    }
    else
    {
        g_string_append(out, e->value);
    }
    /* 保留值后原有的空白与行内注释 */
    g_string_append(out, l->text + ve);
    g_free(l->text);
    l->text = g_string_free(out, FALSE);
    return TRUE;
}

static gboolean
apply_enable_disable(LrLine *l, const LrEdit *e, gboolean enable,
                     GError **error)
{
    gsize i, start;
    gchar *new_text;

    if (e->key != NULL && !line_has_key(l->text, e->key))
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "target line key does not match edit");
        return FALSE;
    }

    start = skip_spaces_from(l->text, 0);
    if (enable)
    {
        if (l->text[start] != '#' && l->text[start] != ';' &&
            !(l->text[start] == '/' && l->text[start + 1] == '/'))
        {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                "line is not commented");
            return FALSE;
        }
        i = start;
        if (l->text[i] == '/' )
            i += 2;
        else
            i++;
        {
            gchar *head = g_strndup(l->text, start);
            new_text = g_strconcat(head, l->text + i, NULL);
            g_free(head);
        }
    }
    else
    {
        if (l->text[start] == '#' || l->text[start] == ';' ||
            (l->text[start] == '/' && l->text[start + 1] == '/'))
        {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                "line is already commented");
            return FALSE;
        }
        gchar *head = g_strndup(l->text, start);
        new_text = g_strconcat(head, "#", l->text + start, NULL);
        g_free(head);
    }
    g_free(l->text);
    l->text = new_text;
    return TRUE;
}

static LrConfigItem *
item_by_line(LrConfigFile *f, guint line)
{
    guint i;

    for (i = 0; i < f->items->len; i++)
    {
        LrConfigItem *it = g_ptr_array_index(f->items, i);
        if (it->source_line == line)
            return it;
    }
    return NULL;
}

static void
append_edit(LrEdit *edits, gsize *count, gsize cap, LrEditType type,
            guint line, const char *key, const char *value)
{
    if (*count >= cap)
        return;
    edits[*count].type = type;
    edits[*count].line = line;
    edits[*count].key = key;
    edits[*count].value = value;
    (*count)++;
}

gboolean
lr_build_edits_from_rows(const char *path, const char *source_content,
                         const LrRowState *rows, gsize n_rows,
                         LrEdit **edits_out, gsize *n_edits_out,
                         GError **error)
{
    LrConfigFile *orig;
    LrEdit *edits;
    gsize count = 0, i;
    gsize cap = n_rows * 2;

    *edits_out = NULL;
    *n_edits_out = 0;
    orig = lr_parse_config_content(path, source_content,
                                   strlen(source_content));
    if (!orig->parsed && orig->items->len == 0)
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "source file could not be parsed for edits");
        lr_config_file_free(orig);
        return FALSE;
    }

    edits = g_new0(LrEdit, MAX(cap, 1));
    for (i = 0; i < n_rows; i++)
    {
        const LrRowState *r = &rows[i];
        LrConfigItem *it;
        const char *orig_type;
        gboolean comment_ok, enabled_now, enabled_orig, type_ok, key_ok;

        if (r->line == G_MAXUINT)
        {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                                "new rows cannot be saved yet");
            goto fail;
        }
        it = item_by_line(orig, r->line);
        if (it == NULL)
        {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "source line %u not found", r->line);
            goto fail;
        }

        key_ok = g_strcmp0(it->key, r->key) == 0;
        if (!key_ok)
        {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "renaming key on line %u is not supported yet",
                        r->line);
            goto fail;
        }
        orig_type = lr_value_type_name(it->type);
        type_ok = g_strcmp0(orig_type, r->type) == 0;
        if (!type_ok)
        {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "type override on line %u is not supported yet",
                        r->line);
            goto fail;
        }
        comment_ok =
            (it->comment == NULL || *it->comment == '\0')
                ? (r->comment == NULL || *r->comment == '\0')
                : (r->comment != NULL && g_strcmp0(it->comment,
                                                   r->comment) == 0);
        if (!comment_ok)
        {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "comment edit on line %u is not supported yet",
                        r->line);
            goto fail;
        }

        enabled_now = g_strcmp0(r->enabled, "false") != 0;
        enabled_orig = it->enabled;

        /* 顺序保证：先启用再改值；先改值再禁用 */
        if (!enabled_orig && enabled_now)
            append_edit(edits, &count, cap, LR_EDIT_ENABLE, r->line,
                        r->key, NULL);
        if (g_strcmp0(it->data, r->data) != 0)
            append_edit(edits, &count, cap, LR_EDIT_SET_VALUE, r->line,
                        r->key, r->data);
        if (enabled_orig && !enabled_now)
            append_edit(edits, &count, cap, LR_EDIT_DISABLE, r->line,
                        r->key, NULL);
    }

    lr_config_file_free(orig);
    *edits_out = edits;
    *n_edits_out = count;
    return TRUE;

fail:
    lr_config_file_free(orig);
    g_free(edits);
    return FALSE;
}

gboolean
lr_apply_edits(const char *content, const LrEdit *edits,
               gsize n_edits, gchar **out, GError **error)
{
    GPtrArray *lines;
    gsize k;

    if (content == NULL)
        content = "";
    lines = split_lines(content, strlen(content));

    for (k = 0; k < n_edits; k++)
    {
        const LrEdit *e = &edits[k];
        LrLine *l;

        if (e->line >= lines->len)
        {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                        "line %u out of range", e->line);
            g_ptr_array_unref(lines);
            return FALSE;
        }
        l = g_ptr_array_index(lines, e->line);

        switch (e->type)
        {
        case LR_EDIT_SET_VALUE:
            if (!apply_set_value(l, e, error))
            {
                g_ptr_array_unref(lines);
                return FALSE;
            }
            break;
        case LR_EDIT_ENABLE:
            if (!apply_enable_disable(l, e, TRUE, error))
            {
                g_ptr_array_unref(lines);
                return FALSE;
            }
            break;
        case LR_EDIT_DISABLE:
            if (!apply_enable_disable(l, e, FALSE, error))
            {
                g_ptr_array_unref(lines);
                return FALSE;
            }
            break;
        default:
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                "unknown edit type");
            g_ptr_array_unref(lines);
            return FALSE;
        }
    }

    *out = rebuild_lines(lines);
    g_ptr_array_unref(lines);
    return TRUE;
}
