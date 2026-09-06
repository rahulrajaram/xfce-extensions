#!/usr/bin/env bash
# install.sh — put this package on a (fresh) machine and wire it in.
#
# On a brand-new XFCE box this is the whole journey:
#
#   git clone <this repo> && cd xfce-screen-saver-settings
#   ./install.sh --deps          # install missing apt build+runtime deps (sudo)
#                                # then builds, installs, configures
#
# What it does, in order:
#   1. Checks build deps (meson/ninja/gcc/pkg-config + gtk/libxfce4ui/xfconf
#      dev headers) and runtime deps (xscreensaver*, mpv, ffmpeg, xset).
#      With --deps on a Debian-family machine it installs the missing ones
#      via sudo apt-get.
#   2. Builds with meson/ninja and installs to $PREFIX (default ~/.local).
#   3. Wires the runtime configuration (safe, idempotent):
#        - xfconf channel  xfce4-screen-saver  /enabled, /timeout  (if unset)
#        - ~/.xscreensaver (only if absent) — mode, fade, imageDirectory
#          pointed at ~/Pictures/wallpapers/
#        - ~/.config/autostart/xscreensaver.desktop      (saver daemon at logon)
#        - ~/.config/autostart/xfce4-screen-saver-apply.desktop (applies xset)
#        - creates ~/Pictures/wallpapers/ and ~/Videos/screensavers/
#
# Re-running is safe (idempotent): skips what already exists, rebuilds changed
# code, re-installs. A fresh box needs exactly this one command.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
PREFIX="${PREFIX:-${HOME:-$(id -un)}/.local}"
AUTO_DEPS=0
DO_CONFIG=1

usage() {
  sed -n '2,28p' "$0"
  echo
  echo "Usage: $0 [options]"
  echo "  --prefix DIR   install to DIR (default: ~/.local)"
  echo "  --deps         install missing apt dependencies (Debian family, sudo)"
  echo "  --no-config    skip runtime configuration wiring"
  echo "  --help         this help"
}

while [ $# -gt 0 ]; do
  case "$1" in
    --prefix) PREFIX="$2"; shift 2 ;;
    --deps) AUTO_DEPS=1; shift ;;
    --no-config) DO_CONFIG=0; shift ;;
    --help|-h) usage; exit 0 ;;
    *) echo "unknown option: $1"; usage; exit 2 ;;
  esac
done

BINDIR="$PREFIX/bin"
is_debian() { [ -f /etc/debian_version ]; }
has() { command -v "$1" >/dev/null 2>&1; }
pc() { pkg-config --exists "$1" 2>/dev/null; }

echo "== xfce-screen-saver-settings installer =="
echo "   prefix:     $PREFIX"
echo "   build dir:  $BUILD_DIR"

# ---------------------------------------------------------------- deps
BUILD_TOOLS=(meson ninja cc pkg-config)
BUILD_PC=(gtk+-3.0 libxfce4ui-2 libxfconf-0)
RUNTIME_TOOLS=(xscreensaver xscreensaver-command xscreensaver-demo mpv ffmpeg xset xfconf-query)

missing=()
for t in "${BUILD_TOOLS[@]}"; do has "$t" || missing+=("$t"); done
for p in "${BUILD_PC[@]}"; do pc "$p" || missing+=("dev:$p"); done
missing_runtime=()
for t in "${RUNTIME_TOOLS[@]}"; do has "$t" || missing_runtime+=("$t"); done

APT_ALL=(xscreensaver xscreensaver-data xscreensaver-gl xscreensaver-data-extra
         xscreensaver-gl-extra mpv ffmpeg meson ninja-build gcc pkg-config
         libgtk-3-dev libxfce4ui-2-dev libxfconf-0-dev libglib2.0-dev)

install_with_apt() {
  echo "== installing missing packages (sudo apt-get) =="
  sudo apt-get install -y "${APT_ALL[@]}"
}

if [ "${#missing[@]}" -gt 0 ]; then
  echo "missing build dependencies: ${missing[*]}"
  if [ "$AUTO_DEPS" -eq 1 ] && is_debian; then
    install_with_apt
  else
    echo "Run again with --deps (Debian family) or install:"
    echo "  sudo apt-get install ${APT_ALL[*]}"
    exit 1
  fi
