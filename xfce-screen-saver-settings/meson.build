project(
  'xfce-screen-saver-settings',
  'c',
  version: '0.1.0',
  meson_version: '>= 0.54.0',
  default_options: ['c_std=c99'],
)

add_project_arguments(
  [
    '-DG_LOG_DOMAIN="xfce4-screen-saver-settings"',
    '-Wno-deprecated-declarations',
  ],
  language: 'c',
)

glib = dependency('glib-2.0', version: '>= 2.50')
gtk = dependency('gtk+-3.0')
libxfce4ui = dependency('libxfce4ui-2')
xfconf = dependency('libxfconf-0')

inc = include_directories('.')

executable(
  'xfce4-screen-saver-settings',
  'src/settings.c',
  include_directories: inc,
  dependencies: [glib, gtk, libxfce4ui, xfconf],
  install: true,
)

executable(
  'xfce4-screen-saver-apply',
  'src/apply.c',
  include_directories: inc,
  dependencies: [glib, xfconf],
  install: true,
)

# Settings Manager registration
#
# Only /usr/bin is guaranteed on the X11 session PATH (/bin:/usr/bin);
# everything else — /usr/local/bin, ~/.local/bin — is invisible to XDG
# launchers and autostart, so the Exec must be an absolute path.
bindir_abs = join_paths(get_option('prefix'), get_option('bindir'))
if bindir_abs == '/usr/bin'
  exec_settings = 'xfce4-screen-saver-settings'
  exec_apply = 'xfce4-screen-saver-apply'
else
  exec_settings = join_paths(bindir_abs, 'xfce4-screen-saver-settings')
  exec_apply = join_paths(bindir_abs, 'xfce4-screen-saver-apply')
endif

desktop_conf = configuration_data()
desktop_conf.set('EXEC_SETTINGS', exec_settings)
desktop_conf.set('EXEC_APPLY', exec_apply)

settings_desktop = configure_file(
  input: 'data/xfce4-screen-saver-settings.desktop.in',
  output: 'xfce4-screen-saver-settings.desktop',
  configuration: desktop_conf,
)

apply_desktop = configure_file(
  input: 'data/xfce4-screen-saver-apply.desktop.in',
  output: 'xfce4-screen-saver-apply.desktop',
  configuration: desktop_conf,
)

install_data(
  settings_desktop,
  install_dir: join_paths(get_option('datadir'), 'applications'),
)

# Autostart applier: reapplies the saved screen-saver state at logon
install_data(
  apply_desktop,
  install_dir: join_paths(get_option('sysconfdir'), 'xdg/autostart'),
)

# Icon (hicolor)
install_data(
  'data/icons/xfce4-screen-saver.svg',
  install_dir: join_paths(get_option('datadir'), 'icons/hicolor/scalable/apps'),
)
icon_sizes = ['48x48', '128x128', '256x256']
foreach size : icon_sizes
  install_data(
    join_paths('data/icons', size, 'xfce4-screen-saver.png'),
    install_dir: join_paths(get_option('datadir'), 'icons/hicolor', size, 'apps'),
  )
endforeach

# Companion scripts (modern high-res slideshow + wallpaper apply + video loop)
fixed_mode = 'rwxr-xr-x'
install_data(
  'scripts/wallpaper-apply',
  install_dir: get_option('bindir'),
  install_mode: fixed_mode,
)
install_data(
  'scripts/make-wallpaper-slideshow',
  install_dir: get_option('bindir'),
  install_mode: fixed_mode,
)
install_data(
  'scripts/saver-videoloop',
  install_dir: get_option('bindir'),
  install_mode: fixed_mode,
)