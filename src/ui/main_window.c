#include "ui/main_window.h"
#include "ui/window_state.h"
#include "ui/favorites.h"
#include "ui/export.h"
#include "ui/dialog_utils.h"
#include "core/format.h"

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gi18n.h>
#include <string.h>
#include <sys/utsname.h>

static void
open_path(LrMainWindow *mw, const char *path, gboolean is_dir)
{
    /* 计算机虚拟根（空路径） */
    if (path == NULL || *path == '\0')
    {
        gtk_entry_set_text(GTK_ENTRY(mw->location_entry), _("Computer"));
        lr_value_pane_clear(mw->value);
        g_free(mw->current_path);
        mw->current_path = NULL;
        lr_window_state_set_path(mw->win_state, NULL);
        return;
    }

    gtk_entry_set_text(GTK_ENTRY(mw->location_entry), path);

    g_free(mw->current_path);
    mw->current_path = g_strdup(path);
    lr_window_state_set_path(mw->win_state, path);

    if (is_dir)
        lr_value_pane_clear(mw->value);
    else
        lr_value_pane_load_file(mw->value, path);
}

static void
on_tree_select(const char *path, gboolean is_dir, gpointer user_data)
{
    LrMainWindow *mw = user_data;
    open_path(mw, path, is_dir);
}

/* 命令行 / 外部调用：直接打开指定文件（目录则仅定位到树中） */
void lr_main_window_open_file(LrMainWindow *mw, const char *path)
{
    gboolean is_dir = g_file_test(path, G_FILE_TEST_IS_DIR);
    open_path(mw, path, is_dir);
    lr_tree_pane_reveal_path(mw->tree, path);
}

static void
on_refresh(GtkWidget *widget, gpointer user_data)
{
    LrMainWindow *mw = user_data;
    (void)widget;
    lr_tree_pane_refresh(mw->tree);
}

/* 查看→地址栏：勾选切换地址栏显示/隐藏 */
static void
on_toggle_location_bar(GtkCheckMenuItem *item, gpointer user_data)
{
    LrMainWindow *mw = user_data;
    gboolean active = gtk_check_menu_item_get_active(item);
    if (mw->location_bar != NULL)
        gtk_widget_set_visible(mw->location_bar, active);
}

/* 编辑→复制项名称：把当前路径复制到剪贴板 */
static void
on_copy_item_name(GtkWidget *widget, gpointer user_data)
{
    LrMainWindow *mw = user_data;
    GtkClipboard *clip;
    (void)widget;

    if (mw->current_path == NULL)
        return;
    clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clip, mw->current_path, -1);
}

static void
on_quit(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *window = user_data;
    (void)widget;
    gtk_widget_destroy(window);
}

/* ========== 关于对话框（模仿 regedit：图标 + 系统名 + 系统信息） ========== */

/* 加载系统图标：优先发行版 logo，其次企鹅 Tux，最后通用计算机图标 */
static GdkPixbuf *
load_about_icon(gint size)
{
    GtkIconTheme *theme = gtk_icon_theme_get_default();
    static const gchar *const names[] = {"distributor-logo", "tux",
                                         "computer", NULL};
    gint i;

    for (i = 0; names[i] != NULL; i++)
    {
        GError *error = NULL;
        GdkPixbuf *pb =
            gtk_icon_theme_load_icon(theme, names[i], size, 0, &error);
        if (pb != NULL)
            return pb;
        g_clear_error(&error);
    }
    return NULL;
}

/* 读取 /etc/os-release 的键值（如 PRETTY_NAME），返回新分配字符串 */
static char *
os_release_value(const char *key)
{
    gchar *content = NULL;
    gchar **lines = NULL;
    gchar *prefix = g_strdup_printf("%s=", key);
    char *out = NULL;
    guint i;

    if (!g_file_get_contents("/etc/os-release", &content, NULL, NULL))
        goto done;

    lines = g_strsplit(content, "\n", -1);
    for (i = 0; lines[i] != NULL; i++)
    {
        if (g_str_has_prefix(lines[i], prefix))
        {
            const char *v = lines[i] + strlen(prefix);
            gchar *dup = g_strdup(v);
            gchar *s = g_strstrip(dup);
            gsize n = strlen(s);
            if (n >= 2 && s[0] == '"' && s[n - 1] == '"')
            {
                s[n - 1] = '\0';
                out = g_strdup(s + 1);
            }
            else
            {
                out = g_strdup(s);
            }
            g_free(dup);
            break;
        }
    }

done:
    g_strfreev(lines);
    g_free(content);
    g_free(prefix);
    return out;
}

