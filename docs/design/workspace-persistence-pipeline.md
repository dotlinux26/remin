# Design - Workspace Persistence/Recovery Pipeline (Remin Core Feature)

**Status**: IMPLEMENTATION-GATED. Reviewed by the user with 5 fixes plus the
addition of `schema_version` and P2.5 (see Review Log at the end). After these
edits the user gave GO to implement P1-P5. Do not write code gap by gap; each
phase has a gate + test. Prerequisite: report
`docs/report-history-save-pane-window-tab.md` confirms we only have a foundation;
capture/restore has not yet gone through one full end-to-end cycle. This doc
pins the **canonical state model + capture/restore pipeline + atomic checkpoint**
before any code.

---

## 1. Objective

A single runnable end-to-end loop:

```text
LIVE WORKSPACE
   -> CAPTURE (everything the user is looking at)
   -> ATOMIC PERSIST (1 transaction, no half-written)
   -> PROCESS TERMINATION
   -> RECREATE
   -> RESTORE
   -> VERIFY (not "looks okay")
```

The single most valuable demo of the whole project: close Remin -> reopen ->
scrollback text comes back **EXACT** and the screen lands on the prompt/bottom.

**Commitment wording (pinned with reviewer)**: we restore *scrollback = EXACT*,
*viewport = bottom/prompt in V1*. We do not claim "exact scroll position" while
the user was scrolled up - VTE 0.76 does not expose `get/set_scrollback_offset`
(sec 5.4). Every acceptance test must follow these two levels; do not record
`viewport` as satisfied when the user was scrolled up.

---

## 2. "Remin State" = Workspace Checkpoint

A checkpoint is a snapshot of **everything the user is looking at + the context
to continue work**, not a save of individual window/tab/pane.

```text
Workspace
+-- identity (id, name, created_at, last_saved_at, schema_version, generation)
+-- ui state
|   +-- active window
|   +-- directory-tree state (current_dir, expanded[], selected, filter, scroll anchor)
+-- windows[]
    +-- window id, title
    +-- geometry (width, height; x, y if the compositor allows)
    +-- active tab
    +-- tabs[]
        +-- tab id, title, kind (Terminal | Note)
        |
        |  -- Terminal --
        |  +-- active pane
        |  +-- pane tree (kind + ratio)
        |  |    +-- pane[]
        |  |        +-- cwd                      (shell context cwd)
        |  |        +-- shell
        |  |        +-- cols / rows
        |  |        +-- environment              (V1: NOT persisted - sec 3.2)
        |  |        +-- scrollback (full text)
        |  |        +-- viewport  (V1: prompt/bottom - see sec 5.4)
        |  |        +-- command_history[]        (per-pane, canonical)
        |  |        +-- interrupted_command      (metadata)
        |  |
        |  -- Note --
        |  +-- note state
        |      +-- document_id, path
        |      +-- content
        |      +-- modified
        |      +-- cursor
        |      +-- scroll
        |      +-- preview_enabled
        |      +-- split_ratio
        |      +-- sync_scroll
        +-- (surface contract: capture_state()/restore_state())
```

**Source of truth (invariant)** - the capture/GUI boundary is not the Core
(boundary in sec 12):

```text
VTE/PTY  ->  TerminalPane.runtime_capture adapter  ->  TerminalRuntimeSnapshot
        ->  SessionController / WorkspaceSnapshotBuilder  ->  WorkspaceCore::apply_runtime_state
        ->  PaneState (canonical, owned by Core)
WorkspaceCore  ->  canonical state  ->  restore adapter (GUI)  ->  TerminalPaneView  ->  VTE + new PTY/shell
```

There is NO VTE -> SQLite, no VTE -> ad-hoc callbacks, no MainWindow -> history
vector, and NO Core calling GUI widgets directly. `WorkspaceCore` is authoritative
for **canonical state + transactions + generation**; the capture adapter and view
rebuild are the **GUI/runtime** responsibility. GTK widgets are only a runtime
representation of state.

---

## 3. Current-State Investigation (verified in code)

