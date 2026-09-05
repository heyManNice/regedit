/* 文件级工具：备份 / 对比
 *
 * 备份放在用户数据目录（$XDG_DATA_HOME/linux-regedit/backups 或
 * ~/.local/share/...），不触碰 /etc 等系统文件，也不要求写权限。 */
#include "ui/file_tools.h"
#include "ui/dialog_utils.h"

#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <string.h>

static gchar *
backups_dir(void)
{
    const gchar *base = g_get_user_data_dir();
    gchar *dir = g_build_filename(base, "linux-regedit", "backups", NULL);
    if (g_mkdir_with_parents(dir, 0755) != 0)
    {
        g_free(dir);
        return NULL;
    }
    return dir;
}

static void
show_simple_dialog(GtkWindow *parent, GtkMessageType kind,
                   const gchar *title, const gchar *msg)
{
    GtkWidget *dialog =
        gtk_message_dialog_new(parent, GTK_DIALOG_DESTROY_WITH_PARENT,
                               kind, GTK_BUTTONS_OK, "%s", msg);
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    g_signal_connect(dialog, "response",
                     G_CALLBACK(lr_dialog_destroy_on_response), NULL);
    lr_dialog_center_on(dialog, parent);
}

void
lr_backup_current_file(GtkWindow *parent, const char *path)
{
    gchar *dir, *base, *ts, *dest = NULL;
    GError *err = NULL;
    GFile *src = NULL, *dst = NULL;
    guint suffix = 0;
    gboolean ok;

    if (path == NULL || *path == '\0' || g_file_test(path, G_FILE_TEST_IS_DIR))
    {
        show_simple_dialog(parent, GTK_MESSAGE_WARNING, _("Backup"),
                           _("Select a file to back up first."));
        return;
    }

    dir = backups_dir();
    if (dir == NULL)
    {
        show_simple_dialog(parent, GTK_MESSAGE_ERROR, _("Backup"),
                           _("Unable to create the backups directory."));
        return;
    }

    base = g_path_get_basename(path);
    {
        GDateTime *now = g_date_time_new_now_local();
        ts = g_date_time_format(now, "%Y%m%d-%H%M%S");
        g_date_time_unref(now);
    }

    do
    {
        g_free(dest);
        if (suffix == 0)
            dest = g_strdup_printf("%s/%s.%s.bak", dir, base, ts);
        else
            dest = g_strdup_printf("%s/%s.%s-%u.bak", dir, base, ts, suffix);
        suffix++;
    } while (g_file_test(dest, G_FILE_TEST_EXISTS));

    g_free(dir);
    g_free(ts);

    src = g_file_new_for_path(path);
    dst = g_file_new_for_path(dest);
    ok = g_file_copy(src, dst, G_FILE_COPY_NONE, NULL, NULL, NULL, &err);

    if (ok)
    {
        gchar *msg = g_strdup_printf(_("Backup saved to %s."), dest);
        show_simple_dialog(parent, GTK_MESSAGE_INFO, _("Backup"), msg);
        g_free(msg);
    }
    else
    {
        gchar *msg = g_strdup_printf(_("Backup failed: %s"),
                                     err != NULL ? err->message
                                                 : _("Unknown Error"));
        show_simple_dialog(parent, GTK_MESSAGE_ERROR, _("Backup"), msg);
        g_free(msg);
        g_clear_error(&err);
    }

    g_object_unref(src);
    g_object_unref(dst);
    g_free(dest);
    g_free(base);
}

/* 展示 diff 文本（等宽、可选、可滚动） */
static void
show_diff_dialog(GtkWindow *parent, const gchar *title, const gchar *text)
{
    GtkWidget *dialog, *content, *scrolled, *view;
    GtkTextBuffer *buffer;

    dialog = gtk_dialog_new_with_buttons(title, parent,
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         _("OK"), GTK_RESPONSE_CLOSE, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 760, 480);

    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    atk_object_set_name(gtk_widget_get_accessible(view), _("Diff output"));

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_buffer_set_text(buffer, text != NULL ? text : "", -1);
    gtk_container_add(GTK_CONTAINER(scrolled), view);
    gtk_box_pack_start(GTK_BOX(content), scrolled, TRUE, TRUE, 0);

    g_signal_connect(dialog, "response",
                     G_CALLBACK(lr_dialog_destroy_on_response), NULL);
    gtk_widget_show_all(dialog);
    lr_dialog_center_on(dialog, parent);
}

