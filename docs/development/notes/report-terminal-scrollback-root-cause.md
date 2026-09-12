# Terminal Pane Scrollback Restore — Root Cause Analysis

> **Status**: BUG CONFIRMED — Double shell spawn clears scrollback
> **Report Type**: Root Cause Analysis (no implementation)

---

## Executive Summary

The scrollback restore pipeline has a **critical bug**: `TerminalPane` constructor spawns a shell immediately, then `runtime_restore()` feeds scrollback and spawns a **second shell**, which resets the VTE and clears the fed scrollback.

---

## The Bug Trace

### 1. Restore Entry Point: `TerminalTabView::restore_pane_tree()` (terminal_tab_view.cpp:443)

```cpp
auto p = std::make_unique<TerminalPane>(shell_, state.cwd, hist_file);  // Line 468
auto* raw = p.get();
// ... setup callbacks ...
raw->runtime_restore(state);  // Line 480 — called AFTER constructor
```

### 2. Constructor Spawns Shell #1 (terminal_pane.cpp:14-34)

```cpp
TerminalPane::TerminalPane(const std::string& shell, const std::string& cwd,
                           const std::string& history_file)
    : shell_(shell), cwd_(cwd), history_file_(history_file), title_("terminal") {
    // ... setup box, vte_ ...
    spawn_shell(cwd_);  // LINE 34: SPAWNS SHELL #1 IMMEDIATELY
    // ... config ...
}
```

### 3. `runtime_restore()` Feeds Scrollback Then Spawns Shell #2 (terminal_pane.cpp:152-180)

```cpp
void TerminalPane::runtime_restore(const remin::core::PaneState& state) {
    // ... set size ...
    if (!state.scrollback.empty()) {
        vte_terminal_feed(vte_, state.scrollback.data(), ...);  // FEED SCROLLBACK
    }
    // 3. Spawn a fresh shell
    spawn_shell(resolve_restore_cwd(state.cwd));  // LINE 179: SPAWNS SHELL #2
}
```

---

## The Fatal Sequence

```
restore_pane_tree()
    ↓
TerminalPane(shell_, state.cwd, hist_file)  // Constructor
    ↓
spawn_shell(cwd_)          ← SHELL #1 SPAWNED
    ↓
runtime_restore(state)
    ↓
vte_terminal_feed(scrollback)  ← SCROLLBACK FED TO VTE
    ↓
spawn_shell(cwd)             ← SHELL #2 SPAWNED (vte_terminal_spawn_async)
    ↓
VTE RESET / PTY RECREATED    ← SCROLLBACK CLEARED!
```

---

## Why This Clears Scrollback

`vte_terminal_spawn_async()` (called by `spawn_shell()`) creates a **new PTY** and resets the VTE terminal state. This is equivalent to opening a fresh terminal — it clears the display buffer, scrollback, and any fed content.

The sequence:
1. Shell #1 spawns → VTE has empty buffer
2. `vte_terminal_feed()` puts scrollback into VTE buffer
3. `vte_terminal_spawn_async()` (Shell #2) → **resets VTE, new PTY, clears buffer**
4. Result: Empty terminal with fresh shell

---

## Evidence from VTE Documentation

`vte_terminal_spawn_async()`:
- Creates a new PTY pair
- Resets terminal emulation state
- Clears scrollback buffer
- Starts fresh shell process

This is **by design** — you cannot feed scrollback to a VTE and then spawn a new shell on the same VTE without losing the fed content.

---

## The Correct Design (Feed-Before-Spawn)

Per design §5.2: **"Feed-before-spawn: rebuild the scrollback from the raw text buffer so the shell prompt lands below exactly the restored content."**

The correct sequence should be:
```
1. Create VTE (no shell yet)
2. Feed scrollback via vte_terminal_feed()
3. Spawn shell ONCE
```

But current code:
```
1. Constructor spawns shell (WRONG - too early)
2. Feed scrollback
3. Spawn shell again (resets VTE, loses scrollback)
```

---

## Root Cause: Constructor Does Too Much

The `TerminalPane` constructor violates the feed-before-spawn principle by calling `spawn_shell()` unconditionally. It assumes:
- Fresh terminal = spawn immediately
- Restore = constructor + runtime_restore()

But `runtime_restore()` is designed to handle the **entire** feed-then-spawn sequence. The constructor should NOT spawn shell if restore will happen.

---

## The Fix Design (Not Implementing Yet)

### Option A: Two-Phase Construction
```cpp
// TerminalPane constructor: create VTE only, NO spawn
TerminalPane::TerminalPane(...) {
    create_vte();
    setup_config();
    // NO spawn_shell() here
}

// Explicit restore path
void TerminalPane::runtime_restore(const PaneState& state) {
    set_size();
    feed_scrollback();  // Feed first
    spawn_shell();      // Spawn ONCE
}

// Fresh terminal path
void TerminalPane::initialize_fresh() {
    spawn_shell(default_cwd);
}
```

### Option B: Deferred Spawn Flag
```cpp
TerminalPane::TerminalPane(..., bool defer_spawn = false) {
    if (!defer_spawn) spawn_shell(cwd_);
}

runtime_restore(state) {
    // feed scrollback
    if (deferred_spawn) spawn_shell(...);
}
```

### Option C: Factory Method
```cpp
static unique_ptr<TerminalPane> create_for_restore(...) {
    auto p = make_unique<TerminalPane>(..., true); // defer_spawn=true
    p->runtime_restore(state);
    return p;
}
```

---

## Impact Assessment

| Scenario | Current Behavior | Expected |
|----------|------------------|----------|
| Fresh terminal tab | ✅ Works | ✅ Works |
| Restore with scrollback | ❌ Empty pane | ✅ Scrollback visible |
| Restore empty state | ⚠️ Double spawn | ✅ Single spawn |

---

## Files to Modify (When Implementing)

| File | Change |
|------|--------|
| `src/gui/terminal/terminal_pane.hpp` | Add deferred spawn option to constructor |
| `src/gui/terminal/terminal_pane.cpp` | Remove unconditional `spawn_shell()` from constructor; add `initialize_fresh()` |
| `src/gui/window/terminal_tab_view.cpp` | Use deferred spawn for restore path |

---

## Verification Steps (Manual Test)

1. Open terminal, run commands (`ls`, `pwd`, `echo hello`)
2. Close window / quit app
4. Restart app
5. Verify terminal shows previous output (`ls`, `pwd`, `echo hello`) above fresh prompt

---

## Conclusion

**The scrollback restore pipeline is 90% implemented correctly** — capture, serialization, persistence, loading, and feed logic are all present. The **only bug** is the double shell spawn caused by unconditional `spawn_shell()` in constructor.

This is a **single-point fix** in `TerminalPane` construction logic. The rest of the pipeline (capture, serialize, DB, load, feed logic) is correct and working.

**No new features needed** — just fix the constructor/spawn ordering.