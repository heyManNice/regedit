#include "test_runner.h"
#include "core/scanner.h"
#include "core/text_file.h"
#include "core/limits.h"

#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

void test_scanner(void)
{
    gchar *base = g_dir_make_tmp("lr-test-XXXXXX", NULL);
    gchar *small, *big, *exact, *under, *bin, *latin, *empty, *fulldir,
        *inner;
    GPtrArray *arr;
    guint i;
    gboolean saw_small = FALSE, saw_fulldir = FALSE, saw_big = FALSE;
    gboolean saw_bin = FALSE, saw_empty = FALSE, saw_exact = FALSE;
    gboolean saw_under = FALSE;

    TEST_ASSERT(base != NULL);

    /* 1. 小文本文件 → 显示 */
    small = g_build_filename(base, "small.conf", NULL);
    g_file_set_contents(small, "key=value\n", -1, NULL);

    /* 2. 大于 128KB 的文件 → 隐藏 */
    big = g_build_filename(base, "big.bin", NULL);
    {
        FILE *fp = fopen(big, "wb");
        char buf[1024];
        memset(buf, 'x', sizeof(buf));
        for (int k = 0; k < 130; k++)
            fwrite(buf, 1, sizeof(buf), fp);
        fclose(fp);
    }

    /* 3. 含 NUL 的二进制文件 → 隐藏 */
    bin = g_build_filename(base, "data.bin", NULL);
    g_file_set_contents(bin, "abc\0def", 7, NULL);

    /* 3a. 非法 UTF-8（latin1）文本：可读但转成合法 UTF-8 */
    latin = g_build_filename(base, "latin.conf", NULL);
    g_file_set_contents(latin, "caf\xe9 = 1\n", 9, NULL);

    /* 3b. 恰好 128KB → 显示（阈值是“大于”才隐藏）；差 1 字节 → 显示 */
    exact = g_build_filename(base, "exact.conf", NULL);
    under = g_build_filename(base, "under.conf", NULL);
    {
        FILE *fp = fopen(exact, "wb");
        char buf[1024];
        gsize left = LR_MAX_FILE_SIZE;

        memset(buf, 'y', sizeof(buf));
        while (left > 0)
        {
            gsize n = MIN(left, sizeof(buf));
            fwrite(buf, 1, n, fp);
            left -= n;
        }
        fclose(fp);
        fp = fopen(under, "wb");
        gsize n = LR_MAX_FILE_SIZE - 1;
        while (n > 0)
        {
            gsize chunk = MIN(n, sizeof(buf));
            fwrite(buf, 1, chunk, fp);
            n -= chunk;
        }
        fclose(fp);
    }

    /* 4. 空目录 → 隐藏 */
    empty = g_build_filename(base, "emptydir", NULL);
    g_mkdir(empty, 0755);

    /* 5. 非空目录 → 显示 */
    fulldir = g_build_filename(base, "fulldir", NULL);
    g_mkdir(fulldir, 0755);
    inner = g_build_filename(fulldir, "x.conf", NULL);
    g_file_set_contents(inner, "a=1\n", -1, NULL);

    arr = lr_scanner_list_dir(base);
    for (i = 0; i < arr->len; i++)
    {
        LrScanEntry *e = g_ptr_array_index(arr, i);
        if (g_str_equal(e->name, "small.conf"))
            saw_small = TRUE;
        if (g_str_equal(e->name, "fulldir"))
            saw_fulldir = TRUE;
        if (g_str_equal(e->name, "big.bin"))
            saw_big = TRUE;
        if (g_str_equal(e->name, "data.bin"))
            saw_bin = TRUE;
        if (g_str_equal(e->name, "exact.conf"))
            saw_exact = TRUE;
        if (g_str_equal(e->name, "under.conf"))
            saw_under = TRUE;
        if (g_str_equal(e->name, "emptydir"))
            saw_empty = TRUE;
    }
    g_ptr_array_unref(arr);

    TEST_ASSERT(saw_small);
    TEST_ASSERT(saw_fulldir);
    TEST_ASSERT(!saw_big);
    TEST_ASSERT(!saw_bin);
    TEST_ASSERT(!saw_empty);
    TEST_ASSERT(saw_exact);
    TEST_ASSERT(saw_under);

    /* 统一读取守卫：小文本可读；恰好 128K 可读；超大/二进制拒绝 */
    {
        gchar *content = NULL;
        gsize content_len = 0;
        GError *err = NULL;
        gchar *missing = g_build_filename(base, "no-such-file", NULL);

        TEST_ASSERT(lr_text_file_read(small, &content, &content_len,
                                      &err) == LR_TEXT_OK);
        g_clear_pointer(&content, g_free);
        TEST_ASSERT(lr_text_file_read(exact, &content, &content_len,
                                      &err) == LR_TEXT_OK);
        g_clear_pointer(&content, g_free);
        TEST_ASSERT(lr_text_file_read(big, &content, &content_len,
                                      &err) == LR_TEXT_TOO_LARGE);
        TEST_ASSERT(lr_text_file_read(bin, &content, &content_len,
                                      &err) == LR_TEXT_BINARY);
        TEST_ASSERT(lr_text_file_read(latin, &content, &content_len,
                                      &err) == LR_TEXT_OK);
        TEST_ASSERT(g_utf8_validate(content, content_len, NULL));
        g_clear_pointer(&content, g_free);
        TEST_ASSERT(lr_text_file_read(missing, &content, &content_len,
                                      &err) == LR_TEXT_ERROR);
        g_clear_error(&err);
        g_free(missing);
    }

    /* 清理 */
    g_unlink(small);
    g_unlink(big);
    g_unlink(bin);
    g_unlink(latin);
    g_unlink(exact);
    g_unlink(under);
    g_unlink(inner);
    g_unlink(empty);
    g_unlink(fulldir);
    g_rmdir(base);
    g_free(small);
    g_free(big);
    g_free(bin);
    g_free(latin);
    g_free(exact);
    g_free(under);
    g_free(inner);
    g_free(fulldir);
    g_free(base);
}
