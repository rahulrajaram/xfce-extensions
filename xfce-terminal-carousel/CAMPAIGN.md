# Plasma Screen Phase — Campaign

Bounded autonomous campaign (autonomous-execution-contract). State lives
here (portable checkpoint); update on every material milestone. The
Campaign / checkpoints are the durable record — git commits are per-slice
checkpoints too.

## Objective

Build the **plasma screen** for xfce4-terminal-carousel: a GTK4 standalone
overlay process that, after the X11 idle threshold, springs up a smooth
web-like card carousel of the terminal's active tabs, with live content
fed by the Terminal Control D-Bus bridge. Keep the existing GTK3 strip
working unchanged while the plasma lands.

## Slice order and verification gates

1. **Campaign setup** (this file) — objective + gates recorded. DONE at
   checkpoints/1.
2. **GTK4 plasma skeleton** — new `xfce4-terminal-plasma` binary in the
   carousel package (GTK4, own process, fullscreen borderless overlay,
   springs up after idle threshold, tick-callback-driven card carousel).
   Gate: builds with `ninja -C build`; under Xvfb+dbus-run-session with
   the dev-built terminal it maps after idle, draws animated cards,
   rotates, hides on input (verified with xdotool + import pixels).
3. **Text mirror** — extend `terminal-control-bridge.xml` Tab interface
   with last-N-lines text access; surface in `terminal-bridge.c`
   (additive only); render fake-terminal cards (monospace line text) in
   GTK4. Gate: bridge exposes lines (gdbus call + dbus-send check);
   plasma cards render the live text; terminal repo still builds.
4. **Pixel-mirror spike (experiment)** — `Tab.Screenshot()` -> PNG with
   VTE offscreen rendering in-process. Feasibility REPORT before any
   wiring; do not promise level 2 until proven. Gate: spike binary
   produces a PNG that visually matches the tab contents.
5. **Tests** — port/extend appearance+behavior suite: `tests/run_all.sh`
   (xdotool input, `import` pixel assertions, slide rotation,
   stop-on-input, click-to-jump) covering strip AND plasma. Gate: script
   exits 0 under Xvfb.
6. **Keep strip working** — strip behavior unchanged across all slices.
   Gate: strip still verifies on its existing flow at the end.

## Stop rules

- 3 consecutive failures of the same verification target with no material
  progress between attempts.
- Product/API/architecture decision not inferable from repo context.
- Destructive op, secrets, external shared state (pushes, deploys).
- GTK4 dev availability regression (now installed: 4.8.3).

## Constraints (carried from handoff)

- GCC only in builds (no clang); clangd is indexer-only inside cultivar.
- Carousel config lives in xfconf channel `xfce4-terminal-carousel` only;
  do NOT add prefs UI to xfce4-terminal (that was reverted; never re-add).
- Bridge changes must be additive; breaking changes need a new interface
  version suffix (Control1..ControlN).
- Dev testing on Xvfb+dbus-run-session; the user's real terminal (stock
  1.0.4, org.xfce.Terminal5, 16 tabs) must not be touched or disturbed.
- Plasma is GTK4; the daemon/strip stays GTK3.

## Session checkpoints

- ckpt 1 (baseline): GTK4 dev installed (4.8.3). Carousel repo master
  @ 0eed807 clean; terminal repo terminal-control-bridge @ f2e171db
  clean (only NEXT_SHELL_PROMPT.md untracked = handoff artifact). Both
  build. Campaign objective ratified by operator; GTK4 provisioning
  decision resolved (operator ran `sudo apt install libgtk-4-dev
  gir1.2-gtk-4.0`).
- ckpt 2 (slice 1/2 DONE — GTK4 plasma skeleton): new
  `src/plasma.c` (GTK4, own process `xfce4-terminal-plasma`, fullscreen
  borderless overlay, dimming, card carousel driven by a tick callback,
  idle start / input stop / idle re-show, bridge-fed titles). Verified
  green by `tests/plasma-smoke.sh` (A1 overlay maps after idle, A2
  cards drawn, A3 animates/rotates, A4 click hides, A5 re-appears).
  Two real bugs found and fixed en route:
  - GTK4 redraws: tick must queue_draw the GtkDrawingArea, not just the
    toplevel — otherwise the X framebuffer freezes at the initial map
    (diagnosed via renderer matrix + instrumented draw count).
  - Test-harness footguns: `import -window root` under Xvfb captures
    everything (fine); dbus-run-session hangs on lingering X clients
    (explicit pkill before exit); pkill patterns must use bracket
    escaping so the harness can't kill its own shell; the user's real
    terminal cmdline has no leading slash so `pkill -f '/xfce4-terminal '`
    is safe, but never rely on that.
  - GTK4 API deltas handled: gtk_init() (no argc), no gtk_main
    (GMainLoop), gtk_css_provider_load_from_data, monitors via
    GListModel, no skip-taskbar/accept-focus hints (deferred to
    layer-shell/Wayland phase).
- ckpt 3 (next): slice 3 — text mirror (bridge Tab last-N-lines -> fake
  terminal cards).