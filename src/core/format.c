#include "core/format.h"
#include "core/limits.h"

#include <string.h>
#include <glib/gi18n.h>
#include <json-glib/json-glib.h>
#include "core/parsers/ini.h"
#include "core/parsers/json.h"
#include "core/parsers/keyword.h"
#include "core/parsers/kv.h"
#include "core/parsers/systemd.h"
#include "core/parsers/apt.h"
#include "core/parsers/xml.h"
#include "core/parsers/toml.h"

static gboolean
has_systemd_extension(const char *path)
{
    static const char *exts[] = {
        ".service", ".socket", ".timer", ".mount", ".automount",
        ".swap", ".path", ".slice", ".scope", ".target", ".device",
        NULL};
    gint i;
    for (i = 0; exts[i] != NULL; i++)
    {
        if (g_str_has_suffix(path, exts[i]))
            return TRUE;
    }
    return FALSE;
}

/* 「关键字-参数」行中参数为多词（自然语言特征）的比例阈值，超过则视为普通文本 */
#define LR_NATURAL_LANG_THRESHOLD 60

/* 取内容前 max 字节用于嗅探（避免扫描超大非文本文件），返回新分配字符串 */
static char *
sniff_head(const char *content, gsize len, gsize max)
{
    return g_strndup(content, MIN(len, max));
}

/* 判断文件内容是否为合法 JSON 数组（[ 开头可能是 INI 节，需整体验证） */
static gboolean
is_json_array_content(const char *content, gsize len)
{
    JsonParser *parser;
    JsonNode *root;
    gboolean ok = FALSE;

    parser = json_parser_new();
    if (json_parser_load_from_data(parser, content, (gssize)len, NULL))
    {
        root = json_parser_get_root(parser);
        ok = root != NULL && JSON_NODE_TYPE(root) == JSON_NODE_ARRAY;
    }
    g_object_unref(parser);
    return ok;
}

/* 判断一行是否为「关键字 + 参数」样式（sshd_config 等）：
 * 首词以字母/下划线开头，后跟空白，且空白后有非空参数 */
static gboolean
is_keyword_line(const char *line)
{
    const char *p = line;

    if (!(g_ascii_isalpha(*p) || *p == '_'))
        return FALSE;
    while (g_ascii_isalnum(*p) || *p == '_' || *p == '-')
        p++;
    if (*p != ' ' && *p != '\t')
        return FALSE;
    while (*p == ' ' || *p == '\t')
        p++;
    return *p != '\0';
}

/* apt 配置特征：含 { } 嵌套块 + ; 结尾的赋值（Acquire::IndexTargets { ... };） */
static gboolean
is_apt_config(const char *content, gsize len)
{
    gchar **lines, **lp;
    gboolean has_block = FALSE;
    gboolean has_semi = FALSE;
    gchar *head = sniff_head(content, len, LR_SNIFF_MAX);

    lines = g_strsplit(head, "\n", -1);
    g_free(head);
    for (lp = lines; lp != NULL && *lp != NULL; lp++)
    {
        gchar *l = g_strstrip(*lp);
        gsize llen;

        if (*l == '\0' || l[0] == '#' || l[0] == '/')
            continue;
        llen = strlen(l);
        if (llen > 0 && l[llen - 1] == '{')
            has_block = TRUE;
        if (llen > 0 && l[llen - 1] == ';')
            has_semi = TRUE;
        if (strstr(l, "::") != NULL && strchr(l, '{') != NULL)
            has_block = TRUE;
    }
    g_strfreev(lines);
    return has_block && has_semi;
}

/* 行特征统计：一次扫描收集 INI / KV / 关键字判定所需的所有特征 */
typedef struct
{
    gboolean has_section;
    gboolean has_kv;
    gboolean has_non_comment;
    gboolean keyword_style;
    guint keyword_lines;
    guint multiword_value_lines;
} LineStats;

