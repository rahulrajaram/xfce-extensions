#!/usr/bin/env bash
# settings-smoke.sh — smoke test for the screen-saver settings dialog + applier.
#
# Xvfb + private dbus + xfconfd under a throwaway HOME/XDG_CONFIG_HOME so the
# real channel is never touched. Asserts:
#   S1 settings dialog maps (window titled "Screen Saver")
#   S2 changing the timeout spin writes the xfconf channel (in seconds)
#     AND applies to the live X server via xset
#   S3 the apply helper mirrors the channel into xset when run standalone
#   S4 external channel changes don't crash the dialog
#   S5 Preview / Preferences buttons issue commands (logged decision)
#   S6 Escape closes the dialog
#
# Usage: tests/settings-smoke.sh (run from the repo root)
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${SAVER_OUT:-/tmp/saver-settings-smoke}"
SETTINGS="$ROOT/build/xfce4-screen-saver-settings"
APPLY="$ROOT/build/xfce4-screen-saver-apply"
DISPLAY_NUM=:99
LOG="$OUT/test.log"
XFCONFD="$(command -v xfconfd 2>/dev/null || echo /usr/lib/x86_64-linux-gnu/xfce4/xfconf/xfconfd)"

if [ ! -x "$SETTINGS" ]; then echo "settings binary missing (build first)"; exit 2; fi
if [ ! -x "$APPLY" ]; then echo "apply binary missing (build first)"; exit 2; fi
if [ ! -x "$XFCONFD" ]; then echo "xfconfd not found"; exit 2; fi

mkdir -p "$OUT"; : > "$LOG"

killall_env() {
  pkill -9 -f 'Xvfb :99' 2>/dev/null
  pkill -9 -x Xvfb 2>/dev/null
  pkill -9 -f 'xfce4-screen-saver-settin[g]' 2>/dev/null
  pkill -9 -x xfconfd 2>/dev/null
  sleep 1
  rm -f /tmp/.X99-lock
}

start_xvfb() {
  for attempt in 1 2; do
    Xvfb "$DISPLAY_NUM" -screen 0 1280x800x24 &
    for i in $(seq 1 10); do
      if DISPLAY="$DISPLAY_NUM" xdpyinfo >/dev/null 2>&1; then return 0; fi
      sleep 0.5
    done
    killall_env
  done
  echo "Xvfb failed to start on $DISPLAY_NUM"
  return 1
}

killall_env
start_xvfb || exit 2
sleep 0.5

timeout 120 dbus-run-session -- bash -s > "$LOG" 2>&1 <<EOS
set -u
export DISPLAY=$DISPLAY_NUM
ROOT="$ROOT"; OUT="$OUT"; SETTINGS="$SETTINGS"; APPLY="$APPLY"; XFCONFD="$XFCONFD"

# throwaway config home: the channel never touches the real one
export HOME="\$OUT/home"; export XDG_CONFIG_HOME="\$HOME/.config"
mkdir -p "\$XDG_CONFIG_HOME"

"\$XFCONFD" --no-daemon --sm-client-disable >/dev/null 2>&1 &
sleep 1.5

xfconf-query -c xfce4-screen-saver -p /enabled -n -t bool -s true 2>/dev/null
xfconf-query -c xfce4-screen-saver -p /timeout -n -t uint -s 600 2>/dev/null
xset s off   # start from a known blanking-off state

G_MESSAGES_DEBUG=all "\$SETTINGS" > "\$OUT/settings.log" 2>&1 &
sleep 2

# S1: dialog maps
WID=\$(timeout 8 xdotool search --name 'Screen Saver' | head -1)
if [ -n "\$WID" ]; then
  echo "S1 PASS dialog visible (wid=\$WID)"
  import -window "\$WID" "\$OUT/s1.png" 2>/dev/null
else
  echo "S1 FAIL"; tail -8 "\$OUT/settings.log"; exit 1
fi

# S2: initial focus is the row-1 switch; one Tab reaches the timeout spin.
# Select-all and type 17 (minutes), commit with Return + focus-out Tab.
xdotool windowfocus "\$WID"; sleep 0.3
xdotool key Tab
sleep 0.3
xdotool key ctrl+a
xdotool type --delay 30 "17"
xdotool key Return
xdotool key Tab
sleep 1

V=\$(timeout 5 xfconf-query -c xfce4-screen-saver -p /timeout 2>/dev/null)
echo "S2 /timeout=\$V (need 1020 = 17 min)"
if [ "\$V" = "1020" ]; then echo "S2 PASS channel write-through"; else echo "S2 FAIL"; exit 1; fi

# blanking must have been applied live: xset q timeout should be 1020
T=\$(xset q 2>/dev/null | grep -o 'timeout: *[0-9]*' | head -1)
echo "S2 xset -> \$T"
case "\$T" in
  *1020*) echo "S2 PASS xset applied";;
  *)      echo "S2 FAIL xset not applied ('\$T')"; exit 1;;
esac

# S3: running the standalone applier re-applies from the channel
xset s off
"\$APPLY" >/dev/null 2>&1
T2=\$(xset q 2>/dev/null | grep -o 'timeout: *[0-9]*' | head -1)
echo "S3 xset -> \$T2"
case "\$T2" in
  *1020*) echo "S3 PASS apply helper";;
  *)      echo "S3 FAIL apply helper ('\$T2')"; exit 1;;
esac

# S4: external channel change must not crash the dialog
xfconf-query -c xfce4-screen-saver -p /enabled -s false 2>/dev/null
sleep 1
if [ -n "\$(timeout 5 xdotool search --name 'Screen Saver' | head -1)" ]; then
  echo "S4 PASS dialog alive after external change"
else
  echo "S4 FAIL dialog died"; tail -8 "\$OUT/settings.log"; exit 1
fi

# S5: action buttons. After S2's trailing Tab, focus is on "Preview now".
# Space activates a focused GTK button reliably; Tab reaches Preferences.
xdotool key space
sleep 0.8
xdotool key Tab
sleep 0.3
xdotool key space
sleep 1.5
if grep -q 'action preview -> ' "\$OUT/settings.log" \
   && grep -Eq 'action prefs -> (xscreensaver-demo)' "\$OUT/settings.log"; then
  echo "S5 PASS action buttons issued commands"
  grep -E 'action (preview|prefs)' "\$OUT/settings.log" | tail -2
else
  echo "S5 FAIL"; grep -iE 'action|error|failed|warning' "\$OUT/settings.log" | tail -8; exit 1
fi

# close any demo/daemon the prefs button may have spawned, then Escape closes
pkill -f 'xscreensavere[r]' 2>/dev/null; sleep 0.5
xdotool windowfocus "\$WID"; sleep 0.3
xdotool key Escape
sleep 1
GONE=\$(timeout 5 xdotool search --name 'Screen Saver' | head -1)
if [ -z "\$GONE" ]; then echo "S6 PASS Escape closes"; else echo "S6 FAIL still open"; exit 1; fi

pkill -f 'xfce4-screen-saver-settings' 2>/dev/null
pkill -x xfconfd 2>/dev/null
EOS
RC=$?
killall_env
echo "harness rc=$RC"
exit $RC