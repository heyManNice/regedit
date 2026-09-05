/* 导出为 .lreg：递归打包 /etc、~/.config、/boot 或当前选中路径 */
#include "ui/export.h"
#include "ui/dialog_utils.h"
#include "ui/test_roots.h"
#include "core/limits.h"

#include <glib/gstdio.h>
#include <glib/gi18n.h>

static void
export_file_into(GString *out, const char *path)
{
    gchar *content = NULL;
    gsize len = 0;

    if (!g_file_get_contents(path, &content, &len, NULL))
        return;
    if (len > LR_MAX_FILE_SIZE || !g_utf8_validate(content, len, NULL))
    {
        g_free(content);
        return;
    }
    g_string_append_printf(out, _("Files: %s\n"), path);
    g_string_append(out, content);
    if (len == 0 || content[len - 1] != '\n')
        g_string_append_c(out, '\n');
    g_string_append_c(out, '\n');
    g_free(content);
}

static void
export_path_into(GString *out, const char *path, GHashTable *visited)
{
    if (g_file_test(path, G_FILE_TEST_IS_DIR))
    {
        GDir *gd = g_dir_open(path, 0, NULL);
        const char *name;

        /* 目录去重：防止符号链接环（同一 dev:inode 只导出一次） */
        {
            GStatBuf st;
            gchar *key;

            if (g_stat(path, &st) != 0)
                return;
            key = g_strdup_printf("%ld:%ld", (long)st.st_dev,
                                  (long)st.st_ino);
            if (g_hash_table_contains(visited, key))
            {
                g_free(key);
                return;
            }
            g_hash_table_add(visited, key);
        }

        if (gd == NULL)
            return;
        g_string_append_printf(out, _("Folders: %s\n"), path);
        while ((name = g_dir_read_name(gd)) != NULL)
        {
            gchar *full = g_build_filename(path, name, NULL);
            export_path_into(out, full, visited);
            g_free(full);
        }
        g_dir_close(gd);
    }
    else
    {
        export_file_into(out, path);
    }
}

static gboolean
do_export(LrMainWindow *mw, gboolean all, const char *dest, GError **err)
{
    GString *out = g_string_new("Linux Registry Export Version 1.0\n");
    GHashTable *visited = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                g_free, NULL);
    GDateTime *now = g_date_time_new_now_local();
    gchar *ts = g_date_time_format(now, "%Y-%m-%d %H:%M:%S");
    g_string_append_printf(out, _("Generated At: %s\n\n"), ts);
    g_free(ts);
    g_date_time_unref(now);

    if (all)
    {
        export_path_into(out, lr_etc_root(), visited);
        export_path_into(out, lr_config_root(), visited);
        export_path_into(out, lr_boot_root(), visited);
    }
    else
    {
        if (mw->current_path == NULL || *mw->current_path == '\0')
        {
            g_set_error_literal(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                                _("No path selected."));
            g_string_free(out, TRUE);
            g_hash_table_unref(visited);
            return FALSE;
        }
        export_path_into(out, mw->current_path, visited);
    }

    gboolean ok = g_file_set_contents(dest, out->str, (gssize)out->len, err);
    g_string_free(out, TRUE);
    g_hash_table_unref(visited);
    return ok;
}

typedef struct
{
    LrMainWindow *mw;
    GtkWidget *radio_all;
    GtkWidget *entry; /* 保存路径输入框 */
} ExportCtx;

/* 浏览…：打开保存对话框选择导出位置 */
static void
on_export_browse(GtkWidget *button, gpointer user_data)
{
    ExportCtx *ctx = user_data;
    GtkWidget *dialog;
    gint resp;
    (void)button;

    dialog = gtk_file_chooser_dialog_new(
        _("Export to..."), NULL, GTK_FILE_CHOOSER_ACTION_SAVE, _("Cancel"),
        GTK_RESPONSE_CANCEL, _("Save"), GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),
                                      "linux-regedit.lreg");

    resp = gtk_dialog_run(GTK_DIALOG(dialog));
    if (resp == GTK_RESPONSE_ACCEPT)
    {
        gchar *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (path != NULL)
        {
            gtk_entry_set_text(GTK_ENTRY(ctx->entry), path);
            g_free(path);
        }
    }
    gtk_widget_destroy(dialog);
}