static LineStats
scan_stats(const char *content, gsize len)
{
    gchar **lines, **lp;
    gchar *head = sniff_head(content, len, LR_SNIFF_MAX);
    LineStats st;

    memset(&st, 0, sizeof(st));
    st.keyword_style = TRUE;

    lines = g_strsplit(head, "\n", -1);
    g_free(head);

    for (lp = lines; lp != NULL && *lp != NULL; lp++)
    {
        char *line = g_strstrip(*lp);

        if (*line == '\0' || line[0] == '#' || line[0] == ';')
            continue;

        st.has_non_comment = TRUE;

        if (line[0] == '[' && strchr(line, ']') != NULL)
        {
            st.has_section = TRUE;
            continue;
        }
        if (strchr(line, '=') != NULL || strchr(line, ':') != NULL)
            st.has_kv = TRUE;
        if (is_keyword_line(line))
        {
            char *sp = strchr(line, ' ');
            if (sp == NULL)
                sp = strchr(line, '\t');
            if (sp != NULL && (strchr(sp + 1, ' ') != NULL ||
                               strchr(sp + 1, '\t') != NULL))
                st.multiword_value_lines++;
            st.keyword_lines++;
        }
        else
        {
            st.keyword_style = FALSE;
        }
    }

    g_strfreev(lines);
    return st;
}

/* 嗅探上下文：承载内容/路径，并惰性计算一次行特征统计供多个 driver 复用 */
typedef struct
{
    const char *content;
    gsize len;
    const char *path;
    LineStats stats; /* 懒计算一次 */
    gboolean stats_valid;
} LrSniffCtx;

/* 惰性获取行特征统计：同一内容只 split/扫描一次 */
static const LineStats *
sniff_stats(LrSniffCtx *ctx)
{
    if (!ctx->stats_valid)
    {
        ctx->stats = scan_stats(ctx->content, ctx->len);
        ctx->stats_valid = TRUE;
    }
    return &ctx->stats;
}

/* ---- 各格式嗅探器（数组顺序即优先级） ---- */

static gboolean
sniff_systemd(LrSniffCtx *ctx)
{
    return has_systemd_extension(ctx->path);
}

/* TOML：以 .toml 后缀为主；其余交给 INI 的“节 + 键值对”兜底识别 */
static gboolean
sniff_toml(LrSniffCtx *ctx)
{
    return g_str_has_suffix(ctx->path, ".toml");
}

static gboolean
sniff_json(LrSniffCtx *ctx)
{
    const char *p = ctx->content;

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    if (*p == '{')
        return TRUE;
    if (*p == '[')
        return is_json_array_content(ctx->content, ctx->len);
    return FALSE;
}

static gboolean
sniff_apt(LrSniffCtx *ctx)
{
    return is_apt_config(ctx->content, ctx->len);
}

/* XML：以 < 开头，且整体是合法 XML（GMarkup 验证） */
static gboolean
sniff_xml(LrSniffCtx *ctx)
{
    const char *p = ctx->content;
    GMarkupParser parser = {0}; /* 全 NULL 回调：仅用于验证合法性 */
    GMarkupParseContext *parse_ctx;
    GError *error = NULL;
    gboolean ok = FALSE;

    if (ctx->content == NULL)
        return FALSE;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    if (*p != '<')
        return FALSE;

    parse_ctx = g_markup_parse_context_new(&parser, 0, NULL, NULL);
    if (g_markup_parse_context_parse(parse_ctx, ctx->content,
                                     (gssize)ctx->len, &error) &&
        g_markup_parse_context_end_parse(parse_ctx, &error))
        ok = TRUE;
    if (error != NULL)
        g_clear_error(&error);
    g_markup_parse_context_free(parse_ctx);
    return ok;
}

static gboolean
sniff_ini(LrSniffCtx *ctx)
{
    return sniff_stats(ctx)->has_section;
}

static gboolean
sniff_kv(LrSniffCtx *ctx)
{
    return sniff_stats(ctx)->has_kv;
}

static gboolean
sniff_keyword(LrSniffCtx *ctx)
{
    const LineStats *st = sniff_stats(ctx);

    if (!st->has_non_comment || !st->keyword_style)
        return FALSE;
    /* 大多数「关键字-参数」行的参数为多词（自然语言特征，如 /etc/legal 的
     * 英文说明），判定为普通文本而非配置 */
    if (st->keyword_lines > 0 &&
        st->multiword_value_lines * 100 / st->keyword_lines >
            LR_NATURAL_LANG_THRESHOLD)
        return FALSE;
    return TRUE;
}

