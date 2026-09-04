#!/usr/bin/env bash
# strip-smoke.sh — behavior smoke test for the GTK3 strip daemon.
#
# Xvfb + private dbus + dev terminal (2 tabs, one rings the bell) under
# the carousel daemon. Asserts:
#   S1 strip window maps after idle
#   S2 strip shows dots (green attention pixels in the top band)
#   S3 strip animates (blink + title rotation between frames)
#   S4 click on a dot jumps and destroys the strip
#   S5 keyboard input hides a re-appeared strip
#
# Usage: tests/strip-smoke.sh (run from the repo root)
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${STRIP_OUT:-/tmp/strip-smoke}"
DEVTERM="$ROOT/../xfce4-terminal/build/terminal/xfce4-terminal"
DAEMON="$ROOT/build/xfce4-terminal-carousel"
DISPLAY_NUM=:97
LOG="$OUT/test.log"

if [ ! -x "$DAEMON" ]; then echo "daemon binary missing (build first)"; exit 2; fi
if [ ! -x "$DEVTERM" ]; then echo "dev terminal binary missing"; exit 2; fi

mkdir -p "$OUT"; : > "$LOG"

killall_env() {
  pkill -9 -f 'Xvfb :97' 2>/dev/null
  pkill -9 -x Xvfb 2>/dev/null
  pkill -9 -f 'build/terminal/xfce4-termina[l]' 2>/dev/null
  pkill -9 -f 'build/xfce4-terminal-carouse[l]' 2>/dev/null
  pkill -9 -f 'build/xfce4-terminal-plasm[a]' 2>/dev/null
  pkill -9 -x xfconfd 2>/dev/null
  sleep 1
  rm -f /tmp/.X97-lock
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
echo "Xvfb now starting on $DISPLAY_NUM" >&2
start_xvfb || exit 2
sleep 0.5

timeout 90 dbus-run-session -- bash -s > "$LOG" 2>&1 <<EOS
set -u
export DISPLAY=$DISPLAY_NUM
ROOT="$ROOT"; OUT="$OUT"
DEVTERM="$DEVTERM"; DAEMON="$DAEMON"

xfconfd --no-daemon --sm-client-disable >/dev/null 2>&1 &
sleep 1.5
xfconf-query -c xfce4-terminal-carousel -p /enabled         -n -t bool -s true  2>/dev/null
xfconf-query -c xfce4-terminal-carousel -p /timeout         -n -t uint -s 3     2>/dev/null
xfconf-query -c xfce4-terminal-carousel -p /slide-interval  -n -t uint -s 2     2>/dev/null
xfconf-query -c xfce4-terminal-carousel -p /activity-window -n -t uint -s 100   2>/dev/null

"$DEVTERM" --geometry 100x20 >/dev/null 2>&1 &
sleep 3
xdotool mousemove 400 250
xdotool key --clearmodifiers ctrl+shift+t          # 2nd tab (active)
sleep 0.5
xdotool type --delay 40 "echo -e '\\\\a'"
xdotool key Return                                   # bell -> NeedsAttention
xdotool type --delay 40 "echo strip-output"
xdotool key Return
sleep 1.5

G_MESSAGES_DEBUG=all "$DAEMON" > "$OUT/daemon.log" 2>&1 &
xdotool mousemove 1270 790
sleep 8

# S1: strip maps
WID=\$(timeout 5 xdotool search --name xfce4-terminal-carousel-strip | head -1)
if [ -n "\$WID" ]; then echo "S1 PASS strip visible (wid=\$WID)"; else echo "S1 FAIL"; tail -5 "$OUT/daemon.log"; fi

# S2/S3 target the strip window itself (override-redirect popup; roots
# capture may not include OR children)
# S2: attention dots — green pixels in the strip
import -window "\$WID" "$OUT/s1.png" 2>/dev/null
GREEN=\$(convert "$OUT/s1.png" \
  -fx '(g>r*1.2 && g>b*1.2) ? 1 : 0' -format "%[fx:mean*w*h]" info: 2>/dev/null)
echo "S2 green-pixels=\$GREEN (need >50)"
if awk -v g="\$GREEN" "BEGIN{exit !(g > 50)}"; then echo "S2 PASS dots rendered"; else echo "S2 FAIL"; fi

# S3: blink ring/title animates — sample 6 frames 0.35s apart; need >=1
# adjacent pair to differ (avoids blink-phase alignment flakes)
PREV3=""; DISTINCT3=0
for i in \$(seq 0 5); do
  sleep 0.35
  F3="$OUT/s3-\$i.png"
  import -window "\$WID" "$F3" 2>/dev/null
  if [ -n "\$PREV3" ]; then
    D3=\$(timeout 5 compare -metric AE "\$PREV3" "$F3" null: 2>&1)
    if [ "\${D3:-0}" -gt 30 ]; then DISTINCT3=\$((DISTINCT3+1)); fi
  fi
  PREV3="$F3"
done
echo "S3 distinct-frames=\$DISTINCT3 (need >=1)"
if [ "\$DISTINCT3" -ge 1 ]; then echo "S3 PASS strip animates"; else echo "S3 FAIL"; fi

# S4: click a dot (2 dots: right-aligned, first_dot_x = W-14-5-26; W=220+52)
# window x0 = (1280-272)/2 = 504, y0 = 4
xdotool mousemove 757 20
xdotool click 1
sleep 0.6
GONE=\$(timeout 5 xdotool search --name xfce4-terminal-carousel-strip | head -1)
if [ -z "\$GONE" ]; then echo "S4 PASS click jumps and stops"; else echo "S4 FAIL strip still present"; fi

# strip re-appears on idle, then keyboard input hides it
sleep 5
if [ -n "\$(timeout 5 xdotool search --name xfce4-terminal-carousel-strip | head -1)" ]; then
  xdotool key --clearmodifiers Return
  sleep 3
  G2=\$(timeout 5 xdotool search --name xfce4-terminal-carousel-strip | head -1)
  if [ -z "\$G2" ]; then echo "S5 PASS input stops carousel"; else echo "S5 FAIL strip still present after input"; fi
else
  echo "S5 FAIL strip never re-appeared"
fi

echo "--- daemon.log tail ---"
grep -E 'starting|slide|stopped' "$OUT/daemon.log" | tail -6
pkill -f "build/terminal/xfce4-terminal" 2>/dev/null
pkill -f "build/xfce4-terminal-carousel" 2>/dev/null
pkill -x xfconfd 2>/dev/null
EOS
RC=$?
killall_env
echo "harness rc=$RC"
grep -E '^S[0-9]' "$LOG"
NUM_FAIL=$(grep -cE '^S[0-9]+ FAIL' "$LOG")
if [ "$NUM_FAIL" -eq 0 ]; then
  echo "ALL PASS"
else
  echo "$NUM_FAIL assertion(s) failed — see $LOG"
  exit 1
fi