| Item | Current state |
|------|---------------|
| PaneState (cwd, shell, cols, rows, env, history, scrollback, interrupted) | exists (pane.hpp:15-23), serializable (serialization.cpp) but **dead** - never populated from the terminal |
| Scrollback | written (autosave.cpp:49-60 -> `scrollbacks` table), **never read back** (load_scrollback only handles note/settings) |
| Command history | 1 global list `settings:command-history` (session_controller.cpp:271-314); MainWindow keeps a copy `history_`; per-pane `PaneState.command_history` unused |
| Note tab | NOT part of the domain `Tab` (only terminal + PaneTree); body saved under note id but tab restore missing (main_window.cpp:543 TODO) |
| Directory tree | `DirectoryTreePanel::State` has all needed fields (directory_tree_panel.hpp:59-68) but private; no capture/restore API |
| Window geometry | `Window.x/y/width/height` serialized (window.hpp:32-35) but not applied on open; `set_default_size(1024,768)` is fixed (main_window.cpp:21) |
| Atomicity | `SqliteDb` has **no transaction helper** (sqlite_db.hpp:16-36) - individual statements only |
| Shutdown flush | **No** close/shutdown handler calls `flush_now()`; `close_workspace()` comment relies on a "shutdown flush" that does not exist (workspace_core.cpp:41-48) |
| Snapshot | API + CLI `snapshot.create` exist but GUI unused; `restore_snapshot` does not emit an event (workspace_core.cpp:351-359) |
| VTE 0.76 API (header checked) | has `get_current_directory_uri`, `get_text_range`, `feed`, `set_size`, `get_column/get_row_count`, `get_cursor_position`; does **NOT** have `get/set_scrollback_offset`; does **NOT** have `vte_pty_get_child_process_id` |
| Shell spawn | `vte_terminal_spawn_async` (terminal_pane.cpp:29-36); shell PID obtainable via `GChildWatchFunc` callback |
| Default CWD | `$HOME` (terminal_pane.cpp:22-24) |

---

## 4. Make PaneState LIVE

### 4.1 New TerminalPane contract

```cpp
// GUI/runtime adapter: read current VTE state -> pure-data snapshot
// (NOT PaneState - an intermediate so Core never depends on GUI, see sec 12).
remin::core::TerminalRuntimeSnapshot runtime_capture();

// GUI/runtime adapter: rebuild VTE from canonical PaneState.
void runtime_restore(const remin::core::PaneState& state);
```

- Capture runs **at checkpoint time** (not per keystroke) - one read.
- `WorkspaceSnapshotBuilder` (SessionController layer) assembles
  `TerminalRuntimeSnapshot` into the canonical `PaneState` and pushes it into
  `WorkspaceCore::apply_runtime_state(...)`.
- Restore runs once at pane birth (startup restore or new pane).

### 4.2 Source of each field

| Field | SOURCE (actual mechanism) |
|-------|--------------------------|
| `cwd` | sec 4 (shell context cwd) |
| `shell` | `shell_` in use (shell.hpp `detect_default_shell` or saved PaneState) |
| `cols`, `rows` | `vte_terminal_get_column_count/row_count` |
| `environment` | **V1: NOT persisted.** A new shell spawn inherits default env normally; a blocklist (TOKEN/PASSWORD/SECRET/KEY) is not safe enough. Future acceptance only via explicit allowlist / user opt-in |
| `command_history` | sec 6 - stack from this pane's commits (required at P2, not deferred) |
| `scrollback` | sec 5.1 - `vte_terminal_get_text_range` full scrollback + visible |
| `interrupted_command` | sec 6.2 - `InterruptedCommand{command, timestamp, source}` with `source = CtrlC | process_exit | unknown`; V1 assigns `CtrlC` ONLY when built from a real `\x03` commit, never inferred |

Do not populate fields with defaults just to make a serialization test pass.

---

## 5. Pane CWD - "shell context cwd"

### 5.1 Definition

> Pane cwd = the directory where the interactive shell currently stands ($PWD),
> i.e. the context the user wants to continue from - NOT the cwd of any child
> process running (ssh/vim/python/sudo...).

Correct restore example: Pane A -> `~/research/gitlab`, B -> `~/research/gitlab/poc`,
C -> `/tmp/testing`; each pane spawns a new shell in **that exact directory**.

### 5.2 Mechanism (priority order, based on verified VTE 0.76 API)

