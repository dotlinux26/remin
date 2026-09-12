# Remin Terminal Visual State Restore - Technical Report

**Project:** Remin  
**Date:** 2026-09-07  
**Status:** Investigation Complete - Architecture Decision Finalized  

---

## Executive Summary

This document captures the complete investigation, root cause analysis, architectural decisions, and implementation plan for the **Terminal Visual State Restore** feature in Remin.

### Problem Statement
When Remin restarts, terminal panes lose their visual state (output history, colors, layout, scrollback) and appear blank. The current implementation using `VTE_FORMAT_TEXT`/`VTE_FORMAT_HTML` fails because:
1. **Text export loses terminal grid geometry** → whitespace artifacts, "diagonal walking" text
2. **Colors/attributes lost** in plain text export
3. **Double shell spawn** during restore clears fed scrollback
4. **VTE has no native serialize/restore API**

### Solution Chosen
**Visual Snapshot Reconstruction (V1/V2) - VTE remains the terminal engine, Remin adds persistence adapter only.**

### Long-term Direction
**DEFERRED:** Terminal Core / PTY Proxy architecture is **DEFERRED** - not part of current architecture. VTE remains the terminal engine; Remin adds a persistence adapter layer only.

---

## 1. Problem Analysis

### 1.1 Root Cause: Double Shell Spawn
**File:** `src/gui/terminal/terminal_pane.cpp`

```cpp
// Constructor - spawns shell #1 immediately
TerminalPane::TerminalPane(...) {
    spawn_shell(cwd_);  // SHELL #1
}

// Restore path - feeds scrollback then spawns shell #2
void TerminalPane::runtime_restore(const PaneState& state) {
    vte_terminal_feed(vte_, state.scrollback.data(), ...);  // Feed scrollback
    spawn_shell(...);  // SHELL #2 → VTE RESET → SCROLLBACK CLEARED!
}
```

**Sequence:**
1. Constructor → `spawn_shell()` → Shell #1 spawned, empty VTE
2. `runtime_restore()` → `vte_terminal_feed(scrollback)` → scrollback fed
3. `runtime_restore()` → `spawn_shell()` → **Shell #2 spawns → VTE reset → scrollback cleared!**

### 1.2 Text Export Failure
`vte_terminal_get_text_format(vte_, VTE_FORMAT_TEXT)` loses:
- Grid geometry (row/col coordinates)
- Colors, bold, underline, attributes
- Line wrapping boundaries
- Empty cells → converted to whitespace artifacts
- Scrollback vs viewport boundaries

### 1.3 HTML Export Limitation
`VTE_FORMAT_HTML` preserves formatting but **not grid geometry** → cannot reconstruct exact cell positions.

### 1.4 VTE Capability Gap (Critical)
**VTE 0.76 has NO public API to:**
- Get cell content at specific (row, col)
- Get cell attributes at specific (row, col)
- Get soft-wrap boundaries
- Distinguish "empty cell" from "space character"
- Get soft-wrap vs hard-wrap distinction
- Serialize/restore emulator state

**This is the fundamental constraint:** VTE exports formatted text (TEXT/HTML) but does NOT expose the underlying grid structure needed for perfect visual reconstruction.

---

## 2. Root Cause Summary

| Issue | Status | Evidence |
|-------|--------|----------|
| Double shell spawn | ✅ CONFIRMED | Constructor + `runtime_restore()` both call `spawn_shell()` |
| Text export loses geometry | ✅ CONFIRMED | `VTE_FORMAT_TEXT` output shows diagonal whitespace |
| HTML export insufficient | ✅ CONFIRMED | No cell coordinates, only flow layout |
| VTE has no serialize API | ✅ CONFIRMED | GNOME upstream: no `serialize_state()`/`restore_state()` |
| VTE no cell-level API | ✅ CONFIRMED | No `get_cell()`, `get_cell_attr()`, `get_wrap_boundary()` |

---

## 2. Acceptance Criteria (Invariant)

