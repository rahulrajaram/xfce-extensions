#!/usr/bin/env bash
# plasma-smoke.sh — behavior smoke test for the GTK4 plasma overlay.
#
# Spins up Xvfb + a private dbus session with the dev-built terminal
# (2 tabs), runs the plasma binary, and asserts:
#   A1 overlay window maps after idle
#   A2 cards are drawn (center region brighter than the dimmed edge)
#   A3 carousel animates/rotates (>=3 distinct frames over 8 samples;
#      rail must also reach 1.00 in the log)
#   A4 click on the overlay hides it promptly
#   A5 overlay re-appears once idle recurs (autonomy, not a hang)
#
# Usage: tests/plasma-smoke.sh   (run from the repo root)
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${PLASMA_OUT:-/tmp/plasma-smoke}"
DEVTERM="$ROOT/../xfce4-terminal/build/terminal/xfce4-terminal"
PLASMA="$ROOT/build/xfce4-terminal-plasma"
DISPLAY_NUM=:97
LOG="$OUT/test.log"

if [ ! -x "$PLASMA" ]; then echo "plasma binary missing (build first)"; exit 2; fi
if [ ! -x "$DEVTERM" ]; then echo "dev terminal binary missing"; exit 2; fi

mkdir -p "$OUT"; : > "$LOG"

