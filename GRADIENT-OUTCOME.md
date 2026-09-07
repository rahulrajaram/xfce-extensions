# Agentic XFCE — Gradient Grilling Outcome

Bounded gradient grilling (wave-parallel decision lattice) over the question:
*which agentic-XFCE initiatives should this repo build, and in what order?*
Ideation seeded the lattice at three creativity levels; a preflight probe
settled the sharpest unknown empirically. Systems reference: `AGENTIC-ROADMAP.md`.

Method: ideate (gptengage, sigma 0.25 / 0.5 / 0.75, codex backend) → 6-stem
gradient (wave 1: 6 respondent workers) → wave 2 (4 follow-up workers) →
internal debate (3 positions) → live preflight probe. Budget: ~13 agent slots
(well under the 45-node cap); all spend reported here.

## What was decided (candidate answers, nothing ratified)

### Verified ground truth (preflight, this box, read-only)
- The live session runs `/usr/bin/xfce4-terminal` **1.0.4-1 (Debian)**, owning
  `org.xfce.Terminal5`, exporting **no** Control interface (empty
  `/org/xfce/Terminal` — stock D-Bus interfaces only). The carousel/plasma
  bridge surface does not exist in the runtime in daily use.
- The fork build (`xfce4-terminal/build/terminal/xfce4-terminal`) **does**
  export `org.xfce.Terminal.Control.Manager` with `ListWindows`,
  `WindowOpened/WindowClosed` signals, and a `Windows` child node (proven under
  Xvfb + private bus). The moat is real and works — conditionally on the fork
  being the runtime.
- `~/.codex/config.toml` already runs 3 MCP servers (one with per-tool
  approval); codex 0.153.4 + Claude Code 2.1.241 installed; **Claude Desktop
  not installed; pi has no MCP client.** Codex CLI is the first consumer.
- Local Ollama is up (writer-style 1.5B ~64 tok/s); PyGObject and
  libxfce4panel dev headers absent (install cost ~0.5–1 session each).

### Lattice conclusions (converged, evidence-grounded)
1. **Input/act path:** bridge-native `Tab.SendText` via the existing VTE
   `feed_child` call site (`terminal_screen_feed_text`, ~15+20 lines in the
   fork) is the deterministic executor — tab-pinned, no X11 focus races,
   headless-testable in the existing Xvfb harness. xdotool focus-type is a
   fallback. A strictly read-only v1 fails the "agents can act" dogfood.
2. **Consent:** host-side MCP approval is the v1 gate (Codex
   `approval_mode="approve"` per tool; Claude Code `requiresUserInteraction`).
   One atomic apply tool whose canonicalized params ARE the card; default-deny
   writes; batch reviews (5–10 cards); no session allow-always; audit log from
   day 1. Card UI (GTK4 overlay renderer) is v1.5 — ship the card *contract*
   now. Rubber-stamp falsifier: ≥5–10% reject/edit rate under audit = gate
   working; ≥95% instant approve = theater.
3. **Adoption:** first consumer = Codex CLI; proactive context = a
   `terminal_brief` tool + one line in AGENTS.md/CLAUDE.md (MCP resources are
   host-driven, not proactive). Non-MCP twin (`client.py` + `mcp-brief`
   wrapper) lets in-repo tools dogfood the identical JSON.
4. **Sequencing (debate-resolved):** moat-first vs wow-first collapsed into
   the risk-gated preflight: **LIVE (fork is runtime) → moat-first;
   DEAD (packaged runtime, today's truth) → wow-first.** Preflight result:
   DEAD today, with a cheap reversible flip to LIVE.

## Recommended build order (orchestrator recommendation — not ratified)

1. **Fork-swap decision (user choice, ~30 min, reversible).** Install/launch
   the fork build as the daily terminal (or autostart it) to open the LIVE
   branch. Flips everything downstream: carousel, plasma, continuity's
   GetLines path, and the entire MCP moat light up against the real session.
2. **Continuity core** (first build regardless) — capture per-tab
   title/cwd/pid → `~/.local/state` JSON → resume CLI/dialog. Bridge-
   INDEPENDENT via /proc (graceful degrade), ~2–3 sessions incl. tests on the
   existing Xvfb harness. The "desktop that remembers" demo: snap → kill →
   resume → tabs return with cwds; agent narrates.
3. **Overlay proof** — Python GTK4 mini-dialog + Ollama streaming +
   clipboard-set via xfconf shortcut. No C, no MCP; visible wow; ~1 session
   (+PyGObject install). Voice is the last increment (sherpa-onnx), gated on
   the no-voice loop proving value.
4. **terminal-mcp v0 → SendText + consent** (once fork is the runtime) —
   MCP stdio server: list_windows/list_tabs/tab_info/get_lines/brief +
   SendText behind the consent gate + client.py twin; AGENTS.md/CLAUDE.md
   nudge; Codex dogfood with the audit-log falsifier. ~2–3 sessions.
5. **Deferred / cut-first:** agent-hub (panel plugin; headers absent, weakest
   wow, ceremony owned by SessionManager/PowerManager today — build only after
   a second consumer of the D-Bus trigger exists). Usage meters: v1.5 with
   health-only fallback (Claude monthly quota is not officially exposed).

## Falsifiers to instrument from day 1
- **Adoption death:** in ≥8/10 terminal-relevant sessions the first bridge
  call happens only after the human mentions the terminal (prompt-dependence).
- **Rubber-stamp consent:** ≥95% of apply calls approved instantly, zero
  edits/rejects, risk-invariant latency.
- **Continuity as feature-not-wow:** labeled/intent resume sees ~zero use over
  plain cwd-resume.
- **Executor nondeterminism:** approve → Activate → type races in the virtual-
  desktop harness.

## Open questions (typed, unresolved unless closed above)
- Fork-swap ratification (HUMAN) — the one decision everything branches on.
- Bridge upstreaming into stock xfce4-terminal (UNKNOWN timeline — external).
- Local LLM answer quality on this machine (VERIFY, cheap: dogfood).
- sherpa-onnx capture/VAD glue quality on this mic (VERIFY, later).

## Sources (as cited by respondents)
- Repo + fork inspection: xfce4-terminal `terminal-screen.c:3485`
  (`terminal_screen_feed_text` → `vte_terminal_feed_child`),
  `terminal-control-bridge.xml`, bus introspections (live AND Xvfb) — primary.
- MCP: modelcontextprotocol.io (resources host-driven, 2026-06 spec),
  code.claude.com permissions (`requiresUserInteraction`), codex config.toml
  (local), mcp SDK 1.15.0 (local pip).
- Consent literature: Brustoloni & Villamarín-Salomón (SOUPS 2007) — audited
  dialogs reduce blind accepts.
- Ecosystem: agent-sh/computer-use-linux, KDE/GNOME AI landscape, Omarchy v4
  release notes (see AGENTIC-ROADMAP.md source list).