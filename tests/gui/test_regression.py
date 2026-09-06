"""linux-regedit GUI regression tests (AT-SPI only, no screenshots).

运行前提：
  1. meson 构建出 builddir/linux-regedit
  2. spire 测试库可用（SPIRE_PATH 或 ../spire 相邻目录）
  3. 一个可用的 X/AT-SPI 会话（本地 :2，或 xvfb-run）
"""

from __future__ import annotations

import pytest
from pathlib import Path
import time
import os
import subprocess
import re

from spire import tree
from spire.input import double_click, click, press, type_text
from spire.session import AppSession
from spire.wait import wait_until, wait_node


def expand_row(app, name: str):
    row = wait_node(app.app, name=name, role="table cell")
    double_click(row)
    return row


def open_sample(app, fake_roots, name: str):
    """通过地址栏直接打开 fake roots 里的某个 testdata 样例。"""
    entry = wait_node(app, name="Address bar input", role="text")
    type_text(entry, str(fake_roots["samples"][name]))
    press("Return")


def menu_item_activate(app, menu_name: str, item_name: str):
    """打开主菜单并触发某项（AT-SPI action，避免坐标脆弱）。"""
    click(wait_node(app, name=menu_name, role="menu"))
    item = wait_until(
        lambda: (tree.find(app, name=item_name, role="check menu item") or
                 tree.find(app, name=item_name, role="menu item")),
        timeout=10, message=f"{item_name!r} did not appear in {menu_name}")
    item.queryAction().doAction(0)


def test_accessible_surface_and_menu_states(regedit):
    """Static surface: menus, named panes and honest menu contents."""
    app = regedit.app
    assert wait_node(app, role="frame", name="Regedit")

    assert wait_node(app, name="Address bar input", role="text",
                     state="EDITABLE")
    assert wait_node(app, name="Directory tree", role="tree table")
    assert wait_node(app, name="Value panel", role="split pane")

    for menu in ("File", "Edit", "View", "Favorites", "Help"):
        assert wait_node(app, name=menu, role="menu"), f"missing menu {menu}"

    assert wait_node(app, name="Save Changes...", role="menu item")

    # 查看 → 地址栏 是勾选状态
    loc = wait_node(app, name="Address", role="check menu item")
    assert tree.has_state(loc, "CHECKED")

    # 只读阶段不提供“点了才说没做”的入口
    for gone in ("Import...", "Print", "Split", "Fonts",
                 "Permission", "Delete", "Rename"):
        assert tree.find(app, name=gone, role="menu item") is None, \
            f"stub menu item {gone!r} should not exist"


def test_fake_root_tree_navigation(regedit, fake_roots):
    """Expand the fake tree and load sample.ini through the UI."""
    app = regedit.app

    expand_row(regedit, "Computer")
    for root in ("HKEY_LOCAL_MACHINE", "HKEY_CURRENT_USER",
                 "HKEY_SYSTEM_BOOT"):
        assert wait_node(app, name=root, role="table cell")

    hklm = wait_node(app, name="HKEY_LOCAL_MACHINE", role="table cell")
    double_click(hklm)
    sample = wait_node(app, name="sample.ini", role="table cell")
    click(sample, x_offset=90)

    wait_until(lambda: tree.find(app, text="Port") is not None,
               timeout=8, message="value pane did not load")
    assert tree.find(app, role="tree table", name="Value table")

    expected = {
        "[server]": "table cell",
        "Port": "table cell",
        "22": "table cell",
        "Enable": "table cell",
        "yes": "table cell",
        "Boolean": "table cell",
        "顶部注释：说明下方配置": "table cell",
    }
    for text, role in expected.items():
        node = tree.find(app, text=text, role=role)
        assert node is not None, f"missing value row text {text!r}"
        assert text in tree.text_of(node)


def test_location_bar_jump(regedit, fake_roots):
    """Typing a path in the address bar opens a config file."""
    app = regedit.app
    entry = wait_node(app, name="Address bar input", role="text")
    type_text(entry, str(fake_roots["sample_ini"]))
    press("Return")

    wait_until(lambda: tree.find(app, text="Level") is not None,
               timeout=8, message="[logging] Level did not appear")
    node = tree.find(app, text="Level", role="table cell")
    assert node is not None


