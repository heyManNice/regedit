"""pytest fixtures: fake roots + managed linux-regedit session.

这些 GUI 回归用例属于 linux-regedit 仓库；测试驱动库 spire 通过
`SPIRE_PATH` 环境变量或默认的相邻目录（../spire）提供。
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]  # linux-regedit 仓库根
REGEDIT = ROOT
SPIRE_FALLBACK = Path(os.environ.get("SPIRE_PATH",
                                     str(ROOT.parent / "spire")))

try:
    import spire  # noqa: F401  (已安装时直接用)
except ImportError:
    sys.path.insert(0, str(SPIRE_FALLBACK))

from spire import input as inp
from spire import tree
from spire.session import AppSession, find_display_env
from spire.tree import find
from spire.wait import wait_node, wait_until

REGEDIT_BIN = REGEDIT / "builddir" / "linux-regedit"
SAMPLES = REGEDIT / "testdata"


def kill_regedit():
    """Ensure no lingering single-instance primary survives between tests."""
    subprocess.run(["pkill", "-x", "linux-regedit"], check=False)


def pytest_configure(config):
    config.addinivalue_line(
        "markers", "ui: desktop UI regression test (requires X session)")


@pytest.fixture(scope="session")
def display() -> str:
    return (os.environ.get("SPIRE_DISPLAY")
            or os.environ.get("DISPLAY", ":2"))


@pytest.fixture(scope="session")
def session_env(display) -> dict:
    return find_display_env(display)


@pytest.fixture(scope="session")
def fake_roots(tmp_path_factory, session_env) -> dict:
    """Build a deterministic fake /etc, ~/.config and /boot."""
    base = tmp_path_factory.mktemp("lr-fake")
    etc = base / "etc"
    config = base / "config"
    boot = base / "boot"
    xdg = base / "xdg"
    data = base / "data"
    runtime = base / "runtime"
    for d in (etc, config, boot, xdg, data, runtime):
        d.mkdir(parents=True)
    copy_map = {
        "sample.ini": etc,
        "sample.json": config,
        "sample.service": etc,
        "sample.toml": config,
        "sample.sshd_config": etc,
        "sample-apt.conf": etc,
        "sample.xml": config,
        "sample.environment.conf": etc,
        "sample.unknown.txt": config,
        "sample-script.sh": etc,
        "sample-evolution.source": etc,
    }
    samples = {}
    for name, dest_dir in copy_map.items():
        shutil.copy(SAMPLES / name, dest_dir / name)
        samples[name] = dest_dir / name
    return {
        "env": {
            "LR_TEST_ETC": str(etc),
            "LR_TEST_CONFIG": str(config),
            "LR_TEST_BOOT": str(boot),
            "XDG_CONFIG_HOME": str(xdg),
            "XDG_DATA_HOME": str(data),
            "XDG_RUNTIME_DIR": str(runtime),
            "HOME": os.environ.get("HOME", "/root"),
            # 每个测试会话独立 GApplication 实例，避免单实例串台
            "LR_TEST_APP_ID": f"org.linux-regedit.test-{os.getpid()}",
            # 回归测试固定走英文界面（源语言回退，无需安装 locale/翻译）
            "LANG": "C.UTF-8",
            "LC_ALL": "C.UTF-8",
            "LANGUAGE": "C",
        },
        "etc": etc,
        "config": config,
        "boot": boot,
        "sample_ini": etc / "sample.ini",
        "samples": samples,
        "data_home": data,
        "runtime_home": runtime,
    }


@pytest.fixture
def regedit(fake_roots, display, tmp_path):
    assert REGEDIT_BIN.exists(), f"build first: {REGEDIT_BIN}"
    kill_regedit()
    # 等 AT-SPI 注册表彻底清空旧实例，避免抓到“将死”的旧句柄
    wait_until(lambda: tree.app_by_name("linux-regedit") is None,
               timeout=10, interval=0.2,
               message="previous instance still in AT-SPI registry")
    with AppSession([str(REGEDIT_BIN)], env=fake_roots["env"],
                    app_name="linux-regedit", display=display,
                    log_path=str(tmp_path / "lr.log")) as app:
        try:
            wait_node(app.app, role="frame", timeout=10)
            wait_until(lambda: inp.activate_window(
                           wm_class="linux-regedit", name="Regedit",
                           display=display),
                       timeout=5, message="window not mappable")
            # 窗口已属于新实例后重新绑定句柄，避免旧 AT-SPI 对象残留
            app.app = tree.app_by_name("linux-regedit", live=True)
            inp.move_window(0, 100, wm_class="linux-regedit",
                            name="Regedit", display=display)
            yield app
        finally:
            kill_regedit()
    # 运行日志不得出现 GLib/应用错误级输出（抓 GTK 误用与崩溃前兆）
    log = tmp_path / "lr.log"
    if log.exists():
        bad = [ln for ln in log.read_text(errors="replace").splitlines()
               if "CRITICAL" in ln or "GLib-ERROR" in ln or "ERROR" in ln]
        assert not bad, f"app log contains error-level lines:\n" + \
            "\n".join(bad)


@pytest.hookimpl(tryfirst=True, hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    if call.when == "call" and outcome.get_result().failed:
        shot = f"/tmp/lr-gui-fail-{item.name}.png"
        env = dict(os.environ,
                   DISPLAY=os.environ.get("SPIRE_DISPLAY",
                                          os.environ.get("DISPLAY", ":2")))
        subprocess.run(["import", "-window", "root", shot], env=env,
                       check=False)
        print(f"\n[failure screenshot] {shot}")