/* 关于对话框右侧多行文本行 */
static void
about_add_line(GtkWidget *right, const char *text)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_box_pack_start(GTK_BOX(right), label, FALSE, FALSE, 0);
}

static void
on_about(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *window = user_data;
    GtkWidget *dialog, *content, *vbox, *hbox, *img, *label, *left, *right;
    GdkPixbuf *icon;
    gchar *name, *pretty, *version, *kernel, *init_prog, *session, *user, *tmp;
    struct utsname uts;
    gboolean have_uts;
    gint k;

    (void)widget;

    dialog = gtk_dialog_new_with_buttons(_("About Regedit"), NULL, 0, _("OK"),
                                         GTK_RESPONSE_CLOSE, NULL);
    /* 关于窗口宽度设窄一些 */
    gtk_window_set_default_size(GTK_WINDOW(dialog), 760, -1);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 20);
    gtk_box_pack_start(GTK_BOX(content), vbox, TRUE, TRUE, 0);

    /* 系统信息 */
    have_uts = (uname(&uts) == 0);
    name = os_release_value("NAME");
    pretty = os_release_value("PRETTY_NAME");
    if (pretty == NULL)
        pretty = g_strdup_printf("%s %s",
                                 have_uts ? uts.sysname : "Linux",
                                 have_uts ? uts.release : "");
    version = os_release_value("VERSION");
    if (version == NULL)
        version = g_strdup(_("Unknown"));
    kernel = g_strdup(have_uts ? uts.release : _("Unknown"));
    init_prog = g_strdup(_("Unknown"));
    if (g_file_get_contents("/proc/1/comm", &tmp, NULL, NULL))
    {
        gchar *s = g_strstrip(tmp);
        g_free(init_prog);
        init_prog = g_strdup(s);
        g_free(tmp);
        tmp = NULL;
    }
    session = g_strdup(g_getenv("XDG_SESSION_TYPE"));
    if (session == NULL || *session == '\0')
        session = g_strdup(g_getenv("WAYLAND_DISPLAY") != NULL ? "wayland"
                           : g_getenv("DISPLAY") != NULL       ? "x11"
                                                               : _("Unknown"));
    user = g_strdup(g_get_user_name());

    /* 顶部：放大 3 倍的系统图标 + 系统名（无“注册表编辑器”字样） */
    icon = load_about_icon(48 * 3);
    img = gtk_image_new_from_pixbuf(icon);
    if (icon != NULL)
        g_object_unref(icon);

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_widget_set_halign(hbox, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(hbox), img, FALSE, FALSE, 0);

    label = gtk_label_new(NULL);
    tmp = g_strdup_printf("<span size=\"36pt\" weight=\"bold\">%s</span>",
                          pretty);
    gtk_label_set_markup(GTK_LABEL(label), tmp);
    g_free(tmp);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    /* 水平分隔线 */
    gtk_box_pack_start(GTK_BOX(vbox),
                       gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE,
                       FALSE, 16);

    /* 左右视图：左 1 倍系统 logo，右多行文本 */
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);

    left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    icon = load_about_icon(48);
    img = gtk_image_new_from_pixbuf(icon);
    if (icon != NULL)
        g_object_unref(icon);
    gtk_box_pack_start(GTK_BOX(left), img, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), left, FALSE, FALSE, 0);

    right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(hbox), right, TRUE, TRUE, 0);

    /* 行1：GNU/Linux + 系统名（无版本） */
    tmp = g_strdup_printf("GNU/Linux %s", name != NULL ? name : "Linux");
    about_add_line(right, tmp);
    g_free(tmp);

    /* 行2：版本 + 系统信息（内核/init/图形服务器）；去掉版本代号括号 */
    {
        gchar *paren = strstr(version, " (");
        if (paren != NULL)
            *paren = '\0';
    }
    tmp = g_strdup_printf(_("Version %s (Linux %s, %s, %s)"),
                          version, kernel, init_prog, session);
    about_add_line(right, tmp);
    g_free(tmp);

    /* 行3：版权符号 + 文案 */
    about_add_line(right, _("© heyManNice All rights reversed."));

    /* 空一行 */
    gtk_box_pack_start(GTK_BOX(right), gtk_label_new(""), FALSE, FALSE, 0);

    /* 开源声明（多行） */
    label = gtk_label_new(
        _("This GNU/Linux operating system and its components are built upon Free and Open-Source Software (FOSS) and are distributed by their respective copyright holders under the terms of open source licenses, including GPL, LGPL, MIT, and Apache licenses. The kernel, tools, and user interface contained in the system are protected under the applicable open source licenses and copyright laws."));
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
    gtk_box_pack_start(GTK_BOX(right), label, FALSE, FALSE, 0);

    /* 空四行 */
    for (k = 0; k < 4; k++)
        gtk_box_pack_start(GTK_BOX(right), gtk_label_new(""), FALSE, FALSE,
                           0);

    /* 许可行 */
    about_add_line(right, _("In accordance with open source licenses, the following users are allowed to use it"));

    /* 空一行 */
    gtk_box_pack_start(GTK_BOX(right), gtk_label_new(""), FALSE, FALSE, 0);

    /* 缩进 2 字符 + 当前用户名 */
    tmp = g_strdup_printf("\u3000\u3000%s", user);
    about_add_line(right, tmp);
    g_free(tmp);

    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    /* 确定按钮与窗口边界留边距，按钮内部左右加内边距 */
    {
        static GtkCssProvider *ok_css = NULL;
        GtkWidget *ok = gtk_dialog_get_widget_for_response(
            GTK_DIALOG(dialog), GTK_RESPONSE_CLOSE);

        if (ok != NULL)
        {
            gtk_widget_set_margin_start(ok, 12);
            gtk_widget_set_margin_end(ok, 12);
            gtk_widget_set_margin_bottom(ok, 12);
        }

        if (ok_css == NULL)
        {
            ok_css = gtk_css_provider_new();
            gtk_css_provider_load_from_data(
                ok_css,
                "button { padding-left: 36px; padding-right: 36px; }", -1,
                NULL);
        }
        if (ok != NULL)
            gtk_style_context_add_provider(
                gtk_widget_get_style_context(ok),
                GTK_STYLE_PROVIDER(ok_css),
                GTK_STYLE_PROVIDER_PRIORITY_USER);
    }

    /* 清理 */
    g_free(name);
    g_free(pretty);
    g_free(version);
    g_free(kernel);
    g_free(init_prog);
    g_free(session);
    g_free(user);

    g_signal_connect(dialog, "response",
                     G_CALLBACK(lr_dialog_destroy_on_response), NULL);
    lr_dialog_center_on(dialog, GTK_WINDOW(window));
}

