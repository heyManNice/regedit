#include "test_runner.h"
#include "core/edits.h"
#include "core/write.h"

#include <glib/gstdio.h>
#include <gio/gio.h>
#include <string.h>

static gchar *
read_file(const char *path)
{
    gchar *data = NULL;

    g_file_get_contents(path, &data, NULL, NULL);
    return data;
}

static guint
backup_count(const char *data_home)
{
    gchar *dir = g_build_filename(data_home, "linux-regedit", "backups",
                                  NULL);
    GDir *gd = g_dir_open(dir, 0, NULL);
    guint n = 0;

    if (gd != NULL)
    {
        while (g_dir_read_name(gd) != NULL)
            n++;
        g_dir_close(gd);
    }
    g_free(dir);
    return n;
}

void test_write(void)
{
    gchar *base = g_dir_make_tmp("lr-write-XXXXXX", NULL);
    gchar *data_home = g_build_filename(base, "data", NULL);
    gchar *path = g_build_filename(base, "sample.ini", NULL);

    g_mkdir_with_parents(data_home, 0755);
    g_setenv("XDG_DATA_HOME", data_home, TRUE);

    /* 1. 正常保存：文件内容正确、生成备份、未编辑行逐字保留 */
    {
        const gchar *src =
            "[s]\n"
            "Port = 22   # note\n"
            "Keep = x\n";
        const gchar *want =
            "[s]\n"
            "Port = 8080   # note\n"
            "Keep = x\n";
        LrEdit edit = {LR_EDIT_SET_VALUE, 1, "Port", "8080"};
        GError *err = NULL;
        gboolean ok;

        g_file_set_contents(path, src, -1, NULL);
        ok = lr_save_config_file(path, src, &edit, 1, &err);
        TEST_ASSERT(ok);
        TEST_ASSERT(err == NULL);
        gchar *now = read_file(path);
        TEST_ASSERT_STR_EQ(now, want);
        g_free(now);
        TEST_ASSERT(backup_count(data_home) == 1);
    }

    /* 2. 磁盘被外部修改：中止且不覆盖 */
    {
        const gchar *src = "Port = 22\n";
        LrEdit edit = {LR_EDIT_SET_VALUE, 0, "Port", "999"};
        GError *err = NULL;
        gboolean ok;

        g_file_set_contents(path, src, -1, NULL);
        g_file_set_contents(path, "Port = 21\n", -1, NULL); /* 外部修改 */
        ok = lr_save_config_file(path, src, &edit, 1, &err);
        TEST_ASSERT(!ok);
        TEST_ASSERT(err != NULL);
        g_clear_error(&err);
        gchar *now = read_file(path);
        TEST_ASSERT_STR_EQ(now, "Port = 21\n");
        g_free(now);
    }

    /* 3. 引擎拒绝（键不匹配）：文件不变、不产生新备份 */
    {
        const gchar *src = "Port = 22\n";
        LrEdit edit = {LR_EDIT_SET_VALUE, 0, "Wrong", "999"};
        GError *err = NULL;
        guint before = backup_count(data_home);
        gboolean ok;

        g_file_set_contents(path, src, -1, NULL);
        ok = lr_save_config_file(path, src, &edit, 1, &err);
        TEST_ASSERT(!ok);
        TEST_ASSERT(err != NULL);
        g_clear_error(&err);
        TEST_ASSERT(backup_count(data_home) == before);
        gchar *now = read_file(path);
        TEST_ASSERT_STR_EQ(now, src);
        g_free(now);
    }

    /* 4. 符号链接目标：拒绝写入，真实文件不受影响 */
    {
        const gchar *src = "a = 1\n";
        gchar *real = g_build_filename(base, "real.conf", NULL);
        gchar *link = g_build_filename(base, "link.conf", NULL);
        LrEdit edit = {LR_EDIT_SET_VALUE, 0, "a", "2"};
        GError *err = NULL;
        gboolean ok;

        g_file_set_contents(real, src, -1, NULL);
        g_unlink(link);
        {
            GFile *lf = g_file_new_for_path(link);
            TEST_ASSERT(g_file_make_symbolic_link(lf, real, NULL, NULL));
            g_object_unref(lf);
        }
        ok = lr_save_config_file(link, src, &edit, 1, &err);
        TEST_ASSERT(!ok);
        g_clear_error(&err);
        gchar *now = read_file(real);
        TEST_ASSERT_STR_EQ(now, src);
        g_free(now);
        g_free(real);
        g_free(link);
    }

    /* 5. 无编辑保存：成功但视为一次受保护写（仍备份） */
    {
        const gchar *src = "a = 1\n";
        GError *err = NULL;
        gboolean ok;

        g_file_set_contents(path, src, -1, NULL);
        ok = lr_save_config_file(path, src, NULL, 0, &err);
        TEST_ASSERT(ok);
        g_clear_error(&err);
    }

    /* 6. 启用列修改（true → false）经 builder + 安全管线写回为注释行 */
    {
        const gchar *src = "[s]\nPort = 22\n";
        LrRowState rows[] = {{1, "Port", "22", "false", NULL, "Number"}};
        LrEdit *edits = NULL;
        gsize n_edits = 0;
        GError *err = NULL;
        gboolean ok;

        g_file_set_contents(path, src, -1, NULL);
        ok = lr_build_edits_from_rows(path, src, rows, 1,
                                      &edits, &n_edits, &err);
        TEST_ASSERT(ok);
        TEST_ASSERT(n_edits == 1);
        TEST_ASSERT(edits[0].type == LR_EDIT_DISABLE);
        ok = lr_save_config_file(path, src, edits, n_edits, &err);
        g_free(edits);
        TEST_ASSERT(ok);
        TEST_ASSERT(err == NULL);
        gchar *now = read_file(path);
        TEST_ASSERT_STR_EQ(now, "[s]\n#Port = 22\n");
        g_free(now);
        g_clear_error(&err);
    }

    /* 7. apt 块内叶子赋值：builder + 安全管线写回，结构完整保留 */
    {
        const gchar *src =
            "Acquire::IndexTargets {\n"
            "    deb::DEP-11 {\n"
            "        MetaKey \"$(COMPONENT)/x.yml\";\n"
            "        KeepCompressed true;\n"
            "    }\n"
            "}\n";
        LrRowState rows[] = {{2, "MetaKey", "NEW", "true", NULL,
                              "String"}};
        LrEdit *edits = NULL;
        gsize n_edits = 0;
        GError *err = NULL;
        gboolean ok;

        g_file_set_contents(path, src, -1, NULL);
        ok = lr_build_edits_from_rows(path, src, rows, 1,
                                      &edits, &n_edits, &err);
        TEST_ASSERT(ok);
        TEST_ASSERT(n_edits == 1);
        ok = lr_save_config_file(path, src, edits, n_edits, &err);
        g_free(edits);
        TEST_ASSERT(ok);
        TEST_ASSERT(err == NULL);
        gchar *now = read_file(path);
        TEST_ASSERT(strstr(now, "MetaKey \"NEW\";") != NULL);
        TEST_ASSERT(strstr(now, "KeepCompressed true;") != NULL);
        TEST_ASSERT(strstr(now, "deb::DEP-11 {") != NULL);
        g_free(now);
        g_clear_error(&err);
    }

    g_free(path);
    g_free(data_home);
    g_free(base);
}
