/* 收藏夹：数据存 /run 临时目录，重启清空 */
#include "ui/favorites.h"
#include "ui/dialog_utils.h"

#include <glib/gstdio.h>
#include <glib/gi18n.h>

/* 收藏夹数据目录：$XDG_RUNTIME_DIR/linux-regedit/favorites（/run/user/<uid>） */
static char *
favorites_dir(void)
{
    const char *rt = g_get_user_runtime_dir();
    if (rt == NULL)
        rt = "/tmp";
    return g_build_filename(rt, "linux-regedit", "favorites", NULL);
}

/* 收藏夹名称转安全文件名（替换 / 等） */
static char *
favorite_file_name(const char *name)
{
    return g_strdelimit(g_strdup(name), "/", '_');
}

/* 返回收藏夹名称列表（排序），需 g_list_free_full(list, g_free) */
static GList *
favorites_list(void)
{
    GList *list = NULL;
    gchar *dir = favorites_dir();
    GDir *gd = g_dir_open(dir, 0, NULL);
    const char *name;

    if (gd == NULL)
    {
        g_free(dir);
        return NULL;
    }
    while ((name = g_dir_read_name(gd)) != NULL)
        list = g_list_append(list, g_strdup(name));
    g_dir_close(gd);
    g_free(dir);
    return g_list_sort(list, (GCompareFunc)g_utf8_collate);
}

static gboolean
favorites_add(const char *name, const char *path)
{
    gchar *dir = favorites_dir();
    gchar *file = g_build_filename(dir, favorite_file_name(name), NULL);
    gboolean ok;

    g_mkdir_with_parents(dir, 0700);
    ok = g_file_set_contents(file, path, -1, NULL);
    g_free(file);
    g_free(dir);
    return ok;
}

static gboolean
favorites_remove(const char *name)
{
    gchar *dir = favorites_dir();
    gchar *file = g_build_filename(dir, favorite_file_name(name), NULL);
    gboolean ok = (g_remove(file) == 0);
    g_free(file);
    g_free(dir);
    return ok;
}

/* 读取收藏夹对应路径（新分配，调用者 g_free） */
static char *
favorites_path(const char *name)
{
    gchar *dir = favorites_dir();
    gchar *file = g_build_filename(dir, favorite_file_name(name), NULL);
    gchar *path = NULL;

    g_file_get_contents(file, &path, NULL, NULL);
    g_free(file);
    g_free(dir);
    return path;
}

/* 点击收藏夹项：跳转到收藏路径 */
static void
on_favorite_activate(GtkMenuItem *item, gpointer user_data)
{
    LrMainWindow *mw = user_data;
    const gchar *name = gtk_menu_item_get_label(item);
    gchar *path;

    if (name == NULL)
        return;
    path = favorites_path(name);
    if (path != NULL)
    {
        /* 打开并定位到树中对应节点 */
        lr_main_window_open_file(mw, path);
        g_free(path);
    }
}

/* 添加到收藏夹对话框 */
typedef struct
{
    LrMainWindow *mw;
    GtkWidget *entry;
} AddFavoriteCtx;

static void
on_add_fav_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    AddFavoriteCtx *ctx = user_data;

    if (response_id == GTK_RESPONSE_ACCEPT)
    {
        const gchar *name = gtk_entry_get_text(GTK_ENTRY(ctx->entry));
        if (name != NULL && *name != '\0')
            favorites_add(name, ctx->mw->current_path);
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
    g_free(ctx);
}