/* diff -u 逐行对比；diff 不可用时退化为相同性检查 */
static gboolean
run_diff(const gchar *a, const gchar *b, gchar **out, gchar **err_out,
         gint *status)
{
    gchar *argv[] = {(gchar *)"diff", (gchar *)"-u", (gchar *)a,
                     (gchar *)b, NULL};
    gchar *stdout_text = NULL, *stderr_text = NULL;
    gint exit_status = 0;
    GError *error = NULL;
    gboolean spawned;

    spawned = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                           NULL, NULL, &stdout_text, &stderr_text,
                           &exit_status, &error);
    if (!spawned)
    {
        g_clear_error(&error);
        *out = NULL;
        *err_out = NULL;
        *status = -1;
        return FALSE;
    }

    *out = stdout_text;
    *err_out = stderr_text;
    *status = exit_status;
    return TRUE;
}

void
lr_compare_with_file(GtkWindow *parent, const char *path)
{
    GtkWidget *chooser;
    gchar *other = NULL;
    gchar *diff_out = NULL, *diff_err = NULL;
    gint diff_status = 0;
    gboolean ok;

    if (path == NULL || *path == '\0' || g_file_test(path, G_FILE_TEST_IS_DIR))
    {
        show_simple_dialog(parent, GTK_MESSAGE_WARNING, _("Compare"),
                           _("Select a file to compare first."));
        return;
    }

    chooser = gtk_file_chooser_dialog_new(
        _("Choose file to compare with..."), parent,
        GTK_FILE_CHOOSER_ACTION_OPEN,
        _("Cancel"), GTK_RESPONSE_CANCEL,
        _("Compare"), GTK_RESPONSE_ACCEPT, NULL);
    if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(chooser), path);

    if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT)
        other = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
    gtk_widget_destroy(chooser);
    if (other == NULL)
        return;

    if (g_strcmp0(path, other) == 0)
    {
        show_simple_dialog(parent, GTK_MESSAGE_INFO, _("Compare"),
                           _("The two files are the same."));
        g_free(other);
        return;
    }

    ok = run_diff(path, other, &diff_out, &diff_err, &diff_status);
    if (!ok)
    {
        /* diff 不可用：退化为相同性检查 */
        gchar *a = NULL, *b = NULL;
        gsize la = 0, lb = 0;
        gboolean same = g_file_get_contents(path, &a, &la, NULL) &&
                        g_file_get_contents(other, &b, &lb, NULL) &&
                        la == lb && memcmp(a, b, la) == 0;
        g_free(a);
        g_free(b);
        show_simple_dialog(parent, GTK_MESSAGE_INFO, _("Compare"),
                           same ? _("Files are identical.")
                                : _("Files differ (diff(1) unavailable)."));
    }
    else if (diff_status == 2 && diff_err != NULL && *diff_err != '\0')
    {
        show_simple_dialog(parent, GTK_MESSAGE_ERROR, _("Compare"),
                           diff_err);
    }
    else if (diff_out != NULL && *diff_out != '\0')
    {
        gchar *base_a = g_path_get_basename(path);
        gchar *base_b = g_path_get_basename(other);
        gchar *title = g_strdup_printf(_("Compare: %s ↔ %s"),
                                       base_a, base_b);
        show_diff_dialog(parent, title, diff_out);
        g_free(base_a);
        g_free(base_b);
        g_free(title);
    }
    else
    {
        show_simple_dialog(parent, GTK_MESSAGE_INFO, _("Compare"),
                           _("Files are identical."));
    }

    g_free(diff_out);
    g_free(diff_err);
    g_free(other);
}
