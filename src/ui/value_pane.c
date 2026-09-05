#include "ui/value_pane.h"
#include "core/format.h"
#include "core/text_file.h"
#include "core/limits.h"
#include "core/edits.h"
#include "core/write.h"

#include <string.h>
#include <glib/gi18n.h>
#include <json-glib/json-glib.h>

/* 底部 man 说明面板的固定高度（像素） */
#define LR_INFO_HEIGHT 200

enum
{
    COL_ENABLED = 0,
    COL_NAME,
    COL_TYPE,
    COL_DATA,
    COL_COMMENT,
    COL_SOURCE, /* 0 起原文行号（G_TYPE_UINT）；新增行为 G_MAXUINT */
    N_COLS
};

/* JSON 树形列表的列 */
enum
{
    COL_J_NAME = 0,
    COL_J_TYPE,
    COL_J_DATA,
    COL_J_N
};

struct _LrValuePane
{
    GtkWidget *widget; /* 根容器：GtkBox（页面栈） */
    GtkWidget *stack;  /* GtkStack：empty / table / text */
    GtkWidget *table_page;
    GtkWidget *text_page;
    GtkTreeView *view;
    GtkTreeStore *store;
    GtkTextView *text;
    GtkLabel *info_title;   /* 底部说明面板标题 */
    GtkTextView *info_text; /* 底部说明面板内容 */
    GtkWidget *info_page;   /* 底部说明面板容器 */
    GtkWidget *json_page;   /* JSON 树形页 */
    GtkTreeView *json_view;
    GtkTreeStore *json_store;
    char *current_basename; /* 当前配置文件 basename（用于 man 5 查询） */
    char *current_name;     /* 当前选中配置项名（用于过滤段落） */
    GHashTable *man_pages;  /* basename → ManPage*（整页文本 + 是否有页） */

    /* 表格内查找状态 */
    GPtrArray *search_rows; /* 表行快照（GtkTreeIter*，加载后扁平化） */
    gchar *search_needle;   /* 上次查找词 */
    guint search_index;     /* 上次命中的行下标 */
    guint search_total;     /* 最近一次 first 扫描的总命中数 */
    gboolean search_valid;  /* 是否存在可继续的查找 */

    GtkTreePath *popup_path; /* 右键菜单作用行 */

    gboolean dirty;         /* 表格存在仅内存的编辑 */
    LrValuePaneDirtyCb dirty_cb;
    gpointer dirty_data;

    gchar *current_path;    /* 当前加载文件路径 */
    gchar *source_content;  /* 打开时的原文快照（写回冲突检测用） */
    gboolean saveable;      /* 当前格式支持行级写回 */
};

/* 一个配置文件的 man 页缓存（整页文本 + 是否存在该页） */
typedef struct
{
    gchar *text;    /* man 5 输出整页文本（found 为 TRUE 时） */
    gboolean found; /* 该文件是否有 man 页 */
} ManPage;

/* 一次 man 查询请求（携带发起时的文件名与配置项名，避免异步竞态） */
typedef struct
{
    LrValuePane *self;
    gchar *basename;
    gchar *name;
} ManRequest;

static void
man_page_free(gpointer p)
{
    ManPage *mp = p;
    if (mp == NULL)
        return;
    g_free(mp->text);
    g_free(mp);
}

/* 从 man 页文本中提取以配置项名开头的段落；返回新分配文本，未找到返回 NULL */
static gchar *
filter_man_paragraphs(const char *page, const char *name)
{
    gchar **paras = g_strsplit(page, "\n\n", -1);
    GString *result = g_string_new(NULL);
    gchar *out = NULL;
    guint i;
    gsize n = strlen(name);

    for (i = 0; paras[i] != NULL; i++)
    {
        const char *p = paras[i];

        while (*p == ' ' || *p == '\t')
            p++;
        if (strncmp(p, name, n) == 0 &&
            (p[n] == '\0' || p[n] == '\n' || p[n] == ' ' || p[n] == '\t'))
        {
            g_string_append(result, paras[i]);
            g_string_append(result, "\n\n");
        }
    }
    g_strfreev(paras);

    if (result->len > 0)
    {
        g_strstrip(result->str);
        out = g_strdup(result->str);
    }
    g_string_free(result, TRUE);
    return out;
}

static void
on_man_done(GObject *source, GAsyncResult *res, gpointer user_data)
{
    ManRequest *req = user_data;
    LrValuePane *self = req->self;
    GSubprocess *proc = G_SUBPROCESS(source);
    GBytes *out = NULL, *err_out = NULL;
    GError *error = NULL;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(self->info_text);
    gboolean is_current = g_strcmp0(self->current_basename, req->basename) == 0 &&
                          g_strcmp0(self->current_name, req->name) == 0;

    if (!g_subprocess_communicate_finish(proc, res, &out, &err_out, &error))
    {
        g_clear_error(&error);
        /* 失败：缓存负面结果，避免反复重试 */
        if (!g_hash_table_contains(self->man_pages, req->basename))
        {
            ManPage *mp = g_new0(ManPage, 1);
            mp->found = FALSE;
            g_hash_table_insert(self->man_pages, g_strdup(req->basename), mp);
        }
        if (is_current)
            gtk_text_buffer_set_text(buf, _("man query failed."), -1);
    }
    else if (out != NULL && g_bytes_get_size(out) > 0)
    {
        gsize len = g_bytes_get_size(out);
        const gchar *data = g_bytes_get_data(out, &len);
        ManPage *mp;

        /* 整页文本缓存（按 basename）：供该文件所有配置项共享一次拉取 */
        mp = g_new0(ManPage, 1);
        mp->found = TRUE;
        mp->text = g_strndup(data, (gsize)MIN(len, G_MAXINT));
        g_hash_table_replace(self->man_pages, g_strdup(req->basename), mp);

        /* 仅当仍是最新请求时才写面板 */
        if (is_current)
        {
            gchar *filtered = filter_man_paragraphs(mp->text, req->name);
            if (filtered != NULL)
            {
                gtk_text_buffer_set_text(buf, filtered, -1);
                g_free(filtered);
            }
            else
            {
                gchar *msg = g_strdup_printf(_("No manual entry found for %s!"), req->name);
                gtk_text_buffer_set_text(buf, msg, -1);
                g_free(msg);
            }
        }
    }
    else
    {
        /* 无 man 页：缓存负面结果，下次直接提示不再启动进程 */
        if (!g_hash_table_contains(self->man_pages, req->basename))
        {
            ManPage *mp = g_new0(ManPage, 1);
            mp->found = FALSE;
            g_hash_table_insert(self->man_pages, g_strdup(req->basename), mp);
        }
        if (is_current)
            gtk_text_buffer_set_text(buf, _("No manual entry for that name."), -1);
    }

    g_free(req->basename);
    g_free(req->name);
    g_free(req);

    g_clear_pointer(&out, g_bytes_unref);
    g_clear_pointer(&err_out, g_bytes_unref);
    g_object_unref(proc);
}