fi
if [ "${#missing_runtime[@]}" -gt 0 ]; then
  echo "NOTE: optional runtime tools missing (install for the full experience): ${missing_runtime[*]}"
  if [ "$AUTO_DEPS" -eq 1 ] && is_debian; then
    install_with_apt
  else
    echo "  They are optional — continuing. You can install them later with:"
    echo "  sudo apt-get install ${APT_ALL[*]}"
  fi
fi
# re-check after a --deps install
for t in "${BUILD_TOOLS[@]}"; do has "$t" || { echo "build dep still missing: $t"; exit 1; }; done

# ---------------------------------------------------------------- build
echo "== building =="
if [ -d "$BUILD_DIR/meson-info" ]; then
  meson setup "$BUILD_DIR" --prefix "$PREFIX" --reconfigure >/dev/null
else
  meson setup "$BUILD_DIR" --prefix "$PREFIX" >/dev/null
fi
ninja -C "$BUILD_DIR"
echo "== installing to $PREFIX =="
meson install -C "$BUILD_DIR" >/dev/null
echo "   installed: $(ls -1 "$BINDIR"/xfce4-screen-saver-settings "$BINDIR"/xfce4-screen-saver-apply "$BINDIR"/wallpaper-apply "$BINDIR"/make-wallpaper-slideshow "$BINDIR"/saver-videoloop 2>/dev/null | wc -l)/5 artifacts in $BINDIR"

# ---------------------------------------------------------------- config
if [ "$DO_CONFIG" -eq 1 ]; then
  echo "== wiring runtime configuration =="

  if has xfconf-query; then
    xfconf-query -c xfce4-screen-saver -p /enabled >/dev/null 2>&1 \
      || xfconf-query -c xfce4-screen-saver -p /enabled -n -t bool -s true
    xfconf-query -c xfce4-screen-saver -p /timeout >/dev/null 2>&1 \
      || xfconf-query -c xfce4-screen-saver -p /timeout -n -t uint -s 600
    echo "   xfconf channel xfce4-screen-saver: defaults in place"
  fi

  mkdir -p "$HOME/Pictures/wallpapers" "$HOME/Videos/screensavers"

  if [ ! -f "$HOME/.xscreensaver" ]; then
    cat > "$HOME/.xscreensaver" <<'EOF'
# Generated by xfce-screen-saver-settings (install.sh) — XScreenSaver user overrides.
# The demo GUI (xscreensaver-demo) rewrites this file on Save and preserves these.
mode:		random
timeout:	0:10:00
fade:		True
imageDirectory:	~/Pictures/wallpapers/
EOF
    echo "   ~/.xscreensaver created (imageDirectory -> ~/Pictures/wallpapers)"
  else
    echo "   ~/.xscreensaver already exists — kept (ensure imageDirectory points at your wallpapers)"
  fi

  mkdir -p "$HOME/.config/autostart"
  if [ ! -f "$HOME/.config/autostart/xscreensaver.desktop" ]; then
    cat > "$HOME/.config/autostart/xscreensaver.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Screen Saver (XScreenSaver)
Comment=Modern high-resolution screensaver daemon
Exec=/usr/bin/xscreensaver -no-splash
Terminal=false
NoDisplay=true
X-GNOME-Autostart-enabled=true
EOF
    echo "   autostart: xscreensaver daemon"
  fi
  if [ -f "$PREFIX/etc/xdg/autostart/xfce4-screen-saver-apply.desktop" ]; then
    cp "$PREFIX/etc/xdg/autostart/xfce4-screen-saver-apply.desktop" \
       "$HOME/.config/autostart/"
    echo "   autostart: xfce4-screen-saver-apply (applies xset state at logon)"
  fi
fi

# ---------------------------------------------------------------- done
echo
echo "== installed. Next steps =="
echo "  1. Drop wallpapers into ~/Pictures/wallpapers/ (it exists now)."
echo "  2. Settings -> All Settings -> System -> Screen Saver  (configure + Preview)."
echo "  3. xscreensaver-demo -> Display Modes -> GL Slideshow + Mode: One"
echo "     for a wallpaper slideshow; Advanced -> Choose Random Image"
echo "     uses ~/Pictures/wallpapers by default."
echo "  4. PowerPoint-style random transitions:  make-wallpaper-slideshow"
echo
echo "Full docs: $ROOT/README.md"