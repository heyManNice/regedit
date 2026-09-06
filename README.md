# linux-regedit

> Linux 版 regedit —— 用 **C + GTK3** 编写，在视觉与交互上模仿 Windows 注册表编辑器（`regedit.exe`）的**系统配置文件浏览器**。

本项目把 Windows `regedit` 的经典交互「左侧注册表树 + 右侧键值列表」映射到 Linux 世界：

| Windows 概念 | 本项目的映射 |
| --- | --- |
| `HKEY_LOCAL_MACHINE`（系统级配置） | `/etc` |
| `HKEY_CURRENT_USER`（用户级配置） | `~/.config` |
| `HKEY_SYSTEM_BOOT`（引导目录） | `/boot` |
| 注册表树（键/子键） | 文件系统目录树 |
| 注册表值（名称 / 类型 / 数据） | 解析后的每一条配置项 |
| 值类型（`REG_DWORD` / `REG_SZ` 等） | `Number` / `String` / `Boolean` |
| 值的启用/禁用 | 配置项是否被注释（`启用` 列） |
| — | 配置文件的注释以**备注**列展示 |

---

## ✨ 截图
<img src="images/preview.png" width="100%" alt="预览">

## ✨ 特性

- 🗂️ **「计算机」根 + 三个根键**：`HKEY_LOCAL_MACHINE`(`/etc`)、`HKEY_CURRENT_USER`(`~/.config`)、`HKEY_SYSTEM_BOOT`(`/boot`)，左侧树懒加载展示目录结构，同级目录以虚线连接。
- 🔍 **基于内容嗅探的格式识别**：Linux 配置无法仅凭后缀判断，程序读取文件内容猜测格式——`systemd 扩展名 → JSON → XML → apt 嵌套块 → INI 节 → key=value → 关键字-参数 → 未知`；`{` 开头判定为 JSON，`[` 开头需验证为合法 JSON 数组（否则按 INI 节处理）。
- 🧭 **表内查找**：`Edit → Find…` 或 `Ctrl+F` 打开查找对话框，`F3` 继续查找；按名称/数据/备注匹配（大小写不敏感），命中项自动选中并滚动，循环遍历。
- 🎛️ **类型强制显示**：右键配置项 → `Type` 可选择 `String / Boolean / Number` 强制显示，或选 `Detect automatically` 恢复自动识别（仅内存，不写盘）。
- 🗂️ **备份**：`File → Backup Current File...` 把当前文件复制为时间戳副本（`$XDG_DATA_HOME/linux-regedit/backups/`），不改动系统原文件。
- 💾 **保存（受限写回）**：`File → Save Changes...`（Ctrl+S）支持 KV/INI/systemd/TOML/keyword 与 apt 块内叶子赋值的**改值、软删除（注释掉）、重命名键**；JSON/XML 及其他不支持的操作保持只读并明确提示。
- 🔁 **文件对比**：`File → Compare with File...` 选择另一文件后用 `diff -u` 展示逐行差异（等宽、可复制视图）。
- 🧮 **智能类型识别**：自动区分配置值的类型——
  - `Number`（整数 / 浮点 / 十六进制，类比 `REG_DWORD`）
  - `String`（类比 `REG_SZ`）
  - `Boolean`（`true/false`、`yes/no`、`0/1`）
- ✅ **启用列**：最前显示 `true`/`false`。被注释的配置（如 `#Port 22`）识别为未启用，说明文字注释自动忽略。
- 💬 **备注列**：每个配置项显示**上方最近的一条注释**，一条注释可说明其下方多个配置项。
- 📖 **man 说明面板**：选中配置项时，底部面板查询 `man 5 <配置文件>` 并只显示该配置项的说明段落；未选中时面板自动隐藏。
- � **INI 节可展开**：带分节（`[xxx]`）的格式（INI / systemd unit / Evolution 数据源）将每个节显示为可展开的父行（`[节名]`），点击展开/收起该节下的配置项；无节的格式（键值对等）保持平铺列表。
- �📝 **文本兜底**：不支持的格式、以及以 shebang（`#!`）开头的脚本，均以纯文本视图打开。
- 🌲 **JSON 树形视图**：内容以 `{` 开头或验证为合法 JSON 数组的文件按 **JSON** 识别，以可展开的树形列表展示（对象/数组/数字/字符串/布尔/空值类型区分，数组元素带 `[下标]` 索引）。
- 🚀 **命令行打开文件**：`linux-regedit /path/to/file` 启动后直接定位到该文件并展示（树中自动逐级展开选中）。
- 🧹 **目录树过滤**：大于 128KB 的文件、非文本（二进制）文件、空文件夹不在树中显示。
- 🖱️ **交互细节**：双击目录展开/收起；树节点右键菜单（展开/折叠、复制项名称等）；表格各列可拖拽调节宽度；地址栏实时显示当前路径并可输入路径跳转。
- 🎨 **regedit 风格界面**：五个菜单（文件 / 编辑 / 查看 / 收藏夹 / 帮助）、地址栏、左右分栏 + 底部说明面板。

