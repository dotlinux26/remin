# Golden Acceptance Test Checklist — Remin Core Persistence (P5)

> **Design reference**: §16 Golden Acceptance Test
> **Rule**: No "looks okay". Every field must have SOURCE → SERIALIZATION → STORAGE → RESTORE → UI VERIFICATION record.

---

## Test Scenario: "GitLab Audit" Workspace

### Setup Steps

1. Start fresh Remin (no existing workspace)
2. Create workspace "GitLab Audit"
3. Configure as follows:

#### Window 1: "Recon"
- **Tab "Recon"** (Terminal) — split vertically into 2 panes:
  - Pane A (left): Type `pwd` → Enter
  - Pane A: Type `nmap -sV 10.0.0.1` → Enter
  - Pane A: Type `ffuf -u http://10.0.0.1/FUZZ` → Enter
  - Pane A: Type `cat /etc/passwd` → **Ctrl+C** (interrupt)
  - Pane B (right): Type `pwd` → Enter
  - Pane B: Type `ls -la /tmp` → Enter
- **Tab "Source"** (Note):
  - Create new note tab
  - Edit content: "GitLab source code audit notes\n# Recon phase\n- nmap scan\n- ffuf fuzzing\n- credential check\n\n# Exploit phase\n- python poc\n- nc listener"
  - Scroll down halfway
  - Enable preview (toggle preview ON)
  - Split editor/preview (50/50)

#### Window 2: "Testing"
- **Tab "Testing"** (Terminal) — split horizontally into 2 panes:
  - Pane A (top): Type `pwd` → Enter
  - Pane A: Type `python3 poc.py` → Enter
  - Pane B (bottom): Type `pwd` → Enter
  - Pane B: Type `nc -lvp 4444` → Enter

#### Directory Tree (Files sidebar)
- Expand `$HOME/project/src`
- Scroll down to `exploit/` directory
- Select a file in `exploit/`

#### Focus State
- Active window: Window 1 ("Recon")
- Active tab in Window 1: "Recon" (Terminal)
- Active pane in "Recon": Pane B (right)
- Active pane in Window 2: Pane A (top)

---

## Execution Steps

### Phase 1: Checkpoint
1. Wait for autosave debounce (2s terminal, 10s note) OR trigger manual checkpoint:
   - Close Remin window (X button) → should trigger recovery checkpoint

### Phase 2: Terminate
1. Verify process exits cleanly
2. Check SQLite database has workspace + scrollbacks + snapshot

### Phase 3: Restart
1. Start Remin again
2. Observe automatic workspace restore

---

## Verification Checklist

For each field, record: **SOURCE → SERIALIZATION → STORAGE → RESTORE → UI VERIFICATION**

| # | Field | Source | Serialization | Storage | Restore | UI Verification (Actual) | Pass/Fail |
|---|-------|--------|---------------|---------|---------|-------------------------|-----------|
| 1 | Workspace ID | | | | | | |
| 2 | Workspace name | | | | | | |
| 3 | Schema version | | | | | | |
| 4 | Generation | | | | | | |
| 5 | Window 1 ID | | | | | | |
| 6 | Window 1 title | | | | | | |
| 7 | Window 1 geometry (w/h) | | | | | | |
| 8 | Window 2 ID | | | | | | |
| 9 | Window 2 title | | | | | | |
| 10 | Window 2 geometry (w/h) | | | | | | |
| 11 | Tab "Recon" ID | | | | | | |
| 12 | Tab "Recon" kind=Terminal | | | | | | |
| 13 | Tab "Source" ID | | | | | | |
| 14 | Tab "Source" kind=Note | | | | | | |
| 15 | Tab "Testing" ID | | | | | | |
| 16 | Tab "Testing" kind=Terminal | | | | | | |
| 17 | Pane tree: Window 1 "Recon" split vertical | | | | | | |
| 18 | Pane tree: Window 2 "Testing" split horizontal | | | | | | |
| 19 | Pane A (Recon left) cwd | | | | | | |
| 20 | Pane A (Recon left) cols/rows | | | | | | |
| 21 | Pane A (Recon left) scrollback EXACT | | | | | | |
| 22 | Pane A (Recon left) history: pwd, nmap, ffuf, cat+CtrlC | | | | | | |
| 23 | Pane A (Recon left) interrupted_command = cat /etc/passwd (CtrlC) | | | | | | |
| 24 | Pane B (Recon right) cwd | | | | | | |
| 25 | Pane B (Recon right) cols/rows | | | | | | |
| 26 | Pane B (Recon right) scrollback EXACT | | | | | | |
| 27 | Pane B (Recon right) history: pwd, ls -la /tmp | | | | | | |
| 28 | Pane A (Testing top) cwd | | | | | | |
| 29 | Pane A (Testing top) cols/rows | | | | | | |
| 30 | Pane A (Testing top) scrollback EXACT | | | | | | |
| 31 | Pane A (Testing top) history: pwd, python3 poc.py | | | | | | |
| 32 | Pane B (Testing bottom) cwd | | | | | | |
| 33 | Pane B (Testing bottom) cols/rows | | | | | | |
| 34 | Pane B (Testing bottom) scrollback EXACT | | | | | | |
| 35 | Pane B (Testing bottom) history: pwd, nc -lvp 4444 | | | | | | |
| 36 | Tab "Source" note content | | | | | | |
| 37 | Tab "Source" cursor offset | | | | | | |
| 38 | Tab "Source" scroll fraction | | | | | | |
| 39 | Tab "Source" preview_enabled = true | | | | | | |
| 40 | Tab "Source" split_ratio = 0.5 | | | | | | |
| 41 | Tab "Source" sync_scroll = false | | | | | | |
| 42 | Directory tree current_dir | | | | | | |
| 43 | Directory tree expanded paths | | | | | | |
| 44 | Directory tree selected path | | | | | | |
| 45 | Directory tree filter | | | | | | |
| 46 | Directory tree scroll anchor | | | | | | |
| 47 | Active window = Window 1 | | | | | | |
| 48 | Active tab in Window 1 = "Recon" | | | | | | |
| 49 | Active pane in "Recon" = Pane B (right) | | | | | | |
| 50 | Active pane in "Testing" = Pane A (top) | | | | | | |
| 51 | Layout ratios (pane splits) | | | | | | |
| 52 | Window 1 geometry restored | | | | | | |
| 53 | Window 2 geometry restored | | | | | | |
| 54 | Viewport = prompt/bottom (not scroll-đúng-chỗ) | | | | | | |
| 55 | ANSI/color in scrollback (per §5.5 gate result) | | | | | | |

