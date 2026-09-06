#include "core/write.h"
#include "core/format.h"
#include "core/text_file.h"

#include <errno.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

static gchar *
backup_dest(const char *path)
{
    const gchar *base = g_get_user_data_dir();
    gchar *dir = g_build_filename(base, "linux-regedit", "backups", NULL);
    gchar *name = g_path_get_basename(path);
    GDateTime *now;
    gchar *ts, *dest = NULL;
    guint suffix = 0;

    if (g_mkdir_with_parents(dir, 0755) != 0)
    {
        g_free(dir);
        g_free(name);
        return NULL;
    }
    now = g_date_time_new_now_local();
    ts = g_date_time_format(now, "%Y%m%d-%H%M%S");
    g_date_time_unref(now);

    do
    {
        g_free(dest);
        if (suffix == 0)
            dest = g_strdup_printf("%s/%s.%s.bak", dir, name, ts);
        else
            dest = g_strdup_printf("%s/%s.%s-%u.bak", dir, name, ts,
                                   suffix);
        suffix++;
    } while (g_file_test(dest, G_FILE_TEST_EXISTS));

    g_free(dir);
    g_free(name);
    g_free(ts);
    return dest;
}

/* 语义门：候选文本重新解析后，除被编辑行外其余项必须与原文一致 */
static gboolean
semantic_gate(const char *path, const char *source_content,
              const char *candidate, const LrEdit *edits, gsize n_edits,
              GError **error)
{
    LrConfigFile *old_f = lr_parse_config_content(path, source_content,
                                                  strlen(source_content));
    LrConfigFile *new_f = lr_parse_config_content(path, candidate,
                                                  strlen(candidate));
    guint i;
    gboolean ok = TRUE;
    gsize k, disabled_lines = 0;

    for (k = 0; k < n_edits; k++)
        if (edits[k].type == LR_EDIT_DISABLE)
            disabled_lines++;

    if (new_f->items->len + disabled_lines != old_f->items->len)
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            _("round-trip gate: item count changed"));
        ok = FALSE;
        goto out;
    }

    for (i = 0; i < old_f->items->len && ok; i++)
    {
        LrConfigItem *old_it = g_ptr_array_index(old_f->items, i);
        LrConfigItem *new_it = NULL;
        guint j;
        gboolean edited = FALSE;

        for (k = 0; k < n_edits; k++)
            if (edits[k].line == old_it->source_line)
                edited = TRUE;
        if (edited)
            continue; /* 被编辑/被禁用的行不再做逐项比对 */

        for (j = 0; j < new_f->items->len; j++)
        {
            LrConfigItem *cand = g_ptr_array_index(new_f->items, j);
            if (cand->source_line == old_it->source_line &&
                g_strcmp0(cand->key, old_it->key) == 0)
            {
                new_it = cand;
                break;
            }
        }
        if (new_it == NULL)
        {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        _("round-trip gate: item %s missing after edit"),
                        old_it->key);
            ok = FALSE;
            break;
        }

        if (g_strcmp0(old_it->data, new_it->data) != 0 ||
            g_strcmp0(old_it->section, new_it->section) != 0 ||
            g_strcmp0(old_it->comment, new_it->comment) != 0 ||
            old_it->enabled != new_it->enabled)
        {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        _("round-trip gate: unintended change on line %u"),
                        old_it->source_line);
            ok = FALSE;
        }
    }

out:
    lr_config_file_free(old_f);
    lr_config_file_free(new_f);
    return ok;
}

static gboolean
copy_file(const char *src, const char *dst, GError **error)
{
    GFile *fs = g_file_new_for_path(src);
    GFile *fd = g_file_new_for_path(dst);
    gboolean ok = g_file_copy(fs, fd, G_FILE_COPY_OVERWRITE, NULL, NULL,
                              NULL, error);
    g_object_unref(fs);
    g_object_unref(fd);
    return ok;
}

gboolean
lr_save_config_file(const char *path, const char *source_content,
                    const LrEdit *edits, gsize n_edits, GError **error)
{
    gchar *disk = NULL, *candidate = NULL, *backup = NULL;
    gchar *tmp = NULL;
    GStatBuf st;
    gsize disk_len = 0;
    gboolean ok = FALSE;

    if (g_file_test(path, G_FILE_TEST_IS_SYMLINK))
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            _("refusing to write through a symbolic link"));
        return FALSE;
    }
    if (!g_file_get_contents(path, &disk, &disk_len, error))
        return FALSE;
    if (source_content != NULL && g_strcmp0(disk, source_content) != 0)
    {
        g_free(disk);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            _("file changed on disk since it was opened"));
        return FALSE;
    }

    if (!lr_apply_edits(disk, edits, n_edits, &candidate, error))
    {
        g_free(disk);
        return FALSE;
    }
    g_free(disk);

    if (!semantic_gate(path, source_content != NULL ? source_content : "",
                       candidate, edits, n_edits, error))
    {
        g_free(candidate);
        return FALSE;
    }

    backup = backup_dest(path);
    if (backup == NULL)
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            _("cannot create backup directory"));
        g_free(candidate);
        return FALSE;
    }
    if (!copy_file(path, backup, error))
    {
        g_free(candidate);
        g_free(backup);
        return FALSE;
    }

    tmp = g_strdup_printf("%s.lrtmp.%d", path, (int)getpid());
    if (!g_file_set_contents(tmp, candidate, (gssize)strlen(candidate),
                             error))
        goto cleanup;
    if (g_stat(path, &st) == 0)
        g_chmod(tmp, st.st_mode & 07777);
    if (g_rename(tmp, path) != 0)
    {
        g_set_error(error, G_IO_ERROR, g_file_error_from_errno(errno),
                    "%s", g_strerror(errno));
        goto cleanup;
    }
    g_free(tmp);
    tmp = NULL;

    /* 写后重解析校验；失败则用备份回滚 */
    {
        gchar *verify = NULL;
        gsize vlen = 0;

        ok = FALSE;
        if (g_file_get_contents(path, &verify, &vlen, NULL))
        {
            ok = vlen == strlen(candidate) &&
                 memcmp(verify, candidate, vlen) == 0;
            g_clear_pointer(&verify, g_free);
        }
        if (!ok)
        {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                                _("post-write parse check failed; rolling back"));
            copy_file(backup, path, NULL);
        }
    }

cleanup:
    if (tmp != NULL)
        g_unlink(tmp);
    g_free(tmp);
    g_free(candidate);
    g_free(backup);
    return ok;
}
