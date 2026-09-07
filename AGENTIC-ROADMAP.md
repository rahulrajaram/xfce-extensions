# Making XFCE an Agentic Powerhouse

Research synthesis — what Omarchy ships, where the "agentic desktop" state of
the art stands, and what this repo should build to make XFCE the agentic DE.
Companion to `IDEAS.md` (tabled ideas) and `README.md` (current projects).

> Status: research memo · Dates researched: 2026 · Sources listed at bottom.
> Distinguish **confirmed** (primary docs / release notes) from **synthesis**
> (our design judgment). Nothing in the ecosystem has been built by us yet.

---

## 1. The one-paragraph thesis

Omarchy (DHH's Arch-based distro) is the first distro whose marketing is
literally *"Beautiful, fun & **agentic** Linux"* — and on the agentic axis its
moves are: (1) AI as a **system citizen** (a configurable coding agent launched
from anywhere, with a bar widget showing agent usage and crash-diagnostics that
brief the agent), (2) an **event-driven, scriptable shell** (bar/launcher/
notifications converged into one plugin-able process), and (3) **semantic
theming** propagating one palette across the whole desktop.

No mainstream DE ships a first-party desktop agent. The ecosystem's emergent
architecture is: **MCP server as the desktop control plane** + **hotkey overlay
chat as the UX** + **context from clipboard / window titles / screenshots** +
**consent via portals** + **local-first LLM (Ollama)**. XFCE today has exactly
one hobby OpenAI-compatible panel plugin and nothing else — **greenfield, no
competitor head start**. Meanwhile this repo already owns the one component no
other DE has: a stable D-Bus **Terminal Control bridge** (list windows/tabs,
titles, cwd, PIDs, NeedsAttention, `GetLines`, `Screenshot`), explicitly
designed as "the foundation for future integrations (panel applets, **agents**,
inter-terminal communication)". That is our moat: wrap it in MCP and every
agent (Claude Code, Codex, pi, whatever) becomes terminal-aware on XFCE.

---

## 2. What Omarchy actually is (confirmed facts)

- Arch Linux base, **Hyprland (Wayland) + Quickshell** shell. Created by David
  Heinemeier Hansson + Ryan Hughes, released Jun 2025; Opencode/Omacom
  Foundation backing (~$10–13M pledged 2026). *Not XFCE*, *not i3*, *not Qtile*.
- v4.0 "Quattro" (Aug 2026) converged the whole shell — bar, launcher, menus,
  notifications, OSDs, lock screen, polkit agent — into **one long-lived
  Quickshell/QML process with a plugin architecture** (replacing
  Waybar/Walker/Mako/SwayOSD/hyprlock).

Agentic-system features (v4.0 notes, confirmed):
1. **Configurable default coding agent** (`claude`/`codex`/`opencode`/`pi`/…)
   launched via shortcut + `a` alias; Claude Code desktop-theme sync.
2. **Bar widget tracking agent usage** (Claude Code / Codex / Fireworks quota).
3. **Crash-diagnosis handoff** — a coredump raises a toast that (with consent)
   briefs your agent to symbolize and report the bug.

Other ink: semantic 24-color palette auto-generating Neovim/VS Code/btop
configs; one text-scaling knob (9–20px) across shell/GTK/terminal; live theme/
background carousels; event-driven shell (idle CPU discipline); zram; Snapper
snapshots w/ self-update guard; DDC/CI external-monitor brightness.

## 3. State of the agentic desktop (confirmed ecosystem survey)

- **KDE**: Plasma 6.x has *no* built-in AI. Real pieces: **KAIChat** (Ollama
  chat app), `krunner-llm-runner` (KRunner `llm explain …`), K-Ollama-Plasmoid/
  Kopilot/Plasma-AI-Chat plasmoids; "Kadai" personal-kernel agent proposal
  (Akademy 2026, speculative). KDE has no AI policy yet.
- **GNOME**: no first-party AI; extension ecosystem carries it (Penguin AI
  Chatbot, AI Assistant via clipboard-select + global shortcut + Ollama, LLM
  Chat w/ window management, Rudra launcher). GNOME Circle: zero AI projects.
- **COSMIC**: nothing on Epoch 2–3 roadmap. Fedora "AI Developer Desktop"
  initiative: rejected by Council (closed). Ubuntu: hardware enablement only.
- **Desktop-control MCP servers** (this is the real state of the art):
  `agent-sh/computer-use-linux` (AT-SPI + portals + ydotool),
  `agent-sh/agent-workspace-linux`, `coe0718/deskbrid` (100+ tools),
  `atassis/kde-mcp`, `isac322/kwin-mcp`, `KpihX/desk-mcp` (xdotool + XDG
  screenshots). Note: xdotool-family = X11; Wayland needs ydotool/libei.
