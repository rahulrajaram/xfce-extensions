project(
  'xfce4-terminal-carousel',
  'c',
  version: '0.1.0',
  meson_version: '>= 0.54.0',
  default_options: ['c_std=c99'],
)

add_project_arguments(
  [
    '-DG_LOG_DOMAIN="xfce4-terminal-carousel"',
    '-Wno-deprecated-declarations',
  ],
  language: 'c',
)

glib = dependency('glib-2.0', version: '>= 2.50')
gtk = dependency('gtk+-3.0')
gtk4 = dependency('gtk4', version: '>= 4.8')
x11 = dependency('x11')
xrandr = dependency('xrandr')
xscrnsaver = dependency('xscrnsaver')
xfconf = dependency('libxfconf-0', required: false)
cc = meson.get_compiler('c')
m = cc.find_library('m', required: false)

conf = configuration_data()
conf.set_quoted('GETTEXT_PACKAGE', meson.project_name())
configure_file(
  input: 'config.h.in',
  output: 'config.h',
  configuration: conf,
)

inc = include_directories('.', 'src')

executable(
  'xfce4-terminal-carousel',
  'src/main.c',
  include_directories: inc,
  dependencies: [glib, gtk, x11, xscrnsaver, xfconf],
  install: true,
)

executable(
  'xfce4-terminal-plasma',
  'src/plasma.c',
  include_directories: inc,
  dependencies: [glib, gtk4, x11, xscrnsaver, xfconf, m],
  install: true,
)

executable(
  'xfce4-monitor-settings',
  'src/monitor-settings.c',
  include_directories: inc,
  dependencies: [glib, gtk, x11, xrandr, xfconf, m],
  install: true,
)

install_data(
  'data/org.xfce.Terminal.Carousel.desktop',
  install_dir: get_option('sysconfdir') / 'xdg/autostart',
)

install_data(
  'data/xfce4-monitor-settings.desktop',
  install_dir: get_option('datadir') / 'applications',
)