def test_snapshot_is_serializable(regedit):
    """The AI snapshot must be plain JSON-able data with indices."""
    snap = tree.snapshot(regedit.app, max_depth=30, max_nodes=300)
    assert snap
    for item in snap:
        assert isinstance(item["idx"], int)
        assert "role" in item and "name" in item and "state" in item


CONTENT_CASES = [
    ("sample.ini", ["Port", "22", "[server]"]),
    ("sample.json", ["linux-regedit", "version"]),
    ("sample.service", ["Description", "Example service"]),
    ("sample.toml", ["port", "8080"]),
    ("sample.sshd_config", ["PermitRootLogin"]),
    ("sample-apt.conf", ["MetaKey"]),
    ("sample.xml", ["layoutmode", "physical"]),
    ("sample.environment.conf", ["PATH"]),
    ("sample-evolution.source", ["DisplayName", "默认代理设置"]),
    ("sample.unknown.txt", ["既不是分节配置"]),
    ("sample-script.sh", ["hello linux-regedit"]),
]


@pytest.mark.parametrize("name,markers", CONTENT_CASES)
def test_content_display_matrix(regedit, fake_roots, name, markers):
    """每种解析格式都要能通过地址栏打开并显示关键内容。"""
    app = regedit.app
    open_sample(app, fake_roots, name)
    for marker in markers:
        wait_until(lambda: tree.find(app, text=marker) is not None,
                   timeout=8,
                   message=f"{name}: marker {marker!r} not visible")


def test_find_hits_and_no_match(regedit, fake_roots):
    """Ctrl+F 查找：命中计数、无匹配提示。"""
    app = regedit.app
    open_sample(app, fake_roots, "sample.ini")

    press("ctrl+f")
    entry = wait_node(app, name="Find what", role="text", timeout=6)
    type_text(entry, "Port")
    press("Return")
    status = wait_node(app, name="Find status", role="label")
    wait_until(lambda: tree.text_of(status) == "Found 1 match(es).",
               timeout=5, message="find count label did not update")

    type_text(entry, "zzzz_no_such_key")
    press("Return")
    wait_until(lambda: tree.text_of(status) == "No matches found.",
               timeout=5, message="no-match label did not appear")
    press("Escape")


def test_backup_current_file(regedit, fake_roots):
    """File → Backup 应把当前文件复制进隔离的 backups 目录。"""
    app = regedit.app
    open_sample(app, fake_roots, "sample.ini")
    menu_item_activate(app, "File", "Backup Current File...")

    bdir = fake_roots["data_home"] / "linux-regedit" / "backups"
    wait_until(lambda: bdir.exists() and any(bdir.glob("*.bak")),
               timeout=6, message="backup file was not created")
    backup = sorted(bdir.glob("*.bak"))[-1]
    assert backup.read_text(errors="replace") == \
        (fake_roots["samples"]["sample.ini"]).read_text(errors="replace")


def test_favorites_load_from_data_home(display, fake_roots):
    """预置到 XDG_DATA_HOME 的收藏项在启动后应出现在 Favorites 菜单。"""
    fav_dir = fake_roots["data_home"] / "linux-regedit" / "favorites"
    fav_dir.mkdir(parents=True, exist_ok=True)
    (fav_dir / "MyServer").write_text(
        str(fake_roots["samples"]["sample.ini"]), encoding="utf-8")

    binary = (Path(__file__).resolve().parents[2] / "builddir" /
              "linux-regedit")
    wait_until(lambda: tree.app_by_name("linux-regedit") is None, timeout=8)
    with AppSession([str(binary)], env=fake_roots["env"],
                    app_name="linux-regedit", display=display) as app:
        app.app = wait_until(
            lambda: tree.app_by_name("linux-regedit", live=True), timeout=10)
        wait_node(app.app, role="frame", timeout=8)
        click(wait_node(app.app, name="Favorites", role="menu"))
        item = wait_node(app.app, name="MyServer", role="menu item",
                         timeout=6)
        assert item is not None


