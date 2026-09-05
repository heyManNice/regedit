#include "ui/tree_pane.h"
#include "core/scanner.h"
#include "ui/dialog_utils.h"
#include "ui/test_roots.h"

#include <glib/gi18n.h>

enum
{
    COL_ICON = 0,
    COL_NAME,
    COL_PATH,
    COL_KIND,
    COL_FORMAT,
    COL_LOADED,
    N_COLS
};

struct _LrTreePane
{
    GtkWidget *widget;
    GtkTreeView *view;
    GtkTreeStore *store;
    LrTreePaneSelectCb select_cb;
    gpointer select_data;

    GdkPixbuf *icon_folder;
    GdkPixbuf *icon_config;
    GdkPixbuf *icon_other;
    GdkPixbuf *icon_computer;

    GtkTreePath *popup_path; /* 右键菜单针对的节点 */
};

static GdkPixbuf *
load_icon(const gchar *name, gint size)
{
    GtkIconTheme *theme = gtk_icon_theme_get_default();
    GError *error = NULL;
    GdkPixbuf *pb = gtk_icon_theme_load_icon(theme, name, size, 0, &error);
    if (pb == NULL)
    {
        g_clear_error(&error);
        return NULL;
    }
    return pb;
}

static void
add_dummy_child(LrTreePane *self, GtkTreeIter *parent)
{
    GtkTreeIter child;
    gtk_tree_store_append(self->store, &child, parent);
    gtk_tree_store_set(self->store, &child,
                       COL_ICON, NULL,
                       COL_NAME, "…",
                       COL_PATH, "",
                       COL_KIND, LR_SCAN_DIR,
                       COL_FORMAT, LR_FORMAT_UNKNOWN,
                       COL_LOADED, FALSE,
                       -1);
}

/* 计算机虚拟根：下挂系统/用户配置根（类比注册表根键） */
static void
add_computer_root(LrTreePane *self)
{
    const char *etc = lr_etc_root();
    const char *config = lr_config_root();
    const char *boot = lr_boot_root();
    GtkTreeIter computer_iter, etc_iter, user_iter, boot_iter;

    gtk_tree_store_append(self->store, &computer_iter, NULL);
    gtk_tree_store_set(self->store, &computer_iter,
                       COL_ICON, self->icon_computer,
                       COL_NAME, _("Computer"),
                       COL_PATH, "",
                       COL_KIND, LR_SCAN_DIR,
                       COL_FORMAT, LR_FORMAT_UNKNOWN,
                       COL_LOADED, TRUE,
                       -1);

    /* HKEY_LOCAL_MACHINE —— 系统级配置，默认 /etc（LR_TEST_ETC 可覆盖） */
    gtk_tree_store_append(self->store, &etc_iter, &computer_iter);
    gtk_tree_store_set(self->store, &etc_iter,
                       COL_ICON, self->icon_folder,
                       COL_NAME, "HKEY_LOCAL_MACHINE",
                       COL_PATH, etc,
                       COL_KIND, LR_SCAN_DIR,
                       COL_FORMAT, LR_FORMAT_UNKNOWN,
                       COL_LOADED, FALSE,
                       -1);
    add_dummy_child(self, &etc_iter);

    /* HKEY_CURRENT_USER —— 用户级配置，默认 ~/.config（LR_TEST_CONFIG 可覆盖） */
    gtk_tree_store_append(self->store, &user_iter, &computer_iter);
    gtk_tree_store_set(self->store, &user_iter,
                       COL_ICON, self->icon_folder,
                       COL_NAME, "HKEY_CURRENT_USER",
                       COL_PATH, (char *)config,
                       COL_KIND, LR_SCAN_DIR,
                       COL_FORMAT, LR_FORMAT_UNKNOWN,
                       COL_LOADED, FALSE,
                       -1);
    add_dummy_child(self, &user_iter);

    /* HKEY_SYSTEM_BOOT —— 引导目录，默认 /boot（LR_TEST_BOOT 可覆盖） */
    gtk_tree_store_append(self->store, &boot_iter, &computer_iter);
    gtk_tree_store_set(self->store, &boot_iter,
                       COL_ICON, self->icon_folder,
                       COL_NAME, "HKEY_SYSTEM_BOOT",
                       COL_PATH, (char *)boot,
                       COL_KIND, LR_SCAN_DIR,
                       COL_FORMAT, LR_FORMAT_UNKNOWN,
                       COL_LOADED, FALSE,
                       -1);
    add_dummy_child(self, &boot_iter);
}