static void
on_export_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    ExportCtx *ctx = user_data;
    gboolean all;
    gchar *dest = NULL;
    GError *err = NULL;

    if (response_id != GTK_RESPONSE_ACCEPT)
    {
        gtk_widget_destroy(GTK_WIDGET(dialog));
        g_free(ctx);
        return;
    }

    all = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ctx->radio_all));
    dest = g_strdup(gtk_entry_get_text(GTK_ENTRY(ctx->entry)));

    if (dest == NULL || *dest == '\0')
    {
        GtkWidget *d = gtk_message_dialog_new(
            NULL, 0, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, _("Please enter the save location."));
        g_signal_connect(d, "response",
                         G_CALLBACK(lr_dialog_destroy_on_response), NULL);
        lr_dialog_center_on(d, GTK_WINDOW(ctx->mw->window));
    }
    else if (do_export(ctx->mw, all, dest, &err))
    {
        GtkWidget *d = gtk_message_dialog_new(
            NULL, 0, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, _("Export finished: %s"), dest);
        g_signal_connect(d, "response",
                         G_CALLBACK(lr_dialog_destroy_on_response), NULL);
        lr_dialog_center_on(d, GTK_WINDOW(ctx->mw->window));
    }
    else
    {
        GtkWidget *d = gtk_message_dialog_new(
            NULL, 0, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, _("Export failed: %s"),
            err != NULL ? err->message : _("Unknown Error"));
        g_signal_connect(d, "response",
                         G_CALLBACK(lr_dialog_destroy_on_response), NULL);
        lr_dialog_center_on(d, GTK_WINDOW(ctx->mw->window));
        g_clear_error(&err);
    }
    g_free(dest);
    gtk_widget_destroy(GTK_WIDGET(dialog));
    g_free(ctx);
}

/* 显示导出对话框（文件→导出…） */
void lr_export_show_dialog(GtkWidget *widget, gpointer user_data)
{
    LrMainWindow *mw = user_data;
    ExportCtx *ctx;
    GtkWidget *dialog, *box, *label, *r1, *r2, *area;
    GtkWidget *hbox, *entry, *browse;

    (void)widget;
    dialog = gtk_dialog_new_with_buttons(_("Export"), NULL, 0, _("Cancel"),
                                         GTK_RESPONSE_CANCEL, _("OK"),
                                         GTK_RESPONSE_ACCEPT, NULL);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);

    label = gtk_label_new(_("Export Range: "));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    r1 = gtk_radio_button_new_with_label(NULL,
                                         _("All (/etc, ~/.config, /boot)"));
    r2 = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(r1),
                                                     _("Selected"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(r1), TRUE);

    /* 保存路径：输入框 + 浏览按钮（GtkFileChooserButton 不支持 SAVE） */
    entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), "linux-regedit.lreg");
    browse = gtk_button_new_with_label(_("Browse..."));

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), browse, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), r1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), r2, FALSE, FALSE, 0);
    gtk_box_pack_start(
        GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE,
        FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new(_("Save to: ")), FALSE, FALSE,
                       0);
    gtk_box_pack_start(GTK_BOX(box), hbox, FALSE, FALSE, 0);

    area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_box_pack_start(GTK_BOX(area), box, TRUE, TRUE, 0);

    ctx = g_new0(ExportCtx, 1);
    ctx->mw = mw;
    ctx->radio_all = r1;
    ctx->entry = entry;
    g_signal_connect(browse, "clicked", G_CALLBACK(on_export_browse), ctx);
    g_signal_connect(dialog, "response", G_CALLBACK(on_export_response), ctx);
    lr_dialog_center_on(dialog, GTK_WINDOW(mw->window));
}