static void
lr_value_pane_show_man(LrValuePane *self, const char *name)
{
    gchar *quoted, *cmd;
    GError *error = NULL;
    GSubprocess *proc;
    ManRequest *req;
    ManPage *mp;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(self->info_text);
    gchar *title = g_strdup_printf(_("Explain: %s (man 5 %s)"), name,
                                   self->current_basename != NULL
                                       ? self->current_basename
                                       : "");

    g_free(self->current_name);
    self->current_name = g_strdup(name);

    gtk_label_set_text(self->info_title, title);
    g_free(title);

    if (self->current_basename == NULL)
    {
        gtk_text_buffer_set_text(buf, _("Could not resolve the configuration filename."), -1);
        return;
    }

    /* 整页缓存（按 basename）命中：内存过滤显示，不再启动 man 子进程 */
    mp = g_hash_table_lookup(self->man_pages, self->current_basename);
    if (mp != NULL)
    {
        if (mp->found)
        {
            gchar *filtered = filter_man_paragraphs(mp->text, name);
            if (filtered != NULL)
            {
                gtk_text_buffer_set_text(buf, filtered, -1);
                g_free(filtered);
            }
            else
            {
              gchar *msg =
                  g_strdup_printf(_("No manual entry found for %s!"), name);
              gtk_text_buffer_set_text(buf, msg, -1);
              g_free(msg);
            }
        }
        else
        {
          gtk_text_buffer_set_text(buf, _("No manual entry for that name."), -1);
        }
        return;
    }

    gtk_text_buffer_set_text(buf, _("Fetching man page..."), -1);

    /* 排版交给 GTK（GtkTextView 自动折行）：
     *  - MANWIDTH 设很大 → groff 不按终端宽度折行，只保留内容本身的换行
     *    （段落、列表、缩进等结构不受影响）
     *  - --no-hyphenation 禁用断词（避免 LOCAL1 → LO‐+换行+CAL1）
     *  - --no-justification 禁用两端对齐 */
    quoted = g_shell_quote(self->current_basename);
    cmd = g_strdup_printf(
        "MANWIDTH=100000 man --no-hyphenation --no-justification 5 %s "
        "2>/dev/null | col -b",
        quoted);
    g_free(quoted);

    proc = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE, &error,
                            "sh", "-c", cmd, NULL);
    g_free(cmd);

    if (proc == NULL)
    {
        gtk_text_buffer_set_text(buf, _("Unable to start manual page query."), -1);
        g_clear_error(&error);
        return;
    }

    /* 请求携带发起时的文件名与配置项名：回调据此过滤并判断是否仍是最新 */
    req = g_new0(ManRequest, 1);
    req->self = self;
    req->basename = g_strdup(self->current_basename);
    req->name = g_strdup(name);
    g_subprocess_communicate_async(proc, NULL, NULL, on_man_done, req);
}

/* 窗口尺寸变化时保持说明面板为固定高度（而非随比例伸缩） */
static void
on_paned_allocate(GtkWidget *widget, GdkRectangle *alloc, gpointer user_data)
{
    LrValuePane *self = user_data;
    gint pos, cur;

    (void)widget;

    pos = alloc->height - LR_INFO_HEIGHT;
    if (pos <= 100)
        return;
    cur = gtk_paned_get_position(GTK_PANED(self->widget));
    if (pos != cur)
        gtk_paned_set_position(GTK_PANED(self->widget), pos);
}

/* 说明面板被显示时：若无选中行则隐藏（避免 show_all 等强制显示） */
static void
on_info_map(GtkWidget *widget, gpointer user_data)
{
    LrValuePane *self = user_data;
    GtkTreeIter iter;
    GtkTreeModel *model;
    (void)widget;

    if (!gtk_tree_selection_get_selected(
            gtk_tree_view_get_selection(self->view), &model, &iter))
        gtk_widget_hide(self->info_page);
}

/* 表格选中行：查询该配置项名称的 man 说明 */
static void
on_table_selection_changed(GtkTreeSelection *sel, gpointer user_data)
{
    LrValuePane *self = user_data;
    GtkTreeIter iter;
    GtkTreeModel *model;
    gchar *name = NULL;

    if (!gtk_tree_selection_get_selected(sel, &model, &iter))
    {
        /* 未选中：隐藏说明面板 */
        if (self->info_page != NULL)
            gtk_widget_hide(self->info_page);
        return;
    }

    /* 选中：显示说明面板 */
    if (self->info_page != NULL)
        gtk_widget_show(self->info_page);

    gtk_tree_model_get(model, &iter, COL_NAME, &name, -1);
    if (name != NULL && *name != '\0')
    {
        gchar *type = NULL;
        gtk_tree_model_get(model, &iter, COL_TYPE, &type, -1);
        /* 节行（Section）不查询 man */
        if (g_strcmp0(type, "Section") != 0)
            lr_value_pane_show_man(self, name);
        g_free(type);
    }
    g_free(name);
}

/* 以文本视图展示内容（content 为 NULL 表示读取失败） */
static void
show_text_content(LrValuePane *self, const char *content, gsize length)
{
    GtkTextBuffer *buf = gtk_text_view_get_buffer(self->text);

    gtk_text_buffer_set_text(buf, "", -1);
    if (content == NULL)
        gtk_text_buffer_set_text(buf, _("Unable to read file."), -1);
    else
        gtk_text_buffer_set_text(buf, content, (gint)MIN(length, G_MAXINT));

    gtk_stack_set_visible_child_name(GTK_STACK(self->stack), "text");
}

