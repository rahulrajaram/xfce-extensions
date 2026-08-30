# xfce4-terminal-carousel

A standalone companion daemon for **xfce4-terminal**: after a period of
computer inactivity it rotates through the terminal tabs that have
active work, and shows a status strip with blinking green lights for
tabs that are waiting for your input.

It is **not** a fork or plugin of xfce4-terminal. It is a separate
package that consumes xfce4-terminal's *Terminal Control* D-Bus API
(`org.xfce.Terminal.Control.*`, shipped inside xfce4-terminal's
`org.xfce.Terminal<N>` session service).

## What it does

- Detects computer-wide inactivity via the X11 screen-saver extension
  (`libxss`), default threshold: 60 seconds.
- Watches every open terminal window and tab through the Control
  bridge. A tab is considered **active work** when:
  - it rang the bell and you have not interacted with it since
    ( NeedsAttention — shown as a **blinking green light**), or
  - it produced output within the last activity window
    (default 10 minutes), or it runs a foreground job.
- Cycles through those tabs (switches the notebook page and raises the
  window) once per slide interval (default 6 seconds).
- Any keyboard or mouse activity stops the carousel immediately.
- Clicking a light jumps to that tab and stops the carousel.

## Plasma screen (GTK4 overlay, experimental)

The package also ships `xfce4-terminal-plasma`, a standalone GTK4
overlay that replaces the strip display with a dimmed fullscreen card
carousel after idle: each active tab becomes a card showing its live
terminal lines (text mirror, see below), with a smooth continuous
carousel drift driven by a frame-clock tick callback. Same xfconf
channel, same idle logic; it runs as its own process, so it can coexist
with the strip.

Plasma controls:

- The carousel drifts at a constant ground speed so the on-screen pace
  is predictable (configurable, default 40 px/s).
- **Hover** over a card pauses the drift so it can be read; move the
  pointer off the cards and it resumes.
- **`q` / `Escape`** quits the carousel and hides the overlay. Any
  other key or a click also dismisses it.

```bash
# carousel speed in pixels/second (default 40)
xfconf-query -c xfce4-terminal-carousel -p /rail-speed -n -t uint -s 40
```

- Text mirror: the Control bridge gained `Tab.GetLines(u max)` — the
  last visible rows of a tab as plain strings (with a scrollback
  fallback for background tabs). The plasma fetches the mirror for the
  slide tabs on every poll and draws them as fake-terminal cards
  (monospace, green prompt line).
- Pixel mirror (spike, not wired): `Tab.Screenshot(u max_width)`
  renders a tab's VTE offscreen into a PNG. Verified feasible for
  mapped (active) tabs — which is exactly when the carousel shows a
  tab — and unusable for unmapped background tabs (renders black).
  Decision pending on wiring it as the card backdrop.

## Tests

`tests/run_all.sh` runs the behavior suites under Xvfb + a private
D-Bus session (they must not run concurrently):

- `tests/strip-smoke.sh` — strip maps after idle, attention dots are
  rendered green, blink/ring animation, click-to-jump stops, input
  stops.
- `tests/plasma-smoke.sh` — overlay maps after idle, cards drawn,
  carousel animates/rotates, text mirror rendered in cards, click
  hides, re-appears after idle.

## Making the green lights meaningful

The light means "this terminal rang the bell and is waiting for you".
Shells do not ring the bell on every prompt by default. Wire it up:

- **bash** (`~/.bashrc`):
  ```bash
  PROMPT_COMMAND='printf "\a"'
  ```
  or only after long jobs: compare `$SECONDS` around `$?`.
- **zsh** (`~/.zshrc`):
  ```zsh
  precmd() { print -n '\a' }
  ```
- **fish** (`~/.config/fish/config.fish`):
  ```fish
  function fish_prompt; echo -n "\a"; end
  ```

## Configuration (xfconf, channel `xfce4-terminal-carousel`)

| Key               | Type   | Default | Meaning                                   |
|-------------------|--------|---------|-------------------------------------------|
| `/enabled`        | bool   | true    | master switch                              |
| `/timeout`        | uint   | 60      | seconds of computer inactivity before start|
| `/slide-interval` | uint   | 6       | seconds each tab is shown                  |
| `/activity-window`| uint   | 10      | minutes of output that count as active work|

```bash
xfconf-query -c xfce4-terminal-carousel -p /timeout -n -t uint -s 60
```

## Running

```bash
xfce4-terminal-carousel
```

Add the shipped `data/org.xfce.Terminal.Carousel.desktop` to your
autostart (installed to `$sysconfdir/xdg/autostart` by
`meson install`). The daemon exits harmlessly when xfce4-terminal is
not running and re-attaches when it appears.

## The Terminal Control API (implemented inside xfce4-terminal)

Stable, versioned D-Bus surface (`terminal-control-bridge.xml` in the
xfce4-terminal source). On the session bus, service
`org.xfce.Terminal<N>` (the same service xfce4-terminal already owns),
object `/org/xfce/Terminal`:

- `org.xfce.Terminal.Control.Manager` — `ListWindows() → ao`,
  signals `WindowOpened(o)`, `WindowClosed(o)`
- `org.xfce.Terminal.Control.Window` — properties `Tabs (ao)`,
  `ActiveTab (o)`, `Dropdown (b)`; method `ActivateTab(o)`
- `org.xfce.Terminal.Control.Tab` — properties `Title (s)`,
  `Active (b)`, `NeedsAttention (b)`, `HasForegroundProcess (b)`,
  `LastOutput (x)` (CLOCK_MONOTONIC µs, 0 = never),
  `WorkingDirectory (s)`, `Pid (u)`; method `Activate()`;
  text mirror `GetLines(u max) -> as`; pixel mirror
  `Screenshot(u max_width) -> ay` (spike; see Plasma screen section)

Property updates arrive via the standard
`org.freedesktop.DBus.Properties.PropertiesChanged` signal. This
interface is the intended foundation for future integrations (panel
applets, agents, inter-terminal communication); additive changes are
backwards compatible.

## Building

```bash
meson setup build
ninja -C build
```

Dependencies: `gtk+-3.0`, `libxss` (xscrnsaver), `libxfconf-0`.

## Why a separate package?

xfce4-terminal intentionally has no plugin system. The Control bridge
is a *thin, generic, versioned* addition inside the terminal; all
policy and UI (idle detection, rotation, strip, lights) lives here, so
it can evolve (panel plugin, other terminals, Wayland backend) without
touching xfce4-terminal again.