1. **Primary - `vte_terminal_get_current_directory_uri()` (OSC 7)**: the shell
   reports its "$PWD" via OSC 7 (`\e]7;file://host/path\a`). Correct "shell
   context" semantics, no /proc. Limit: only works when the shell emits OSC 7
   (zsh/fish on by default; bash needs PROMPT_COMMAND - we **do not** touch the
   user's rc; ship an optional shell-integration later).
2. **Fallback - `/proc/<shell_pid>/cwd`**: shell_pid from the `GChildWatchFunc`
   of `vte_terminal_spawn_async` (already using spawn, so just add a callback).
   Read the PID of **the shell only** (interactive `cd` changes it), never a
   foreground child -> correct semantics, no "blind kid-process read".
3. **Fallback - cached cwd**: updated on every `commit`/checkpoint (last value
   read), then `$HOME` if empty.

Restore: verify the path still exists -> spawn there; if gone (volume not
mounted) -> log + fall back to `$HOME`.

---

## 6. Scrollback - the "soul" of Remin

### 6.1 Capture (verified available)

`vte_terminal_get_text_range(vte, negative_start_row, 0, end_row, 0,
VTE_FORMAT_TEXT, ...)` with a negative `start_row` to cover the full scrollback
plus visible region. Current row = `get_row_count()`. (Standard scrollback-scan
technique of terminals with restore support.) `cols/rows` are captured alongside
to rebuild the grid at the correct size.

### 6.2 Restore - deterministic lifecycle, NO sleep/paste hacks

```text
1. Create VTE (shell not spawned yet), set size to captured cols/rows,
   set scrollback_lines = max(captured length, 10000)
2. vte_terminal_feed(captured_text)      <-- rebuild all visuals + scrollback
                                             from the widget buffer (not via PTY)
3. vte_terminal_spawn_async(shell, captured cwd)   <-- env INHERIT default (sec 3.2);
                                             shell prompt prints below restored content
4. Viewport: align to prompt/bottom (sec 5.4)
```

feed-before-spawn avoids "bash prints prompt then paste lands misaligned".
There is no `sleep(100ms)` anywhere.

We do **NOT** restore running processes (no reviving vim/ssh). We restore:
shell context (cwd) + scrollback + size + viewport, then spawn a new shell with
default env.

### 6.3 Fidelity (clearly separated)

| Type | Restored? |
|------|-----------|
| PTY runtime state (running process) | no - permanently out of scope |
| Scrollback text | yes, EXACT (`get_text_range` -> `feed`) |
| Visible screen (at prompt / mid output) | text restored; ANSI/color/format fidelity must pass the sec 5.5 gate |
| Viewport while user was scrolled up | V1: bottom/prompt only - sec 5.4 (no exact-scroll-position claim) |

### 6.4 Viewport - VTE 0.76 fidelity limit

Header checked: **VTE 0.76 does not expose `get/set_scrollback_offset`** and has
no `vte_pty_get_child_process_id`. Because `set_scroll_on_keystroke/output = TRUE`,
the "scrolled up" state only occurs while idle.

**Acceptance (pinned with reviewer)**: `scrollback` -> **EXACT**; `viewport` ->
**bottom/prompt in V1** (no claim of "exact position" when scrolled up). V1
viewport restore = align to prompt/bottom - exactly right for the demo scenario
(user closes while at prompt/keyboard). The scrollback text is fully there; the
user can scroll up. Record this in Known Limitations. Future enhancement =
OSC-based scroll tracking (shell integration) or a different VTE version.

### 6.5 Capture/Restore Fidelity Gate (mandatory - NOT treated as "proven" merely because the API exists)

`get_text_range()` returns **text**, but the runtime terminal also has cursor,
attributes, alternate screen, hyperlinks, colors... Re-feeding text may not
reproduce byte-for-byte visual state. A dedicated technical gate with a small test
is required:

```text
VTE live terminal
   | generate ANSI/control sequences   (color, bold, cursor move, alt-screen, NL/CR, wide chars)
v capture (get_text_range VTE_FORMAT_TEXT)
v new VTE  ->  feed
v compare visual/text semantics:  each output line keeps order + content;
                                  is ANSI color/format preserved as expected
```

**V1 goal (recorded so acceptance cannot fool itself)**: restore the terminal's
*textual scrollback faithfully*; do not restore arbitrary runtime terminal state
(absolute cursor position, alt-screen buffer, fine-grained hyperlink GRID...).
If this gate fails what we claim -> lower the claim, do not expand scope.

---

## 7. Per-Pane Command History (canonical)

```text
Workspace -> Window -> Tab -> Pane -> command_history[]
```

### 7.1 New flow

`TerminalPane` already has `on_command_` (commit -> split on `\n`, trim -
terminal_pane.cpp:162-182): the correct per-pane source. Instead of pushing
globally, `TerminalTabView::add_history` (terminal_tab_view.cpp:102-108) changes
to: `controller_->add_command_to_pane(tab, pane, cmd)` -> `WorkspaceCore`
appends to `PaneState.command_history` (dedupe adjacent commands, cap ~1000).
Drop `settings:command-history` as canonical (keep it only if needed to migrate
old data).

### 7.2 Interrupted command

```cpp
struct InterruptedCommand {
    std::string command;
    std::int64_t timestamp_us;           // when detected
    enum class Source { CtrlC, ProcessExit, Unknown } source;
};
```

- `source = CtrlC` ONLY when a commit containing `\x03` is actually observed right
  after that command. The old heuristic "last command + `\x03`" is inference -
  SIGTERM, app crash, user closing the pane, tool self-exit, and a child receiving
  Ctrl+C while the shell does not are all NOT Ctrl+C.
- `ProcessExit` = spawn callback reports the shell/program exited (GChildWatchFunc).
- `Unknown` = every remaining case. Max V1 guarantee: only record `CtrlC` when
  observed; never label every interrupted process as Ctrl+C.

### 7.3 Workspace History = view/query, not a second store

The History sidebar becomes an **aggregate query over all panes** of the current
workspace, keeping provenance `(window -> tab -> pane -> command)`.
`clear_history()` must persist (delete the `PaneState.command_history` entries),
not drop a temp copy (the `main_window.hpp` `history_` vector is removed).

---

## 8. Note Tab Becomes First-Class Domain State

The domain `Tab` gains `TabKind kind` + `std::optional<NoteTabState>` alongside
`PaneTree` (window.hpp:13-26). Serialization gets **migration**: old JSON (no
kind) -> default to terminal. `NoteTabState`: document_id, path, title, content,
modified, cursor (iter), scroll, preview_enabled, split_ratio, sync_scroll.
Restore: `restore_workspace()` builds the `NoteTabView` from state and sets
content/cursor/scroll/preview/split - not "body saved only".

---

## 9. Workspace UI State (directory tree + active window)

`Workspace` gains `UiState ui`:

```cpp
struct DirectoryTreeState {
    std::filesystem::path current_dir;
    std::vector<std::filesystem::path> expanded;
    std::filesystem::path selected;
    std::string filter;
    std::filesystem::path scroll_anchor;
    double anchor_offset{0.0};
};
```

`DirectoryTreePanel` exposes `capture_state()` / `apply_state()` (fields already
in `State`, directory_tree_panel.hpp:59-68 - now public). Restore applies state
before mapping, with no aggressive refresh-to-top (the panel already holds this
via the scroll anchor; it only needs the input state loaded). Active window:
`focus_window_id` already exists - applied on restore.

---

## 10. Window Geometry

- Capture (at checkpoint/close): read size + position from `Gdk::Toplevel`
  (`get_width/height`; position read per Wayland) -> `Window.x/y/width/height`.
- Restore: always `set_default_size(captured w/h)`; `move(x, y)` **only when the
  backend supports it** (Wayland compositors may refuse) - fail-safe to default.

---

## 11. Atomic Checkpoint (snapshot = transaction)

### 11.1 Semantics

```text
BEGIN IMMEDIATE
  -> capture the whole workspace (json + scrollback per pane)
  -> validate (valid ids, no half tree, generation increments)
  -> write: workspaces + n x (scrollbacks) + 1 snapshot row (reason, generation)
COMMIT
```

A failed checkpoint = rollback; it can **never** become "latest recovery".

### 11.2 Storage changes

- `SqliteDb` gains **transaction RAII**: `begin()/commit()/rollback()` (holding
  the mutex).
- The `snapshots` table gains `schema_version INTEGER NOT NULL`,
  `generation INTEGER NOT NULL`, `reason TEXT NOT NULL`
  (`recovery|autosave|window_history|manual`).
- `WorkspaceCore::checkpoint(reason)` - unified API for autosave/close/manual:
  capture + validate + write in one transaction + set `generation`,
  `last_saved_at`.
- **Recovery = latest valid committed checkpoint** (max generation, committed
  state). The current autosave (per-resource individual writes,
  workspace_core.cpp:368-375) is replaced by `checkpoint(reason=autosave)` - at
  this scale one workspace JSON + scrollback in one transaction is cheap and
  correctly atomic.

### 11.3 Schema version vs Generation (two SEPARATE concepts)

```json
{ "schema_version": 3, "generation": 108 }
```

| Concept | Changes when | Meaning |
|---------|--------------|---------|
| `schema_version` | the data structure changes (adding TabKind, NoteTabState, PluginState...) | tied to **shipping code**; needs migration when increased |
| `generation` | every successful checkpoint | checkpoint sequence number, monotonically increasing |

`generation 101 -> 102 -> 103`; if 104 fails -> latest valid = 103. Recovery
never points at half-written data. Migration by `schema_version` is the
invariant when adding new domain state (TabKind/NoteTabState/PluginState later).

---

## 12. The Three Persistence Options (D-7) Now Meaningful

- **Option 1 - Recovery**: shutdown/restart -> `checkpoint(recovery)`, display
  the latest state.
- **Option 2 - Window History**: user closes a Window -> snapshot that window
  (`.closed_window_history`). UI **not implemented (later task)** - the policy
  only reserves the field.
- **Option 3 - Input checkpoint**: ENTER -> checkpoint immediately; typing ->
  idle 10s -> checkpoint. The checkpoint covers the **WHOLE workspace** (Windows
  A + B + C simultaneously), not just the typed pane. Autosaver calls
  `checkpoint(reason=autosave|input)`.

`PersistencePolicy` struct (persistence_policy.hpp) is wired in: SessionController
reads/writes it; Autosaver uses `input_checkpoint` to decide flush -> checkpoint
vs skip.

---

## 13. Layered Data Flow (invariant)

```text
GUI runtime   (TerminalPane / NoteTabView / DirectoryTreePanel - capture adapters)
 v runtime_capture() -> TerminalRuntimeSnapshot / NoteState / TreeState
SessionController   (single orchestration + WorkspaceSnapshotBuilder)
 v apply_runtime_state(...)
WorkspaceCore   (canonical state + validate + checkpoint - authoritative)
 v transaction
Storage   (SqliteDb txn RAII, schemas)
```

Restore (reverse direction): `core canonical -> SessionController -> runtime
adapter (GUI) -> VTE + PTY/shell / editor / tree`.

**Invariant**: WorkspaceCore does **NOT** import GUI and does **NOT** call
`TerminalPane::capture_state()` - Core does not define the widget set. Capture
adapters belong to GUI/runtime; canonical state + transactions belong to Core.
That separation is exactly why `TerminalRuntimeSnapshot` and
`WorkspaceSnapshotBuilder` exist. MainWindow/NoteTabView/DirectoryTreePanel do
**not** read/write Storage directly (currently violated: `main_window` `history_`
and the global `add_history`).

---

## 14. Window Identity & Persistence Semantics (P5 contract)

### 14.1 Window = entity with stable identity

```text
Window
+-- id          # immutable, generated once
+-- label       # user-assigned name ("GitLab Audit"); rename only changes label
+-- state       # current tabs / panes / ui state
+-- lifecycle   # open / closed
```

- **Autosave/checkpoint only UPDATEs the state of currently existing windows.**
  NEVER INSERT a new Window because autosave ran.
- **A checkpoint is a version (generation) of workspace state, not a new Window.**

```text
W42    <- same W42 across G101, G102, G103 (generations = versions, not windows)
```

### 14.2 Mandatory behavior by moment

| Moment | Required behavior |
|--------|-------------------|
| Autosave | capture live state -> UPDATE each open window -> +1 generation. No W43/W44. |
| Multiple windows | checkpoint updates ALL open windows (W42+W51): no merge, no clone, no duplicate records. |
| Shutdown | capture live state **immediately before teardown** -> update every window -> `checkpoint("recovery")` -> destroy. Never restore a stale image. |
| Startup | **first** find the latest valid recovery checkpoint -> reconstruct -> restore -> **then** consider creating a default window. Never create a default window first and restore after. |
| Default window | only created when there is NO usable persisted/recovery state (empty DB). Valid persisted W42 -> restore W42, do **not** create "My Window 2". |

### 14.3 Programmatic invariants (enforced with tests/DB queries)

- Repeated autosave (create W42 -> checkpoint x3) => **1 Window identity + many
  generations**.
- App restart => **Window count in DB does not grow** just from restarting.
- Create W42 + W51, checkpoint, shutdown, restart => **exactly W42 + W51**, no
  extra default window.

### 14.4 Setting: `persist_open_windows` (key `settings:persist-open-windows`, default ON)

- **ON**: open windows join the recovery checkpoint; startup restores the latest
  open-window state.
- **OFF**: no session windows persisted/restored; startup **explicitly clears old
  window state (cleanup)** and the app runs fresh. Cleanup must be explicit -
  never silently INSERT duplicate Window entities while OFF.

### 14.5 "Most recent window"

The window from the **latest valid committed workspace state**, NOT
`MAX(created_at)` / max ID / the newest inserted window. V1 implementation:
`Workspace::focus_window_id` (written whenever a window is focused/added) = "the
window used most recently" at the last checkpoint.

### 14.6 Three SEPARATE concepts - never confused

```text
History
+-- Commands      # what the user ran (current sidebar, aggregate sec 7.3)
+-- Transcripts   # Remin-owned transcript path, independent of the VTE current
+                 # screen; `clear` does not delete history/transcripts
+-- Windows       # (future) closed windows to bring back - closed-window snapshot

Recovery          # last workspace before shutdown/restart
```

- Closing a helper: if closing W51 - Window History OFF => delete locally;
  Window History ON => capture final state + label + timestamp as closed-window
  history, delete live W51.
- Closing/restoring a window must **NOT** create/spawn a new Window in core.

### 14.7 V1 implementation (single-window GUI)

The GUI uses one core window; restore binds to `focus_window_id` (most recent)
and displays that window. Do **NOT** prune/delete other windows from the model -
they are valid entities and must survive restart (stable DB count). Multi-window
GUI = V2+. Default label for a new window = `My Window N` (N monotonically
increases to avoid collisions) via `SessionController::default_window_label`.
Setting key `settings:persist-open-windows` (default "1" = ON; empty = "1").

---

## 15. Lifecycle Changes

1. `Application::on_activate` opens the workspace -> do **not** auto-create a new
   tab when a valid checkpoint exists (main_window.cpp:120-123 currently wraps
   restore in `if (tabs_.empty()) new_terminal_tab()`); restore fully (sec 8, 9,
   10).
2. Close/quit: `MainWindow`/`Application` close handler -> `checkpoint(recovery)`
   -> `flush_now()` -> default close. (Current bug: no such handler.)
3. `Autosaver::write_entry` Kind::Terminal becomes part of the checkpoint
   transaction (no standalone `scrollbacks` table writes outside a transaction).
4. `restore_snapshot` gains emit-event + `mark_dirty` for GUI rebuild (later task,
   when the UI is attached).

**Autosaver fix (P5)**: periodic `flush()` (tick 250ms) - when a
`Kind::Workspace` entry is due, call the workspace provider -> runtime capture +
`checkpoint("autosave")`. `flush_now()` is for explicit save / shutdown flush.

---

## 16. Per-Field: SOURCE -> SERIALIZATION -> STORAGE -> RESTORE -> VERIFY

| Field | SOURCE | SERIALIZATION | STORAGE | RESTORE | VERIFY |
|-------|--------|---------------|---------|---------|--------|
| workspace id/name | core | json | workspaces | load | name matches |
| schema_version | core (const) | json + snapshots.schema_version | workspaces/snapshots | migration | changes when data schema changes, not on checkpoint |
| generation | core counter | json + snapshots.generation | workspaces/snapshots | recovery picks max | increments after each checkpoint |
| last_saved_at | checkpoint() | json + column | workspaces | - | != created_at after a save |
| active window | core focus_window_id | json | workspaces | apply focus | correct window active |
| dir tree state | DirectoryTreePanel::capture_state | json UiState | workspaces | apply_state() | expanded/selected/filter reproduced |
| window geometry | Gdk::Toplevel | Window.x/y/w/h | workspaces | set_default_size/move | size matches; position if backend allows |
| window/tab ids + titles | core | json | workspaces | recreate views | all windows/tabs, correct titles |
| tab active | core focus_tab_id | json | workspaces | set_visible_child | tab activation correct |
| pane tree + ratios | core | json (kind/ratio) | workspaces | build pane tree + restore ratio | split identical |
| pane shell | PaneState.shell | json | workspaces | spawn that shell | shell correct |
| pane cwd | shell-context cwd (sec 5) | json | workspaces | spawn at cwd (if it exists) | `pwd` matches |
| pane cols/rows | vte get_column/row_count | json | workspaces | vte set_size | grid matches |
| pane env | - V1: NOT persisted (sec 3.2) | - | - | spawn inherits default env | shell runs; no secret stored |
| scrollback | get_text_range (full) | json PaneState.scrollback (or scrollbacks table) | transaction | vte feed before spawn | old output fully present (EXACT) - sec 5.5 gate |
| viewport | V1 = prompt/bottom | json (marker) | workspaces | align bottom | prompt line on the expected screen (NO exact-scroll claim) |
| command_history[] | TerminalPane commit stack | json per-pane | workspaces | restore per pane | each pane gets its own history |
| interrupted_command | InterruptedCommand{cmd, ts, source} (sec 7.2) | json | workspaces | show metadata; source records truth | CtrlC only when `\x03` observed |
| note tab kind/state | NoteTabView | json NoteTabState | workspaces | recreate NoteTabView + apply | content/cursor/scroll/preview/split correct |
| note modified | is_modified() | json | workspaces | resync dirty-dot | correct unsaved state |
| active/focused pane | core focus_pane_id + GUI click | json | workspaces | apply focus + CSS class | correct pane focused |

---

## 17. Known Limitations (recorded, not hidden)

- **Exact viewport while scrolled up**: VTE 0.76 has no `get/set_scrollback_offset`
  -> V1 aligns to prompt; higher fidelity via shell integration (OSC-based scroll
  tracking) in a later phase. Acceptance: scrollback EXACT, viewport
  bottom/prompt - nothing more.
- **ANSI/color/format fidelity**: `get_text_range` -> text, `feed` rebuilds the
  grid; cursor/alt-screen/hyperlink GRID may not be reproduced exactly. The
  sec 5.5 gate decides the claim; if it fails -> lower the claim, do not expand
  scope.
- **OSC 7 cwd**: depends on the shell emitting OSC 7; the /proc fallback reads
  only the shell PID (not a child) - rarely wrong but still a heuristic.
- **Restore does not revive processes** (a running vim/ssh is "cut off"): by design.
- **environment NOT persisted in V1** (blocklist not safe enough); later only via
  explicit allowlist / user opt-in.
- **interrupted_command**: V1 assigns `source=CtrlC` only when `\x03` is actually
  observed; every other interrupt is recorded `ProcessExit`/`Unknown`, never
  guessed.
- **Note cursor/scroll** is exact down to iter/offset; absolute scroll depends on
  regenerated line heights - best-effort.

---

## 18. Golden Acceptance Test (the goal of the whole project)

Manual scripted scenario (recorded checklist, **no "looks okay"**).

**Workspace: GitLab Audit**
- Window 1: Tab Recon (2 panes: run `pwd`, `nmap...`, `ffuf...`, 1 CTRL+C command) + Tab Source (Note)
- Window 2: Tab Testing (2 panes: `python poc.py`, `nc ...`)
- Note: edit, scroll, preview ON, split
- Directory: expand project/src, scroll to exploit/

**Then**: checkpoint -> terminate Remin -> start Remin.

**MUST restore**: window 1+2, tabs, pane split, each pane `cwd` (check actual
`pwd`), per-pane shell history, terminal scrollback (**EXACT**), viewport =
prompt/bottom (no exact-scroll-while-scrolled-up claim), ANSI/color in scrollback
(per sec 5.5 gate - record the actual result), note content, note tab, note
scroll/preview, directory tree + expanded, active window/tab/pane, layout ratios.

The final report must contain, for each field:
`SOURCE -> SERIALIZATION -> STORAGE -> RESTORE -> UI VERIFICATION` record. No item
may be recorded as "looks okay" in place of a real record.

---

## 19. Phase Plan (ordered - each phase has a gate + test)

```text
            REMIN CORE FEATURE
                   |
   +---------------+----------------+
   | P1 Domain: Tab kind/NoteState  | UNITS: serialization round-trip
   |       UiState, migration       |        + schema_version migration
   +--------------------------------+
   | P2 Runtime Capture: runtime_capture | UNIT: cwd/pwd, cols/rows,
   |   adapter + command_history        |       command_history[p] populated
   +--------------------------------+
   | P2.5 Fidelity Gate (sec 5.5)   | GATE: ANSI/color + scrollback compare;
   |                                |       if fail -> lower claim, no scope expansion
   +--------------------------------+
   | P3 Transaction: SqliteDb txn   | UNIT: checkpoint atomicity,
   |       checkpoint(reason),      |       schema_version + generation,
   |       autosave->checkpoint,    |       recovery = latest valid
   |       shutdown flush           |
   +--------------------------------+
   | P4 Restore: full restore       | MANUAL: note + tree + geometry + focus
   |       workspace (terminals,    |
   |       notes, tree, geo)        |
   +--------------------------------+
   | P5 Golden acceptance + report  | GOLDEN WORKFLOW (sec 18, recorded)
   +----------------+---------------+
                    |
      CORE PERSISTENCE WORKS
                    |
   +----------------+---------------+
   | Option 1: Recovery             | shutdown/restart -> checkpoint(recovery)
   | Option 2: Window History UI    | later task, ONLY AFTER P1-P5 PASS
   | Option 3: Input checkpoint     | ENTER -> checkpoint; typing -> idle -> checkpoint
   +--------------------------------+
```

**NOT implemented now**: Window History UI, Ctrl+Shift+H (distinct from
Ctrl+H find/replace), snapshot browser, portable export, plugin persistence.
They are *consumers* of this engine. TerminalTab/NoteTab/PluginTab later all talk
to Remin through the same `runtime_capture()/runtime_restore()` contract - that is
how the workspace platform takes shape.

**Mandatory order**: P1-P5 complete (**Core Persistence Works**) -> then Option
1-3. No reordering. No Window History / UI polish before passing: terminal capture
-> SQLite transaction -> process termination -> workspace reconstruction ->
scrollback/history/cwd restore. That is the moment Remin truly begins to exist.
The UI/UX is already LTS stable (v0.0.3lts); from here on, only persistence work.

---

## 20. Files Changed (projected - not code yet)

| Area | Change |
|------|--------|
| `core/workspace/workspace.hpp` | add `schema_version`, `generation`, `UiState` (dir tree, active window) |
| `core/window/window.hpp` | `Tab` + `TabKind` + `NoteTabState` (migration) |
| `core/pane/pane.hpp` | `PaneState` keeps fields (now alive); `InterruptedCommand{command,timestamp,source}`; env unused in V1 |
| `core/workspace_core.{hpp,cpp}` | `apply_runtime_state(...)` (canonical ingest, no widgets), `checkpoint(reason)` (validate + transactional write), restore hook, `restore_snapshot` emits |
| `core/serialization.{hpp,cpp}` | UiState / NoteTabState / kind / schema_version / generation |
| `storage/sqlite/sqlite_db.{hpp,cpp}` | transaction RAII (BEGIN IMMEDIATE/COMMIT/ROLLBACK) |
| `storage/sqlite/sqlite_db.cpp` (schema) | snapshots + schema_version + generation + reason (migration) |
| `core/persistence_policy` | wiring (SessionController get/set; autosaver uses input_checkpoint) |
| `core/autosave.cpp` | Terminal/Note flush folded into the checkpoint transaction |
| `gui/session/session_controller.cpp` | WorkspaceSnapshotBuilder, add_command_to_pane, aggregate history query, persistent clear, policy read/write |
| `gui/terminal/terminal_pane.{hpp,cpp}` | `runtime_capture()/runtime_restore()` adapter, cwd (sec 5), viewport (sec 6.4) |
| `gui/window/main_window.{hpp,cpp}` | full restore, close -> checkpoint, remove history_ vector, geometry capture |
| `gui/window/terminal_tab_view.cpp` | add_history -> per-pane, calls the pane runtime adapter |
| `gui/window/directory_tree_panel.{hpp,cpp}` | public `capture_state()/apply_state()` |
| `gui/window/note_tab_view.{hpp,cpp}` | capture/restore NoteTabState |
| `gui/window/settings_dialog.cpp` | Persistence page (3 policies) bound to PersistencePolicy |
| `tests/unit/...` | serialization migration, checkpoint atomicity, per-pane history, cwd fallback logic, sec 5.5 fidelity gate |

---

## 21. Review Log & Approve Record (authoritative)

**Reviewer: user - Verdict: ~95% ready -> after 5 fixes = APPROVE IMPLEMENTATION.**

| # | Review point | Applied at | Status |
|---|--------------|------------|--------|
| 1 | Commitment wording: `scrollback=EXACT`, `viewport=bottom/prompt in V1` - no "exact position" claim (VTE 0.76 does not expose scrollback offset) | sec 1, 6.3, 6.4, 18 | done |
| 2 | Dedicated technical gate for `get_text_range() -> feed()`: VTE live -> ANSI/control -> capture -> new VTE -> feed -> compare. V1 goal = restore textual scrollback faithfully, not arbitrary runtime terminal state | sec 5.5, P2.5 | done |
| 3 | `PaneState.environment` NOT persisted in V1 (blocklist unsafe: AWS_KEY/GITHUB_TOKEN/KRB5CCNAME/SSH_AUTH_SOCK...). Future only allowlist/opt-in; new shell inherits default env | sec 3.2, 6.2, 16, 17 | done |
| 4 | `interrupted_command` -> `InterruptedCommand{command, timestamp, source=CtrlC|ProcessExit|unknown}`. V1 assigns `CtrlC` only when `\x03` actually observed, never inferred (SIGTERM/app crash/pane close/tool exit != Ctrl+C) | sec 7.2, 3.2, 16 | done |
| 5 | `WorkspaceCore::checkpoint()` must not itself capture GUI widgets - breaks dependency direction. Boundary: GUI runtime -> SessionController/WorkspaceSnapshotBuilder -> `WorkspaceCore::apply_runtime_state(...)` -> Storage. TerminalRuntimeSnapshot = pure-data intermediate | sec 2, 4.1, 13, 20 | done |
| 6 | `schema_version` (changes when data structure changes -> needs migration) separate from `generation` (increments per checkpoint) | sec 11.3, 16, 20 | done |
| 7 | Phase plan adds **P2.5 Capture/Restore Fidelity Gate** (sec 5.5); `command_history[]` required at **P2** (golden restore must prove Remin remembers terminal work) | sec 19, P2 | done |
| 8 | Strict order: P1-P5 (**Core Persistence Works**) -> Option 1 Recovery -> Option 2 Window History -> Option 3 Input checkpoint. No reordering, no Window History/UI polish before passing capture->txn->terminate->reconstruct->restore | sec 19 | done |

**GO**: start implementing P1. Each phase closes with its own gate + test, no
"looks okay". Record every result `SOURCE -> ... -> UI VERIFICATION`.