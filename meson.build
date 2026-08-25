project('linux-regedit', 'c',
  version: '0.1.0',
  license: 'GPL-3.0-or-later',
  meson_version: '>=0.60',
  default_options: ['c_std=c11', 'warning_level=3', 'buildtype=debugoptimized'])

i18n = import('i18n')

add_project_arguments('-DGETTEXT_PACKAGE="' + meson.project_name() + '"',language: 'c')

glibdep = dependency('glib-2.0')
giodep = dependency('gio-2.0')
gtkdep = dependency('gtk+-3.0')

# json-glib：优先 pkg-config；无系统权限时回退到用户目录安装（~/.local/usr）
jsondep = dependency('json-glib-1.0', required: false)
if not jsondep.found()
  home_dir = run_command('sh', '-c', 'echo "$HOME"',
                         check: false).stdout().strip()
  jsondep = declare_dependency(
    include_directories: include_directories(
      home_dir + '/.local/usr/include/json-glib-1.0'),
    link_args: ['-L' + home_dir + '/.local/usr/lib/x86_64-linux-gnu',
                '-ljson-glib-1.0'],
  )
endif

# 核心库：纯 GLib，不含 UI，便于单元测试
lr_core_sources = files(
  'src/core/value.c',
  'src/core/format.c',
  'src/core/scanner.c',
  'src/core/parsers/common.c',
  'src/core/parsers/ini.c',
  'src/core/parsers/kv.c',
  'src/core/parsers/systemd.c',
  'src/core/parsers/keyword.c',
  'src/core/parsers/json.c',
  'src/core/parsers/apt.c',
  'src/core/parsers/xml.c',
)

lr_core_inc = include_directories('src')

lr_core = static_library('lr_core', lr_core_sources,
  dependencies: [glibdep, giodep, jsondep],
  include_directories: lr_core_inc,
)

# 主程序
executable('linux-regedit',
  'src/main.c',
  'src/app.c',
  'src/ui/dialog_utils.c',
  'src/ui/favorites.c',
  'src/ui/export.c',
  'src/ui/main_window.c',
  'src/ui/tree_pane.c',
  'src/ui/value_pane.c',
  'src/ui/window_state.c',
  link_with: lr_core,
  dependencies: [gtkdep, giodep, jsondep],
  include_directories: lr_core_inc,
  install: true,
)

# 单元测试
if get_option('tests')
  test_runner = executable('test_runner',
    'tests/test_value.c',
    'tests/test_parsers.c',
    'tests/test_scanner.c',
    'tests/test_runner.c',
    link_with: lr_core,
    dependencies: [glibdep, giodep, jsondep],
    include_directories: [lr_core_inc, include_directories('tests')],
  )
  # 在源码根目录运行测试，便于以相对路径读取 testdata/ 下的真实示例文件
  test('core-tests', test_runner,
       workdir: meson.project_source_root())
endif

subdir('po')
