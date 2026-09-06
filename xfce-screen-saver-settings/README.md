# xfce-screen-saver-settings

**Give XFCE a modern, high-resolution screen saver — and a settings UI to
control it — on a desktop (XFCE 4.18 / Debian 12) that ships with neither.**

## What this package is for (the problem)

- **XFCE 4.18 has no screen saver UI at all.** The only screensaver hooks in
  a stock XFCE session are the X server's raw blanking (`xset s`) and the
  optional third-party **XScreenSaver**. Debian 12 additionally does not
  package `xfce4-screensaver`.
- The X11 blanking that *is* on by default answers to `xset` but has no
  graphical settings, and it has no animated savers, no locking, and no
  fun.
- Result: "wallpapers / screen saver don't apply" and "there is no way to
  configure a real screensaver".

This package closes that gap the way XFCE itself would: an idiomatic
**Settings Manager dialog** (XfceTitledDialog + libxfce4ui-2) that reads and
writes a dedicated **xfconf channel** and applies changes **live** with
`xset`, plus the tools to make the saver **modern and high-resolution**
(an ffmpeg-built animated slideshow of your own wallpapers with
PowerPoint-style random transitions).

## Fresh install (new machine)

One command installs everything and wires it into the desktop:

```bash
git clone <repo-url> && cd xfce-screen-saver-settings
./install.sh --deps     # apt deps via sudo, then build + install + configure
```

What it does:

- **`--deps`** installs any missing Debian packages (`xscreensaver*`, `mpv`,
  `ffmpeg`, `meson`, `ninja`, `gcc`, dev headers) via `sudo apt-get`.
- Builds with meson/ninja and installs to `~/.local` (or `--prefix DIR`).
- Wires the runtime config (idempotent — safe to re-run):
  - xfconf channel `xfce4-screen-saver` defaults (`/enabled` true,
    `/timeout` 600) — only set if absent,
  - `~/.xscreensaver` — only created if absent, pointing
    `imageDirectory:` at `~/Pictures/wallpapers/`,
  - autostart entries for the **xscreensaver daemon** and the
    **xset applier** in `~/.config/autostart`,
  - creates `~/Pictures/wallpapers/` and `~/Videos/screensavers/`.

Re-running is safe: existing config is kept, changed code is rebuilt, nothing
duplicated. A fresh box needs exactly that one command.

## What it installs

| Artifact                        | Purpose                                                              |
|---------------------------------|----------------------------------------------------------------------|
| `xfce4-screen-saver-settings`   | Settings dialog → **Settings → All Settings → System → Screen Saver** |
| `xfce4-screen-saver-apply`      | Autostart helper: re-applies saved `xset s` state at every logon      |
| `wallpaper-apply`               | Set/rotate the XFCE wallpaper reliably (all monitors, all workspaces) |
| `make-wallpaper-slideshow`      | Render `~/Pictures/wallpapers` into an mp4 with **random transitions**|
| `saver-videoloop`               | XScreenSaver "saver" that plays a loop/mp4 fullscreen (embedded)      |
| Settings Manager entry          | `data/xfce4-screen-saver-settings.desktop` (`X-XfcePluggable`)        |
| Autostart entry                 | `data/xfce4-screen-saver-apply.desktop`                               |
| Icons                           | hicolor 48/128/256 + scalable                                        |

## Settings storage

xfconf channel **`xfce4-screen-saver`**, applied to the live X server on
every change:

| Key         | Type  | Default | Meaning                                 |
|-------------|-------|---------|-----------------------------------------|
| `/enabled`  | bool  | true    | blanking after inactivity on/off        |
| `/timeout`  | uint  | 600     | seconds of inactivity before blanking   |

```bash
xfconf-query -c xfce4-screen-saver -p /timeout -s 900    # 15 minutes
xfconf-query -c xfce4-screen-saver -p /enabled -s false  # disable blanking
```

## The modern, high-resolution saver (the fun part)

XScreenSaver's built-in **GL Slideshow** plays your wallpapers as a full screen
photo saver (fade + pan/zoom), driven by the *image source* you point it at:

1. `xscreensaver-demo` → **Advanced → Image Manipulation → Choose Random
   Image** → source = `/home/rahul/Pictures/wallpapers` (writes
   `imageDirectory:` in `~/.xscreensaver`).
2. **Display Modes** → choose **GL Slideshow** → set **Mode: One** → OK.

For **PowerPoint-style random transitions** (fade, slide left/right, up/down,
wipe, circle, smooth, zoom) instead of just fades, build the video slideshow
with the included generator. It renders your wallpaper folder into
`~/Videos/screensavers/wallpaper-slideshow.mp4`, choosing a **random
transition at every cut**, and the screensaver wrapper (`saver-videoloop`)
prefers that clip automatically:

```bash
# renders in resolution order you like; defaults 1920x1080, 4s per image
make-wallpaper-slideshow
SLIDESHOW_SCALE=3840x2160 SLIDESHOW_DUR=5 make-wallpaper-slideshow   # 4K version

# preview it right now from the settings dialog, or:
xscreensaver-command -activate
```

Any additional video you drop in `~/Videos/screensavers/` joins the rotation.

## The reliable wallpaper fix

`wallpaper-apply` fixes the classic XFCE "wallpaper won't apply" problems:
it writes the image to **every monitor namespace** xfdesktop consults
(`monitor0/1` numeric and `monitorDP-1-0`/`monitorHDMI-1-0` named), sets
`image-path` to your folder, and reloads with the **exact** display string
the session uses (`:0.0`):

```bash
wallpaper-apply                                  # next random wallpaper
wallpaper-apply ~/Pictures/wallpapers/378568.jpg # a specific image
```

Common cause of broken wallpapers on this machine: images pointed at the
unmounted `/mnt/samba_share/…`. Keep wallpaper paths local.

## Building, installing, testing

```bash
meson setup build --prefix "$HOME/.local"     # or /usr for system-wide
ninja -C build && ninja -C build install
bash tests/settings-smoke.sh                  # Xvfb + private dbus + throwaway HOME
```

The smoke test never touches your real channel, your real X session, or your
real screen saver.

## Where things land (user install)

```
~/.local/bin/xfce4-screen-saver-settings, -apply, wallpaper-apply, ...
~/.local/share/applications/xfce4-screen-saver-settings.desktop   (Settings Manager)
~/.config/autostart/xfce4-screen-saver-apply.desktop + xscreensaver.desktop
~/.local/share/icons/hicolor/{48,128,256}x*/apps, scalable/apps
~/.xscreensaver          (XScreenSaver settings, incl. imageDirectory + mode)
~/Pictures/wallpapers    (your wallpaper collection — the saver's image source)
~/Videos/screensavers    (generated slideshow mp4 + any video loops)
```

## Design notes (why it's a standalone package)

By design **not** part of `xfce-terminal-carousel` — that package is for
xfce4-terminal concerns. Screen-saver/blanking policy is desktop-level and
lives in its own component, exactly like the carousel lives in its own.
Everything here is X11-focused (`xset`, XScreenSaver, xfconf); a future
Wayland backend would live in the same package under a different mechanism.