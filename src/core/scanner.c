#include "core/scanner.h"
#include "core/limits.h"
#include "core/text_file.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

void lr_scan_entry_free(LrScanEntry *entry)
{
    if (entry == NULL)
        return;
    g_free(entry->name);
    g_free(entry->path);
    g_free(entry);
}

static gint
compare_entries(gconstpointer a, gconstpointer b)
{
    const LrScanEntry *ea = *((LrScanEntry **)a);
    const LrScanEntry *eb = *((LrScanEntry **)b);
    gint ad = (ea->kind == LR_SCAN_DIR);
    gint bd = (eb->kind == LR_SCAN_DIR);

    if (ad != bd)
        return bd - ad; /* 目录优先 */
    return g_ascii_strcasecmp(ea->name, eb->name);
}

/* 文件大小超过阈值则跳过 */
static gboolean
is_oversized(const char *path)
{
    GStatBuf st;

    if (g_stat(path, &st) != 0)
        return FALSE;
    return st.st_size > LR_MAX_FILE_SIZE;
}

/* 读取文件头部最多 bufsize 字节，返回是否文本（含 NUL 判非文本），
 * *n 为实际读到的字节数；读取失败仍视为文本（兜底显示）。 */
static gboolean
read_file_head(const char *path, guchar *buf, gsize bufsize, gsize *n)
{
    GFile *file;
    GFileInputStream *stream;
    gssize r;
    gboolean text = TRUE;
    GError *error = NULL;
    *n = 0;
    file = g_file_new_for_path(path);
    stream = g_file_read(file, NULL, &error);
    if (stream == NULL)
    {
        g_clear_error(&error);
        g_object_unref(file);
        return TRUE; /* 无法读取时仍显示 */
    }

    r = g_input_stream_read(G_INPUT_STREAM(stream), buf, bufsize, NULL, NULL);
    if (r >= 0)
    {
        *n = (gsize)r;
        if (lr_text_is_binary(buf, *n))
            text = FALSE;
    }

    g_input_stream_close(G_INPUT_STREAM(stream), NULL, NULL);
    g_object_unref(stream);
    g_object_unref(file);
    return text;
}

/* 目录为空（无任何条目）则跳过 */
static gboolean
is_empty_dir(const char *path)
{
    GDir *dir = g_dir_open(path, 0, NULL);
    gboolean empty;

    if (dir == NULL)
        return TRUE;
    empty = (g_dir_read_name(dir) == NULL);
    g_dir_close(dir);
    return empty;
}

GPtrArray *
lr_scanner_list_dir(const char *path)
{
    GPtrArray *arr = g_ptr_array_new_with_free_func(
        (GDestroyNotify)lr_scan_entry_free);
    GDir *dir;
    const char *name;

    dir = g_dir_open(path, 0, NULL);
    if (dir == NULL)
        return arr;

    while ((name = g_dir_read_name(dir)) != NULL)
    {
        char *full = g_build_filename(path, name, NULL);
        LrScanEntry *entry;
        gboolean is_dir;

        if (g_str_equal(name, ".") || g_str_equal(name, ".."))
        {
            g_free(full);
            continue;
        }

        is_dir = g_file_test(full, G_FILE_TEST_IS_DIR);

        if (is_dir)
        {
            /* 过滤：空文件夹不显示 */
            if (is_empty_dir(full))
            {
                g_free(full);
                continue;
            }

            entry = g_new0(LrScanEntry, 1);
            entry->name = g_strdup(name);
            entry->path = full;
            entry->kind = LR_SCAN_DIR;
            g_ptr_array_add(arr, entry);
        }
        else
        {
            /* 过滤超大文件 / 非文本文件；只读一次头部，既判断文本性又用于
             * 格式嗅探，避免为判断格式而整文件读盘 */
            guchar head[LR_SNIFF_MAX + 1];
            gsize n = 0;
            LrConfigFormat fmt;

            if (is_oversized(full) ||
                !read_file_head(full, head, LR_SNIFF_MAX, &n))
            {
                g_free(full);
                continue;
            }
            head[n] = '\0';

            entry = g_new0(LrScanEntry, 1);
            entry->name = g_strdup(name);
            entry->path = full;

            /* 基于头部嗅探（JSON 数组 / XML 需全文验证：超 LR_SNIFF_MAX 的
             * 超大文件可能归为“其他文件”，仍可打开，仅影响图标分类） */
            fmt = lr_format_detect_content(full, (const char *)head, n);
            if (lr_format_supported(fmt))
            {
                entry->kind = LR_SCAN_SUPPORTED_FILE;
                entry->format = fmt;
            }
            else
            {
                entry->kind = LR_SCAN_OTHER_FILE;
            }
            g_ptr_array_add(arr, entry);
        }
    }

    g_dir_close(dir);
    g_ptr_array_sort(arr, compare_entries);
    return arr;
}