def test_refresh_picks_up_external_change(regedit, fake_roots):
    """View → Refresh 后应看到磁盘上的新配置项。"""
    app = regedit.app
    path = fake_roots["samples"]["sample.ini"]
    open_sample(app, fake_roots, "sample.ini")

    with path.open("a", encoding="utf-8") as fh:
        fh.write("NewExternalKey = 1\n")
    menu_item_activate(app, "View", "Refresh")
    wait_until(lambda: tree.find(app, text="NewExternalKey") is not None,
               timeout=8, message="refreshed content not visible")


def test_toggle_address_bar(regedit):
    """Ctrl+Shift+A 勾选切换应隐藏/恢复地址栏（键盘=用户真实路径）。

    注意：隐藏后 AT-SPI 节点仍然存在，必须用 SHOWING 状态判断可见性。
    """
    app = regedit.app
    assert wait_node(app, name="Address bar input", role="text",
                     state="SHOWING")

    press("ctrl+shift+a")
    wait_until(lambda: tree.find(app, name="Address bar input", role="text",
                                 state="SHOWING") is None,
               timeout=5, message="address bar still SHOWING after toggle")

    press("ctrl+shift+a")
    wait_node(app, name="Address bar input", role="text", state="SHOWING",
              timeout=5)


def test_snapshot_order_is_stable(regedit):
    """两次抓取无障碍树快照的结构与顺序应一致。"""
    snap1 = tree.snapshot(regedit.app, max_depth=30, max_nodes=300)
    snap2 = tree.snapshot(regedit.app, max_depth=30, max_nodes=300)
    shape = lambda s: [(i["role"], i["name"]) for i in s]
    assert shape(snap1) == shape(snap2)


def test_cli_open_file_outside_roots(display, fake_roots, tmp_path):
    """命令行直接打开根目录外的文件也应显示内容（不被首帧选中事件清空）。"""
    outside = tmp_path / "cli-demo.toml"
    outside.write_text("[server]\nport = 8080\n", encoding="utf-8")
    binary = (Path(__file__).resolve().parents[2] / "builddir" /
              "linux-regedit")

    wait_until(lambda: tree.app_by_name("linux-regedit") is None, timeout=8)
    with AppSession([str(binary), str(outside)],
                    env=fake_roots["env"], app_name="linux-regedit",
                    display=display) as app:
        app.app = wait_until(
            lambda: tree.app_by_name("linux-regedit", live=True), timeout=10)
        wait_node(app.app, role="frame", timeout=8)
        wait_until(lambda: tree.find(app.app, text="port",
                                     max_depth=80) is not None,
                   timeout=6, message="outside-root file did not display")


def test_unsaved_edit_banner(regedit, fake_roots, display):
    """内存编辑出现“未保存”提示，切换文件后提示消失。"""
    app = regedit.app
    open_sample(app, fake_roots, "sample.ini")

    assert tree.find(app, name="Edit status", showing=True) is None
    # 真实键盘路径：Edit → New → Number
    click(wait_node(app, name="Edit", role="menu"))
    time.sleep(0.3)
    press("Down")          # 聚焦 New
    time.sleep(0.15)
    press("Right")         # 展开 New 子菜单
    time.sleep(0.3)
    for _ in range(3):     # Section/String/Boolean 之后为 Number
        press("Down")
        time.sleep(0.05)
    press("Return")

    bar = wait_node(app, name="Edit status", role="label", timeout=6)
    wait_until(lambda: tree.has_state(bar, "SHOWING"),
               timeout=5, message="unsaved-edit banner did not appear")
    assert "not saved" in tree.text_of(bar)

    open_sample(app, fake_roots, "sample.json")
    time.sleep(0.8)
    press("Right")
    time.sleep(0.2)
    press("Return")
    wait_until(lambda: tree.find(app, name="Edit status",
                                 showing=True) is None,
               timeout=6, message="banner did not clear after file switch")


