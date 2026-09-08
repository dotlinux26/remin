# Terminal Pane Scrollback / Visual State Restore — Analysis Report

> **Status**: Analysis complete. Implementation NOT started. Report only.

---

## Executive Summary

The Remin codebase has **full capture + serialization pipeline for terminal pane scrollback**, but the **restore path into VTE is incomplete** for the "pane visual state" requirement.

### Key Finding

| Pipeline Stage | Status | Evidence |
|----------------|--------|----------|
| `TerminalPane::runtime_capture()` | ✅ **COMPLETE** | Captures `cols`, `rows`, `shell`, `cwd`, `scrollback`, `interrupted_command` |
| `PaneState` serialization (`scrollback` field) | ✅ **COMPLETE** | `to_json` / `from_json` includes `"scrollback"` |
| `WorkspaceCore::apply_runtime_state()` | ✅ **COMPLETE** | Overwrites `st.scrollback = snap.scrollback` |
| `TerminalTabView::restore_pane_tree()` | ✅ **COMPLETE** for topology | Calls `runtime_restore(state)` per pane |
| `TerminalPane::runtime_restore()` | ✅ **COMPLETE IMPLEMENTATION** | **FEEDS SCROLLBACK via `vte_terminal_feed()`** |
| `WorkspaceCore::open_workspace()` | ✅ **COMPLETE** | Loads scrollback from DB into `PaneState` |

**CONCLUSION**: The pipeline **IS implemented end-to-end**. Scrollback capture → serialize → deserialize → restore → VTE feed is wired.

---

## The Real Problem: Requirement Mismatch

The user's requirement:
> "Pane visual state = snapshot of **output/log** at window close. Fresh shell on restore, but old scrollback replayed into terminal."

The implementation does:
1. ✅ Capture full VTE scrollback (`capture_scrollback()` → 10k lines)
2. ✅ Persist to SQLite (`scrollbacks` table)
3. ✅ Load into `PaneState.scrollback` on workspace open
3. ✅ Restore via `vte_terminal_feed()` **before** spawning shell

**BUT** the issue is **timing / initialization order**:

```cpp
// TerminalPane::runtime_restore()
1. vte_terminal_set_size()           // OK
2. vte_terminal_feed(scrollback)     // OK - feeds into VTE buffer
3. spawn_shell()                     // OK - spawns fresh shell
```

**The shell spawns AFTER scrollback feed**. This is correct design (feed-before-spawn per design §5.2).

---

## Why User Sees "Empty Pane"

If the user sees empty panes after restore, possible causes:

### A. Scrollback Not Loaded from DB
**Fixed in commit `ccc85f1`** — `WorkspaceCore::open_workspace()` now calls `storage_->load_scrollback(pane_id)` for each pane.

### B. Scrollback Empty at Capture Time
If `capture_scrollback()` returns empty string:
- VTE not initialized? (`!vte_`)
- Scrollback buffer empty?

```cpp
constexpr glong kScrollbackLines = 10000;
char* text = vte_terminal_get_text_range_format(vte_, VTE_FORMAT_TEXT,
                                                -(rows + kScrollbackLines), 0, rows, 0, &len);
```

### C. `vte_terminal_feed()` Not Working
- VTE not realized?
- Feed called before VTE ready?
- Encoding issue?

### D. Restore Not Called
Check if `TerminalTabView::restore_pane_tree()` → `runtime_restore()` is called during workspace restore.

---

## Evidence Trace (Code → Code)

### 1. Capture: `TerminalPane::runtime_capture()` (terminal_pane.cpp:139)

```cpp
snap.scrollback = capture_scrollback();  // ← Captures full VTE buffer
```

### 2. Serialization: `PaneState` (serialization.hpp:84)

```cpp
{"scrollback", s.scrollback}  // ← Included in JSON
```

### 3. Persistence: `WorkspaceCore::checkpoint()` (workspace_core.cpp:446)

```cpp
scrollbacks.emplace_back(p->id, p->state.scrollback);  // ← Sent to storage
```

### 4. Storage: `SqliteStorage::checkpoint()` (storage.cpp:30)

```cpp
for (const auto& [pane, content] : scrollbacks) {
    store_scrollback(pane, content);  // ← INSERT INTO scrollbacks
}
```

### 5. Load: `WorkspaceCore::open_workspace()` (workspace_core.cpp:34-48)

```cpp
auto loaded = storage_->load_workspace(id);
ws_current_ = std::move(*loaded);

// Load scrollbacks from DB into pane states
for (auto& w : ws_current_->windows) {
    for (auto& t : w.tabs) {
        for (auto* p : panes) {
            p->state.scrollback = storage_->load_scrollback(p->id);
        }
    }
}
```

### 6. Restore: `TerminalTabView::restore_pane_tree()` (terminal_tab_view.cpp:478)

```cpp
raw->runtime_restore(state);  // ← Calls TerminalPane::runtime_restore()
```

### 7. Restore Implementation: `TerminalPane::runtime_restore()` (terminal_pane.cpp:152)

```cpp
if (!state.scrollback.empty()) {
    vte_terminal_feed(vte_, state.scrollback.data(), ...);  // ← FEEDS SCROLLBACK
}
spawn_shell(...);  // Fresh shell AFTER scrollback feed
```

---

## Command History Filtering (Separate Issue)

The user also asked:
> "KHI Ở TAB TERMINAL ĐANG FOCUS PANE NÀO THÌ TAB HISTORY MỤC COMMAND SẼ TỰ ĐỘNG HIỂN THỊ DANH SÁCH CÁC LỆNH ĐÃ THỰC THI CỦA PANE ĐÓ."

**Status**: Implemented in commit `758beb7`:
- `SessionController::get_command_history(pane_id)` accepts optional filter
- `TerminalTabView` fires `on_pane_focus_()` callback on pane switch
- `MainWindow::update_history_sidebar()` filters by `focused_pane_id`

---

## Action Items (No Implementation Yet)

### Verification Needed
1. **Run manual test**: Open terminal, run commands, close window, restart app → verify scrollback visible
2. **Debug scrollback content**: Add logging to `capture_scrollback()` and `runtime_restore()` to see actual string lengths
2. **Check DB**: Query `scrollbacks` table after checkpoint to confirm data persisted

### If Scrollback Still Missing
Investigate in order:
1. `capture_scrollback()` returning empty?
2. `scrollbacks` table empty after checkpoint?
3. `load_scrollback()` returning empty?
4. `runtime_restore()` not called / VTE not ready?

---

## Files Involved

| File | Role |
|------|------|
| `src/gui/terminal/terminal_pane.cpp` | Capture & Restore implementation |
| `src/core/serialization.hpp` | PaneState JSON (de)serialization |
| `src/core/workspace_core.cpp` | Checkpoint + Load scrollback |
| `src/storage/sqlite/sqlite_db.cpp` | Scrollback table schema |
| `src/storage/storage.cpp` | Checkpoint scrollback persistence |
| `src/gui/window/terminal_tab_view.cpp` | Restore pipeline entry |

---

## Conclusion

**The scrollback restore pipeline IS implemented end-to-end**. The design correctly:
- Captures full VTE buffer at checkpoint
- Serializes to JSON + SQLite
- Loads on workspace open
- Feeds into VTE **before** spawning fresh shell (feed-before-spawn)

If user sees empty panes, it's likely a **runtime bug** (empty capture, failed DB load, VTE not ready) not a missing feature. Need to debug with actual runtime logs.

**No new implementation needed until root cause identified.**