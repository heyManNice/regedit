/*
 * linux-regedit — 程序入口
 *
 * Linux 版 regedit：以注册表编辑器的交互浏览 /etc 与 ~/.config 的配置文件。
 */
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <libintl.h>
#include <locale.h>
#include "app.h"

int main(int argc, char **argv)
{
    /* 本地化：界面语言跟随系统（gettext） */
    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
    textdomain(GETTEXT_PACKAGE);

    GtkApplication *app;
    gint status;

    app = gtk_application_new("org.linux-regedit.app",
                              G_APPLICATION_HANDLES_OPEN);
    g_signal_connect(app, "activate", G_CALLBACK(lr_app_activate), NULL);
    g_signal_connect(app, "open", G_CALLBACK(lr_app_open), NULL);

    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
