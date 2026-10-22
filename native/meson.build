project(
  'dots-native',
  'c',
  version: '0.1.0',
  license: 'GPL-3.0-or-later',
  meson_version: '>=1.2.0',
  default_options: [
    'c_std=gnu11',
    'warning_level=2',
    'werror=false',
    'buildtype=debugoptimized',
  ],
)

cc = meson.get_compiler('c')

wayland_client = dependency('wayland-client')
cairo = dependency('cairo')
pangocairo = dependency('pangocairo')
json_c = dependency('json-c')
math = cc.find_library('m', required: false)

src_incs = include_directories('src')
incs = include_directories(
  'src',
  'protocols',
)

# ---------------------------------------------------------------------------
# Shared low-level code: logging, XDG paths, config, Unix-socket IPC.
# ---------------------------------------------------------------------------
dots_common = static_library(
  'dots-common',
  [
    'src/common/log.c',
    'src/common/paths.c',
    'src/common/ipc.c',
    'src/common/config.c',
  ],
  include_directories: src_incs,
  dependencies: [json_c],
)

protocol_sources = files(
  'protocols/wlr-layer-shell-protocol.c',
  'protocols/xdg-shell-protocol.c',
)

# ---------------------------------------------------------------------------
# dots-bar
#
# Tahap awal: source masih berupa hasil port cbar. Ia sengaja dibangun sebagai
# executable terpisah supaya bisa dites tanpa mengganggu Quickshell. Setelah
# fondasi `dotsd`/`dots-shell` siap, surface bar akan dipindahkan ke sana.
# ---------------------------------------------------------------------------
dots_bar = executable(
  'dots-bar',
  [
    'src/ui/bar.c',
    protocol_sources,
  ],
  include_directories: incs,
  link_with: dots_common,
  dependencies: [wayland_client, cairo, pangocairo, json_c, math],
  install: true,
)

# ---------------------------------------------------------------------------
# dotsd / dots-ctl — fondasi IPC. Layanan sistem menyusul di Fase 2.
# ---------------------------------------------------------------------------
dotsd = executable(
  'dotsd',
  'src/daemon/dotsd.c',
  include_directories: src_incs,
  link_with: dots_common,
  dependencies: [json_c],
  install: true,
)

dots_ctl = executable(
  'dots-ctl',
  'src/ctl/dots-ctl.c',
  include_directories: src_incs,
  link_with: dots_common,
  dependencies: [json_c],
  install: true,
)

# Aset dibaca relatif terhadap executable saat dijalankan dari build dir.
# Salin ke build dir supaya `meson compile` langsung menghasilkan layout yang
# bisa dijalankan tanpa install.
foreach asset : ['kitty.png', 'brave.png', 'dolphin.png', 'spark_icon.png']
  configure_file(
    input: 'assets' / asset,
    output: asset,
    copy: true,
  )
endforeach

install_data(
  [
    'assets/kitty.png',
    'assets/brave.png',
    'assets/dolphin.png',
    'assets/spark_icon.png',
  ],
  install_dir: get_option('datadir') / 'dots' / 'assets',
)

test(
  'ipc-smoke',
  find_program('tests/test-ipc.sh'),
  env: ['BUILD_DIR=' + meson.current_build_dir()],
  timeout: 20,
)

summary({
  'wayland-client': wayland_client.version(),
  'cairo': cairo.version(),
  'pangocairo': pangocairo.version(),
  'json-c': json_c.version(),
}, section: 'dependencies')
