"""linux-regedit GUI regression tests (AT-SPI only, no screenshots).

运行前提：
  1. meson 构建出 builddir/linux-regedit
  2. spire 测试库可用（SPIRE_PATH 或 ../spire 相邻目录）
  3. 一个可用的 X/AT-SPI 会话（本地 :2，或 xvfb-run）
"""

from __future__ import annotations

from spire import tree
from spire.input import double_click, click, press, type_text
from spire.wait import wait_until, wait_node


def expand_row(app, name: str):
    row = wait_node(app.app, name=name, role="table cell")
    double_click(row)
    return row


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