/* 递归将 JSON 节点加入树形列表 */
static void
add_json_node(LrValuePane *self, JsonNode *node, const char *key,
              GtkTreeIter *parent)
{
    JsonNodeType type = JSON_NODE_TYPE(node);
    GtkTreeIter iter;

    if (type == JSON_NODE_OBJECT)
    {
        GtkTreeIter *container = parent;
        GtkTreeIter obj_iter;
        JsonObject *obj;
        GList *members, *l;

        if (key != NULL)
        {
            gtk_tree_store_append(self->json_store, &obj_iter, parent);
            gtk_tree_store_set(self->json_store, &obj_iter,
                               COL_J_NAME, key,
                               COL_J_TYPE, "Object",
                               COL_J_DATA, "",
                               -1);
            container = &obj_iter;
        }

        obj = json_node_get_object(node);
        members = json_object_get_members(obj);
        for (l = members; l != NULL; l = l->next)
        {
            const char *mkey = l->data;
            add_json_node(self, json_object_get_member(obj, mkey), mkey,
                          container);
        }
        g_list_free(members);
        return;
    }

    if (type == JSON_NODE_ARRAY)
    {
        JsonArray *arr = json_node_get_array(node);
        guint len = json_array_get_length(arr);
        guint i;
        gchar *label = g_strdup_printf("%s[Array]",
                                       key != NULL ? key : "root");

        gtk_tree_store_append(self->json_store, &iter, parent);
        gtk_tree_store_set(self->json_store, &iter,
                           COL_J_NAME, label,
                           COL_J_TYPE, "Array",
                           COL_J_DATA, "",
                           -1);
        g_free(label);

        for (i = 0; i < len; i++)
        {
            gchar *ikey = g_strdup_printf("[%u]", i);
            add_json_node(self, json_array_get_element(arr, i), ikey, &iter);
            g_free(ikey);
        }
        return;
    }

    /* 标量值（json-glib 1.10 直接在 JsonNode 上取值） */
    {
        const char *type_name = "";
        gchar *data = g_strdup("");

        if (type == JSON_NODE_VALUE)
        {
            GType vtype = json_node_get_value_type(node);

            if (json_node_is_null(node))
            {
                type_name = "Null";
                g_free(data);
                data = g_strdup("null");
            }
            else if (vtype == G_TYPE_BOOLEAN)
            {
                type_name = "Boolean";
                g_free(data);
                data = g_strdup(json_node_get_boolean(node) ? "true" : "false");
            }
            else if (vtype == G_TYPE_INT64 || vtype == G_TYPE_INT)
            {
                type_name = "Number";
                g_free(data);
                data = g_strdup_printf("%" G_GINT64_FORMAT,
                                       json_node_get_int(node));
            }
            else if (vtype == G_TYPE_DOUBLE)
            {
                type_name = "Number";
                g_free(data);
                data = g_strdup_printf("%g", json_node_get_double(node));
            }
            else
            {
                type_name = "String";
                g_free(data);
                data = g_strdup(json_node_get_string(node) != NULL
                                    ? json_node_get_string(node)
                                    : "");
            }
        }

        gtk_tree_store_append(self->json_store, &iter, parent);
        gtk_tree_store_set(self->json_store, &iter,
                           COL_J_NAME, key != NULL ? key : "",
                           COL_J_TYPE, type_name,
                           COL_J_DATA, data,
                           -1);
        g_free(data);
    }
}

static void
lr_value_pane_load_json(LrValuePane *self, const char *content, gsize length)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;

    gtk_tree_store_clear(self->json_store);

    if (content != NULL &&
        json_parser_load_from_data(parser, content, (gssize)length, &error))
    {
        JsonNode *root = json_parser_get_root(parser);
        gtk_stack_set_visible_child_name(GTK_STACK(self->stack), "json");
        add_json_node(self, root, NULL, NULL);
        gtk_tree_view_expand_all(self->json_view);
    }
    else
    {
        show_text_content(self, content, length);
    }

    if (error != NULL)
        g_error_free(error);
    g_object_unref(parser);
}

/* ---------- 表格内查找（Edit → Find… / F3） ---------- */

static void
search_reset(LrValuePane *self)
{
    if (self->search_rows != NULL)
        g_ptr_array_unref(self->search_rows);
    self->search_rows = NULL;
    g_free(self->search_needle);
    self->search_needle = NULL;
    self->search_index = 0;
    self->search_total = 0;
    self->search_valid = FALSE;
}

/* 递归收集子树全部行（含节/容器行），快照为 GtkTreeIter 数组 */
static void
collect_rows_recursive(GtkTreeModel *model, GtkTreeIter *parent,
                       GPtrArray *out)
{
    GtkTreeIter iter;
    gboolean valid = (parent == NULL)
                         ? gtk_tree_model_get_iter_first(model, &iter)
                         : gtk_tree_model_iter_children(model, &iter, parent);

    while (valid)
    {
        g_ptr_array_add(out, g_memdup2(&iter, sizeof(GtkTreeIter)));
        collect_rows_recursive(model, &iter, out);
        valid = gtk_tree_model_iter_next(model, &iter);
    }
}

/* 折叠后是否包含查找词（大小写不敏感，UTF-8 安全） */
static gboolean
text_contains(const gchar *text, const gchar *folded_needle)
{
    gchar *folded;
    gboolean ok;

    if (text == NULL || *text == '\0')
        return FALSE;
    folded = g_utf8_casefold(text, -1);
    ok = strstr(folded, folded_needle) != NULL;
    g_free(folded);
    return ok;
}

static gboolean
row_matches(LrValuePane *self, GtkTreeIter *iter, const gchar *folded)
{
    gchar *name = NULL, *data = NULL, *comment = NULL;
    gboolean ok;

    gtk_tree_model_get(GTK_TREE_MODEL(self->store), iter,
                       COL_NAME, &name, COL_DATA, &data,
                       COL_COMMENT, &comment, -1);
    ok = text_contains(name, folded) || text_contains(data, folded) ||
         text_contains(comment, folded);
    g_free(name);
    g_free(data);
    g_free(comment);
    return ok;
}

/* 选中并滚动到指定行 */
static void
select_row(LrValuePane *self, GtkTreeIter *iter)
{
    GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(self->store),
                                                iter);
    gtk_tree_view_set_cursor(self->view, path, NULL, FALSE);
    gtk_tree_view_scroll_to_cell(self->view, path, NULL, FALSE, 0.0, 0.0);
    gtk_tree_path_free(path);
}

gboolean
lr_value_pane_search_first(LrValuePane *self, const char *needle,
                           guint *matches)
{
    gchar *folded;
    guint i, total = 0;
    gboolean found = FALSE;

    if (self == NULL || needle == NULL || *needle == '\0')
        return FALSE;

    search_reset(self);
    self->search_rows =
        g_ptr_array_new_with_free_func((GDestroyNotify)g_free);
    collect_rows_recursive(GTK_TREE_MODEL(self->store), NULL,
                           self->search_rows);

    folded = g_utf8_casefold(needle, -1);
    for (i = 0; i < self->search_rows->len; i++)
    {
        GtkTreeIter *it = g_ptr_array_index(self->search_rows, i);
        if (!row_matches(self, it, folded))
            continue;
        total++;
        if (!found)
        {
            select_row(self, it);
            self->search_index = i;
            found = TRUE;
        }
    }
    g_free(folded);

    self->search_total = total;
    self->search_valid = TRUE;
    self->search_needle = g_strdup(needle);
    if (matches != NULL)
        *matches = total;
    return found;
}

gboolean
lr_value_pane_search_next(LrValuePane *self)
{
    gchar *folded;
    guint len, k, idx;

    if (self == NULL || !self->search_valid ||
        self->search_rows == NULL || self->search_rows->len == 0)
        return FALSE;

    len = self->search_rows->len;
    folded = g_utf8_casefold(self->search_needle, -1);
    for (k = 1; k <= len; k++)
    {
        idx = (self->search_index + k) % len;
        GtkTreeIter *it = g_ptr_array_index(self->search_rows, idx);
        if (row_matches(self, it, folded))
        {
            g_free(folded);
            select_row(self, it);
            self->search_index = idx;
            return TRUE;
        }
    }
    g_free(folded);
    return FALSE;
}