/* 未实现功能：复用通用提示框 */
static void
on_new_not_impl(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    lr_dialog_not_impl(gtk_widget_get_toplevel(GTK_WIDGET(item)), user_data);
}

/* 编辑菜单「新建配置项」：在当前表格追加一行（仅内存） */
static void
on_new_value_item(GtkMenuItem *item, gpointer user_data)
{
    LrMainWindow *mw = user_data;
    lr_value_pane_add_value(mw->value, gtk_menu_item_get_label(item));
}

/* 新建 → 子菜单（编辑菜单，更全面）：文件夹 + 文件格式 + 分割线 + 配置项 */
static void
build_new_submenu(LrMainWindow *mw, GtkWidget *menu)
{
    const char *const *names;
    GtkWidget *item;
    gint i;

    item = gtk_menu_item_new_with_label(_("Folder"));
    g_signal_connect(item, "activate", G_CALLBACK(on_new_not_impl),
                     (gpointer) _("New Folder"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    names = lr_format_new_file_names();
    for (i = 0; names[i] != NULL; i++)
    {
        item = gtk_menu_item_new_with_label(_(names[i]));
        g_signal_connect(item, "activate", G_CALLBACK(on_new_not_impl),
                         (gpointer)names[i]);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }

    item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    /* 配置项：追加到当前表格（仅内存） */
    {
        const char *const *types = lr_value_type_names();
        for (i = 0; types[i] != NULL; i++)
        {
            item = gtk_menu_item_new_with_label(types[i]);
            g_signal_connect(item, "activate", G_CALLBACK(on_new_value_item),
                             mw);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        }
    }
}

/* ---------- 查找对话框（Edit → Find… / Ctrl+F / F3） ---------- */

typedef struct
{
    LrMainWindow *mw;
    GtkWidget *entry;
    GtkWidget *status;
    gchar *last_needle; /* 上次执行查找的词，用于区分 first/next */
} FindCtx;

static void
find_destroy(FindCtx *ctx)
{
    if (ctx == NULL)
        return;
    g_free(ctx->last_needle);
    g_free(ctx);
}

/* 执行一次查找：词未变且存在可继续状态时查找下一处，否则从头查找 */
static gboolean
find_do_next(FindCtx *ctx)
{
    LrMainWindow *mw = ctx->mw;
    gchar *needle;
    gboolean ok;
    guint total = 0;

    needle = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(ctx->entry))));
    if (*needle == '\0')
    {
        g_free(needle);
        gtk_label_set_text(GTK_LABEL(ctx->status),
                           _("Type a search term."));
        return FALSE;
    }

    if (lr_value_pane_search_has_query(mw->value) &&
        ctx->last_needle != NULL &&
        g_strcmp0(ctx->last_needle, needle) == 0)
    {
        ok = lr_value_pane_search_next(mw->value);
        if (!ok)
            gtk_label_set_text(GTK_LABEL(ctx->status),
                               _("No matches found."));
    }
    else
    {
        g_free(ctx->last_needle);
        ctx->last_needle = g_strdup(needle);
        ok = lr_value_pane_search_first(mw->value, needle, &total);
        if (!ok)
        {
            gtk_label_set_text(GTK_LABEL(ctx->status),
                               _("No matches found."));
        }
        else
        {
            gchar *msg = g_strdup_printf(_("Found %u match(es)."), total);
            gtk_label_set_text(GTK_LABEL(ctx->status), msg);
            g_free(msg);
        }
    }

    g_free(needle);
    return ok;
}