---

## 🖥️ 界面布局

```
┌─────────────────────────────────────────────────────────────────┐
│ 文件  编辑  查看  收藏夹  帮助                                 │
├─────────────────────────────────────────────────────────────────┤
│ ┌─────────────────────────────────────────────────────────────┐ │
│ │ /etc/systemd/system/（地址栏：显示/输入路径回车跳转）        │ │
│ └─────────────────────────────────────────────────────────────┘ │
├──────────────────────┬──────────────────────────────────────────┤
│  树视图（左侧）       │  配置项列表（右侧）                      │
│                      │  ┌──────┬───────────┬──────┬─────────┐ │
│  计算机              │  │ 启用 │ 名称      │ 类型 │  数据   │ │
│  ├─ HKEY_LOCAL_MACH  │  ├──────┼───────────┼──────┼─────────┤ │
│  │  └─ systemd       │  │ true │ Port      │Number│ 22      │ │
│  ├─ HKEY_CURRENT_USE │  │ false│ PermitR…  │String│ yes     │ │
│  ├─ HKEY_SYSTEM_BOOT │  └──────┴───────────┴──────┴─────────┘ │
│  └─ …                │  ────────────────────────────────────── │
│                      │  说明：PermitRootLogin（man 5 sshd_config）│
│                      │  被选中的配置项的 man 说明段落 …        │
└──────────────────────┴──────────────────────────────────────────┘
```

> 未选中表格行时底部说明面板隐藏；选中后显示并查询 man。

---

## 📦 支持的配置文件格式

> 均为「一行一条配置」的风格，天然符合「每一行配置变成一个可视化选项」的设计；其余格式（含 shebang 脚本）以文本编辑器兜底展示。

| 格式 | 语法要点 | 示例位置 |
| --- | --- | --- |
| **INI**（分节键值） | `[节]` + `key = value`；注释 `;` / `#` | `~/.config` 下大量应用配置 |
| **TOML** | `[表]` + `key = value`；`#` 注释；值自动去引号（复合值暂以文本展示） | `~/.config/**/*.toml` |
| **扁平 KeyValue** | `key=value` / `key value`，无分节 | `/etc/environment` 等 |
| **systemd unit** | `[Unit]/[Service]` 等节 + `Key=Value` | `/etc/systemd/system/` |
| **关键字-参数** | `关键字 参数`（空白/Tab 分隔），如 sshd_config | `/etc/ssh/sshd_config` |
| **JSON** | 以 `{` / `[` 开头；以可展开树形列表展示，不解析为配置行 | `~/.config/**/*.json` |
| **apt 配置** | 嵌套块 `Key { ... }` + `::` 命名空间键 + `;` 赋值；按路径逐级可展开 | `/etc/apt/apt.conf.d/` |
| **XML** | 嵌套元素树 + 属性；容器元素可展开，叶子元素为配置项，属性以 `@名` 展示 | `~/.config/**/*.xml` |

> JSON 采用树形视图（对象/数组/标量类型区分），不属于「一行一条配置」的列表风格，因此不使用启用/备注列。

> apt 配置的嵌套块（如 `Acquire::IndexTargets { deb::DEP-11 { ... } }`）按 `::` 路径逐级显示为可展开的行；列表值 `Key { "..."; }` 以 `[n]` 索引项展示。

> 识别到 `#Port 22` 这类被注释的配置时，会以「未启用」项展示；说明文字（长句）注释忽略。

> 格式识别采用**注册表模式**：每种格式是一个 driver（嗅探器 + 解析器），按优先级排列，新增格式只需追加一个条目；检测与解析都基于**一次性读取**的文件内容，不重复读盘。

---

## 🧩 数据类型

| 配置文件中的写法 | 识别类型 | 显示 | regedit 类比 |
| --- | --- | --- | --- |
| `123`、`3.14`、`0x1F` | 数字 | `Number` | `REG_DWORD` / `REG_QWORD` |
| `"..."` 或普通文本 | 字符串 | `String` | `REG_SZ` |
| `true/false`、`yes/no`、`on/off`、`1/0` | 布尔值 | `Boolean` | `REG_DWORD (0/1)` |
| `# ...` / `; ...` | 注释 | 备注列 | 上方最近一条注释 |
| JSON 值 | 对象 / 数组 / 数字 / 字符串 / 布尔 / 空 | `Object` / `Array` / `Number` / `String` / `Boolean` / `Null` | — |

---

## 🛠️ 构建与运行

### 依赖

- **GTK3**（`libgtk-3-dev`）
- **GLib / GIO**（通常随 GTK3 一并安装）
- **json-glib**（`libjson-glib-dev`，用于 JSON 解析）
- **Meson** ≥ 0.60 与 **Ninja**

Debian / Ubuntu（有管理员权限）：

```bash
sudo apt install build-essential libgtk-3-dev libjson-glib-dev meson ninja-build
```

