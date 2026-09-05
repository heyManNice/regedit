"""linux-regedit GUI regression tests (AT-SPI only, no screenshots).

运行前提：
  1. meson 构建出 builddir/linux-regedit
  2. spire 测试库可用（SPIRE_PATH 或 ../spire 相邻目录）
  3. 一个可用的 X/AT-SPI 会话（本地 :2，或 xvfb-run）
"""

from __future__ import annotations

import pytest

from spire import tree
from spire.input import double_click, click, press, type_text
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
    """Static surface: menus, named panes and disabled actions."""
    app = regedit.app
    assert wait_node(app, role="frame", name="Regedit")

    assert wait_node(app, name="Address bar input", role="text",
                     state="EDITABLE")
    assert wait_node(app, name="Directory tree", role="tree table")
    assert wait_node(app, name="Value panel", role="split pane")

    for menu in ("File", "Edit", "View", "Favorites", "Help"):
        assert wait_node(app, name=menu, role="menu"), f"missing menu {menu}"

    # 查看 → 地址栏 是勾选状态
    loc = wait_node(app, name="Address", role="check menu item")
    assert tree.has_state(loc, "CHECKED")

    # 只读阶段的禁用菜单项
    assert tree.has_state(wait_node(app, name="Permission", role="menu item"),
                          "DISABLED")
    assert tree.has_state(wait_node(app, name="Delete", role="menu item"),
                          "DISABLED")


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
    """View → Address 勾选切换应隐藏/恢复地址栏。

    注意：隐藏后 AT-SPI 节点仍然存在，必须用 SHOWING 状态判断可见性。
    """
    app = regedit.app
    assert wait_node(app, name="Address bar input", role="text",
                     state="SHOWING")

    menu_item_activate(app, "View", "Address")
    wait_until(lambda: tree.find(app, name="Address bar input", role="text",
                                 state="SHOWING") is None,
               timeout=5, message="address bar still SHOWING after toggle")

    menu_item_activate(app, "View", "Address")
    wait_node(app, name="Address bar input", role="text", state="SHOWING",
              timeout=5)


def test_snapshot_order_is_stable(regedit):
    """两次抓取无障碍树快照的结构与顺序应一致。"""
    snap1 = tree.snapshot(regedit.app, max_depth=30, max_nodes=300)
    snap2 = tree.snapshot(regedit.app, max_depth=30, max_nodes=300)
    shape = lambda s: [(i["role"], i["name"]) for i in s]
    assert shape(snap1) == shape(snap2)