gboolean
lr_value_pane_search_has_query(LrValuePane *self)
{
    return self != NULL && self->search_valid &&
           self->search_needle != NULL && *self->search_needle != '\0';
}

static void
value_pane_set_dirty(LrValuePane *self, gboolean dirty)
{
    if (self->dirty == dirty)
        return;
    self->dirty = dirty;
    if (self->dirty_cb != NULL)
        self->dirty_cb(dirty, self->dirty_data);
}

void
lr_value_pane_set_dirty_cb(LrValuePane *self, LrValuePaneDirtyCb cb,
                           gpointer user_data)
{
    self->dirty_cb = cb;
    self->dirty_data = user_data;
}

void lr_value_pane_load_file(LrValuePane *self, const char *path)
{
    gchar *content = NULL;
    gsize len = 0;
    GError *error = NULL;
    LrConfigFormat fmt;

    g_clear_pointer(&self->current_path, g_free);
    g_clear_pointer(&self->source_content, g_free);
    self->saveable = FALSE;
    value_pane_set_dirty(self, FALSE);
    search_reset(self);
    g_clear_pointer(&self->popup_path, gtk_tree_path_free);
    g_free(self->current_basename);
    self->current_basename = g_path_get_basename(path);

    /* 统一读取守卫：大小上限 + 非文本(NUL)过滤，树与直接打开同一套规则 */
    {
        LrTextReadStatus rst = lr_text_file_read(path, &content, &len,
                                                 &error);
        gchar *msg = NULL;

        if (rst == LR_TEXT_ERROR)
        {
            msg = g_strdup_printf(_("Unable to read file: %s"),
                                  error != NULL ? error->message
                                                : _("Unknown Error"));
        }
        else if (rst == LR_TEXT_TOO_LARGE)
        {
            msg = g_strdup_printf(
                _("File is too large (limit %u KiB)."),
                LR_MAX_FILE_SIZE / 1024);
        }
        else if (rst == LR_TEXT_BINARY)
        {
            msg = g_strdup(_("File appears to be binary."));
        }

        if (msg != NULL)
        {
            show_text_content(self, msg, strlen(msg));
            g_free(msg);
            g_clear_error(&error);
            return;
        }
    }

    fmt = lr_format_detect_content(path, content, len);
    g_clear_pointer(&self->current_path, g_free);
    g_clear_pointer(&self->source_content, g_free);
    self->current_path = g_strdup(path);
    self->source_content = g_strdup(content);
    self->saveable = (fmt == LR_FORMAT_INI || fmt == LR_FORMAT_KV ||
                      fmt == LR_FORMAT_SYSTEMD || fmt == LR_FORMAT_TOML ||
                      fmt == LR_FORMAT_KEYWORD);
    if (!lr_format_supported(fmt))
    {
        show_text_content(self, content, len);
        g_free(content);
        return;
    }

    /* JSON：以树形列表展示 */
    if (fmt == LR_FORMAT_JSON)
    {
        lr_value_pane_load_json(self, content, len);
        g_free(content);
        return;
    }

    {
        LrConfigFile *file = lr_parse_config_content(path, content, len);
        guint i;

        if (!file->parsed)
        {
            show_text_content(self, content, len);
            g_free(content);
            lr_config_file_free(file);
            return;
        }
        g_free(content);

        gtk_tree_store_clear(self->store);

        /* 按节路径（:: 分隔）逐级创建可展开节点：
         * INI/systemd 单段节 → [节名]；apt 嵌套路径 → 每段一个节点 */
        {
            GHashTable *nodes = g_hash_table_new_full(
                g_str_hash, g_str_equal, g_free, g_free);

            for (i = 0; i < file->items->len; i++)
            {
                LrConfigItem *item = g_ptr_array_index(file->items, i);
                GtkTreeIter iter;
                GtkTreeIter *parent = NULL;

                if (item->section != NULL && *item->section != '\0')
                {
                    gchar **segs = g_strsplit(item->section, "::", -1);
                    gboolean multi = strstr(item->section, "::") != NULL;
                    GString *cur = g_string_new(NULL);
                    guint j;

                    for (j = 0; segs[j] != NULL; j++)
                    {
                        gchar *seg = g_strstrip(segs[j]);
                        GtkTreeIter *node;
                        gchar *display;

                        if (*seg == '\0')
                            continue;
                        if (cur->len > 0)
                            g_string_append(cur, "::");
                        g_string_append(cur, seg);

                        display = multi
                                      ? g_strdup(seg)
                                      : g_strdup_printf("[%s]", seg);
                        node = g_hash_table_lookup(nodes, cur->str);
                        if (node == NULL)
                        {
                            GtkTreeIter sit;
                            gtk_tree_store_append(self->store, &sit, parent);
                            gtk_tree_store_set(self->store, &sit,
                                               COL_ENABLED, "",
                                               COL_NAME, display,
                                               COL_TYPE, "Section",
                                               COL_DATA, "",
                                               COL_COMMENT, "",
                                               COL_SOURCE, G_MAXUINT,
                                               -1);
                            node = g_memdup2(&sit, sizeof(GtkTreeIter));
                            g_hash_table_insert(nodes, g_strdup(cur->str),
                                                node);
                        }
                        parent = node;
                        g_free(display);
                    }
                    g_strfreev(segs);
                    g_string_free(cur, TRUE);
                }

                gtk_tree_store_append(self->store, &iter, parent);
                gtk_tree_store_set(self->store, &iter,
                                   COL_ENABLED,
                                   item->enabled ? "true" : "false",
                                   COL_NAME, item->key,
                                   COL_TYPE, lr_value_type_name(item->type),
                                   COL_DATA, item->data,
                                   COL_COMMENT, item->comment != NULL ? item->comment : "",
                                   COL_SOURCE, item->source_line,
                                   -1);
            }

            g_hash_table_destroy(nodes);
        }

        /* 默认展开所有节 */
        gtk_tree_view_expand_all(self->view);

        gtk_stack_set_visible_child_name(GTK_STACK(self->stack), "table");
        lr_config_file_free(file);
    }
}

