/* apt.conf 配置解析器
 *
 * 语法（man apt.conf）：
 *   - 嵌套块：  Key { ... } / Key::Sub { ... }
 *   - 赋值：    Key = value; / Key "value"; / Key value;
 *   - 注释：    # 或 //（整行）
 * 叶子项的 section 记录完整 :: 路径，UI 据此逐级构建可展开树。
 */
#include "core/parsers/apt.h"
#include "core/parsers/common.h"

#include <string.h>

/* 解析上下文：块路径栈 + 当前路径字符串 */
typedef struct
{
    GQueue *stack;  /* 各层块名（gchar*） */
    GString *path;  /* 栈连接成的 :: 路径 */
    guint list_idx; /* 当前块内列表值元素的计数 */
} AptCtx;

static void
apt_push(AptCtx *ctx, const char *seg)
{
    gchar *s = g_strdup(seg);
    if (ctx->path->len > 0)
        g_string_append(ctx->path, "::");
    g_string_append(ctx->path, s);
    g_queue_push_tail(ctx->stack, s);
    ctx->list_idx = 0;
}

static void
apt_pop(AptCtx *ctx)
{
    GList *l;

    g_free(g_queue_pop_tail(ctx->stack));
    g_string_truncate(ctx->path, 0);
    for (l = ctx->stack->head; l != NULL; l = l->next)
    {
        if (ctx->path->len > 0)
            g_string_append(ctx->path, "::");
        g_string_append(ctx->path, (const char *)l->data);
    }
}

/* 去除首尾双引号 */
static char *
unquote(const char *s)
{
    gsize len = strlen(s);
    if (len >= 2 && s[0] == '"' && s[len - 1] == '"')
        return g_strndup(s + 1, len - 2);
    return g_strdup(s);
}

/* 在 s 中查找不在引号内的 c1（或 c2）位置，返回 NULL 若未找到 */
static char *
find_unquoted(char *s, char c1, char c2)
{
    gboolean in_str = FALSE;
    char *p;

    for (p = s; *p != '\0'; p++)
    {
        if (in_str)
        {
            if (*p == '"')
                in_str = FALSE;
            continue;
        }
        if (*p == '"')
        {
            in_str = TRUE;
            continue;
        }
        if (*p == c1 || *p == c2)
            return p;
    }
    return NULL;
}

/* 处理一条赋值语句 "Key = value" 或 "Key value"（assign 为临时缓冲） */
static void
handle_assign(AptCtx *ctx, GPtrArray *items, char *assign,
              char **pending_comment, guint line)
{
    char *eq = strchr(assign, '=');
    char *key = NULL;
    char *value_raw;
    char *to_free = NULL;
    gchar *v;
    LrConfigItem *item;

    if (eq != NULL)
    {
        key = g_strstrip(g_strndup(assign, eq - assign));
        value_raw = g_strstrip(eq + 1);
    }
    else
    {
        char *sp = strchr(assign, ' ');
        if (sp == NULL)
            sp = strchr(assign, '\t');
        if (sp != NULL)
        {
            key = g_strstrip(g_strndup(assign, sp - assign));
            value_raw = g_strstrip(sp + 1);
        }
        else
        {
            key = g_strstrip(g_strdup(assign));
            value_raw = to_free = g_strdup("");
        }
    }

    if (*key != '\0')
    {
        v = unquote(value_raw);
        item = lr_config_item_new(key, v, lr_value_detect_type(v),
                                  ctx->path->len > 0 ? ctx->path->str : NULL,
                                  *pending_comment);
        item->source_line = line;
        g_ptr_array_add(items, item);
        g_free(v);
        g_free(*pending_comment);
        *pending_comment = NULL;
    }

    g_free(key);
    g_free(to_free);
}

/* 处理 apt 列表值元素：块内 { "str"; }，无键名，用 [n] 作名称 */
static void
handle_list_value(AptCtx *ctx, GPtrArray *items, const char *raw,
                  char **pending_comment, guint line)
{
    gchar *v = unquote(raw);
    gchar *idx = g_strdup_printf("[%u]", ctx->list_idx++);
    LrConfigItem *item =
        lr_config_item_new(idx, v, LR_VALUE_STRING,
                           ctx->path->len > 0 ? ctx->path->str : NULL,
                           *pending_comment);
    item->source_line = line;
    g_ptr_array_add(items, item);
    g_free(idx);
    g_free(v);
    g_free(*pending_comment);
    *pending_comment = NULL;
}

/* 解析一行内可能包含的多个语句（{ } ; 分隔） */
static void
parse_line(AptCtx *ctx, GPtrArray *items, char *line,
           char **pending_comment, guint source_line)
{
    char *p = line;

    while (*p != '\0')
    {
        char *s = g_strstrip(p);
        char *brace, *semi;

        if (*s == '\0')
            break;

        if (*s == '}')
        {
            apt_pop(ctx);
            p = s + 1;
            if (*p == ';')
                p++;
            continue;
        }

        brace = find_unquoted(s, '{', '\0');
        semi = find_unquoted(s, ';', '\0');

        if (brace != NULL && (semi == NULL || brace < semi))
        {
            /* 块开始：Key { */
            char *key = g_strstrip(g_strndup(s, brace - s));
            if (*key != '\0')
                apt_push(ctx, key);
            g_free(key);
            p = brace + 1;
        }
        else if (semi != NULL)
        {
            char *assign = g_strstrip(g_strndup(s, semi - s));
            if (*assign != '\0')
            {
                /* 列表值元素：以双引号开头且无键名 */
                if (assign[0] == '"')
                    handle_list_value(ctx, items, assign, pending_comment,
                                      source_line);
                else
                    handle_assign(ctx, items, assign, pending_comment,
                                  source_line);
            }
            g_free(assign);
            p = semi + 1;
        }
        else
        {
            /* 行尾无分号：按赋值容忍处理 */
            if (*s != '\0')
            {
                if (s[0] == '"')
                    handle_list_value(ctx, items, s, pending_comment,
                                      source_line);
                else
                    handle_assign(ctx, items, s, pending_comment,
                                  source_line);
            }
            break;
        }
    }
}

gboolean
lr_parse_apt(const char *content, gsize length, LrConfigFile *file)
{
    gchar **lines, **linep;
    AptCtx ctx;
    char *pending_comment = NULL;
    guint line_idx = 0;

    (void)length;
    if (content == NULL)
    {
        file->parsed = FALSE;
        return FALSE;
    }

    ctx.stack = g_queue_new();
    ctx.path = g_string_new(NULL);

    lines = g_strsplit(content, "\n", -1);

    for (line_idx = 0; lines[line_idx] != NULL; line_idx++)
    {
        linep = &lines[line_idx];
        char *line = g_strstrip(*linep);

        if (*line == '\0')
            continue;

        /* 注释：# 或 //（含 ##）→ 说明文字 */
        if (lr_capture_comment(line, &pending_comment))
            continue;

        parse_line(&ctx, file->items, line, &pending_comment, line_idx);
    }

    g_strfreev(lines);
    g_queue_free_full(ctx.stack, g_free);
    g_string_free(ctx.path, TRUE);
    g_free(pending_comment);

    file->parsed = file->items->len > 0;
    return file->parsed;
}