static GdkPixbuf *
icon_for_kind(LrTreePane *self, LrScanKind kind)
{
    switch (kind)
    {
    case LR_SCAN_DIR:
        return self->icon_folder;
    case LR_SCAN_SUPPORTED_FILE:
        return self->icon_config;
    default:
        return self->icon_other;
    }
}

static void
fill_children(LrTreePane *self, GtkTreeIter *parent, const char *dirpath)
{
    GPtrArray *entries = lr_scanner_list_dir(dirpath);
    guint i;

    for (i = 0; i < entries->len; i++)
    {
        LrScanEntry *e = g_ptr_array_index(entries, i);
        GtkTreeIter child;

        gtk_tree_store_append(self->store, &child, parent);
        gtk_tree_store_set(self->store, &child,
                           COL_ICON, icon_for_kind(self, e->kind),
                           COL_NAME, e->name,
                           COL_PATH, e->path,
                           COL_KIND, e->kind,
                           COL_FORMAT, e->format,
                           COL_LOADED, FALSE,
                           -1);
        if (e->kind == LR_SCAN_DIR)
            add_dummy_child(self, &child);
    }

    g_ptr_array_unref(entries);
    gtk_tree_store_set(self->store, parent, COL_LOADED, TRUE, -1);
}

/* 加载目录节点：先填真实子节点，再移除占位 dummy。
 * 顺序不能颠倒：若先删 dummy，该行会瞬间“无子节点”，
 * GtkTreeView 会因此自动折叠，导致首次需两次展开的 bug。 */
static void
load_dir_node(LrTreePane *self, GtkTreeIter *iter, const char *dirpath)
{
    GtkTreeModel *model = GTK_TREE_MODEL(self->store);
    GtkTreeIter child;

    fill_children(self, iter, dirpath);

    /* 移除占位的 dummy 子节点（若有） */
    if (gtk_tree_model_iter_children(model, &child, iter))
    {
        gchar *cpath = NULL;
        gtk_tree_model_get(model, &child, COL_PATH, &cpath, -1);
        if (cpath == NULL || *cpath == '\0')
            gtk_tree_store_remove(self->store, &child);
        g_free(cpath);
    }
}

static void
on_row_expanded(GtkTreeView *view, GtkTreeIter *iter, GtkTreePath *tpath,
                gpointer user_data)
{
    LrTreePane *self = user_data;
    gboolean loaded = FALSE;
    gchar *dirpath = NULL;

    (void)view;
    (void)tpath;

    gtk_tree_model_get(GTK_TREE_MODEL(self->store), iter,
                       COL_LOADED, &loaded,
                       COL_PATH, &dirpath, -1);
    if (loaded || dirpath == NULL || *dirpath == '\0')
    {
        g_free(dirpath);
        return;
    }

    load_dir_node(self, iter, dirpath);
    g_free(dirpath);
}

/* 递归为目录行绘制左侧竖虚线：同级目录对齐成线，层级递进错位 */
static void
draw_tree_lines_recursive(LrTreePane *self, cairo_t *cr, GtkTreeIter *parent)
{
    GtkTreeModel *model = GTK_TREE_MODEL(self->store);
    GtkTreeIter child;

    if (!gtk_tree_model_iter_children(model, &child, parent))
        return;

    do
    {
        gint kind;
        gtk_tree_model_get(model, &child, COL_KIND, &kind, -1);

        if (kind == LR_SCAN_DIR)
        {
            GtkTreePath *path = gtk_tree_model_get_path(model, &child);
            GdkRectangle rect;

            gtk_tree_view_get_cell_area(self->view, path, NULL, &rect);
            gtk_tree_path_free(path);

            if (rect.height > 0)
            {
                double dashes[] = {2.0, 2.0};
                double x = rect.x + 1;

                cairo_save(cr);
                cairo_set_source_rgba(cr, 0.55, 0.55, 0.55, 0.7);
                cairo_set_line_width(cr, 1.0);
                cairo_set_dash(cr, dashes, 2, 0);
                cairo_move_to(cr, x + 0.5, rect.y + 2);
                cairo_line_to(cr, x + 0.5, rect.y + rect.height - 2);
                cairo_stroke(cr);
                cairo_restore(cr);
            }

            draw_tree_lines_recursive(self, cr, &child);
        }
    } while (gtk_tree_model_iter_next(model, &child));
}

/* 在树视图默认绘制完成后叠加竖虚线 */
static gboolean
on_draw_after(GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
    LrTreePane *self = user_data;
    (void)widget;

    draw_tree_lines_recursive(self, cr, NULL);
    return FALSE;
}