gboolean
lr_value_pane_save_changes(LrValuePane *self, GError **error)
{
    GPtrArray *iters;
    LrRowState *rows;
    LrEdit *edits = NULL;
    gsize n_edits = 0, real = 0, i;
    gboolean ok = FALSE;

    if (self == NULL)
        return FALSE;
    if (!self->dirty)
        return TRUE;
    if (!self->saveable)
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "write-back is not supported for this format yet");
        return FALSE;
    }
    if (self->current_path == NULL || self->source_content == NULL)
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "no file loaded to save");
        return FALSE;
    }

    iters = g_ptr_array_new_with_free_func((GDestroyNotify)g_free);
    collect_rows_recursive(GTK_TREE_MODEL(self->store), NULL, iters);
    rows = g_new0(LrRowState, iters->len);

    for (i = 0; i < iters->len; i++)
    {
        GtkTreeIter *it = g_ptr_array_index(iters, i);
        gchar *name = NULL, *data = NULL, *enabled = NULL;
        gchar *comment = NULL, *type = NULL;
        guint source_line;

        gtk_tree_model_get(GTK_TREE_MODEL(self->store), it,
                           COL_NAME, &name, COL_DATA, &data,
                           COL_ENABLED, &enabled, COL_COMMENT, &comment,
                           COL_TYPE, &type, COL_SOURCE, &source_line, -1);
        if (g_strcmp0(type, "Section") != 0)
        {
            rows[real].line = source_line;
            rows[real].key = name;
            rows[real].data = data;
            rows[real].enabled = enabled;
            rows[real].comment = comment;
            rows[real].type = type;
            real++;
        }
        else
        {
            g_free(name);
            g_free(data);
            g_free(enabled);
            g_free(comment);
            g_free(type);
        }
    }
    g_ptr_array_unref(iters);

    ok = lr_build_edits_from_rows(self->current_path, self->source_content,
                                  rows, real, &edits, &n_edits, error);

    if (!ok)
    {
        for (i = 0; i < real; i++)
        {
            g_free((gchar *)rows[i].key);
            g_free((gchar *)rows[i].data);
            g_free((gchar *)rows[i].enabled);
            g_free((gchar *)rows[i].comment);
            g_free((gchar *)rows[i].type);
        }
        g_free(rows);
        return FALSE;
    }

    if (n_edits == 0)
    {
        for (i = 0; i < real; i++)
        {
            g_free((gchar *)rows[i].key);
            g_free((gchar *)rows[i].data);
            g_free((gchar *)rows[i].enabled);
            g_free((gchar *)rows[i].comment);
            g_free((gchar *)rows[i].type);
        }
        g_free(rows);
        g_free(edits);
        return TRUE;
    }
    ok = lr_save_config_file(self->current_path, self->source_content,
                             edits, n_edits, error);
    g_free(edits);
    if (ok)
    {
        /* load_file 会先清空 self->current_path，必须先复制再传参 */
        gchar *reload_path = g_strdup(self->current_path);
        lr_value_pane_load_file(self, reload_path);
        g_free(reload_path);
    }
    for (i = 0; i < real; i++)
    {
        g_free((gchar *)rows[i].key);
        g_free((gchar *)rows[i].data);
        g_free((gchar *)rows[i].enabled);
        g_free((gchar *)rows[i].comment);
        g_free((gchar *)rows[i].type);
    }
    g_free(rows);
    return ok;
}

void lr_value_pane_clear(LrValuePane *self)
{
    g_clear_pointer(&self->current_path, g_free);
    g_clear_pointer(&self->source_content, g_free);
    self->saveable = FALSE;
    value_pane_set_dirty(self, FALSE);
    search_reset(self);
    gtk_tree_store_clear(self->store);
    gtk_tree_store_clear(self->json_store);
    gtk_stack_set_visible_child_name(GTK_STACK(self->stack), "empty");
}

/* 追加一列文本列：title 表头，col 模型列，expand 是否随窗口扩展填满剩余空间，
 * gray 是否灰色前景，width 固定列宽（0 则用默认值）。
 * 列宽采用 FIXED 模式 + 省略号：内容再长也不会把列顶开。 */
static void
append_text_column(GtkTreeView *view, const char *title, gint col,
                   gboolean expand, gboolean gray, gint width)
{
    GtkTreeViewColumn *column = gtk_tree_view_column_new();
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();

    gtk_tree_view_column_set_title(column, title);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    if (gray)
        g_object_set(renderer, "foreground", "gray", NULL);
    gtk_tree_view_column_pack_start(column, renderer, expand);
    gtk_tree_view_column_add_attribute(column, renderer, "text", col);
    gtk_tree_view_column_set_fixed_width(column, width > 0 ? width : 120);
    gtk_tree_view_append_column(view, column);
}

/* ========== 编辑（仅内存，不写盘） ========== */

/* 数字文本校验：仅数字/符号/十六进制 */
static gboolean
is_number_text(const char *s)
{
    const char *p;
    gboolean has_digit = FALSE;

    if (s == NULL || *s == '\0')
        return FALSE;
    for (p = s; *p; p++)
    {
        if (g_ascii_isdigit(*p))
            has_digit = TRUE;
        else if (g_ascii_isspace(*p))
            ;
        else if (strchr("+-.xXabcdefABCDEF", *p) != NULL)
            ;
        else
            return FALSE;
    }
    return has_digit;
}

/* 布尔文本校验：true/false/yes/no/0/1 */
static gboolean
is_valid_bool_text(const char *s)
{
    static const gchar *vals[] = {"true", "false", "yes", "no", "0", "1",
                                  NULL};
    gint i;

    if (s == NULL)
        return FALSE;
    for (i = 0; vals[i] != NULL; i++)
        if (g_ascii_strcasecmp(s, vals[i]) == 0)
            return TRUE;
    return FALSE;
}

/* 数字类型：输入时过滤非数字字符 */
static void
on_number_insert_text(GtkEditable *editable, const gchar *text, gint length,
                      gint *position, gpointer data)
{
    const gchar *p;
    (void)length;
    (void)position;
    (void)data;

    for (p = text; *p; p++)
    {
        if (!g_ascii_isdigit(*p) && !g_ascii_isspace(*p) &&
            strchr("+-.xXabcdefABCDEF", *p) == NULL)
        {
            g_signal_stop_emission_by_name(editable, "insert-text");
            return;
        }
    }
}

/* 数据列开始编辑：按类型加数字过滤 / 布尔补全 */
static void
on_data_editing_started(GtkCellRenderer *renderer, GtkCellEditable *editable,
                        const gchar *path, gpointer user_data)
{
    LrValuePane *self = user_data;
    GtkTreeIter iter;
    GtkTreePath *tp;
    gchar *type = NULL;
    (void)renderer;

    if (!GTK_IS_ENTRY(editable))
        return;
    tp = gtk_tree_path_new_from_string(path);
    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(self->store), &iter, tp))
    {
        gtk_tree_path_free(tp);
        return;
    }
    gtk_tree_path_free(tp);

    gtk_tree_model_get(GTK_TREE_MODEL(self->store), &iter, COL_TYPE, &type, -1);
    if (type == NULL)
        return;

    if (lr_value_type_from_name(type) == LR_VALUE_NUMBER)
    {
        g_signal_connect(editable, "insert-text",
                         G_CALLBACK(on_number_insert_text), NULL);
    }
    else if (lr_value_type_from_name(type) == LR_VALUE_BOOL)
    {
        static const gchar *vals[] = {"true", "false", "yes", "no", "0",
                                      "1"};
        GtkListStore *cs = gtk_list_store_new(1, G_TYPE_STRING);
        GtkEntryCompletion *completion = gtk_entry_completion_new();
        gint i;

        for (i = 0; i < 6; i++)
            gtk_list_store_insert_with_values(cs, NULL, i, 0, vals[i], -1);
        gtk_entry_completion_set_model(completion, GTK_TREE_MODEL(cs));
        gtk_entry_completion_set_text_column(completion, 0);
        gtk_entry_completion_set_inline_completion(completion, TRUE);
        gtk_entry_set_completion(GTK_ENTRY(editable), completion);
        g_object_unref(cs);
    }
    g_free(type);
}