def test_save_changes_writes_file(regedit, fake_roots, display):
    """编辑值后 Ctrl+S：文件被安全写回，进程存活，未保存提示消失。"""
    app = regedit.app
    path = fake_roots["samples"]["sample.ini"]
    open_sample(app, fake_roots, "sample.ini")

    cell = wait_node(app, text="22", role="table cell", timeout=8)
    double_click(cell)
    time.sleep(0.4)
    subprocess.run(["xdotool", "type", "--delay", "25", "8080"],
                   env=dict(os.environ, DISPLAY=display), check=True)
    subprocess.run(["xdotool", "key", "Return"],
                   env=dict(os.environ, DISPLAY=display), check=True)
    wait_until(lambda: tree.find(app, name="Edit status",
                                 showing=True) is not None,
               timeout=5, message="edit did not mark dirty")

    press("ctrl+s")
    wait_until(lambda: "Port = 8080" in path.read_text(encoding="utf-8"),
               timeout=8, message="file was not written back")
    wait_until(lambda: tree.find(app, name="Edit status",
                                 showing=True) is None,
               timeout=6, message="dirty banner did not clear after save")


def test_rename_key_saves(regedit, fake_roots, display):
    """把键名 Port 改为 ListenPort 后 Ctrl+S 应写回文件。"""
    app = regedit.app
    path = fake_roots["samples"]["sample.ini"]
    open_sample(app, fake_roots, "sample.ini")

    cell = wait_node(app, text="Port", role="table cell", timeout=8)
    double_click(cell)
    time.sleep(0.4)
    subprocess.run(["xdotool", "key", "ctrl+a"],
                   env=dict(os.environ, DISPLAY=display), check=True)
    subprocess.run(["xdotool", "type", "--delay", "25", "ListenPort"],
                   env=dict(os.environ, DISPLAY=display), check=True)
    subprocess.run(["xdotool", "key", "Return"],
                   env=dict(os.environ, DISPLAY=display), check=True)
    wait_until(lambda: tree.find(app, name="Edit status",
                                 showing=True) is not None,
               timeout=5, message="rename did not mark dirty")

    press("ctrl+s")
    wait_until(lambda: "ListenPort =" in path.read_text(encoding="utf-8"),
               timeout=8, message="renamed key was not written back")
    wait_until(lambda: tree.find(app, name="Edit status",
                                 showing=True) is None,
               timeout=6, message="dirty banner did not clear after save")


def _first_window_id(display: str):
    out = subprocess.run(["xdotool", "search", "--class", "linux-regedit"],
                         capture_output=True, text=True,
                         env=dict(os.environ, DISPLAY=display)).stdout.split()
    for wid in out:
        x, y, w, h = _window_geometry(display, wid)
        if w is not None and w >= 500 and h is not None and h >= 300:
            return wid
    return None


def _window_geometry(display: str, wid: str):
    out = subprocess.run(["xdotool", "getwindowgeometry", wid],
        capture_output=True, text=True,
        env=dict(os.environ, DISPLAY=display)).stdout
    pos_x = pos_y = width = height = None
    for line in out.splitlines():
        line = line.strip()
        m = re.search(r"Position: (\d+),(\d+)", line)
        if m:
            pos_x, pos_y = int(m.group(1)), int(m.group(2))
        m = re.search(r"Geometry: (\d+)x(\d+)", line)
        if m:
            width, height = int(m.group(1)), int(m.group(2))
    return pos_x, pos_y, width, height


def _window_is_maximized(display: str, wid: str) -> bool:
    out = subprocess.run(["xprop", "-id", wid, "_NET_WM_STATE"],
                         capture_output=True, text=True,
                         env=dict(os.environ, DISPLAY=display)).stdout
    return ("_NET_WM_STATE_MAXIMIZED_HORZ" in out and
            "_NET_WM_STATE_MAXIMIZED_VERT" in out)


def _maximize_window(display: str, wid: str):
    from Xlib import X, Xatom, display as xdisplay

    d = xdisplay.Display(display)
    root = d.screen().root
    win = d.create_resource_object("window", int(wid))
    state = d.intern_atom("_NET_WM_STATE")
    max_h = d.intern_atom("_NET_WM_STATE_MAXIMIZED_HORZ")
    max_v = d.intern_atom("_NET_WM_STATE_MAXIMIZED_VERT")
    from Xlib.protocol import event as xevent

    ev = xevent.ClientMessage(
        window=win, client_type=state,
        data=(32, [1, max_h, max_v, 0, 0]))
    root.send_event(ev, event_mask=X.SubstructureRedirectMask |
                    X.SubstructureNotifyMask)
    d.flush()