static void
on_find_dialog_response(GtkDialog *dialog, gint response_id,
                        gpointer user_data)
{
    FindCtx *ctx = user_data;

    if (response_id == GTK_RESPONSE_ACCEPT)
    {
        find_do_next(ctx);
        gtk_widget_grab_focus(ctx->entry);
        return;
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void
on_find_dialog_destroy(GtkWidget *dialog, gpointer user_data)
{
    LrMainWindow *mw = user_data;
    FindCtx *ctx = g_object_get_data(G_OBJECT(dialog), "lr-find-ctx");

    if (mw != NULL && mw->find_dialog == dialog)
        mw->find_dialog = NULL;
    find_destroy(ctx);
}

static void
open_find_dialog(LrMainWindow *mw)
{
    GtkWidget *dialog, *content, *hbox, *label, *status;
    GtkWidget *entry;
    FindCtx *ctx;

    if (mw->find_dialog != NULL)
    {
        gtk_window_present(GTK_WINDOW(mw->find_dialog));
        return;
    }

    dialog = gtk_dialog_new_with_buttons(
        _("Find"), GTK_WINDOW(mw->window), GTK_DIALOG_DESTROY_WITH_PARENT,
        _("Cancel"), GTK_RESPONSE_CANCEL, _("Find Next"),
        GTK_RESPONSE_ACCEPT, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

    ctx = g_new0(FindCtx, 1);
    ctx->mw = mw;
    g_object_set_data(G_OBJECT(dialog), "lr-find-ctx", ctx);
    g_signal_connect(dialog, "response",
                     G_CALLBACK(on_find_dialog_response), ctx);
    g_signal_connect(dialog, "destroy",
                     G_CALLBACK(on_find_dialog_destroy), mw);

    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    label = gtk_label_new(_("Find what:"));
    entry = gtk_entry_new();
    atk_object_set_name(gtk_widget_get_accessible(entry), _("Find what"));
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);

    status = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(status), 0.0f);
    atk_object_set_name(gtk_widget_get_accessible(status), _("Find status"));

    gtk_box_pack_start(GTK_BOX(content), hbox, FALSE, FALSE, 8);
    gtk_box_pack_start(GTK_BOX(content), status, FALSE, FALSE, 4);
    gtk_widget_set_margin_start(hbox, 8);
    gtk_widget_set_margin_end(hbox, 8);

    ctx->entry = entry;
    ctx->status = status;
    mw->find_dialog = dialog;

    gtk_widget_show_all(dialog);
    lr_dialog_center_on(dialog, GTK_WINDOW(mw->window));
    gtk_widget_grab_focus(entry);
}

static void
on_find_activate(GtkWidget *widget, gpointer user_data)
{
    LrMainWindow *mw = user_data;
    (void)widget;
    open_find_dialog(mw);
}

static void
on_find_next_activate(GtkWidget *widget, gpointer user_data)
{
    LrMainWindow *mw = user_data;
    (void)widget;

    if (!lr_value_pane_search_next(mw->value))
        open_find_dialog(mw);
}

/* Ctrl+F 打开查找；F3 继续查找（无结果时打开对话框） */
static gboolean
on_window_key_press(GtkWidget *widget, GdkEventKey *event,
                    gpointer user_data)
{
    LrMainWindow *mw = user_data;
    (void)widget;

    if ((event->state & GDK_CONTROL_MASK) != 0 && event->keyval == GDK_KEY_f)
    {
        open_find_dialog(mw);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_F3)
    {
        if (!lr_value_pane_search_next(mw->value))
            open_find_dialog(mw);
        return TRUE;
    }
    return FALSE;
}

static GtkWidget *
build_menubar(LrMainWindow *mw)
{
    GtkWidget *menubar = gtk_menu_bar_new();
    GtkWidget *menu, *menu_item, *item;

    /* 文件：导入 / 导出 / 打印 / 退出 */
    menu_item = gtk_menu_item_new_with_label(_("File"));
    menu = gtk_menu_new();

    item = gtk_menu_item_new_with_label(_("Import..."));
    gtk_widget_set_sensitive(item, FALSE); /* 占位 */
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label(_("Export..."));
    g_signal_connect(item, "activate", G_CALLBACK(lr_export_show_dialog), mw);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label(_("Print"));
    gtk_widget_set_sensitive(item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label(_("Quit"));
    g_signal_connect(item, "activate", G_CALLBACK(on_quit), mw->window);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item), menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), menu_item);

    /* 编辑：新建 / 权限 / 删除 / 重命名 / 复制项名称 / 查找 / 查找下一个 */
    menu_item = gtk_menu_item_new_with_label(_("Edit"));
    menu = gtk_menu_new();

    item = gtk_menu_item_new_with_label(_("New"));
    {
        GtkWidget *new_sub = gtk_menu_new();
        build_new_submenu(mw, new_sub);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), new_sub);
    }
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label(_("Permission"));
    gtk_widget_set_sensitive(item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label(_("Delete"));
    gtk_widget_set_sensitive(item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label(_("Rename"));
    gtk_widget_set_sensitive(item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label(_("Copy Name"));
    g_signal_connect(item, "activate", G_CALLBACK(on_copy_item_name), mw);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label(_("Find..."));
    g_signal_connect(item, "activate", G_CALLBACK(on_find_activate), mw);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label(_("Find Next"));
    g_signal_connect(item, "activate", G_CALLBACK(on_find_next_activate), mw);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item), menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), menu_item);

    /* 查看：地址栏 / 拆分 / 刷新 / 字体 */
    menu_item = gtk_menu_item_new_with_label(_("View"));
    menu = gtk_menu_new();

    item = gtk_check_menu_item_new_with_label(_("Address"));
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), TRUE);
    g_signal_connect(item, "toggled", G_CALLBACK(on_toggle_location_bar), mw);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label(_("Split"));
    gtk_widget_set_sensitive(item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label(_("Refresh"));
    g_signal_connect(item, "activate", G_CALLBACK(on_refresh), mw);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label(_("Fonts"));
    gtk_widget_set_sensitive(item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item), menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), menu_item);

    /* 收藏夹：添加/删除 + 分割线 + 收藏项（菜单显示时动态刷新） */
    menu_item = gtk_menu_item_new_with_label(_("Favorites"));
    menu = gtk_menu_new();
    g_signal_connect(menu, "show", G_CALLBACK(lr_favorites_fill_menu), mw);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item), menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), menu_item);

    /* 帮助 */
    menu_item = gtk_menu_item_new_with_label(_("Help"));
    menu = gtk_menu_new();
    item = gtk_menu_item_new_with_label(_("About Regedit"));
    g_signal_connect(item, "activate", G_CALLBACK(on_about), mw->window);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item), menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), menu_item);

    return menubar;
}

