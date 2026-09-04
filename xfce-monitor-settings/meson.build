project(
  'xfce-monitor-settings',
  'c',
  version: '0.1.0',
  meson_version: '>= 0.54.0',
  default_options: ['c_std=c99'],
)

add_project_arguments('-Wno-deprecated-declarations', language: 'c')

glib = dependency('glib-2.0', version: '>= 2.50')
gtk = dependency('gtk+-3.0')
x11 = dependency('x11')
xrandr = dependency('xrandr')
xfconf = dependency('libxfconf-0')
m = meson.get_compiler('c').find_library('m', required: false)

executable(
  'xfce4-monitor-settings',
  'src/monitor-settings.c',
  dependencies: [glib, gtk, x11, xrandr, xfconf, m],
  install: true,
)

install_data(
  'data/xfce4-monitor-settings.desktop',
  install_dir: get_option('datadir') / 'applications',
)