def test_window_state_restores_geometry(display, fake_roots, tmp_path):
    """重启后应记住上次的窗口位置与尺寸（XDG_RUNTIME_DIR state.ini）。"""
    binary = (Path(__file__).resolve().parents[2] / "builddir" /
              "linux-regedit")
    env = dict(fake_roots["env"])

    def _launch(tag: str):
        e = dict(env, LR_TEST_APP_ID=f"org.linux-regedit.state-{tag}")
        return AppSession([str(binary)], env=e, app_name="linux-regedit",
                          display=display)

    wait_until(lambda: tree.app_by_name("linux-regedit") is None, timeout=8)
    with _launch("a") as app:
        app.app = wait_until(
            lambda: tree.app_by_name("linux-regedit", live=True), timeout=10)
        wait_node(app.app, role="frame", timeout=8)
        wid = wait_until(
            lambda: _first_window_id(display),
            timeout=8, message="window id not found")
        subprocess.run(
            ["xdotool", "windowsize",
             wid, "900", "620"],
            env=dict(os.environ, DISPLAY=display), check=True)
        subprocess.run(
            ["xdotool", "windowmove",
             wid, "90", "110"],
            env=dict(os.environ, DISPLAY=display), check=True)
        time.sleep(1.5)  # 等 window_state 的延迟保存生效
        quit_item = wait_node(app.app, name="Quit", role="menu item")
        quit_item.queryAction().doAction(0)
    wait_until(lambda: tree.app_by_name("linux-regedit") is None, timeout=8)

    state = (fake_roots["runtime_home"] / "linux-regedit" / "state.ini")
    assert state.exists(), "window state file was not written"

    with _launch("b") as app:
        app.app = wait_until(
            lambda: tree.app_by_name("linux-regedit", live=True), timeout=10)
        wait_node(app.app, role="frame", timeout=8)
        time.sleep(1)
        wid2 = wait_until(lambda: _first_window_id(display), timeout=8,
                          message="restored window id not found")
        x, y, w, h = _window_geometry(display, wid2)
        assert w is not None and abs(w - 900) <= 40, f"width {w}"
        assert h is not None and abs(h - 620) <= 40, f"height {h}"
        assert x is not None and abs(x - 90) <= 120, f"x {x}"
        assert y is not None and abs(y - 110) <= 120, f"y {y}"


def test_window_state_restores_maximized(display, fake_roots):
    """重启后应恢复最大化状态。"""
    binary = (Path(__file__).resolve().parents[2] / "builddir" /
              "linux-regedit")
    env = dict(fake_roots["env"])

    def _launch(tag: str):
        e = dict(env, LR_TEST_APP_ID=f"org.linux-regedit.max-{tag}")
        return AppSession([str(binary)], env=e, app_name="linux-regedit",
                          display=display)

    wait_until(lambda: tree.app_by_name("linux-regedit") is None, timeout=8)
    with _launch("a") as app:
        app.app = wait_until(
            lambda: tree.app_by_name("linux-regedit", live=True), timeout=10)
        wait_node(app.app, role="frame", timeout=8)
        wid = wait_until(lambda: _first_window_id(display), timeout=8,
                         message="window id not found")
        _maximize_window(display, wid)
        time.sleep(1.5)
        assert _window_is_maximized(display, wid)
        quit_item = wait_node(app.app, name="Quit", role="menu item")
        quit_item.queryAction().doAction(0)
    wait_until(lambda: tree.app_by_name("linux-regedit") is None, timeout=8)

    with _launch("b") as app:
        app.app = wait_until(
            lambda: tree.app_by_name("linux-regedit", live=True), timeout=10)
        wait_node(app.app, role="frame", timeout=8)
        time.sleep(1)
        wid2 = wait_until(lambda: _first_window_id(display), timeout=8,
                          message="restored window id not found")
        assert _window_is_maximized(display, wid2)
