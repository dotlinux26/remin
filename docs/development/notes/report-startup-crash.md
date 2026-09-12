# Startup Crash Report — TerminalPane use-after-free on restore

Date: 2026-09-06
Status: FIXED (verified ASan-clean + release-clean)
Build dir affected: `build/` (release), `build-debug/`, `build-asan/`

## 1. Symptom (user-visible)

`./remin gui` against the real database (`~/.local/share/remin/remin.db`,
which carries a legacy 453-window artifact and note tabs) crashes at startup:

```
Gtk-CRITICAL **: gtk_text_buffer_get_modified:
    assertion 'GTK_IS_TEXT_BUFFER (buffer)' failed
Segmentation fault (core dumped)
```

The Gtk-CRITICAL is a **red herring**: the first failure is a heap-use-after-free
in a `TerminalPane`. The unrelated `gtk_text_buffer_get_modified` assertion on a
note editor is a downstream/coincidental symptom that appears on the same frame
but is **not** the root cause (proved below by reproducing with note-only DB
where nothing crashes).

## 2. Reproduction

### Build
ASan build (`build-asan`) is required to surface the true error (release build
just segfaults without reason).

### Command
```
cd /home/nguyenduccanh/remin
gdb -q -batch -ex "set env XDG_DATA_HOME /home/nguyenduccanh/.local/share/remin" \
  -ex "set env WAYLAND_DISPLAY wayland-0" -ex "set env XDG_RUNTIME_DIR /run/user/1000" \
  -ex run -ex "bt 40" --args ./build-asan/src/app/remin gui
```

### Dataset triage (isolated XDG_DATA_HOME)
| Dataset | Result |
|---------|--------|
| A. Empty DB | runs fine (no crash) |
| B. terminal-only | runs fine, including second launch/restore |
| C. note-tab-only | runs fine |
| D. Real DB (note + terminal tabs) | **crash** |

The crash required the combination present in the real DB: a focus window with
**both terminal and other tabs**. It is a terminal-restore lifecycle bug, not a
note-editor bug.

## 3. Root cause

`MainWindow::restore_workspace()` (`src/gui/window/main_window.cpp`) built each
restored terminal tab with the **wrong constructor**.

```cpp
// BUG:
auto* view = new TerminalTabView(controller_, this, win.id, tab.id, remin::core::PaneId{});
// 5-argument (non-restore) ctor → calls rebuild() → spawn_shell() async
```

`TerminalTabView` has two constructors:
- `(controller, main_window, window, tab, root_pane)` — the **normal** ctor,
  which calls `rebuild()` → `build_node()` → `make_unique<TerminalPane>` →
  `TerminalPane` ctor runs `vte_terminal_spawn_async(..., &on_spawned_trampoline, this)`.
- `(controller, main_window, window, tab)` — the **restore** ctor, which
  deliberately does **not** call `rebuild()` (the pane tree is supplied later
  via `restore_pane_tree()`).

The buggy line passed 5 arguments (including `remin::core::PaneId{}`), binding
the **non-restore** ctor — it spawned shells asynchronously. Then
`view->restore_pane_tree(tab.pane_tree)` ran, whose first step is
`panes_.clear()` (the just-spawned `TerminalPane`s), destroying a pane whose
`vte_terminal_spawn_async` completion callback was still pending. When the GLib
main loop later dispatched that callback, `on_spawned_trampoline` dereferenced
the freed `this` → heap-use-after-free → SIGSEGV.

### ASan evidence
```
WRITE of size 8 ... heap-use-after-free
  #0 TerminalPane::on_spawned_trampoline  terminal_pane.cpp:119   (this = freed)
freed by thread T0:
  #11 TerminalTabView::restore_pane_tree  terminal_tab_view.cpp:443  (panes_.clear())
previously allocated:
  #4  TerminalTabView::TerminalTabView     terminal_tab_view.cpp:74   rebuild()
  #2  TerminalTabView::build_node          terminal_tab_view.cpp:294  make_unique<TerminalPane>
  (call chain rooted at MainWindow::restore_workspace main_window.cpp:578)
```

## 4. Fix

`src/gui/window/main_window.cpp:578` — drop the trailing `remin::core::PaneId{}`
so the store-bound **restore constructor** is selected (no `rebuild()`, no
premature async spawn):

```cpp
// FIXED:
auto* view = new TerminalTabView(controller_, this, win.id, tab.id);
```

The pane tree is created exactly once, in `restore_pane_tree()`, from persisted
state — the intended restore flow. No pane is spawned-and-immediately-destroyed,
so no pending async callback can fire into a freed object.

## 5. Why the fix is safe

- A restored terminal tab has no "current pane" yet; `root_pane` is supplied
  after restore, so the non-restore ctor's `rebuild()` was redundant work that
  had to be thrown away by `restore_pane_tree()`'s `panes_.clear()`.
- The restore ctor was designed for exactly this path (see its comment at
  `terminal_tab_view.cpp:101`: "Don't call rebuild() here; the pane tree will
  be restored via restore_pane_tree()").
- No UI/CSS/layout change. Pure object-lifecycle correction.
- The async-spawn callback lifetime hazard in `TerminalPane` remains, but is now
  only reachable when a pane is destroyed while actually in use (tab close etc.),
  out of scope for this fix; the restore path no longer exercises it.

## 6. Regression tests

- ASan (`build-asan`) run against the real DB: **0** AddressSanitizer errors,
  **0** SIGSEGV, app ran until gdb timeout (124).
- Release (`build/`) run against the real DB: **0** Gtk-CRITICAL /assertion,
  **0** segfault, ran until timeout (124).
- Cases A–C (empty / terminal-only / note-only) still run clean.

## 7. Full verification after fix

| Check | Result |
|-------|--------|
| ASan real DB (gdb, 40s) | clean, no errors, no signal |
| Release real DB (12s) | clean, no Gtk-CRITICAL, exit 124 (ran) |
| Empty DB / terminal-only / note-only | clean (unchanged) |

## 8. Follow-up

- Re-run the workspace persistence acceptance suite (window identity stable
  across restarts, scrollback visible on pane) now that startup is stable.
- Optionally harden `TerminalPane` against late async spawn on explicit destroy
  (separate, lower-priority lifecycle improvement).