# History System Implementation Spec - 3-Mode History (Commands / Transcripts / Windows)

Status: SPEC (finalized with user 2026-09-06) - NOT yet implemented
Authoritative docs (READ FIRST):
1. `docs/design/terminal-history-semantics.md`
2. `docs/design/workspace-persistence-pipeline.md` (the original spec named
   `workspace-persistence-recovery-pipeline.md`; the real file is `workspace-persistence-pipeline.md`)
3. `docs/report-history-save-pane-window-tab.md`
4. `docs/ui-audit.md`
5. `AGENTS.md`

> **FROZEN GUI/UX**: no redesign, restyle, resize, recolor, icon replacement, tab
> geometry change, or context-menu layout change. Only implement behavior + state.
> If the UI must change -> STOP and report why.

> `implement-note-1.md` (old concept) is **superseded** by the new design and
> semantics docs - it must not be treated as the final spec.

---

## 1. Overview - History = "historical workspace browser" (3-MODE system)

> The History tab officially becomes a **"historical workspace browser"**, not a
> plain command list. It is the **workspace's memory at three levels**:
> **what did I run** (Commands)  |  **what did the terminal experience** (Transcripts)  | 
> **which working windows did I have** (Windows).

```text
History
|-- Commands       # command history per pane
|-- Transcripts    # terminal transcript/output history per pane
'-- Windows        # closed-window history / restore
```

The three kinds are **3 distinct concepts** - never stored or implemented as the
same thing. Option A was "a separate shortcut per kind", but **finalized: do not
invent too many shortcuts** - the user must not memorize `Ctrl+Shift+M` /
`Ctrl+Shift+T` / `Ctrl+Shift+W`.

**Keyboard decision (finalized):**
- `Ctrl+Shift+H` -> overall History panel
- Inside the panel: `[Commands] [Transcripts] [Windows]` are **filters/subviews** -
  no mandatory per-mode global shortcut.
- Later: if one kind is used frequently enough -> THEN add a dedicated shortcut.

**UI principle:** no new UI that breaks the frozen baseline; only wire behavior into
existing controls/panel.
**Restore screen != resurrect process:** when opened again, the pane shows its final
screen state, BUT the shell/pane is spawned fresh - no old process is restored
(details: `terminal-history-semantics.md`).

---

## 2. Command History (mode 1)

**Canonical source** (per-pane, unchanged):

```text
Workspace
 -> Window
 -> Tab
 -> Pane
 -> command_history[]
```

Each **CommandRecord** holds at minimum:

- `command`
- `timestamp`
- `pane identity`
- `tab identity`
- `window identity`

The History UI may aggregate across panes/windows, but the canonical underlying
source remains **per-pane**.

**User flow:**

```text
Focus pane
-> open History
-> Commands
-> select command
-> INSERT command into the focused pane's input
```

- **Does NOT** auto-execute on click (safe: insert into the cmd line; the user
  presses Enter to run).
- **The shell's native Up/Down is STILL shell-owned.** Remin does NOT replace
  bash/zsh/fish readline.

**Shell-native history vs Remin command history - the two are NOT exclusive:**

```text
Shell-native history  = managed by bash/zsh/fish (readline navigation, Up/Down)
Remin command history = Remin records committed commands per pane (command_history[])
```

- Up/Down: PTY -> shell readline. Remin does NOT replace it.
- History UI: `Pane.command_history[]`. Remin manages it.
- Result: Up/Down works **exactly like a normal terminal**, while Remin also has a
  **better historical browser than a normal terminal** (search/provenance/per-pane).

**Per-record provenance:**

```text
timestamp
workspace/window/tab/pane
command
```

**Display:**

```text
Today
15:21  ls -la
15:18  cd ~/remin
15:12  cmake --build build
Yesterday
...
```

---

## 3. Transcript History (mode 2)

**NOT command history.** Represents historical terminal output/context generated
during a pane's lifetime.

**Minimal per-TerminalPane model:**

```text
TerminalPane
|-- current screen state
|-- command history
'-- transcript history
```