static void
on_location_activate(GtkWidget *widget, gpointer user_data)
{
    LrMainWindow *mw = user_data;
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(widget));
    gchar *path;

    if (text == NULL)
        return;
    path = g_strstrip(g_strdup(text));
    if (*path == '\0')
    {
        g_free(path);
        return;
    }

    /* 优先在树中定位（成功则选择回调会同步右侧） */
    if (lr_tree_pane_reveal_path(mw->tree, path))
    {
        g_free(path);
        return;
    }

    gtk_entry_set_text(GTK_ENTRY(mw->location_entry), path);

    if (g_file_test(path, G_FILE_TEST_IS_DIR))
        lr_value_pane_clear(mw->value);
    else
        lr_value_pane_load_file(mw->value, path);
    g_free(path);
}

/* 通过 CSS 降低地址栏输入框高度（更紧凑） */
static void
add_location_css(GtkWidget *entry)
{
    static GtkCssProvider *css = NULL;

    if (css == NULL)
    {
        css = gtk_css_provider_new();
        gtk_css_provider_load_from_data(
            css,
            "#lr-location { min-height: 0; padding: 1px 6px; }",
            -1, NULL);
    }
    gtk_widget_set_name(entry, "lr-location");
    gtk_style_context_add_provider(
        gtk_widget_get_style_context(entry), GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_USER);
}