/* 数据列编辑完成：按类型校验后更新（不写盘） */
static void
on_data_edited(GtkCellRendererText *renderer, const gchar *path,
               const gchar *new_text, gpointer user_data)
{
    LrValuePane *self = user_data;
    GtkTreeIter iter;
    GtkTreePath *tp;
    gchar *type = NULL;
    (void)renderer;

    tp = gtk_tree_path_new_from_string(path);
    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(self->store), &iter, tp))
    {
        gtk_tree_path_free(tp);
        return;
    }
    gtk_tree_path_free(tp);

    gtk_tree_model_get(GTK_TREE_MODEL(self->store), &iter, COL_TYPE, &type, -1);
    if (type != NULL)
    {
        LrValueType tt = lr_value_type_from_name(type);
        if (tt == LR_VALUE_NUMBER && !is_number_text(new_text))
        {
            g_free(type);
            return; /* 非法数字：拒绝修改 */
        }
        if (tt == LR_VALUE_BOOL && !is_valid_bool_text(new_text))
        {
            g_free(type);
            return;
        }
        g_free(type);
    }
    gtk_tree_store_set(self->store, &iter, COL_DATA, new_text, -1);
    value_pane_set_dirty(self, TRUE);
}

/* 启用列（下拉 true/false）编辑完成 */
static void
on_enabled_edited(GtkCellRendererText *renderer, const gchar *path,
                  const gchar *new_text, gpointer user_data)
{
    LrValuePane *self = user_data;
    GtkTreeIter iter;
    GtkTreePath *tp;
    (void)renderer;

    tp = gtk_tree_path_new_from_string(path);
    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(self->store), &iter, tp))
    {
        gtk_tree_path_free(tp);
        return;
    }
    gtk_tree_path_free(tp);
    gtk_tree_store_set(self->store, &iter, COL_ENABLED, new_text, -1);
    value_pane_set_dirty(self, TRUE);
}

/* 在表格末尾追加一个配置项（仅内存，不写盘） */
void lr_value_pane_add_value(LrValuePane *self, const char *type)
{
    const gchar *def_name, *def_data;
    GtkTreeIter iter;

    if (lr_value_type_from_name(type) == LR_VALUE_SECTION)
    {
        def_name = "NewSection";
        def_data = "";
    }
    else if (lr_value_type_from_name(type) == LR_VALUE_BOOL)
    {
        def_name = "NewBoolean";
        def_data = "false";
    }
    else if (lr_value_type_from_name(type) == LR_VALUE_NUMBER)
    {
        def_name = "NewNumber";
        def_data = "0";
    }
    else
    {
        def_name = "NewString";
        def_data = "";
    }

    gtk_tree_store_append(self->store, &iter, NULL);
    gtk_tree_store_set(self->store, &iter, COL_ENABLED, "true", COL_NAME,
                       def_name, COL_TYPE, type, COL_DATA, def_data,
                       COL_COMMENT, "", COL_SOURCE, G_MAXUINT, -1);
    value_pane_set_dirty(self, TRUE);
}

/* 新建一行（仅内存，不写盘） */
static void
on_new_value(GtkMenuItem *item, gpointer user_data)
{
    LrValuePane *self = user_data;
    (void)item;
    lr_value_pane_add_value(self, gtk_menu_item_get_label(item));
}

/* 新建 → 子菜单：列出所有可新建的类型（点击即新建一行） */
static void
build_new_submenu(LrValuePane *self, GtkWidget *menu)
{
    const char *const *types = lr_value_type_names();
    gint i;

    for (i = 0; types[i] != NULL; i++)
    {
        GtkWidget *item = gtk_menu_item_new_with_label(types[i]);
        g_signal_connect(item, "activate", G_CALLBACK(on_new_value), self);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }
}

/* 启用列：下拉 true/false */
static void
build_enabled_column(LrValuePane *self)
{
    GtkCellRenderer *renderer = gtk_cell_renderer_combo_new();
    GtkListStore *store = gtk_list_store_new(1, G_TYPE_STRING);
    GtkTreeViewColumn *col;

    gtk_list_store_insert_with_values(store, NULL, 0, 0, "true", -1);
    gtk_list_store_insert_with_values(store, NULL, 1, 0, "false", -1);
    g_object_set(renderer, "model", store, "text-column", 0, "editable", TRUE,
                 "has-entry", FALSE, NULL);
    g_object_unref(store);
    g_signal_connect(renderer, "edited", G_CALLBACK(on_enabled_edited), self);

    col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col, _("Enable"));
    gtk_tree_view_column_set_resizable(col, TRUE);
    gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(col, 75);
    gtk_tree_view_column_pack_start(col, renderer, FALSE);
    gtk_tree_view_column_add_attribute(col, renderer, "text", COL_ENABLED);
    gtk_tree_view_append_column(self->view, col);
}

/* 类型列（下拉）编辑完成：更新类型，并按新类型给数据合理默认值 */
static void
on_type_edited(GtkCellRendererText *renderer, const gchar *path,
               const gchar *new_text, gpointer user_data)
{
    LrValuePane *self = user_data;
    GtkTreeIter iter;
    GtkTreePath *tp;
    const gchar *def_data = NULL;
    (void)renderer;

    tp = gtk_tree_path_new_from_string(path);
    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(self->store), &iter, tp))
    {
        gtk_tree_path_free(tp);
        return;
    }
    gtk_tree_path_free(tp);

    /* 类型切换时给数据一个该类型下的合理默认值 */
    if (lr_value_type_from_name(new_text) == LR_VALUE_BOOL)
        def_data = "false";
    else if (lr_value_type_from_name(new_text) == LR_VALUE_NUMBER)
        def_data = "0";

    if (def_data != NULL)
        gtk_tree_store_set(self->store, &iter, COL_TYPE, new_text, COL_DATA,
                           def_data, -1);
    else
        gtk_tree_store_set(self->store, &iter, COL_TYPE, new_text, -1);
    value_pane_set_dirty(self, TRUE);
}

