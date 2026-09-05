#include "ui/dialog_utils.h"

#include <glib/gi18n.h>

/* 独立子窗口：显示并把位置定到主窗口中心（之后可自由拖动，不与主窗口联动） */
void lr_dialog_center_on(GtkWidget *dialog, GtkWindow *parent)
{
    gint px, py, pw, ph, dw, dh;

    gtk_window_get_position(parent, &px, &py);
    gtk_window_get_size(parent, &pw, &ph);
    gtk_widget_show_all(dialog);
    gtk_window_get_size(GTK_WINDOW(dialog), &dw, &dh);
    gtk_window_move(GTK_WINDOW(dialog),
                    MAX(px + (pw - dw) / 2, 0),
                    MAX(py + (ph - dh) / 2, 0));
}

/* 对话框任何响应（含关闭）都销毁自身 */
void lr_dialog_destroy_on_response(GtkDialog *dialog, gint response_id,
                                   gpointer user_data)
{
    (void)response_id;
    (void)user_data;
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

/* 尚未实现功能：弹出提示（独立子窗口） */
void lr_dialog_not_impl(GtkWidget *toplevel, const char *label)
{
    GtkWidget *dialog;

    dialog = gtk_message_dialog_new(NULL, 0, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                                    _("%s is not implemented yet."), label);
    g_signal_connect(dialog, "response",
                     G_CALLBACK(lr_dialog_destroy_on_response), NULL);
    lr_dialog_center_on(dialog, GTK_WINDOW(toplevel));
}