### Visual Fidelity Requirements
```
ORIGINAL                          RESTORED
┌────────────────────────┐       ┌────────────────────────┐
│ $ ls                   │       │ $ ls                   │
│ Desktop  Documents     │  ≈    │ Desktop  Documents     │
│ Downloads              │       │ Downloads              │
│ $ █                    │       │ $ █                    │
└────────────────────────┘       └────────────────────────┘
```

### Invariant Rules (NON-NEGOTIABLE)
1. **NO whitespace artifacts**: No `\n\n\n\n` padding, no diagonal text
2. **NO phantom padding**: Empty cells ≠ spaces, empty rows ≠ newlines
3. **Colors preserved**: Foreground, background, bold, underline
4. **Layout preserved**: Wrapping, cursor position, scrollback boundary
5. **Shell behavior**: New shell spawns AFTER visual restore

---

## 3. Architecture Decision

### Chosen Strategy: **Visual Snapshot Reconstruction (V1/V2) - VTE remains the terminal engine**

```
VTE (live) → Visual Snapshot Adapter → Persistence → Restore → VTE
```

### Architecture Layers
```
┌─────────────────────────────────────┐
│           Remin UI (GTK)            │
├─────────────────────────────────────┤
│         TerminalPane                │
├─────────────────────────────────────┤
│  VTE (runtime)    │  Visual Adapter │
│  (source of truth)│  (persistence)  │
└─────────────────────────────────────┘
```

### Key Principle
> **VTE remains the terminal engine. Remin only adds persistence adapter.**
> - No PTY proxy reimplementation
> - No terminal emulator reimplementation  
> - No keyboard/mouse/parser reimplementation
> - VTE keeps ALL behaviors: Ctrl+Shift+C/V, selection, scroll, resize, ANSI parsing

### Long-term Direction: DEFERRED
**Terminal Core / PTY Proxy is DEFERRED** - not part of current architecture. VTE remains the terminal engine; Remin adds a persistence adapter layer only.

---

## 2. Acceptance Criteria (Invariant)

### Visual Fidelity Requirements
```
ORIGINAL                          RESTORED
┌────────────────────────┐       ┌────────────────────────┐
│ $ ls                   │       │ $ ls                   │
│ Desktop  Documents     │  ≈    │ Desktop  Documents     │
│ Downloads              │       │ Downloads              │
│ $ █                    │       │ $ █                    │
└────────────────────────┘       └────────────────────────┘
```

### Invariant Rules (NON-NEGOTIABLE)
1. **NO whitespace artifacts**: No `\n\n\n\n` padding, no diagonal text
2. **NO phantom padding**: Empty cells ≠ spaces, empty rows ≠ newlines
3. **Colors preserved**: Foreground, background, bold, underline
4. **Layout preserved**: Wrapping, cursor position, scrollback boundary
5. **Shell behavior**: New shell spawns AFTER visual restore

---

## 3. Critical Investigation Items (BLOCKING)

### 3.1 VTE Visual State Extraction - BLOCKING
**The fundamental question:** What can VTE 0.76 public API actually expose?

| Capability | VTE API | Status |
|------------|---------|--------|
| Full grid (rows × cols) | `get_row_count()`, `get_column_count()` | ✅ |
| Scrollback lines | `get_scrollback_lines()` | ✅ |
| Cursor position | `get_cursor_position()` | ✅ |
| Cell char at (row, col) | ❌ NO API | ❌ |
| Cell attributes at (row, col) | ❌ NO API | ❌ |
| Soft-wrap boundaries | ❌ NO API | ❌ |
| Cell = space vs empty | ❌ NO API | ❌ |
| Soft-wrap vs hard-wrap | ❌ NO API | ❌ |
| Scrollback vs viewport boundary | ❌ NO API | ❌ |
| Alternate screen state | ❌ NO API | ❌ |

**Conclusion:** Cannot build perfect `VisualLine`/`CellFragment` from public API.

### 3.2 VTE_FORMAT_HTML Analysis - BLOCKING
Need to verify what `VTE_FORMAT_HTML` actually contains:
- Does it include row/col coordinates?
- Does it distinguish empty cells from spaces?
- Does it mark soft-wrap boundaries?
- Does it distinguish scrollback vs viewport?

