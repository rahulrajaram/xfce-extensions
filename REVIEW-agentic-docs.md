# Code Review — agentic research docs (AGENTIC-ROADMAP.md, GRADIENT-OUTCOME.md)

Reviewer: independent assessment for human verification. Status: **HOLD — do not
commit or push any of this yet.**
Criteria applied (from the author): nothing research-heavy, nothing experimental,
nothing we aren't ready to publish should go into the repo or to GitHub.

Repo state at review time: both files are **untracked**; `main` == `origin/main`
(`f3c818a`); no commit or push made from this work.

---

## Verdict summary

| File | Verdict | Reason |
|---|---|---|
| `AGENTIC-ROADMAP.md` | **REMOVE from repo** (keep local) | Entirely a research memo + strategy portfolio; research-heavy by definition; contains third-party funding figures and a named-person account that is *public* but reads as proprietary strategy |
| `GRADIENT-OUTCOME.md` | **REMOVE from repo** (keep local) | Internal decision log: methodology, experimental build order, falsifiers, local operational facts |

Recommended disposition: keep both files **out of git** (they remain as
untracked working files, or move them to a folder outside the repo /
`docs/private/` so they can't be `git add .`'d accidently). Do not publish a
trimmed version until you decide this is public strategy.

---

## 2. AGENTIC-ROADMAP.md — content inventory & flags

187 lines: research synthesis feeding a product thesis. Every section is
either research-heavy, strategy, or both.

| Section | Content type | Push-safety | Flag |
|---|---|---|---|
| §1 "The one-paragraph thesis" | Strategy thesis + repo moat claim | **Risk** | Names Omarchy marketing copy; asserts "no competitor head start" |
| §2 "What Omarchy actually is" | Research (public) + **funding/institutional details** | **Risk** | DHH, Omacom Foundation, "$10–13M pledged", patron org names (Shopify/Stripe/Dell / Dorsey / Dropbox founders), 1Password/$37signals contribution narrative, The Verge controversy reference |
| §3 "State of the agentic desktop" | Research survey | Moderate | All public (KDE/GNOS js), but long source dumps + product names read as research dump |
| §4 "XFCE reality check" | Research (technical facts) | Moderate | Public facts; includes strategic conclusions ("xfwm4 is X11-only, period") |
| §5 "Portfolio" table | **Product strategy** (P0/P1/P2, effort tiers) | **Risk** | Explicit roadmap with initiative names + repo anchors |
| §6 "Moat — xfce-terminal-mcp" | **Product strategy** | **Risk** | Recommends first build, tool surface |
| §7 "Build notes & risks" | Strategy / decision context | **Risk** | Internal engineering reasoning |
| §8 "Open questions for us" | **Internal deliberation** | **Risk** | Clearly-internal Q&A |
| Sources list | Research citations | Neutral | Public links; length itself signals "research doc" |

**Bottom line:** this is a strategy/research memo, not repo documentation. If
you later want *some* public doc, the only publishable residue would be a
3–5 line "Status / direction" blurb in README.md — not this file.

---

## 3. GRADIENT-OUTCOME.md — content review

| Section | Content type | Flags? |
|---|---|---|
| Method block (ideate × gradient × debate, budgets, "13 agent slots") | **Internal methodology** | Risk — describes your private agent workflow |
| "Verified ground truth (preflight)" | Operational facts: packaged Debian `xfce4-terminal` 1.0.4-1 lacks bridge; `~/.codex/config.toml` has 3 MCP servers; local Ollama model speeds; local paths | **Risk** — personal environment fingerprint, internal findings |
| "Lattice conclusions" (act-path, consent, adoption) | Decision content | Risk — internal decisions + proposed design |
| "Recommended build order" | **Experimental strategy** | Risk — exactly what you said not to push yet |
| "Falsifiers to instrument" | Metastrategy | Risk — experimental |
| "Open questions" (incl. a HUMAN-flagged fork-swap decision) | Decision ledger | Risk |
| Sources | Mixed | light |

**Bottom line:** internal decision-procedure log; not intended for a public
repo. Recommend removal from any version control.

---

## 3. What should be removed if a public doc is ever wanted

If you ever decide to publish *anything*, the removal list is:

**Remove entirely (never publish):**
1. GRADIENT-OUTCOME.md — the whole file.
2. AGENTIC-ROADMAP.md §1, §5, §6, §7, §8 — all strategy/deliberation.
3. AGENTIC-ROADMAP.md §2 funding & people detail — DHH personal lore, foundation
   amounts, patron orgs, press controversy.

**Would be OK if trimmed for publication:**
- §4 XFCE facts (public technical facts) — standalone, low risk.
- §3 ecosystem survey — low risk but bulky; trim to named links if published.

---

## 4. Your verification checklist

- [ ] I accept the REMOVE/keep-local verdict on both files
- [ ] (or) I want a trimmed public doc — open the specific flags I listed
- [ ] I want the files kept as untracked working files (move to `docs/private/`
      or add to `.gitignore` when I say so) — avoided touching .gitignore
- [ ] Confirm: nothing to push — `main` matches `origin/main` (verified above)

---

## 5. What I did / did not do

- Undid commit `68e680b` (mixed reset) — branch is back to `f3c818a`,
  `origin/main` matches; the commit is unreachable (never pushed), only a
  reflog reference.
- Left both research files **untracked** — nothing deleted, nothing staged.
- Did **not** touch `.gitignore`, did **not** delete files, did **not** push.
- Suggested next: separate `docs/private/` (or ignore rule) when you choose;
  until then the files simply stay out of `git add`.