/* 双击目录节点时展开/收起 */
static void
on_row_activated(GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *col,
                 gpointer user_data)
{
    LrTreePane *self = user_data;
    GtkTreeIter iter;
    gint kind;

    (void)view;
    (void)col;

    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(self->store), &iter, path))
        return;
    gtk_tree_model_get(GTK_TREE_MODEL(self->store), &iter, COL_KIND, &kind, -1);
    if (kind != LR_SCAN_DIR)
        return;

    if (gtk_tree_view_row_expanded(self->view, path))
        gtk_tree_view_collapse_row(self->view, path);
    else
        gtk_tree_view_expand_row(self->view, path, FALSE);
}

static void
on_selection_changed(GtkTreeSelection *sel, gpointer user_data)
{
    LrTreePane *self = user_data;
    GtkTreeIter iter;
    GtkTreeModel *model;
    gchar *path = NULL;
    gint kind = LR_SCAN_OTHER_FILE;

    if (!gtk_tree_selection_get_selected(sel, &model, &iter))
        return;

    gtk_tree_model_get(model, &iter, COL_PATH, &path, COL_KIND, &kind, -1);
    if (self->select_cb != NULL)
    {
        /* 计算机虚拟根（空路径）也通知：按目录 + 空路径处理 */
        if (path == NULL || *path == '\0')
            self->select_cb("", TRUE, self->select_data);
        else
            self->select_cb(path, kind == LR_SCAN_DIR, self->select_data);
    }
    g_free(path);
}

/* 展开/折叠被右键的目录节点 */
static void
on_popup_expand_collapse(GtkMenuItem *item, gpointer user_data)
{
    LrTreePane *self = user_data;
    (void)item;

    if (self->popup_path == NULL)
        return;
    if (gtk_tree_view_row_expanded(self->view, self->popup_path))
        gtk_tree_view_collapse_row(self->view, self->popup_path);
    else
        gtk_tree_view_expand_row(self->view, self->popup_path, FALSE);
}

/* 复制节点名称到剪贴板 */
static void
on_popup_copy_name(GtkMenuItem *item, gpointer user_data)
{
    LrTreePane *self = user_data;
    GtkTreeIter iter;
    gchar *name = NULL;
    (void)item;

    if (self->popup_path == NULL)
        return;
    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(self->store), &iter,
                                 self->popup_path))
        return;

    gtk_tree_model_get(GTK_TREE_MODEL(self->store), &iter, COL_NAME, &name, -1);
    if (name != NULL)
    {
        GtkClipboard *clipboard =
            gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        gtk_clipboard_set_text(clipboard, name, -1);
        g_free(name);
    }
}

/* 尚未实现的功能：复用通用提示框 */
static void
on_popup_not_impl(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    lr_dialog_not_impl(gtk_widget_get_toplevel(GTK_WIDGET(item)), user_data);
}