static GtkWidget *
build_location_bar(LrMainWindow *mw)
{
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    gtk_widget_set_margin_top(bar, 2);
    gtk_widget_set_margin_bottom(bar, 2);

    mw->location_bar = bar;
    mw->location_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(mw->location_entry),
                                   _("Type a Path and Press Enter"));
    atk_object_set_name(gtk_widget_get_accessible(mw->location_entry),
                        _("Address bar input"));
    add_location_css(mw->location_entry);
    gtk_box_pack_start(GTK_BOX(bar), mw->location_entry, TRUE, TRUE, 0);

    g_signal_connect(mw->location_entry, "activate",
                     G_CALLBACK(on_location_activate), mw);
    return bar;
}

/* 窗口位置/尺寸变化时记录并节流保存 */
static gboolean
on_configure_event(GtkWidget *widget, GdkEventConfigure *event,
                   gpointer user_data)
{
    LrMainWindow *self = user_data;
    (void)widget;

    lr_window_state_set_geometry(self->win_state, event->x, event->y,
                                 event->width, event->height);
    lr_window_state_schedule_save(self->win_state);
    return FALSE;
}

/* 窗口状态变化（最大化等）时记录尺寸与最大化标志 */
static gboolean
on_window_state_event(GtkWidget *widget, GdkEventWindowState *event,
                      gpointer user_data)
{
    LrMainWindow *self = user_data;
    gboolean maxed;
    (void)widget;

    maxed = (event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) != 0;
    lr_window_state_set_maximized(self->win_state, maxed);

    /* 非最大化时记录真实尺寸（最大化时窗口尺寸无参考意义） */
    if (!maxed)
    {
        gint w = 0, h = 0;
        gtk_window_get_size(GTK_WINDOW(self->window), &w, &h);
        lr_window_state_set_size(self->win_state, w, h);
    }
    return FALSE;
}