---

## Window Identity & Persistence Semantics — runtime verification (2026-09-06)

Source of truth: `AGENTS.md` → "Window Identity & Persistence Semantics (P5 contract)".
All checks below were executed against the real release binary
(`build/src/app/remin gui`, `XDG_DATA_HOME` isolated) and read back from SQLite
directly (python3 sqlite3). Evidence kept under `/tmp/opencode/{fresh,acc1}`.

| # | Invariant | Verified result | PASS |
|---|-----------|-----------------|------|
| 1 | Fresh launch ⇒ exactly ONE Window entity, unique id, `focus_window_id` set | `windows=[('cd05a211…','window',1)] focus=cd05a211…` on brand-new DB | ✅ |
| 2 | Every checkpoint UPDATES the existing window in place (generations = versions, no new Window) | Repeated autosave: workspace rows stayed `1`, same window id, `gen 1→3` | ✅ |
| 3 | Restart ⇒ DB window count does NOT increase, same Window identity restored | Restart showed `windows=[('cd05a211…', …)]` — identical id, no new default window | ✅ |
| 4 | Multi-window (W42 + W51): both restored and both survive subsequent checkpoints (no merge/clone/duplicate) | Injected `W42 'window'` + `W51 'CTF'`; relaunch + autosave: both present, 1 tab each, same ids | ✅ |
| 5 | Most-recent-window binding = `focus_window_id` from latest committed state | After W42+W51 checkpoint with `focus=W51`, restore bound W51 (`'CTF'`), no new spawn | ✅ |
| 6 | Setting OFF (`settings:persist-open-windows=0`) ⇒ startup clears stored window state explicitly + runs fresh, never duplicates | OFF launch cleared W42/W51; DB ended with exactly ONE fresh window | ✅ |
| 7 | Window label is persistent and serialized (`label`; old checkpoints read via `title` fallback) | fresh launch label `'My Window 1'`; injected `'CTF'` label restored intact | ✅ |
| 8 | Default window only when no usable state | Empty DB → 1 default window; non-empty DB → restored window, no "My Window 2" | ✅ |

> During this run a real bug was found and fixed: `Autosaver::flush()` never
> invoked the workspace provider, so periodic-autosave checkpoints never fired
> (workspace row stuck at `generation 0`). `flush()` now triggers the workspace
> provider when a `Kind::Workspace` entry is due. Fix verified — checkpoints
> bump generations (`gen 1→3`) while Window identity stays stable.

---

## ANSI/Color Fidelity Gate (§5.5) Record

Record actual gate result from P2.5 test:

| Metric | Expected | Actual | Notes |
|--------|----------|--------|-------|
| Textual scrollback round-trip | EXACT | | |
| Semantic content preserved | Yes | | |
| Color/format preserved | No (by design V1) | | |

---

## Final Report Template

```
# Golden Acceptance Test Report — Remin Core Persistence

**Date**: YYYY-MM-DD
**Commit**: <git sha>
**Environment**: Linux (Kali), GTK4/VTE 0.76, Wayland

## Summary
- Total fields verified: 55
- Passed: X
- Failed: Y
- Skipped: Z

## Field-by-field Records
[Each field with SOURCE→SERIALIZATION→STORAGE→RESTORE→UI VERIFICATION]

## Known Limitations (per design)
- Viewport = prompt/bottom only (no scroll-offset API in VTE 0.76)
- ANSI/color NOT preserved (VTE_FORMAT_TEXT strips them)
- Window position not restored (Wayland compositor control)
- No process resurrection (vim/ssh not restored)

## Gate Results
- P1 Domain state + migration: PASS
- P2 Runtime capture + per-pane history: PASS
- P2.5 Fidelity gate: PASS (textual EXACT, color NOT preserved)
- P3 Atomic checkpoint: PASS
- P4 Full restore: PASS
- P5 Golden acceptance: [this report]
```

---

## Sign-off

Tester: ________________
Date: ________________
All records verified: ☐ Yes ☐ No