/* 新建 → 子菜单：文件夹 + 分割线 + 支持的文件格式（未实现，点击提示） */
static void
build_new_submenu(LrTreePane *self, GtkWidget *menu)
{
    const char *const *names;
    GtkWidget *item;
    gint i;

    (void)self;
    item = gtk_menu_item_new_with_label(_("Folder"));
    g_signal_connect(item, "activate", G_CALLBACK(on_popup_not_impl),
                     (gpointer) _("New Folder"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    names = lr_format_new_file_names();
    for (i = 0; names[i] != NULL; i++)
    {
        item = gtk_menu_item_new_with_label(_(names[i]));
        g_signal_connect(item, "activate", G_CALLBACK(on_popup_not_impl),
                         (gpointer)names[i]);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }
}

static void
show_popup_menu(LrTreePane *self, GtkTreePath *path, GdkEventButton *event)
{
    GtkTreeIter iter;
    gint kind = LR_SCAN_OTHER_FILE;
    GtkWidget *menu, *item;
    gboolean expanded;

    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(self->store), &iter, path))
        return;

    gtk_tree_model_get(GTK_TREE_MODEL(self->store), &iter, COL_KIND, &kind, -1);
    gtk_tree_selection_select_path(gtk_tree_view_get_selection(self->view),
                                   path);
    gtk_tree_view_set_cursor(self->view, path, NULL, FALSE);

    g_clear_pointer(&self->popup_path, gtk_tree_path_free);
    self->popup_path = gtk_tree_path_copy(path);

    menu = gtk_menu_new();

    /* 目录节点：展开/折叠（按当前状态显示） + 新建 */
    if (kind == LR_SCAN_DIR)
    {
        expanded = gtk_tree_view_row_expanded(self->view, path);
        item = gtk_menu_item_new_with_label(expanded ? _("Collapse") : _("Expand"));
        g_signal_connect(item, "activate",
                         G_CALLBACK(on_popup_expand_collapse), self);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

        item = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

        item = gtk_menu_item_new_with_label(_("New"));
        {
            GtkWidget *new_sub = gtk_menu_new();
            build_new_submenu(self, new_sub);
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), new_sub);
        }
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }

    item = gtk_menu_item_new_with_label(_("Find"));
    g_signal_connect(item, "activate", G_CALLBACK(on_popup_not_impl),
                     (gpointer) _("Find"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label(_("Delete"));
    g_signal_connect(item, "activate", G_CALLBACK(on_popup_not_impl),
                     (gpointer) _("Delete"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label(_("Rename"));
    g_signal_connect(item, "activate", G_CALLBACK(on_popup_not_impl),
                     (gpointer) _("Rename"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label(_("Export"));
    g_signal_connect(item, "activate", G_CALLBACK(on_popup_not_impl),
                     (gpointer) _("Export"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label(_("Permission"));
    g_signal_connect(item, "activate", G_CALLBACK(on_popup_not_impl),
                     (gpointer) _("Permission"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label(_("Copy Name"));
    g_signal_connect(item, "activate", G_CALLBACK(on_popup_copy_name), self);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
}

/* 右键点击树节点 */
static gboolean
on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    LrTreePane *self = user_data;
    GtkTreePath *path = NULL;
    (void)widget;

    if (event->type == GDK_BUTTON_PRESS && event->button == 3)
    {
        if (gtk_tree_view_get_path_at_pos(self->view, (gint)event->x,
                                          (gint)event->y, &path, NULL, NULL,
                                          NULL))
        {
            show_popup_menu(self, path, event);
            gtk_tree_path_free(path);
            return TRUE;
        }
    }
    return FALSE;
}

/* 键盘弹出菜单（Shift+F10 / 菜单键） */
static gboolean
on_popup_menu(GtkWidget *widget, gpointer user_data)
{
    LrTreePane *self = user_data;
    GtkTreeSelection *sel = gtk_tree_view_get_selection(self->view);
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreePath *path;
    (void)widget;

    if (gtk_tree_selection_get_selected(sel, &model, &iter))
    {
        path = gtk_tree_model_get_path(model, &iter);
        show_popup_menu(self, path, NULL);
        gtk_tree_path_free(path);
        return TRUE;
    }
    return FALSE;
}

LrTreePane *
lr_tree_pane_new(void)
{
    LrTreePane *self = g_new0(LrTreePane, 1);
    GtkCellRenderer *pix, *txt;
    GtkTreeViewColumn *col;

    /* 图标 */
    self->icon_folder = load_icon("folder", 16);
    self->icon_config = load_icon("text-x-generic", 16);
    self->icon_other = load_icon("text-x-generic", 16);
    self->icon_computer = load_icon("computer", 16);

    self->widget = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(self->widget),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);

    self->store = gtk_tree_store_new(N_COLS,
                                     GDK_TYPE_PIXBUF, G_TYPE_STRING,
                                     G_TYPE_STRING, G_TYPE_INT,
                                     G_TYPE_INT, G_TYPE_BOOLEAN);
    self->view = GTK_TREE_VIEW(gtk_tree_view_new_with_model(
        GTK_TREE_MODEL(self->store)));
    g_object_unref(self->store);

    gtk_tree_view_set_headers_visible(self->view, FALSE);
    gtk_container_add(GTK_CONTAINER(self->widget), GTK_WIDGET(self->view));
    atk_object_set_name(gtk_widget_get_accessible(GTK_WIDGET(self->view)),
                        _("Directory tree"));

    pix = gtk_cell_renderer_pixbuf_new();
    txt = gtk_cell_renderer_text_new();
    /* 图标与文字之间留出间隔 */
    g_object_set(pix, "xpad", 6, NULL);
    col = gtk_tree_view_column_new();
    gtk_tree_view_column_pack_start(col, pix, FALSE);
    gtk_tree_view_column_pack_start(col, txt, TRUE);
    gtk_tree_view_column_add_attribute(col, pix, "pixbuf", COL_ICON);
    gtk_tree_view_column_add_attribute(col, txt, "text", COL_NAME);
    gtk_tree_view_column_set_expand(col, TRUE);
    gtk_tree_view_append_column(self->view, col);

    g_signal_connect(self->view, "row-expanded",
                     G_CALLBACK(on_row_expanded), self);
    g_signal_connect(self->view, "row-activated",
                     G_CALLBACK(on_row_activated), self);
    g_signal_connect_after(self->view, "draw",
                           G_CALLBACK(on_draw_after), self);
    g_signal_connect(self->view, "button-press-event",
                     G_CALLBACK(on_button_press), self);
    g_signal_connect(self->view, "popup-menu",
                     G_CALLBACK(on_popup_menu), self);
    g_signal_connect(gtk_tree_view_get_selection(self->view), "changed",
                     G_CALLBACK(on_selection_changed), self);

    add_computer_root(self);

    return self;
}

GtkWidget *
lr_tree_pane_get_widget(LrTreePane *self)
{
    return self->widget;
}

void lr_tree_pane_focus(LrTreePane *self)
{
    gtk_widget_grab_focus(GTK_WIDGET(self->view));
}

void lr_tree_pane_set_select_cb(LrTreePane *self, LrTreePaneSelectCb cb,
                                gpointer user_data)
{
    self->select_cb = cb;
    self->select_data = user_data;
}

void lr_tree_pane_refresh(LrTreePane *self)
{
    gtk_tree_store_clear(self->store);
    add_computer_root(self);
}

void lr_tree_pane_expand_all(LrTreePane *self)
{
    gtk_tree_view_expand_all(self->view);
}

void lr_tree_pane_collapse_all(LrTreePane *self)
{
    gtk_tree_view_collapse_all(self->view);
}

/* 若节点为未加载的目录，移除占位子节点并填充真实子节点 */
static void
ensure_children_loaded(LrTreePane *self, GtkTreeIter *iter)
{
    GtkTreeModel *model = GTK_TREE_MODEL(self->store);
    gboolean loaded = FALSE;
    gchar *dirpath = NULL;

    gtk_tree_model_get(model, iter, COL_LOADED, &loaded,
                       COL_PATH, &dirpath, -1);
    if (loaded || dirpath == NULL || *dirpath == '\0')
    {
        g_free(dirpath);
        return;
    }

    load_dir_node(self, iter, dirpath);
    g_free(dirpath);
}

static gboolean
reveal_recursive(LrTreePane *self, GtkTreeIter *iter, const char *path,
                 GtkTreeIter *out)
{
    GtkTreeModel *model = GTK_TREE_MODEL(self->store);
    gchar *node_path = NULL;
    gint kind = LR_SCAN_OTHER_FILE;

    gtk_tree_model_get(model, iter, COL_PATH, &node_path, COL_KIND, &kind, -1);
    if (node_path != NULL && g_str_equal(node_path, path))
    {
        g_free(node_path);
        *out = *iter;
        return TRUE;
    }
    g_free(node_path);

    if (kind == LR_SCAN_DIR)
    {
        GtkTreeIter child;
        ensure_children_loaded(self, iter);
        if (gtk_tree_model_iter_children(model, &child, iter))
        {
            do
            {
                if (reveal_recursive(self, &child, path, out))
                    return TRUE;
            } while (gtk_tree_model_iter_next(model, &child));
        }
    }
    return FALSE;
}

gboolean
lr_tree_pane_reveal_path(LrTreePane *self, const char *path)
{
    GtkTreeModel *model = GTK_TREE_MODEL(self->store);
    GtkTreeIter root, out;
    GtkTreePath *tp;

    if (path == NULL || *path == '\0')
        return FALSE;
    if (!gtk_tree_model_get_iter_first(model, &root))
        return FALSE;
    if (!reveal_recursive(self, &root, path, &out))
        return FALSE;

    tp = gtk_tree_model_get_path(model, &out);
    gtk_tree_view_expand_to_path(self->view, tp);
    gtk_tree_selection_select_path(gtk_tree_view_get_selection(self->view),
                                   tp);
    gtk_tree_view_scroll_to_cell(self->view, tp, NULL, FALSE, 0, 0);
    gtk_tree_path_free(tp);
    return TRUE;
}

void lr_tree_pane_free(LrTreePane *self)
{
    if (self == NULL)
        return;
    g_clear_pointer(&self->popup_path, gtk_tree_path_free);
    g_clear_object(&self->icon_folder);
    g_clear_object(&self->icon_config);
    g_clear_object(&self->icon_other);
    g_clear_object(&self->icon_computer);
    g_free(self);
}