/* 类型列：下拉 Section/String/Boolean/Number */
static void
build_type_column(LrValuePane *self)
{
    GtkCellRenderer *renderer = gtk_cell_renderer_combo_new();
    GtkListStore *store = gtk_list_store_new(1, G_TYPE_STRING);
    GtkTreeViewColumn *col;
    const char *const *types = lr_value_type_names();
    gint i;

    for (i = 0; types[i] != NULL; i++)
        gtk_list_store_insert_with_values(store, NULL, i, 0, types[i], -1);
    g_object_set(renderer, "model", store, "text-column", 0, "editable", TRUE,
                 "has-entry", FALSE, NULL);
    g_object_unref(store);
    g_signal_connect(renderer, "edited", G_CALLBACK(on_type_edited), self);

    col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col, _("Type"));
    gtk_tree_view_column_set_resizable(col, TRUE);
    gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(col, 93);
    gtk_tree_view_column_pack_start(col, renderer, FALSE);
    gtk_tree_view_column_add_attribute(col, renderer, "text", COL_TYPE);
    gtk_tree_view_append_column(self->view, col);
}

/* 数据列：可编辑文本，按类型做输入限制/补全 */
static void
build_data_column(LrValuePane *self)
{
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col;

    g_object_set(renderer, "editable", TRUE, "ellipsize",
                 PANGO_ELLIPSIZE_END, NULL);
    g_signal_connect(renderer, "edited", G_CALLBACK(on_data_edited), self);
    g_signal_connect(renderer, "editing-started",
                     G_CALLBACK(on_data_editing_started), self);

    col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col, _("Data"));
    gtk_tree_view_column_set_resizable(col, TRUE);
    gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_FIXED);
    /* 初始宽度较小，实际宽度由 expand 分配：数据列占满剩余空间 */
    gtk_tree_view_column_set_fixed_width(col, 100);
    gtk_tree_view_column_pack_start(col, renderer, TRUE);
    gtk_tree_view_column_add_attribute(col, renderer, "text", COL_DATA);
    gtk_tree_view_column_set_expand(col, TRUE);
    gtk_tree_view_append_column(self->view, col);
}

/* 名称/备注列编辑完成：直接写回表格（不写盘） */
static void
on_text_edited(GtkCellRendererText *renderer, const gchar *path,
               const gchar *new_text, gpointer user_data)
{
    LrValuePane *self = user_data;
    gint col = GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(renderer), "lr-col"));
    GtkTreeIter iter;
    GtkTreePath *tp;

    tp = gtk_tree_path_new_from_string(path);
    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(self->store), &iter, tp))
        gtk_tree_store_set(self->store, &iter, col, new_text, -1);
    gtk_tree_path_free(tp);
    value_pane_set_dirty(self, TRUE);
}

/* 可编辑文本列：title 表头，col 模型列，gray 是否灰色前景，width 固定列宽 */
static void
build_editable_text_column(LrValuePane *self, const char *title, gint col,
                           gboolean gray, gint width)
{
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column;

    g_object_set(renderer, "editable", TRUE, "ellipsize",
                 PANGO_ELLIPSIZE_END, NULL);
    if (gray)
        g_object_set(renderer, "foreground", "gray", NULL);
    g_signal_connect(renderer, "edited", G_CALLBACK(on_text_edited), self);

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, title);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(column, width);
    gtk_tree_view_column_pack_start(column, renderer, FALSE);
    gtk_tree_view_column_add_attribute(column, renderer, "text", col);
    gtk_tree_view_append_column(self->view, column);

    /* 记录模型列：编辑回调据此写回正确列 */
    g_object_set_data(G_OBJECT(renderer), "lr-col", GINT_TO_POINTER(col));
}

/* 右键菜单动作：把作用行强制显示为指定类型（不改动数据） */
static void
on_popup_force_type(GtkMenuItem *item, gpointer user_data)
{
    LrValuePane *self = user_data;
    GtkTreeIter iter;
    const gchar *new_type;

    if (self->popup_path == NULL)
        return;
    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(self->store),
                                 &iter, self->popup_path))
        return;

    new_type = gtk_menu_item_get_label(item);
    if (lr_value_type_from_name(new_type) == LR_VALUE_SECTION)
        return;
    gtk_tree_store_set(self->store, &iter, COL_TYPE, new_type, -1);
    value_pane_set_dirty(self, TRUE);
}

/* 右键菜单动作：根据数据自动重新识别类型 */
static void
on_popup_detect_type(GtkMenuItem *item, gpointer user_data)
{
    LrValuePane *self = user_data;
    GtkTreeIter iter;
    gchar *data = NULL;

    (void)item;
    if (self->popup_path == NULL)
        return;
    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(self->store),
                                 &iter, self->popup_path))
        return;

    gtk_tree_model_get(GTK_TREE_MODEL(self->store), &iter,
                       COL_DATA, &data, -1);
    if (data != NULL)
    {
        gtk_tree_store_set(self->store, &iter, COL_TYPE,
                           lr_value_type_name(lr_value_detect_type(data)),
                           -1);
        value_pane_set_dirty(self, TRUE);
        g_free(data);
    }
}

/* 类型子菜单：String / Boolean / Number + 自动识别 */
static void
build_type_submenu(LrValuePane *self, GtkWidget *menu)
{
    const char *const *names = lr_value_type_names();
    GtkWidget *item;
    gint i;

    (void)self;
    for (i = 0; names[i] != NULL; i++)
    {
        if (strcmp(names[i], "Section") == 0)
            continue;
        item = gtk_menu_item_new_with_label(names[i]);
        g_signal_connect(item, "activate", G_CALLBACK(on_popup_force_type),
                         self);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }

    item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label(_("Detect automatically"));
    g_signal_connect(item, "activate", G_CALLBACK(on_popup_detect_type),
                     self);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

/* 右键弹出值面板菜单：新建 / 类型（强制显示） */
static void
show_value_popup_menu(LrValuePane *self, GdkEventButton *event)
{
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *new_item = gtk_menu_item_new_with_label(_("New"));
    GtkWidget *new_sub = gtk_menu_new();
    GtkWidget *sep;
    GtkWidget *type_item = gtk_menu_item_new_with_label(_("Type"));
    GtkWidget *type_sub = gtk_menu_new();

    build_new_submenu(self, new_sub);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(new_item), new_sub);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), new_item);

    sep = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);

    build_type_submenu(self, type_sub);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(type_item), type_sub);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), type_item);

    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
}

/* 表格页右键：弹出菜单 */
static gboolean
on_value_button_press(GtkWidget *widget, GdkEventButton *event,
                      gpointer user_data)
{
    LrValuePane *self = user_data;
    (void)widget;

    if (event->type == GDK_BUTTON_PRESS && event->button == 3)
    {
        GtkTreePath *path = NULL;

        if (gtk_tree_view_get_path_at_pos(self->view, (gint)event->x,
                                          (gint)event->y, &path,
                                          NULL, NULL, NULL))
        {
            GtkTreeIter iter;
            if (gtk_tree_model_get_iter(GTK_TREE_MODEL(self->store),
                                        &iter, path))
            {
                gtk_tree_selection_select_iter(
                    gtk_tree_view_get_selection(self->view), &iter);
            }
            g_clear_pointer(&self->popup_path, gtk_tree_path_free);
            self->popup_path = gtk_tree_path_copy(path);
            gtk_tree_path_free(path);
        }
        show_value_popup_menu(self, event);
        return TRUE;
    }
    return FALSE;
}

