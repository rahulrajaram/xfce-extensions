#!/usr/bin/env bash
# virtual-desktop.sh — run the REAL Xfce window manager (+ patched terminal
# and plasma) on a virtual X display, fully isolated from the user's :0
# desktop. No virtualization, native performance, genuine X11 semantics.
#
# Two display backends:
#   Xvfb   (default) — headless; good for automated verification
#   Xephyr (try -D xephyr) — visible nested desktop; needs xserver-xephyr
#
# Run from the repo root:  bash tests/virtual-desktop.sh [ [-D xephyr] c1 c2 ... ]
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DISP="${VD_DISPLAY:-:98}"
BACKEND="xvfb"
if [ "${1:-}" = "-D" ]; then BACKEND="${2:-xvfb}"; shift 2; fi
EXTRA_APPS=("$@")   # optional extra commands to run inside the session
# bash arrays can't cross the inner bash -s boundary; pass a scalar env
# (when empty the extra-apps block below is skipped)
export VD_EXTRA_APPS="${*:-}"

TERM_BIN="$ROOT/../xfce4-terminal/build/terminal/xfce4-terminal"
PLASMA_BIN="$ROOT/build/xfce4-terminal-plasma"
OUT=/tmp/vd-${DISP#:}
mkdir -p "$OUT"

echo "== virtual desktop on $DISP (backend=$BACKEND) =="

# ---- PID-scoped cleanup (NEVER broad pkill of xfwm4/xfconfd) ----
XVFB_PID=""; WM_PID=""; CONF_PID=""; BUS_PID=""; APP_PIDS=()
cleanup() {
  local p
  for p in "${APP_PIDS[@]:-}"; do kill "$p" 2>/dev/null; done
  [ -n "$WM_PID" ] && kill "$WM_PID" 2>/dev/null
  [ -n "$CONF_PID" ] && kill "$CONF_PID" 2>/dev/null
  [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null
  # DBus session: let it drain; kill only our private bus
  [ -n "$BUS_PID" ] && kill "$BUS_PID" 2>/dev/null
  rm -f "/tmp/.X${DISP#:}-lock" 2>/dev/null
  exit 0
}
trap cleanup EXIT INT TERM

# ---- start the display backend ----
case "$BACKEND" in
  xvfb)   Xvfb "$DISP" -screen 0 1280x800x24 >"$OUT/xvfb.log" 2>&1 & XVFB_PID=$! ;;
  xephyr) Xephyr "$DISP" -screen 1280x800 -ac >"$OUT/xephyr.log" 2>&1 & XVFB_PID=$! ;;
  *) echo "unknown backend $BACKEND (xvfb|xephyr)"; exit 2 ;;
esac
for i in $(seq 1 20); do
  DISPLAY="$DISP" xdpyinfo >/dev/null 2>&1 && break
  sleep 0.5
done

# ---- private D-Bus session + xfconfd (isolated from the real session bus) ----
dbus-run-session -- bash -s >"$OUT/session.log" 2>&1 <<EOS &
set -u
export DISPLAY="$DISP"
ROOT="$ROOT"; OUT="$OUT"; TERM_BIN="$TERM_BIN"; PLASMA_BIN="$PLASMA_BIN"
echo "SESSION_START pid=\$\$"

# real Xfce window manager on the virtual display (native, real X11 semantics)
xfconfd --no-daemon --sm-client-disable >/dev/null 2>&1 &
xfsettingsd >/dev/null 2>&1 &
sleep 1.5
xfwm4 --display "\$DISPLAY" >"$OUT/wm.log" 2>&1 &
echo "WM up (pid=\$!)"

# patched terminal: open a couple of tabs with content so the bridge has data
"$TERM_BIN" --geometry 100x20 >"$OUT/term.log" 2>&1 &
echo "TERM up (pid=\$!)"
sleep 3
xdotool mousemove 400 200
xdotool key --clearmodifiers ctrl+shift+t
sleep 0.5
xdotool type --delay 30 "echo virtual-desktop-works; seq 1 8"
xdotool key Return
sleep 1.5

"$PLASMA_BIN" >"$OUT/plasma.log" 2>&1 &
echo "PLASMA up (pid=\$!)"

# wait for the plasma overlay to appear under the WM (idle threshold 1s)
xfconf-query -c xfce4-terminal-carousel -p /enabled  -n -t bool -s true  2>/dev/null
xfconf-query -c xfce4-terminal-carousel -p /timeout  -n -t uint -s 1     2>/dev/null
xfconf-query -c xfce4-terminal-carousel -p /rail-speed -n -t uint -s 40  2>/dev/null
sleep 4
PLASMA_WID=\$(DISPLAY="\$DISPLAY" xdotool search --name xfce4-terminal-plasma | head -1)
TERM_WID=\$(DISPLAY="\$DISPLAY" xdotool search --name Terminal | head -1)
echo "PLASMA_WID=[\$PLASMA_WID] TERM_WID=[\$TERM_WID]"
if [ -n "\$PLASMA_WID" ]; then echo "VERIFY overlay mapped under WM: PASS"; else echo "VERIFY overlay mapped under WM: FAIL (see plasma.log)"; fi
# WM-managed window count — the WM should now be managing our windows
echo "root children managed by WM:"
DISPLAY="\$DISPLAY" xwininfo -root -tree 2>/dev/null | grep -cE 'Terminal|plasma' || true

# optional extra test apps (exported scalar; run as one shell command
# line inside the session)
if [ -n "\${VD_EXTRA_APPS:-}" ]; then
  eval "\$VD_EXTRA_APPS" & echo "EXTRA up: \$VD_EXTRA_APPS"
  sleep 2
fi

# interactive hold: keep the session alive until killed (or --selfcheck to exit)
if [ "\${VD_SELFCHECK:-0}" = "1" ]; then
  sleep 2
else
  echo "INTERACTIVE: virtual desktop running on :98. Ctrl-C to stop."
  sleep 3600 &
  wait
fi
EOS
BUS_PID=$!
sleep 12
echo "== session log tail =="; tail -8 "$OUT/session.log"
echo "== wm.log =="; tail -4 "$OUT/wm.log" 2>/dev/null