无管理员权限时，可将 Meson / Ninja 安装到用户目录（如 `~/.local/bin`），并确保其加入 `PATH`；json-glib 同样可下载 `libjson-glib-dev` 的 `.deb` 后解压到 `~/.local/usr`（构建脚本会自动回退到该路径）。

### 编译

```bash
meson setup builddir
meson compile -C builddir
```

### 运行

```bash
./builddir/linux-regedit            # 浏览 /etc 与 ~/.config
./builddir/linux-regedit 路径/文件   # 启动并直接打开指定文件
./builddir/linux-regedit testdata/sample.json  # 快速预览 JSON 树形展示
```

### 测试

```bash
meson test -C builddir
```

> 单元测试除临时构造的输入外，还会读取 `testdata/` 下每种格式的**真实示例文件**并逐项断言解析结果（格式识别、类型、注释、启用状态等），作为回归保护；这些示例也可直接用 `linux-regedit testdata/<文件>` 在界面中人工预览。

### GUI / 无障碍回归（随本仓库维护）

`tests/gui/` 下是用 **AT-SPI（无障碍树）** 驱动真实界面的回归用例，不依赖截图：

```bash
meson setup builddir && meson compile -C builddir
python3 -m pytest tests/gui -v
```

运行前提：可用的 X/AT-SPI 会话（本地 `:2`，或 `xvfb-run`）；测试驱动库
[spire](https://github.com/heyManNice/spire)（通过 `SPIRE_PATH` 指定，或放在
仓库旁的 `../spire`）。用例固定走英文界面，保证在任何机器上可复现。

> ⚠️ 提示：`/etc` 下部分文件需要 root 权限才能读取，此时可用 `sudo` 运行以获得完整目录树。

---

## 📁 项目结构

```
linux-regedit/
├── meson.build              # Meson 构建定义（含单元测试）
├── src/
│   ├── main.c               # 程序入口
│   ├── app.c / app.h        # GTK 应用初始化与生命周期
│   ├── ui/
│   │   ├── main_window.c/h  # 主窗口：五个菜单 + 地址栏 + 左右分栏
│   │   ├── tree_pane.c/h    # 左侧文件树（计算机 + 三个根键，懒加载/右键/虚线）
│   │   ├── value_pane.c/h   # 右侧配置项表格 + 文本兜底 + man 说明面板（含结果缓存）
│   │   └── window_state.c/h # 窗口几何与上次路径的会话状态保存/恢复
│   ├── core/
│   │   ├── scanner.c/h      # 目录扫描（含超大/非文本/空目录过滤）
│   │   ├── format.c/h       # 格式注册表式识别与分发（driver 模式，内容嗅探）
│   │   ├── value.c/h        # 配置项模型与类型识别（含启用标记）
│   │   └── parsers/
│   │       ├── common.c/h   # 节 + 键值对共享解析逻辑（注释「上方最近一条」）
│   │       ├── ini.c/h      # INI 解析器
│   │       ├── kv.c/h       # 扁平 key=value 解析器
│   │       ├── systemd.c/h  # systemd unit 解析器
│   │       ├── keyword.c/h  # 关键字-参数解析器（sshd_config）
│   │       ├── json.c/h     # JSON 解析器（json-glib，树形展示）
│   │       ├── apt.c/h      # apt 配置解析器（嵌套块 + :: 路径）
│   │       └── xml.c/h      # XML 解析器（GMarkup，元素树 + 属性）
├── testdata/             # 各格式真实示例文件（单元测试 + 手动 GUI 验证）
└── tests/                # 单元测试（类型识别 / 解析器 / 目录扫描过滤）
```

---

## 🧭 Roadmap

- [x] v0.1：目录树扫描 + regedit 风格主界面（只读浏览）
- [x] v0.1：`INI` / 扁平 `KeyValue` / `systemd unit` 解析与类型识别
- [x] v0.2：`关键字-参数` 格式（sshd_config）、基于内容的格式嗅探
- [x] v0.2：备注「上方最近一条」、启用列、被注释配置识别
- [x] v0.2：man 说明面板、目录树过滤（超大/非文本/空目录）、shebang 文本兜底
- [x] v0.3：`JSON` 结构化格式支持（树形可展开列表）
- [x] v0.3：搜索 / 查找（当前配置文件内，Ctrl+F / F3）
- [x] v0.3：类型强制显示（右键 Type 菜单 / 自动识别）
- [ ] v0.4：编辑配置并**写回文件**（含权限处理与安全校验），支持 `sudo` 提权流程 —— [安全设计文档](docs/design/write-back.md)
- [x] v0.5：`TOML` 结构化格式支持（表 + 键值对行模型，复合值暂以文本展示）
- [x] v0.6：备份当前文件（时间戳副本）与文件对比（diff -u 视图）

---

## 📜 许可

本项目使用 **GPL-3.0** 许可，详见 `LICENSE`。

---

*这是 Linux 生态的「配置即注册表」——把散落在 `/etc`、`~/.config` 与 `/boot` 中的文本配置，以熟悉且友好的方式呈现出来。*
