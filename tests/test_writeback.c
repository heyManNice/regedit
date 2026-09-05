#include "test_runner.h"
#include "core/edits.h"
#include "core/format.h"

#include <glib/gstdio.h>

static gchar *
apply_one(const gchar *content, LrEditType type, guint line,
          const gchar *key, const gchar *value, gboolean *ok)
{
    LrEdit edit;
    GError *err = NULL;
    gchar *out = NULL;

    edit.type = type;
    edit.line = line;
    edit.key = key;
    edit.value = value;
    *ok = lr_apply_edits(content, &edit, 1, &out, &err);
    if (!*ok)
    {
        TEST_ASSERT(err != NULL);
        g_clear_error(&err);
    }
    return out;
}

static LrConfigItem *
find_item(LrConfigFile *f, const char *key)
{
    guint i;

    for (i = 0; i < f->items->len; i++)
    {
        LrConfigItem *it = g_ptr_array_index(f->items, i);
        if (g_strcmp0(it->key, key) == 0)
            return it;
    }
    return NULL;
}

void test_writeback(void)
{
    /* 1. SET_VALUE：只改 value，保留缩进/行内注释/其他行逐字不变 */
    {
        const gchar *src =
            "[server]\n"
            "# 端口说明\n"
            "Port = 22   # 行内注释\n"
            "Name = \"my host\"\n"
            "Keep = same\n";
        const gchar *want =
            "[server]\n"
            "# 端口说明\n"
            "Port = 8080   # 行内注释\n"
            "Name = \"my host\"\n"
            "Keep = same\n";
        gboolean ok = FALSE;
        gchar *out = apply_one(src, LR_EDIT_SET_VALUE, 2, "Port", "8080",
                               &ok);

        TEST_ASSERT(ok);
        TEST_ASSERT_STR_EQ(out, want);
        g_free(out);
    }

    /* 2. 引号风格保留：值仍按原引号包裹 */
    {
        const gchar *src = "Name = \"my host\"\n";
        gboolean ok = FALSE;
        gchar *out = apply_one(src, LR_EDIT_SET_VALUE, 0, "Name",
                               "new host", &ok);

        TEST_ASSERT(ok);
        TEST_ASSERT_STR_EQ(out, "Name = \"new host\"\n");
        g_free(out);
    }

    /* 3. CRLF 与行尾细节保留 */
    {
        const gchar *src = "Port = 22\r\nEnable = yes\r\n";
        gboolean ok = FALSE;
        gchar *out = apply_one(src, LR_EDIT_SET_VALUE, 0, "Port", "8080",
                               &ok);

        TEST_ASSERT(ok);
        TEST_ASSERT_STR_EQ(out, "Port = 8080\r\nEnable = yes\r\n");
        g_free(out);
    }

    /* 4. 末尾无换行的文件 */
    {
        const gchar *src = "Port = 22";
        gboolean ok = FALSE;
        gchar *out = apply_one(src, LR_EDIT_SET_VALUE, 0, "Port", "8080",
                               &ok);

        TEST_ASSERT(ok);
        TEST_ASSERT_STR_EQ(out, "Port = 8080");
        g_free(out);
    }

    /* 5. DISABLE / ENABLE 往返 */
    {
        const gchar *src = "Port = 22\n";
        gboolean ok = FALSE;
        gchar *off = apply_one(src, LR_EDIT_DISABLE, 0, "Port", NULL, &ok);

        TEST_ASSERT(ok);
        TEST_ASSERT_STR_EQ(off, "#Port = 22\n");
        gchar *on = apply_one(off, LR_EDIT_ENABLE, 0, "Port", NULL, &ok);
        TEST_ASSERT(ok);
        TEST_ASSERT_STR_EQ(on, src);
        g_free(off);
        g_free(on);
    }

    /* 6. UTF-8 BOM 前缀保留 */
    {
        const gchar *src = "\xEF\xBB\xBF[server]\nPort = 22\n";
        gboolean ok = FALSE;
        gchar *out = apply_one(src, LR_EDIT_SET_VALUE, 1, "Port", "8080",
                               &ok);

        TEST_ASSERT(ok);
        TEST_ASSERT_STR_EQ(out,
                           "\xEF\xBB\xBF[server]\nPort = 8080\n");
        g_free(out);
    }

    /* 7. 引号内的 # 不是注释 */
    {
        const gchar *src = "Name = \"a#b\"   # 尾注\n";
        gboolean ok = FALSE;
        gchar *out = apply_one(src, LR_EDIT_SET_VALUE, 0, "Name", "x#y",
                               &ok);

        TEST_ASSERT(ok);
        TEST_ASSERT_STR_EQ(out, "Name = \"x#y\"   # 尾注\n");
        g_free(out);
    }

    /* 8. 重复键：按行号精确修改，不动其他同名行 */
    {
        const gchar *src = "[s]\na = 1\na = 2\n";
        gboolean ok = FALSE;
        gchar *out = apply_one(src, LR_EDIT_SET_VALUE, 2, "a", "99", &ok);

        TEST_ASSERT(ok);
        TEST_ASSERT_STR_EQ(out, "[s]\na = 1\na = 99\n");
        g_free(out);
    }

    /* 9. 意外输入全部安全拒绝 */
    {
        gboolean ok = FALSE;
        gchar *out;

        out = apply_one("", LR_EDIT_SET_VALUE, 0, "k", "v", &ok);
        TEST_ASSERT(!ok);
        g_free(out);

        out = apply_one("[s]\na = 1\n", LR_EDIT_SET_VALUE, 9, "a", "v",
                        &ok);
        TEST_ASSERT(!ok);
        g_free(out);

        out = apply_one("a = 1\n", LR_EDIT_SET_VALUE, 0, "Wrong", "v",
                        &ok);
        TEST_ASSERT(!ok);
        g_free(out);

        out = apply_one("# just a comment\n", LR_EDIT_SET_VALUE, 0, "a",
                        "v", &ok);
        TEST_ASSERT(!ok);
        g_free(out);

        out = apply_one("a = 1\n", LR_EDIT_ENABLE, 0, "a", NULL, &ok);
        TEST_ASSERT(!ok);
        g_free(out);

        out = apply_one("#a = 1\n", LR_EDIT_DISABLE, 0, "a", NULL, &ok);
        TEST_ASSERT(!ok);
        g_free(out);
    }

    /* 10. 写后 round-trip：重新解析语义一致、其余项逐字未变 */
    {
        const gchar *src =
            "[server]\n"
            "# 端口说明\n"
            "Port = 22   # 行内注释\n"
            "Name = \"my host\"\n"
            "Keep = same\n";
        gboolean ok = FALSE;
        gchar *out = apply_one(src, LR_EDIT_SET_VALUE, 2, "Port", "8080",
                               &ok);
        LrConfigFile *old_f = lr_parse_config_content("t.conf", src,
                                                      strlen(src));
        LrConfigFile *new_f = lr_parse_config_content("t.conf", out,
                                                      strlen(out));
        LrConfigItem *old_port = find_item(old_f, "Port");
        LrConfigItem *new_port = find_item(new_f, "Port");

        TEST_ASSERT(ok);
        TEST_ASSERT(new_port != NULL);
        TEST_ASSERT_STR_EQ(new_port->data, "8080");
        TEST_ASSERT(new_port->type == LR_VALUE_NUMBER);
        TEST_ASSERT_STR_EQ(new_port->comment, old_port->comment);
        TEST_ASSERT(new_f->items->len == old_f->items->len);

        lr_config_file_free(old_f);
        lr_config_file_free(new_f);
        g_free(out);
    }
}