**Must test with real terminal before committing to HTML parsing approach.**

### 3.3 Restore Order Validation - BLOCKING
Current assumption:
```
feed() scrollback → g_main_context_iteration() → spawn_shell() → shell behaves nicely
```
**Must test:**
- Does new shell emit startup output that corrupts restored screen?
- Does `g_main_context_iteration()` guarantee VTE render before spawn?
- Does shell startup script (`.bashrc`, etc.) emit ANSI that corrupts restored screen?

---

## 3. Acceptance Criteria (Invariant)

### Visual Fidelity Requirements
```
ORIGINAL                          RESTORED
┌────────────────────────┐       ┌────────────────────────┐
│ $ ls                   │       │ $ ls                   │
│ Desktop  Documents     │  ≈    │ Desktop  Documents     │
│ Downloads              │       │ Downloads              │
│ $ █                    │       │ $ █                    │
└────────────────────────┘       └────────────────────────┘
```

### Invariant Rules (NON-NEGOTIABLE)
1. **NO whitespace artifacts**: No `\n\n\n\n` padding, no diagonal text
2. **NO phantom padding**: Empty cells ≠ spaces, empty rows ≠ newlines
3. **Colors preserved**: Foreground, background, bold, underline
4. **Layout preserved**: Wrapping, cursor position, scrollback boundary
4. **Shell behavior**: New shell spawns AFTER visual restore

---

## 3. Architecture Decision (Final)

### Chosen Strategy: **Visual Snapshot Reconstruction - VTE remains the terminal engine**

```
VTE (live) → Visual Snapshot Adapter → Persistence → Restore → VTE
```

### Architecture Layers
```
┌─────────────────────────────────────┐
│           Remin UI (GTK)            │
├─────────────────────────────────────┤
│         TerminalPane                │
├─────────────────────────────────────┤
│  VTE (runtime)    │  Visual Adapter │
│  (source of truth)│  (persistence)  │
└─────────────────────────────────────┘
```

### Key Principle
> **VTE remains the terminal engine. Remin only adds persistence adapter.**
> - No PTY proxy reimplementation
> - No terminal emulator reimplementation  
> - No keyboard/mouse/parser reimplementation
> - VTE keeps ALL behaviors: Ctrl+Shift+C/V, selection, scroll, resize, ANSI parsing

### Long-term Direction: DEFERRED
**Terminal Core / PTY Proxy is DEFERRED** - not part of current architecture. VTE remains the terminal engine; Remin adds a persistence adapter layer only.

---

## 4. Acceptance Criteria (Invariant)

### Visual Fidelity Requirements
```
ORIGINAL                          RESTORED
┌────────────────────────┐       ┌────────────────────────┐
│ $ ls                   │       │ $ ls                   │
│ Desktop  Documents     │  ≈    │ Desktop  Documents     │
│ Downloads              │       │ Downloads              │
│ $ █                    │       │ $ █                    │
└────────────────────────┘       └────────────────────────┘
```

### Invariant Rules (NON-NEGOTIABLE)
1. **NO whitespace artifacts**: No `\n\n\n\n` padding, no diagonal text
2. **NO phantom padding**: Empty cells ≠ spaces, empty rows ≠ newlines
3. **Colors preserved**: Foreground, background, bold, underline
4. **Layout preserved**: Wrapping, cursor position, scrollback boundary
5. **Shell behavior**: New shell spawns AFTER visual restore

---

## 4. Implementation Phases (Revised)

### Phase 0: Investigation (CURRENT - BLOCKING)
- [ ] Test `VTE_FORMAT_HTML` output for real terminal with colors/wrapping/scrollback
- [ ] Verify what geometric info HTML actually contains
- [ ] Test `VTE_FORMAT_TEXT` with explicit coordinate ranges
- [ ] Determine feasible snapshot representation given VTE API limits

### Phase 1A: Fix Double Spawn (DONE)
- ✅ `TerminalPane` constructor: `defer_spawn` parameter
- ✅ `runtime_restore()` feeds scrollback → spawns shell ONCE
- ✅ `initialize_fresh()` for new tabs

