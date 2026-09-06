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

    /* 7b. keyword 风格：空白分隔行同样只替换值 */
    {
        const gchar *src = "Port 22\nProtocol 2\n";
        gboolean ok = FALSE;
        gchar *out = apply_one(src, LR_EDIT_SET_VALUE, 0, "Port", "2222",
                               &ok);

        TEST_ASSERT(ok);
        TEST_ASSERT_STR_EQ(out, "Port 2222\nProtocol 2\n");
        g_free(out);
    }

    /* 7c. keyword 风格：分隔空白保留 */
    {
        const gchar *src = "Port  22\n";
        gboolean ok = FALSE;
        gchar *out = apply_one(src, LR_EDIT_SET_VALUE, 0, "Port", "2222",
                               &ok);

        TEST_ASSERT(ok);
        TEST_ASSERT_STR_EQ(out, "Port  2222\n");
        g_free(out);
    }

    /* 7d. apt 块内叶子赋值：只改值，保留 {}、缩进与分号 */
    {
        const gchar *src =
            "Acquire::IndexTargets {\n"
            "    deb::DEP-11 {\n"
            "        MetaKey \"$(COMPONENT)/x.yml\";\n"
            "        KeepCompressed true;\n"
            "    }\n"
            "}\n";
        const gchar *want =
            "Acquire::IndexTargets {\n"
            "    deb::DEP-11 {\n"
            "        MetaKey \"NEW\";\n"
            "        KeepCompressed true;\n"
            "    }\n"
            "}\n";
        gboolean ok = FALSE;
        gchar *out = apply_one(src, LR_EDIT_SET_VALUE, 2, "MetaKey",
                               "NEW", &ok);

        TEST_ASSERT(ok);
        TEST_ASSERT_STR_EQ(out, want);
        g_free(out);
    }

    /* 7e. Tab 分隔与“空值补写”等畸形但安全的情形 */
    {
        gboolean ok = FALSE;
        gchar *out;

        out = apply_one("Port\t22\n", LR_EDIT_SET_VALUE, 0, "Port",
                        "8080", &ok);
        TEST_ASSERT(ok);
        TEST_ASSERT_STR_EQ(out, "Port\t8080\n");
        g_free(out);

        out = apply_one("a =\n", LR_EDIT_SET_VALUE, 0, "a", "v", &ok);
        TEST_ASSERT(ok);
        TEST_ASSERT_STR_EQ(out, "a =v\n");
        g_free(out);
    }

    /* 7f. REMOVE：物理删除整行，其余行逐字保留 */
    {
        const gchar *src = "a=1\nb=2\nc=3\n";
        LrEdit edit = {LR_EDIT_REMOVE, 1, NULL, NULL};
        GError *err = NULL;
        gchar *out = NULL;

        TEST_ASSERT(lr_apply_edits(src, &edit, 1, &out, &err));
        TEST_ASSERT(err == NULL);
        TEST_ASSERT_STR_EQ(out, "a=1\nc=3\n");
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

    /* 11. 行状态 → 编辑操作：值变更生成 SET_VALUE */
    {
        const gchar *src = "[s]\nPort = 22   # note\nKeep = x\n";
        LrRowState rows[] = {
            {1, "Port", "8080", "true", "note", "Number"},
            {2, "Keep", "x", "true", NULL, "String"},
        };
        LrEdit *edits = NULL;
        gsize n = 0;
        GError *err = NULL;

        TEST_ASSERT(lr_build_edits_from_rows("t.ini", src, rows, 2,
                                             &edits, &n, &err));
        TEST_ASSERT(err == NULL);
        TEST_ASSERT(n == 1);
        TEST_ASSERT(edits[0].type == LR_EDIT_SET_VALUE);
        TEST_ASSERT(edits[0].line == 1);
        TEST_ASSERT_STR_EQ(edits[0].value, "8080");
        g_free(edits);
    }

    /* 12. 禁用变更生成 DISABLE */
    {
        const gchar *src = "[s]\nPort = 22\n";
        LrRowState rows[] = {{1, "Port", "22", "false", NULL, "Number"}};
        LrEdit *edits = NULL;
        gsize n = 0;
        GError *err = NULL;

        TEST_ASSERT(lr_build_edits_from_rows("t.ini", src, rows, 1,
                                             &edits, &n, &err));
        TEST_ASSERT(n == 1);
        TEST_ASSERT(edits[0].type == LR_EDIT_DISABLE);
        g_free(edits);
    }

    /* 12b. keyword 被注释行启用生成 ENABLE */
    {
        const gchar *src = "#Port 22\nProtocol 2\n";
        LrRowState rows[] = {{0, "Port", "22", "true", NULL, "Number"},
                             {1, "Protocol", "2", "true", NULL, "Number"}};
        LrEdit *edits = NULL;
        gsize n = 0;
        GError *err = NULL;

        TEST_ASSERT(lr_build_edits_from_rows("t.conf", src, rows, 2,
                                             &edits, &n, &err));
        TEST_ASSERT(err == NULL);
        TEST_ASSERT(n >= 1);
        TEST_ASSERT(edits[0].type == LR_EDIT_ENABLE);
        g_free(edits);
    }

    /* 13. 不支持修改必须拒绝：重命名 / 类型 / 备注 / 新增行 / 未知行 */
    {
        const gchar *src = "[s]\nPort = 22   # note\nKeep = x\n";
        GError *err = NULL;
        LrEdit *edits = NULL;
        gsize n = 0;

        LrRowState rename_row[] = {{1, "PortX", "22", "true", "note",
                                    "Number"}};
        TEST_ASSERT(!lr_build_edits_from_rows("t.ini", src, rename_row, 1,
                                              &edits, &n, &err));
        g_clear_error(&err);

        LrRowState type_row[] = {{1, "Port", "22", "true", "note",
                                  "String"}};
        TEST_ASSERT(!lr_build_edits_from_rows("t.ini", src, type_row, 1,
                                              &edits, &n, &err));
        g_clear_error(&err);

        LrRowState comment_row[] = {{1, "Port", "22", "true", "other",
                                     "Number"}};
        TEST_ASSERT(!lr_build_edits_from_rows("t.ini", src, comment_row, 1,
                                              &edits, &n, &err));
        g_clear_error(&err);

        LrRowState new_row[] = {{G_MAXUINT, "New", "1", "true", NULL,
                                 "Number"}};
        TEST_ASSERT(!lr_build_edits_from_rows("t.ini", src, new_row, 1,
                                              &edits, &n, &err));
        g_clear_error(&err);

        LrRowState unknown_row[] = {{99, "Port", "22", "true", "note",
                                     "Number"}};
        TEST_ASSERT(!lr_build_edits_from_rows("t.ini", src, unknown_row, 1,
                                              &edits, &n, &err));
        g_clear_error(&err);
    }
}