- **Voice/OCR**: `richiejp/VoxInput`, `MiguelLopesDel/nexora`,
  `k2-fsa/sherpa-onnx` (offline STT/TTS).
- **Panels**: mostly *usage meters* (claudebar, codexbar-waybar, ai-usagebar,
  ai-gauge) + `Moinax/vibewatch` (click-to-approve Claude Code prompts). Only
  KDE plasmoids do real in-panel chat.
- **Consent**: flatpak/xdg-desktop-portal#1743 proposes a sandboxed **AI
  Portal** (Windows-agentic-style capability broker + approval gates).
- **Emergent architecture (synthesis)**: MCP server = control plane; hotkey
  overlay in the launcher = UX surface; AT-SPI + window titles + clipboard +
  screenshots/OCR = context; portal-like gating = consent; Ollama/local =
  cognition; usage widgets = visibility.

## 4. The XFCE reality check (confirmed)

- XFCE 4.20 (Dec 2024): experimental Wayland via **libxfce4windowing**
  abstraction + **labwc** (default) / **wayfire**. **xfwm4 is X11-only, period.**
  New Rust/Smithay compositor **xfwl4** announced Jan 2026 (in-dev).
- 4.22 not released; dev cycle = Wayland stabilization + Meson migration +
  exo deprecation. No GTK4 migration roadmap anywhere.
- Plugin reality: native plugins are **C/GTK3 + libxfce4panel**
  (`XFCE_PANEL_PLUGIN_REGISTER`, `.desktop` in `/usr/share/xfce4/panel/plugins/`).
  Python = *external* plugin (`X-XFCE-Exec` standalone PyGObject app) or
  **AppIndicator/StatusNotifier** (AyatanaAppIndicator3, the Wayland-safe tray
  path). Genmon refresh via `xfce4-panel --plugin-event`. Xfconf persists all
  config.
- Existing AI: `rabfulton/xfce-ask` (hobby C/GTK3 OpenAI-compatible plugin),
  `TheLevti/whisperer` (dictation). **No LLM assistant, no agent integration,
  no MCP, no voice-agent, nothing on xfwl4.** The panel plumbing (SNI, D-Bus,
  genmon events, xfconf) designed exactly for this is untapped.

## 5. Portfolio: Omarchy idea → XFCE build → repo anchor

| # | Omarchy feature | XFCE/agentic port | Anchor in this repo | Effort |
|---|---|---|---|---|
| P0 | Default coding agent + `a` alias | **`xfce-agent-hub`** panel plugin: launch configured agent, usage status, D-Bus control surface so anything can trigger it | Panel-plugin pattern (cf. monitor-settings, screensaver settings apps; meson + xfconf) | M |
| P0 | Terminal-aware agents (unique moat) | **`xfce-terminal-mcp`** — MCP server (stdio) exposing Terminal Control bridge: list windows/tabs, cwd/PID/attention, `GetLines`, `Screenshot`, `Activate` | D-Bus Control bridge already implemented in xfce4-terminal fork | S–M |
| P0 | Hotkey agent prompt overlay (GNOME AI Assistant / KRunner style) | **`xfce-agent-overlay`** — GTK4 overlay (reuse plasma.c machinery), global hotkey, floating prompt → streaming chat → insert into clipboard or focused terminal; local-first (Ollama) | `plasma.c` (GTK4 overlay + libxss + xfconf) | M–L |
| P1 | Crash-diagnosis handoff | systemd-coredump hook + xfce4-notifyd toast "brief your agent?" → symbolize w/ gdb → hand to `xfce-agent-hub` | screensaver settings' autostart/installer patterns; notifyd D-Bus | S |
| P1 | Agent-attention approvals (vibewatch) | carousel/plasma shows agent "waiting for approval" state; click to approve | carousel `NeedsAttention` lights + plasma cards | M |
| P1 | Context pipeline (clipboard, active window title, screenshot) | **`xfce-contextd`** — tiny daemon watching clipboard + `_NET_ACTIVE_WINDOW` (X11) / portal screenshot, exposing over D-Bus + MCP context tools | Terminal bridge already models X11 session bus patterns | M |
| P1 | Voice input → agent/terminal | hotkey → record → whisper/sherpa-onnx → text into focused tab via Control bridge | Control bridge `Tab.Activate` + GetLines | M |
| P2 | Usage widget (Claude/Codex quota) | panel plugin reading usage endpoints; or local usage logs | `xfce-agent-hub` extension | S–M |
| P2 | Semantic theming + single text-scaling knob | settings app generating xfconf palette + per-toolkit scale (GTK, terminal) | monitor-settings/screensaver `apply.c` + xfconf pattern | M |
| P2 | Event-driven shell discipline | audit carousel/plasma polling → signals/events; idle-CPU budget | carousel idle loop | S |
| P2 | Wayland story | port StatusNotifier variant + `xfwl4` layer-shell overlay when it stabilizes | plasma.c (GTK4, portable) | M–L |
| P2 | Consent gating (portal-style) | in-repo approval gate for destructive agent MCP tools; engage xdg-desktop-portal#1743 | all agent paths | M |