- `clear` affects only **current screen state**.
- `clear` must NOT erase: `command history` + `transcript history`.

**Example:**

```text
$ ls
file-a
file-b

$ clear
```

After clear: current screen = prompt. But:
- Command history still: `ls`  |  `clear`
- Transcript still: `ls`  |  `file-a`  |  `file-b`  |  `clear`

**Transcript UI (unique - "review what the terminal actually went through"):**

```text
Window: GitLab Audit
Tab: Recon
Pane: 2

15:12
$ nmap ...

15:13
PORT 22/tcp open
PORT 80/tcp open
...

15:14
$ ffuf ...
```

Click transcript -> focus the correct pane/workspace or open the transcript view
depending on mode.

> **Transcript != Screen restore.** Current Screen = the pane's final state;
> Transcript History = what the pane rendered during its working life.
> clear: screen -> cleared; transcript -> NOT erased; command history -> NOT erased.

---

## 4. Transcript MUST NOT depend only on the VTE current screen

VTE = current terminal emulator state, **NOT a historical journal**.

If transcript retention after `clear` is needed, there must be a dedicated
**Remin-owned transcript path**:

```text
PTY / terminal runtime
      |--> VTE current screen/scrollback
      '--> Remin transcript recorder
```

For transcript history, do **NOT** simply rely on the final
`vte_terminal_get_text_range()` snapshot. For the immediate-persistence MVP: keep
VTE snapshot restoration separate from transcript journaling - do not silently merge
them.

---

## 5. Window History (mode 3)

= Windows **deliberately closed** while the Window History policy is ON.

**Example:**

```text
Window: W42, label = "GitLab Audit"
```

**Closing with Window History ON:**

```text
W42
-> capture final state
-> create closed-window history entry
-> keep label + timestamp
-> close runtime Window
```

**Closing with Window History OFF:**

```text
W42 -> close -> NO reopenable history entry created
```

**Restore Window (click "GitLab Audit"):**

- restore that Window snapshot.
- **Does NOT** create a new window first and then restore.
- **Does NOT** clone into `GitLab Audit (2)` - unless the user explicitly duplicates.

---

## 6. RECOVERY != WINDOW HISTORY

```text
Recovery:        latest valid open-workspace checkpoint
Window History:  historical snapshots of closed Windows (explicit)
```

**Do NOT merge the two into one collection.**

---

## 7. Separate "Current Workspace" from "Historical Data" (model)

```text
Workspace
|
|-- current state
|   |-- windows
|   |-- tabs
|   '-- panes
|
'-- history
    |-- command records
    |-- transcript records
    '-- closed-window snapshots
```

History is **NOT** one huge blob. Transcripts in particular can be large - storage
should use **record/chunk or separate blobs**, not the generic `settings` or
`scrollbacks`.

---

## 8. History UI (frozen)

- **NO redesign** of the current History panel. Use the existing panel.
- Inside, provide the 3 logical modes: `[Commands] [Transcripts] [Windows]`.
- Reuse the existing visual language. **No new decorative cards, no large buttons,
  no new chrome** unless already in the baseline.

---

## 9. Keyboard access

- `Ctrl+Shift+H` -> open/focus the History panel.
- Inside the panel: `Commands / Transcripts / Windows` = selectable modes.
- No extra global shortcuts unless a specific UX need arises.

---

## 10. Per-pane isolation (required)

Command history and Transcript are both **per-pane**:

```text
Pane A != Pane B
```

**Test:**

```text
Pane A:  printf 'TRANSCRIPT_A\n'
Pane B:  printf 'TRANSCRIPT_B\n'
```

History must keep provenance and never mix up data.

---

## 11. Clear semantics (finalized test)

```text
$ printf 'BEFORE_CLEAR\n'
$ clear
$ printf 'AFTER_CLEAR\n'
```

**After clear:**

```text
CURRENT SCREEN: only AFTER_CLEAR context / current prompt
COMMAND HISTORY: BEFORE_CLEAR command  |  clear  |  AFTER_CLEAR command
TRANSCRIPT:     BEFORE_CLEAR output  |  clear event/context  |  AFTER_CLEAR output
```