LrValuePane *
lr_value_pane_new(void)
{
    LrValuePane *self = g_new0(LrValuePane, 1);
    GtkWidget *scrolled;
    GtkWidget *label;

    self->man_pages = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                            man_page_free);

    self->widget = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    atk_object_set_name(gtk_widget_get_accessible(self->widget),
                        _("Value panel"));

    self->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(self->stack),
                                  GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_paned_pack1(GTK_PANED(self->widget), self->stack, TRUE, FALSE);

    /* --- 占位页（未选择时保持空白，不显示提示） --- */
    label = gtk_label_new("");
    gtk_stack_add_named(GTK_STACK(self->stack), label, "empty");

    /* --- 表格页 --- */
    self->table_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    self->store = gtk_tree_store_new(N_COLS, G_TYPE_STRING, G_TYPE_STRING,
                                     G_TYPE_STRING, G_TYPE_STRING,
                                     G_TYPE_STRING, G_TYPE_UINT);
    self->view = GTK_TREE_VIEW(gtk_tree_view_new_with_model(
        GTK_TREE_MODEL(self->store)));
    g_object_unref(self->store);
    atk_object_set_name(gtk_widget_get_accessible(GTK_WIDGET(self->view)),
                        _("Value table"));

    gtk_tree_view_set_grid_lines(self->view, GTK_TREE_VIEW_GRID_LINES_VERTICAL);
    g_signal_connect(gtk_tree_view_get_selection(self->view), "changed",
                     G_CALLBACK(on_table_selection_changed), self);
    g_signal_connect(self->view, "button-press-event",
                     G_CALLBACK(on_value_button_press), self);

    build_enabled_column(self);
    build_editable_text_column(self, _("Name"), COL_NAME, FALSE, 213);
    build_type_column(self);
    build_data_column(self);
    build_editable_text_column(self, _("Note"), COL_COMMENT, TRUE, 360);

    scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolled), GTK_WIDGET(self->view));
    gtk_box_pack_start(GTK_BOX(self->table_page), scrolled, TRUE, TRUE, 0);
    gtk_stack_add_named(GTK_STACK(self->stack), self->table_page, "table");

    /* --- 文本页 --- */
    self->text_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    self->text = GTK_TEXT_VIEW(gtk_text_view_new());
    gtk_text_view_set_editable(self->text, FALSE);
    gtk_text_view_set_cursor_visible(self->text, FALSE);
    gtk_text_view_set_wrap_mode(self->text, GTK_WRAP_WORD_CHAR);
    /* 文本内容与面板四周留出边距 */
    gtk_text_view_set_left_margin(self->text, 8);
    gtk_text_view_set_right_margin(self->text, 8);
    gtk_text_view_set_top_margin(self->text, 6);
    gtk_text_view_set_bottom_margin(self->text, 6);

    scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolled), GTK_WIDGET(self->text));
    gtk_box_pack_start(GTK_BOX(self->text_page), scrolled, TRUE, TRUE, 0);
    gtk_stack_add_named(GTK_STACK(self->stack), self->text_page, "text");

    /* --- JSON 页（树形列表，可展开；无启用/备注列） --- */
    self->json_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    self->json_store = gtk_tree_store_new(COL_J_N, G_TYPE_STRING,
                                          G_TYPE_STRING, G_TYPE_STRING);
    self->json_view = GTK_TREE_VIEW(gtk_tree_view_new_with_model(
        GTK_TREE_MODEL(self->json_store)));
    g_object_unref(self->json_store);
    atk_object_set_name(gtk_widget_get_accessible(GTK_WIDGET(self->json_view)),
                        _("JSON tree"));

    append_text_column(self->json_view, _("Name"), COL_J_NAME, TRUE, FALSE, 0);
    append_text_column(self->json_view, _("Type"), COL_J_TYPE, FALSE, FALSE, 0);
    append_text_column(self->json_view, _("Data"), COL_J_DATA, TRUE, FALSE, 0);

    scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolled), GTK_WIDGET(self->json_view));
    gtk_box_pack_start(GTK_BOX(self->json_page), scrolled, TRUE, TRUE, 0);
    gtk_stack_add_named(GTK_STACK(self->stack), self->json_page, "json");

    gtk_stack_set_visible_child_name(GTK_STACK(self->stack), "empty");

    /* --- 底部信息说明面板（选中表格行时显示 man 说明） --- */
    {
        GtkWidget *info_scrolled;

        self->info_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

        self->info_title = GTK_LABEL(gtk_label_new(_("Note")));
        gtk_widget_set_halign(GTK_WIDGET(self->info_title), GTK_ALIGN_START);
        gtk_widget_set_margin_start(GTK_WIDGET(self->info_title), 6);
        gtk_widget_set_margin_top(GTK_WIDGET(self->info_title), 4);
        gtk_widget_set_margin_bottom(GTK_WIDGET(self->info_title), 2);
        gtk_box_pack_start(GTK_BOX(self->info_page),
                           GTK_WIDGET(self->info_title), FALSE, FALSE, 0);

        self->info_text = GTK_TEXT_VIEW(gtk_text_view_new());
        gtk_text_view_set_editable(self->info_text, FALSE);
        gtk_text_view_set_wrap_mode(self->info_text, GTK_WRAP_WORD_CHAR);

        info_scrolled = gtk_scrolled_window_new(NULL, NULL);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(info_scrolled),
                                       GTK_POLICY_AUTOMATIC,
                                       GTK_POLICY_AUTOMATIC);
        gtk_container_add(GTK_CONTAINER(info_scrolled),
                          GTK_WIDGET(self->info_text));
        gtk_box_pack_start(GTK_BOX(self->info_page), info_scrolled, TRUE, TRUE,
                           0);

        gtk_paned_pack2(GTK_PANED(self->widget), self->info_page, FALSE, FALSE);
        g_signal_connect(self->widget, "size-allocate",
                         G_CALLBACK(on_paned_allocate), self);
        g_signal_connect_after(self->info_page, "map",
                               G_CALLBACK(on_info_map), self);
    }

    return self;
}

GtkWidget *
lr_value_pane_get_widget(LrValuePane *self)
{
    return self->widget;
}

void lr_value_pane_free(LrValuePane *self)
{
    if (self == NULL)
        return;
    g_clear_pointer(&self->current_path, g_free);
    g_clear_pointer(&self->source_content, g_free);
    search_reset(self);
    g_free(self->current_basename);
    g_free(self->current_name);
    g_hash_table_destroy(self->man_pages);
    g_free(self);
}