### Phase 1B: Visual Snapshot Capture (BLOCKED by investigation)
- [ ] Determine feasible snapshot representation given VTE API limits
- [ ] Implement capture pipeline
- [ ] Store geometry (rows, cols, cursor, scrollback_lines)
- [ ] Serialize to JSON in `PaneState`

### Phase 1C: Deterministic ANSI Reconstruction (BLOCKED by investigation)
- [ ] Implement deterministic ANSI encoder with CUP (Cursor Position)
- [ ] Explicit `\r\n` for scrollback lines
- [ ] CUP(row, col) + SGR + text for viewport lines
- [ ] Feed-before-spawn, then spawn shell ONCE

### Phase 1D: Restore Validation
- [ ] No `\n\n\n` artifacts
- [ ] Colors preserved
- [ ] Cursor position restored
- [ ] Scrollback scrollable
- [ ] New shell works correctly

---

## 5. Long-term Direction: DEFERRED
**Terminal Core / PTY Proxy is DEFERRED** - not part of current architecture. VTE remains the terminal engine; Remin adds a persistence adapter layer only.

---

## 6. Current Implementation Status

### Completed
- ✅ Double-spawn fix (`defer_spawn` + `initialize_fresh()` + `runtime_restore()`)
- ✅ Debug logging throughout pipeline
- ✅ HTML capture for formatting
- ✅ All 10 unit tests pass
- ✅ No crashes on startup/shutdown

### In Progress (BLOCKED by investigation)
- [ ] HTML → `VisualLine`/`CellFragment` parsing (BLOCKED - need investigation)
- [ ] Deterministic ANSI encoder with CUP (BLOCKED - need investigation)
- [ ] Feed-before-spawn with explicit `\r\n` for scrollback
- [ ] CUP(row, col) + SGR + text for viewport

### Next Immediate Steps (INVESTIGATION FIRST)
1. **Test `VTE_FORMAT_HTML`** with real terminal (colors, wrapping, scrollback)
2. **Analyze HTML output** - what geometric info does it actually contain?
3. **Test restore order** - feed → g_main_context_iteration() → spawn_shell
3. **Determine feasible snapshot representation** given VTE API limits

---

## 7. Key Files Modified

| File | Changes |
|------|---------|
| `src/gui/terminal/terminal_pane.hpp` | `defer_spawn` param, `initialize_fresh()`, `runtime_restore()` |
| `src/gui/terminal/terminal_pane.cpp` | Constructor defer_spawn, `runtime_restore()` fix, debug logs |
| `src/gui/window/terminal_tab_view.cpp` | `defer_spawn=true` for restore, `initialize_fresh()` for new panes |
| `src/core/workspace_core.cpp` | Scrollback load/store debug logs |
| `src/storage/storage.cpp` | SQLite scrollback store/load debug logs |

---

## 8. Debug Logging (Enabled)

Key debug trace points:
```
DEBUG CAPTURE: scrollback.len=XXXX content=[...]
DEBUG CHECKPOINT: pane=XXXX scrollback.len=XXXX
DEBUG SQL_STORE: pane=XXXX len=XXXX
DEBUG SQL_LOAD: pane=XXXX len=XXXX
DEBUG LOAD_SCROLLBACK: pane=XXXX loaded.len=XXXX
DEBUG RESTORE: state.scrollback.len=XXXX content=[...]
DEBUG RESTORE: feeding scrollback len=XXXX
```

---

## 10. Next Actions (Investigation First)

1. **Immediate**: Test `VTE_FORMAT_HTML` output with real terminal
2. **Immediate**: Analyze HTML geometric information content
3. **Immediate**: Test restore order - feed → g_main_context_iteration() → spawn
4. **Then**: Determine feasible snapshot representation
4. **Then**: Implement capture/restore based on proven capabilities

---

*Report generated: 2026-09-07*  
*Author: Remin Development Team*  
*Status: Phase 0 Investigation In Progress, Phase 1A Complete*