/* 窗口显示后延迟定位上次路径 */
static gboolean
on_reveal_path_idle(gpointer user_data)
{
    LrMainWindow *self = user_data;

    if (self->pending_path != NULL)
        lr_tree_pane_reveal_path(self->tree, self->pending_path);
    g_clear_pointer(&self->pending_path, g_free);
    return G_SOURCE_REMOVE;
}

/* 窗口显示后把键盘焦点给树视图（避免默认聚焦到地址栏输入框） */
static gboolean
on_focus_tree_idle(gpointer user_data)
{
    LrMainWindow *self = user_data;
    lr_tree_pane_focus(self->tree);
    return G_SOURCE_REMOVE;
}

static void
on_window_destroy(GtkWidget *widget, gpointer user_data)
{
    LrMainWindow *self = user_data;
    (void)widget;
    lr_window_state_save_now(self->win_state);
}

void lr_main_window_restore_state(LrMainWindow *self)
{
    char *last_path = NULL;
    gint w, h, x, y;

    if (!lr_window_state_restore(self->win_state, &last_path))
        return;

    lr_window_state_get_size(self->win_state, &w, &h);
    if (w > 0 && h > 0)
        gtk_window_resize(GTK_WINDOW(self->window), w, h);

    lr_window_state_get_pos(self->win_state, &x, &y);
    gtk_window_move(GTK_WINDOW(self->window), x, y);

    if (lr_window_state_is_maximized(self->win_state))
        gtk_window_maximize(GTK_WINDOW(self->window));

    if (last_path != NULL)
    {
        g_free(self->pending_path);
        self->pending_path = last_path;
        g_idle_add(on_reveal_path_idle, self);
    }
}

LrMainWindow *
lr_main_window_new(void)
{
    LrMainWindow *mw = g_new0(LrMainWindow, 1);
    GtkWidget *vbox, *menubar, *location_bar, *paned;

    mw->win_state = lr_window_state_new("linux-regedit");
    /* 与默认窗口大小一致，首次关闭前缓存有效 */
    lr_window_state_set_size(mw->win_state, 920, 600);

    mw->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(mw->window), _("Regedit"));
    gtk_window_set_default_size(GTK_WINDOW(mw->window), 920, 600);
    gtk_window_set_position(GTK_WINDOW(mw->window), GTK_WIN_POS_CENTER);
    g_signal_connect(mw->window, "destroy",
                     G_CALLBACK(on_window_destroy), mw);
    g_signal_connect(mw->window, "configure-event",
                     G_CALLBACK(on_configure_event), mw);
    g_signal_connect(mw->window, "window-state-event",
                     G_CALLBACK(on_window_state_event), mw);
    g_signal_connect(mw->window, "key-press-event",
                     G_CALLBACK(on_window_key_press), mw);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    menubar = build_menubar(mw);
    location_bar = build_location_bar(mw);

    paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    mw->tree = lr_tree_pane_new();
    mw->value = lr_value_pane_new();
    gtk_paned_pack1(GTK_PANED(paned), lr_tree_pane_get_widget(mw->tree),
                    FALSE, FALSE);
    gtk_paned_pack2(GTK_PANED(paned), lr_value_pane_get_widget(mw->value),
                    TRUE, TRUE);
    gtk_paned_set_position(GTK_PANED(paned), 320);

    gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), location_bar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), paned, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(mw->window), vbox);

    lr_tree_pane_set_select_cb(mw->tree, on_tree_select, mw);

    /* 窗口显示（show_all）后主循环首个 idle 即把焦点给树视图 */
    g_idle_add(on_focus_tree_idle, mw);

    return mw;
}

GtkWidget *
lr_main_window_get_window(LrMainWindow *self)
{
    return self->window;
}

void lr_main_window_free(LrMainWindow *self)
{
    if (self == NULL)
        return;
    g_free(self->current_path);
    g_clear_pointer(&self->pending_path, g_free);
    lr_window_state_free(self->win_state);
    lr_tree_pane_free(self->tree);
    lr_value_pane_free(self->value);
    g_free(self);
}