killall_env() {
  pkill -9 -f 'Xvfb :97' 2>/dev/null
  pkill -9 -x Xvfb 2>/dev/null
  pkill -9 -f 'build/terminal/xfce4-termina[l]' 2>/dev/null
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
start_xvfb || exit 2
sleep 0.5

timeout 75 dbus-run-session -- bash -s > "$LOG" 2>&1 <<EOS
set -u
export DISPLAY=$DISPLAY_NUM
ROOT="$ROOT"; OUT="$OUT"
DEVTERM="$DEVTERM"; PLASMA="$PLASMA"

xfconfd --no-daemon --sm-client-disable >/dev/null 2>&1 &
sleep 1.5
xfconf-query -c xfce4-terminal-carousel -p /timeout         -n -t uint -s 1   2>/dev/null
xfconf-query -c xfce4-terminal-carousel -p /slide-interval  -n -t uint -s 2   2>/dev/null
xfconf-query -c xfce4-terminal-carousel -p /activity-window -n -t uint -s 100 2>/dev/null

"$DEVTERM" --geometry 100x20 >/dev/null 2>&1 &
sleep 3
xdotool mousemove 400 250
xdotool key --clearmodifiers ctrl+shift+t          # 2nd tab
sleep 0.5
xdotool type --delay 40 "echo smoke-output"
xdotool key Return
sleep 1.5
G_MESSAGES_DEBUG=all "$PLASMA" > "$OUT/plasma.log" 2>&1 &
sleep 6

# A1: overlay maps after idle
WID=\$(timeout 5 xdotool search --name xfce4-terminal-plasma | head -1)
if [ -n "\$WID" ]; then echo "A1 PASS overlay visible (wid=\$WID)"; else echo "A1 FAIL"; cat "$OUT/plasma.log"; fi

# A2: cards drawn -> center region brighter than the dimmed overlay
# background. Sample the top strip (y<100) as the dim reference; cards
# (y≈210..590) never cover it regardless of the current slide position.
import -window root "$OUT/f0.png" 2>/dev/null
CENTER=\$(convert "$OUT/f0.png" -crop 300x200+490+300 -format "%[fx:mean]" info:)
EDGE=\$(  convert "$OUT/f0.png" -crop 200x100+0+0    -format "%[fx:mean]" info:)
if awk -v c="\$CENTER" -v e="\$EDGE" "BEGIN{exit !(c > e + 0.06)}"; then
  echo "A2 PASS cards drawn (center=\$CENTER top=\$EDGE)"
else
  echo "A2 FAIL center=\$CENTER top=\$EDGE"
fi

# A6: text mirror — card body shows live terminal lines. The prompt
# line of each card is tinted green. With continuous drift the cards
# are in motion, so scan the whole window (a green prompt line exists on
# whatever card is visible) rather than a fixed crop.
import -window "\$WID" "$OUT/win_mirror.png" 2>/dev/null
GREEN=\$(convert "$OUT/win_mirror.png" \
  -fx '(g>r*1.2 && g>b*1.2) ? 1 : 0' -format "%[fx:mean*w*h]" info: 2>/dev/null)
echo "A6 green-prompt-pixels=\$GREEN (need >150)"
if awk -v g="\$GREEN" "BEGIN{exit !(g > 150)}"; then
  echo "A6 PASS text mirror rendered in cards"
else
  echo "A6 FAIL no mirrored text visible"
fi

# A3: animation / rotation — sample 8 frames; need >=3 distinct
PREV=""; DISTINCT=0
for i in \$(seq 0 7); do
  sleep 0.6
  F="\$OUT/f\$i.png"
  import -window root "\$F" 2>/dev/null
  if [ -n "\$PREV" ]; then
    D=\$(timeout 5 compare -metric AE "\$PREV" "\$F" null: 2>&1)
    if [ "\${D:-0}" -gt 200 ]; then DISTINCT=\$((DISTINCT+1)); fi
  fi
  PREV="\$F"
done
echo "A3 distinct-transitions=\$DISTINCT (need >=3)"
[ "\$DISTINCT" -ge 3 ] && echo "A3 PASS carousel animates/rotates" || echo "A3 FAIL"
# A3b: rail moved off 0.00 at some point (frame log is throttled to
# every 120 frames, so accept any nonzero rail observation)
if grep 'rail=' "$OUT/plasma.log" | grep -v 'rail=0\.00' >/dev/null; then
  echo "A3b PASS rail moved"
else
  echo "A3b FAIL rail never left 0.00"
fi

# A7: continuous drift speed — the log prints rail velocity in idx/s;
# ground speed = vel * pitch (CARD_W + CARD_GAP = 676 px). Expect ~20 px/s.
RAILVEL=\$(grep -oE 'vel=[0-9.]+' "$OUT/plasma.log" | tail -1 | cut -d= -f2)
PXPERSEC=\$(awk -v v="\$RAILVEL" "BEGIN{printf \"%.0f\", v*676}")
echo "A7 drift-speed=\$PXPERSEC px/s (expect ~20)"
if [ "\$PXPERSEC" -ge 15 ] && [ "\$PXPERSEC" -le 30 ]; then
  echo "A7 PASS constant ~20px/s drift"
else
  echo "A7 FAIL drift speed out of range (\$PXPERSEC)"
fi

# A4: click hides promptly (threshold 1s so check within 0.5s)
xdotool mousemove 640 400
xdotool click 1
sleep 0.5
HIDDEN=\$(timeout 5 xdotool search --onlyvisible --name xfce4-terminal-plasma | head -1)
if [ -z "\$HIDDEN" ]; then echo "A4 PASS click hides overlay"; else echo "A4 FAIL overlay still visible after click"; fi

# A5: re-appears after idle recurs
sleep 5
RE=\$(timeout 5 xdotool search --onlyvisible --name xfce4-terminal-plasma | head -1)
if [ -n "\$RE" ]; then echo "A5 PASS overlay re-appears after idle"; else echo "A5 FAIL no re-appearance"; fi

echo "--- plasma.log tail ---"
grep -E 'shown|slide|rail=|hidden|poll' "$OUT/plasma.log" | tail -14
pkill -f "build/terminal/xfce4-terminal" 2>/dev/null
pkill -f "build/xfce4-terminal-plasma" 2>/dev/null
pkill -x xfconfd 2>/dev/null
EOS
RC=$?
killall_env
echo "harness rc=$RC"
grep -E '^A[0-9]' "$LOG"
NUM_FAIL=$(grep -cE '^A[0-9]+ FAIL' "$LOG")
if [ "$NUM_FAIL" -eq 0 ]; then
  echo "ALL PASS"
else
  echo "$NUM_FAIL assertion(s) failed — see $LOG"
  exit 1
fi