> Clear is a **SCREEN STATE operation**, not a **HISTORY DELETE operation**.

---

## 12. Startup restore

```text
If a valid recovery workspace exists -> restore it.
Do NOT create a default Window before restore.
Do NOT create duplicate windows.
Stable Window ID stays stable across checkpoints.
Checkpoint generation != Window identity.
```

---

## 13. Transcript storage - audit before choosing a journal

Before choosing an implementation, audit the current persist code:

- current VTE capture path
- current scrollback storage
- where PTY/VTE output can be observed **safely**
- whether recording terminal output interferes with the PTY
- expected storage growth

> Do **NOT** implement "append each byte synchronously to SQLite".
> Use buffered/chunked persistence. The UI thread MUST NOT block per event.

---

## 14. Performance

Transcript recording must NOT:

- block terminal rendering
- write one SQLite transaction per output event
- rescan entire terminal content continuously
- duplicate large buffers unnecessarily

-> Buffer/chunk output and persist through the existing checkpoint/session pipeline.

---

## 15. Persisted data model (proposed)

```text
CommandRecord        # per-pane command history record
TranscriptChunk      # per-pane transcript chunk
ClosedWindowSnapshot # closed-window history snapshot
```

Do not force everything into `settings` or the generic `scrollbacks` blob. Storage
should expose dedicated APIs.

---

## 16. No UI regression (frozen list)

- History panel visual structure
- tab UI / tabs
- buttons
- CSS
- colors
- iconography
- layout
- editor
- terminal context menu
- directory panel visuals

If the implementation requires a UI change -> **STOP** and report why.

---

## 17. Implementation order

```text
Phase A: audit current History panel
Phase B: wire Ctrl+Shift+H
Phase C: wire Command History into the per-pane canonical model
Phase D: implement transcript model/storage
Phase E: implement Window History storage/lifecycle
Phase F: wire the 3 modes into the existing History panel
Phase G: golden tests
```

---

## 18. Golden history test

```text
Window W1: "GitLab Audit"

Tab Recon:
  Pane A:  pwd  |  ls  |  printf 'A\n'
  Pane B:  pwd  |  printf 'B\n'

Perform: clear in Pane A
Close the Window with Window History ENABLED.

VERIFY:

COMMANDS:  Pane A commands separated from Pane B; timestamps/provenance kept.
TRANSCRIPT: Pane A transcript contains pre-clear output; Pane B separate;
           clear does NOT erase the transcript.
WINDOWS:   W1 appears in Window History; label = "GitLab Audit";
           timestamp exists; selecting it restores the closed Window state.
```

Then close/reopen the app and verify **Recovery stays independent**.

---

## 19. Acceptance (do not conclude prematurely)

Do NOT report completion just because:
- command history exists
- SQLite contains blobs
- unit tests pass

**Real acceptance requires ALL of the following simultaneously:**

```text
Command History works          (accepted)
Transcript History works       (accepted)
Window History works           (accepted)
per-pane isolation works       (accepted)
recovery works                 (accepted)
Ctrl+Shift+H works             (accepted)
clear semantics correct        (accepted)
```

---

## 20. Final report

Create/update: `docs/report-history-system.md`

Present the pipeline per feature:

```text
SOURCE -> CAPTURE -> STORAGE -> QUERY -> UI -> ACTION -> RESTORE
```

Record: implementation status  |  tests  |  known limits  |  storage growth assumptions
 |  performance measurements.
**Do not change the frozen UI during this process.**

---

## Relationship to old specs

- `implement-note-1.md` (original terminal history/capture concept) - **superseded**
  by `terminal-history-semantics.md` + this spec.
- `docs/problem-terminal-transcript-capture.md` - P0-B capture fidelity FAILING,
  still a real blocker: capture must be proven to contain deterministic markers
  before transcript can be said to work. Pinned to Phase D (transcript storage).
- `docs/design/workspace-persistence-pipeline.md` - only this filename exists;
  there is no `workspace-persistence-recovery-pipeline.md`.