static void
on_add_favorite(GtkWidget *widget, gpointer user_data)
{
    LrMainWindow *mw = user_data;
    AddFavoriteCtx *ctx;
    GtkWidget *dialog, *box, *label, *entry, *area;
    gchar *base;

    (void)widget;
    if (mw->current_path == NULL || *mw->current_path == '\0')
        return;

    dialog = gtk_dialog_new_with_buttons(_("Add to Favourites"), NULL, 0, _("Cancel"),
                                         GTK_RESPONSE_CANCEL, _("OK"),
                                         GTK_RESPONSE_ACCEPT, NULL);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);

    label = gtk_label_new_with_mnemonic(_("Group Name (_F): "));
    entry = gtk_entry_new();
    base = g_path_get_basename(mw->current_path);
    gtk_entry_set_text(GTK_ENTRY(entry), base);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), entry);
    gtk_widget_set_halign(label, GTK_ALIGN_START);

    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);

    area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_box_pack_start(GTK_BOX(area), box, TRUE, TRUE, 0);

    ctx = g_new0(AddFavoriteCtx, 1);
    ctx->mw = mw;
    ctx->entry = entry;
    g_signal_connect(dialog, "response", G_CALLBACK(on_add_fav_response), ctx);

    lr_dialog_center_on(dialog, GTK_WINDOW(mw->window));
    g_free(base);
}

/* 删除收藏夹对话框 */
static void
on_remove_fav_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    GtkWidget *combo = user_data;

    if (response_id == GTK_RESPONSE_ACCEPT)
    {
        gchar *name =
            gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
        if (name != NULL)
        {
            favorites_remove(name);
            g_free(name);
        }
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void
on_remove_favorite(GtkWidget *widget, gpointer user_data)
{
    LrMainWindow *mw = user_data;
    GtkWidget *dialog, *box, *label, *combo, *area;
    GList *favs;

    (void)widget;
    favs = favorites_list();
    if (favs == NULL)
    {
        GtkWidget *d = gtk_message_dialog_new(NULL, 0, GTK_MESSAGE_INFO,
                                              GTK_BUTTONS_OK, _("There is no group yet."));
        g_signal_connect(d, "response",
                         G_CALLBACK(lr_dialog_destroy_on_response), NULL);
        lr_dialog_center_on(d, GTK_WINDOW(mw->window));
        return;
    }

    dialog = gtk_dialog_new_with_buttons(_("Remove Group"), NULL, 0, _("Cancel"),
                                         GTK_RESPONSE_CANCEL, _("OK"),
                                         GTK_RESPONSE_ACCEPT, NULL);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);
    label = gtk_label_new(_("Select Grroup: "));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    combo = gtk_combo_box_text_new();
    {
        GList *l;
        for (l = favs; l != NULL; l = l->next)
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo),
                                           (const gchar *)l->data);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), combo, FALSE, FALSE, 0);

    area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_box_pack_start(GTK_BOX(area), box, TRUE, TRUE, 0);

    g_signal_connect(dialog, "response", G_CALLBACK(on_remove_fav_response),
                     combo);
    lr_dialog_center_on(dialog, GTK_WINDOW(mw->window));
    g_list_free_full(favs, g_free);
}

/* 收藏夹菜单显示时重建：添加/删除 + 分割线 + 收藏项 */
void lr_favorites_fill_menu(GtkWidget *menu, gpointer user_data)
{
    LrMainWindow *mw = user_data;
    GList *children = gtk_container_get_children(GTK_CONTAINER(menu));
    GList *l;

    for (l = children; l != NULL; l = l->next)
        gtk_container_remove(GTK_CONTAINER(menu), GTK_WIDGET(l->data));
    g_list_free(children);

    GtkWidget *item = gtk_menu_item_new_with_label(_("Add to Group"));
    g_signal_connect(item, "activate", G_CALLBACK(on_add_favorite), mw);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label(_("Delete Group"));
    g_signal_connect(item, "activate", G_CALLBACK(on_remove_favorite), mw);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    GList *favs = favorites_list();
    if (favs != NULL)
    {
        gtk_menu_shell_append(GTK_MENU_SHELL(menu),
                              gtk_separator_menu_item_new());
        for (l = favs; l != NULL; l = l->next)
        {
            item = gtk_menu_item_new_with_label((const gchar *)l->data);
            g_signal_connect(item, "activate", G_CALLBACK(on_favorite_activate),
                             mw);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        }
        g_list_free_full(favs, g_free);
    }
    gtk_widget_show_all(menu);
}