S = small (<1 session), M = medium, L = large.

## 6. Repo-native moat — `xfce-terminal-mcp` (recommended first build)

Why this one wins:
- The Control bridge is **already shipped and tested** in the xfce4-terminal
  fork; this repo already has Xvfb + private-D-Bus test discipline.
- MCP is the ecosystem's accepted control plane. One small Python/JS server
  (stdio transport) exposing ~8 tools gives **every MCP-capable agent**
  (Claude Desktop, Codex, pi, OpenCode…) live terminal context on XFCE — a
  story no other DE can tell today.
- It composes with everything else: overlay calls it to "insert into focused
  tab"; contextd extends it; agent-hub launches it.

Proposed tool surface (v0):
`terminal.list_windows` · `terminal.list_tabs(win)` · `tab.info(tab)` (title,
cwd, pid, NeedsAttention, HasForegroundProcess) · `tab.get_lines(tab, n)`
· `tab.screenshot(tab, max_w)` · `tab.activate(tab)` · `clipboard.get/set`
· `active_window.title()`. (Clippy getters are X11 for now; see §7.)

## 7. Build notes & risks

- **Language split**: agent brain/plumbing in Python (MCP server, contextd,
  voice) + thin C/GTK3 panel plugin and GTK4 overlay where the repo already
  speaks C. Don't write the whole portfolio in C.
- **X11 vs Wayland**: bridge, SNI tray, genmon events are Wayland-safe; plasma/
  xdotool pieces are X11 until xfwl4 stabilizes. Ship X11-first, keep GTK4
  overlay portable, revisit on `xfwl4`/4.22.
- **Consent**: never let agent tools act destructively without an approval
  gate (Omarchy's own crash-briefing asks consent first). Contribute to
  xdg-desktop-portal#1743 eventually.
- **Verify before claiming**: Omarchy feature claims are from release notes/
  reviews (agent-verified); quota-widget APIs need keys and real endpoints;
  xfce-ask maturity is unconfirmed (assume hobby-grade).

## 8. Open questions for us

1. First build: `xfce-terminal-mcp` (moat) or `xfce-agent-hub` (visible
   system-citizen feature, Omarchy-flavored)?
2. Agent scope: local-first only (Ollama), or configurable per-agent like
   Omarchy's `a` alias (claude/codex/pi)?
3. Do we keep everything X11-first, or target xfwl4 layer-shell once usable?
4. Is the scratchpad idea (IDEAS.md) an agent-context win (paste context into
   the tab-attached pane)? Likely yes — fold in.

## Sources

- Omarchy: omarchy.org · github.com/basecamp/omarchy (releases v1.0→v4.0.2) ·
  en.wikipedia.org/wiki/Omarchy · distrowatch.com/omarchy ·
  linuxiac.com/arch-based-omarchy-4-0-quattro · theregister.com (Omacom
  Foundation funding) · thenewstack.io (37signals migration) ·
  world.hey.com/dhh/omarchy-is-out.
- Agentic ecosystem: apps.kde.org/en-gb/kaichat · github.com/Blacksuan19/
  krunner-llm-runner · github.com/chevybowtie/K-Ollama-Plasmoid ·
  extensions.gnome.org/extension/7338, /6834 · circle.gnome.org ·
  github.com/agent-sh/computer-use-linux · github.com/agent-sh/
  agent-workspace-linux · github.com/coe0718/deskbrid · github.com/atassis/
  kde-mcp · github.com/isac322/kwin-mcp · github.com/KpihX/desk-mcp ·
  github.com/richiejp/VoxInput · github.com/k2-fsa/sherpa-onnx ·
  github.com/Moinax/vibewatch · github.com/flatpak/xdg-desktop-portal/
  discussions/1743.
- XFCE: xfce.org/news + changelogs/4.20 · wiki.xfce.org/releng/wayland_roadmap ·
  blog.xfce.org (xfwl4) · docs.xfce.org xfce4-sample-plugin / xfce4-genmon /
  xfce4-statusnotifier · developer.xfce.org libxfce4panel ·
  wiki.xfce.org/dev/howto/panel_plugins · github.com/rabfulton/xfce-ask ·
  github.com/TheLevti/whisperer · github.com/xfce-mirror/xfce4-sample-plugin.