/* 确定性模糊：随机畸形内容 + 随机编辑，绝不允许崩溃/内存错误 */
void test_writeback_fuzz(void)
{
    static const gchar *const tokens[] = {
        "a", "b", "Key", "value", "\"q # x\"", "#comment", "; semi",
        "[s]", "{", "}", ";", "=", ":", "\t", "\\", "中文配置",
        "x=1", "#Port 22", "K\"v", "Port 22", "http://x/#y",
        NULL};
    GRand *r = g_rand_new_with_seed(0x20260906);
    guint iter, i;

    for (iter = 0; iter < 300; iter++)
    {
        GString *content = g_string_new(NULL);
        guint nlines = 1 + g_rand_int_range(r, 0, 8);
        LrEdit edit;
        GError *err = NULL;
        gchar *out = NULL;

        for (i = 0; i < nlines; i++)
        {
            guint ntoks = 1 + g_rand_int_range(r, 0, 5);
            guint j;

            for (j = 0; j < ntoks; j++)
            {
                const gchar *tok =
                    tokens[g_rand_int_range(r, 0, 21)];
                g_string_append(content, tok);
                if (j + 1 < ntoks && g_rand_boolean(r))
                    g_string_append_c(content,
                                      g_rand_boolean(r) ? ' ' : '\t');
            }
            g_string_append_c(content, '\n');
        }

        edit.type = (LrEditType)g_rand_int_range(r, 0, 3);
        edit.line = g_rand_int_range(r, 0, nlines + 4);
        edit.key = tokens[g_rand_int_range(r, 0, 21)];
        edit.value = tokens[g_rand_int_range(r, 0, 21)];

        (void)lr_apply_edits(content->str, &edit, 1, &out, &err);
        g_clear_error(&err);
        g_free(out);

        /* 随机内容也能安全走解析器（成败皆可，但不得崩溃） */
        {
            LrConfigFile *f = lr_parse_config_content(
                "fuzz.conf", content->str, content->len);
            lr_config_file_free(f);
        }
        g_string_free(content, TRUE);
    }
    g_rand_free(r);
}
