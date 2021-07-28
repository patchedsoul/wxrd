project('wxrd', 'c', version: '0.16.0',
        meson_version: '>= 0.46.0',
        default_options : [
          'c_std=gnu11',
          'warning_level=1',
        ],
)

project_args = []

compiler = meson.get_compiler('c')
compiler_id = compiler.get_id()

add_project_arguments(
  compiler.get_supported_arguments([
    '-DWLR_USE_UNSTABLE',
  ]),
  language: 'c'
)

warning_flags = [
  '-Weverything',
  '-Wno-reserved-id-macro',
  '-Wno-documentation',
  '-Wno-documentation-unknown-command',
  '-Wno-padded',
  '-Wno-overlength-strings',
  '-Wno-disabled-macro-expansion',
  '-Wno-atomic-implicit-seq-cst',
  '-Wno-float-equal',
  '-Wno-used-but-marked-unused',
  '-Wno-assign-enum',
  '-Wno-gnu-folding-constant',
  '-Wno-cast-qual', # G_DEFINE_TYPE produces this
  '-Wno-covered-switch-default',
  '-Wno-unused-parameter',
]

foreach w : warning_flags
  if compiler.has_argument(w)
    project_args += w
  else
  endif
endforeach

# Paths
prefix = get_option('prefix')
libdir = join_paths(prefix, get_option('libdir'))
includedir = join_paths(prefix, get_option('includedir'))
datadir = join_paths(prefix, get_option('datadir'))
bindir = join_paths(prefix, get_option('bindir'))
pkgdatadir = join_paths(datadir, meson.project_name())
desktopdir = join_paths(datadir, 'applications')
icondir = join_paths(datadir, 'icons')

src_inc = include_directories('src')

### Dependencies
xrdesktop_dep = dependency('xrdesktop-0.16', include_type: 'system')
wayland_client_dep = dependency('wayland-client')
wayland_server_dep = dependency('wayland-server')
wayland_protocols_dep = dependency('wayland-protocols')
glesv2_dep = dependency('glesv2')
xkbcommon_dep = dependency('xkbcommon')
egl_dep = dependency('egl')

# Try first to find wlroots as a subproject, then as a system dependency
wlroots_version = ['>=0.15.0']
wlroots_proj = subproject(
  'wlroots',
  default_options: ['examples=false', 'default_library=static'],
  required: false,
  version: wlroots_version,
)
if wlroots_proj.found()
  wlroots_dep = wlroots_proj.get_variable('wlroots')
else
  wlroots_dep = dependency('wlroots', version: wlroots_version)
endif

# generate xdg-shell-protocol.h
wl_protos_src = []
wl_protos_headers = []

wayland_scanner_dep = dependency('wayland-scanner', required: false, native: true)
if wayland_scanner_dep.found()
  wayland_scanner = find_program(
    wayland_scanner_dep.get_pkgconfig_variable('wayland_scanner'),
    native: true,
  )
else
  wayland_scanner = find_program('wayland-scanner', native: true)
endif

wayland_protos = dependency('wayland-protocols', version: '>=1.14')
wl_protocol_dir = wayland_protos.get_pkgconfig_variable('pkgdatadir')

protocols = [
  [wl_protocol_dir, 'stable/xdg-shell/xdg-shell.xml'],
]

foreach p : protocols
  xml = join_paths(p)
  wl_protos_src += custom_target(
    xml.underscorify() + '_server_c',
    input: xml,
    output: '@BASENAME@-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )
  wl_protos_headers += custom_target(
    xml.underscorify() + '_server_h',
    input: xml,
    output: '@BASENAME@-protocol.h',
    command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
  )
endforeach

subdir('src')

