# Xfce Extensions

A monorepo for locally developed Xfce extensions and desktop integrations.

## Projects

- `xfce-terminal-carousel` — idle terminal activity carousel and plasma overlay.
- `xfce-screen-saver-settings` — modern high-res screensaver for XFCE: Settings-dialog + xset/ xfconf applier, XScreenSaver launcher, wallpaper slideshow generator with random PowerPoint-style transitions (ffmpeg xfade), reliable wallpaper-apply script, and a video-loop saver wrapper. Needs `xscreensaver*` + `mpv` + `ffmpeg` installed. **Fresh install:** `cd xfce-screen-saver-settings && ./install.sh --deps` installs deps, builds, and wires the config.
- `xfce-monitor-settings` — external-monitor brightness and night-mode controls.

Each project keeps its own build files, tests, and documentation while sharing
this repository's Git history.