/* ---- 格式注册表：新增格式只需在此追加一个条目 ---- */
typedef struct
{
    const char *name; /* 显示名 */
    LrConfigFormat id;
    gboolean (*sniff)(LrSniffCtx *ctx);
    gboolean (*parse)(const char *content, gsize len, LrConfigFile *file);
} LrFormatDriver;

static const LrFormatDriver k_drivers[] = {
    {N_("systemd Unit"), LR_FORMAT_SYSTEMD, sniff_systemd, lr_parse_systemd},
    {N_("JSON"), LR_FORMAT_JSON, sniff_json, lr_parse_json},
    {N_("XML"), LR_FORMAT_XML, sniff_xml, lr_parse_xml},
    {N_("APT Configuration"), LR_FORMAT_APT, sniff_apt, lr_parse_apt},
    {N_("TOML"), LR_FORMAT_TOML, sniff_toml, lr_parse_toml},
    {N_("INI"), LR_FORMAT_INI, sniff_ini, lr_parse_ini},
    {N_("Key-value"), LR_FORMAT_KV, sniff_kv, lr_parse_kv},
    {N_("Keyword-Argument"), LR_FORMAT_KEYWORD, sniff_keyword, lr_parse_keyword},
};

LrConfigFormat
lr_format_detect_content(const char *path, const char *content, gsize len)
{
    LrSniffCtx ctx = {content, len, path, {0}, FALSE};
    guint i;

    if (content == NULL)
        return LR_FORMAT_UNKNOWN;

    /* 脚本解释器（shebang #!）：一律以文本形式打开，不做配置解析 */
    if (g_str_has_prefix(content, "#!"))
        return LR_FORMAT_UNKNOWN;

    for (i = 0; i < G_N_ELEMENTS(k_drivers); i++)
    {
        if (k_drivers[i].sniff(&ctx))
            return k_drivers[i].id;
    }
    return LR_FORMAT_UNKNOWN;
}

/* 兼容入口：按路径读取文件内容后检测（供测试与外部调用） */
LrConfigFormat
lr_format_detect(const char *path)
{
    gchar *content = NULL;
    gsize len = 0;
    LrConfigFormat fmt;

    if (!g_file_get_contents(path, &content, &len, NULL))
        return LR_FORMAT_UNKNOWN;
    fmt = lr_format_detect_content(path, content, len);
    g_free(content);
    return fmt;
}

const char *
lr_format_name(LrConfigFormat fmt)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS(k_drivers); i++)
        if (k_drivers[i].id == fmt)
            return _(k_drivers[i].name);
    return _("Unknown");
}

/* 新建文件时可选的格式显示名（静态数组，NULL 结尾） */
static const char *const k_new_file_names[] = {
    N_("INI File"),
    N_("Key-Value Pair File"),
    N_("JSON File"),
    N_("XML File"),
    N_("systemd Unit File"),
    N_("APT Configuration"),
    N_("Keyword-Argument File"),
    N_("TOML File"),
    NULL,
};

const char *const *
lr_format_new_file_names(void)
{
    return k_new_file_names;
}

gboolean
lr_format_supported(LrConfigFormat fmt)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS(k_drivers); i++)
        if (k_drivers[i].id == fmt)
            return TRUE;
    return FALSE;
}

LrConfigFile *
lr_parse_config_content(const char *path, const char *content, gsize len)
{
    LrConfigFile *file = lr_config_file_new(path);
    LrConfigFormat fmt = lr_format_detect_content(path, content, len);
    guint i;
    gboolean ok = FALSE;

    for (i = 0; i < G_N_ELEMENTS(k_drivers); i++)
    {
        if (k_drivers[i].id == fmt)
        {
            ok = k_drivers[i].parse(content, len, file);
            break;
        }
    }
    if (ok)
        file->parsed = TRUE;
    return file;
}

/* 兼容入口：按路径读取文件内容后解析（供测试与外部调用） */
LrConfigFile *
lr_parse_config(const char *path)
{
    gchar *content = NULL;
    gsize len = 0;

    if (!g_file_get_contents(path, &content, &len, NULL))
    {
        LrConfigFile *file = lr_config_file_new(path);
        file->parsed = FALSE;
        return file;
    }

    {
        LrConfigFile *file = lr_parse_config_content(path, content, len);
        g_free(content);
        return file;
    }
}
