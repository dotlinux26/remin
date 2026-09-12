# VTE Extension Feasibility Report

**Project:** Remin  
**Date:** 2026-09-07  
**Status:** Architecture APPROVED — Implementation NOT YET APPROVED  

---

## Executive Summary

This document captures the investigation, root cause analysis, architectural decisions, and implementation plan for the **Terminal Visual State Restore** feature in Remin.

### Problem Statement
When Remin restarts, terminal panes lose their visual state (output history, colors, layout, scrollback). Current approaches fail because:
1. **Text export loses terminal grid geometry** → whitespace artifacts, "diagonal walking" text
2. **Colors/attributes lost** in plain text export
3. **Double shell spawn** during restore clears fed scrollback
4. **VTE has no native serialize/restore API**

### Solution Chosen
**VTE Extension (`remin-vte-extension`) - VTE Internal Snapshot/Restore**

### Architecture
```
VTE upstream
     │
     ├── normal VTE API
     │
     └── Remin VTE Extension
             │
             ├── visual snapshot capture
             ├── visual state restore
             ├── grid/state access
             └── persistence-neutral serialization
```

### Key Principle
> **VTE remains the terminal engine. Remin adds a thin extension for visual state persistence.**
> - VTE remains the terminal engine
> - Extension only does capture/restore
> - All VTE behaviors preserved (Ctrl+Shift+C/V, selection, scroll, ANSI, vim, tmux, etc.)
> - No PTY proxy, no terminal emulator reimplementation

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

**Status**: ✅ FIXED (Phase 1A - DONE)

---

### 1.2 VTE Capability Gap (Critical)

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
| Double shell spawn | ✅ FIXED | Constructor + `runtime_restore()` both call `spawn_shell()` |
| VTE no cell-level API | ✅ CONFIRMED | No `get_cell()`, `get_cell_attr()`, `get_wrap_boundary()` |
| VTE no serialize API | ✅ CONFIRMED | GNOME upstream: no `serialize_state()`/`restore_state()` |

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

### Chosen Strategy: **VTE Extension (`remin-vte-extension`) - VTE Internal Snapshot/Restore**

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
4. **Shell behavior**: New shell spawns AFTER visual restore

---

## 3. Critical Investigation Items (BLOCKING)

### 3.1 VTE Internal State Extraction - BLOCKING
**The fundamental question:** What can VTE 0.76 internal structures expose?

| Capability | VTE Internal | Status |
|------------|--------------|--------|
| Full grid (rows × cols) | `m_screen->m_ring`, `VteRowData`, `VteCell` | ✅ AVAILABLE |
| Scrollback lines | `m_screen->m_ring` | ✅ AVAILABLE |
| Cursor position | `m_screen->cursor` | ✅ AVAILABLE |
| Cell char at (row, col) | `VteCell::c` | ✅ AVAILABLE |
| Cell attributes at (row, col) | `VteCell::attr` | ✅ AVAILABLE |
| Soft-wrap boundaries | `VteRowAttr::soft_wrapped` | ✅ AVAILABLE |
| Cell = space vs empty | ❌ NO direct API | ❌ NEEDS INVESTIGATION |
| Soft-wrap vs hard-wrap | `VteRowAttr::soft_wrapped` | ✅ AVAILABLE |
| Scrollback vs viewport boundary | `m_screen->insert_delta` | ✅ AVAILABLE |
| Alternate screen state | `m_normal_screen` / `m_alternate_screen` | ✅ AVAILABLE |

**Conclusion:** All data exists internally but requires minimal patch to expose.

### 3.2 Snapshot Read Feasibility - NEEDS VALIDATION
Need to verify that a minimal VTE-internal patch can export:
- Primary screen grid
- Alternate screen grid  
- Scrollback ring
- Cell characters + attributes
- Colors (packed RGB in `VteCellAttr::m_colors`)
- Width/wide-character info (`VteCellAttr::columns()`)
- Wrapped-line info (`VteRowAttr::soft_wrapped`)
- Cursor position (`m_screen->cursor`)
- Cursor shape
- Terminal modes (ECMA + Private)

### 3.3 Snapshot Write Feasibility - NEEDS VALIDATION
Need to verify that internal structures can be restored into a NEW VTE instance:
- Screen cells via `vte_terminal_feed()` with ANSI sequences
- Scrollback via feed-before-spawn
- Colors/attributes via ANSI SGR sequences
- Wrapping via explicit CUP positioning
- Cursor via `vte_terminal_set_cursor_position()`
- Modes via ANSI mode sequences

**Critical**: Restore must NOT use `VTE_FORMAT_TEXT/HTML` as reconstruction mechanism.

### 3.4 Restore Order Validation - BLOCKING
Current assumption:
```
feed() scrollback → g_main_context_iteration() → spawn_shell() → shell behaves nicely
```
**Must test:**
- Does new shell emit startup output that corrupts restored screen?
- Does `g_main_context_iteration()` guarantee VTE render before spawn?
- Does shell startup script (`.bashrc`, etc.) emit ANSI that corrupts restored screen?

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

## 4. Architecture Decision (Final)

### Chosen Strategy: **VTE Extension (`remin-vte-extension`) - VTE Internal Snapshot/Restore**

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

## 4. Investigation Plan (Phase 0 - CURRENT)

### 4.1 VTE Internal Structure Mapping
- [ ] Map VTE 0.76 internal structures (Terminal, Screen, Ring, RowData, Cell, CellAttr)
- [ ] Identify exact fields needed for snapshot
- [ ] Document private structure layouts

### 4.2 Snapshot Read Feasibility
- [ ] Determine minimal patch to export all required fields
- [ ] Test cell-level access (character, attributes, colors, width)
- [ ] Test scrollback ring iteration
- [ ] Test alternate screen access

### 4.3 Snapshot Write Feasibility
- [ ] Test feed-before-spawn with ANSI sequences
- [ ] Verify CUP(row,col) + SGR + text restores visual state
- [ ] Verify no double-spawn (defer_spawn pattern)
- [ ] Test shell spawn timing (feed → iterate → spawn)

### 4.4 Round-trip Validation (MANDATORY)
- [ ] Capture terminal state A
- [ ] Serialize snapshot
- [ ] Restore to VTE instance B
- [ ] Capture terminal state B
- [ ] **Assert: snapshot(A) == snapshot(B)** for all supported fields

### 4.5 Visual Fidelity Tests (MANDATORY)
Test fixtures must include:
- Plain output
- 16-color
- Truecolor
- Bold/italic/underline
- Background
- Wide characters
- Combining characters
- Wrapped lines
- Intentional spaces
- Empty rows
- Content at arbitrary columns
- Large scrollback
- Cursor in middle
- Primary screen
- Alternate screen

**Assertion**: `snapshot(A) == snapshot(B)` for supported fields.

### 4.6 Behavior Preservation Tests
After visual restore works:
- [ ] Spawn fresh shell → verify startup output doesn't corrupt restored state
- [ ] Ctrl+Shift+C/V
- [ ] ↑↓ scroll
- [ ] Selection
- [ ] Resize
- [ ] ANSI output

---

## 5. Architecture Decision (Final)

### Chosen Strategy: **VTE Extension (`remin-vte-extension`) - VTE Internal Snapshot/Restore**

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

## 5. Investigation Plan (Phase 0 - CURRENT - BLOCKING)

### Phase 0: Investigation (CURRENT - BLOCKING)
- [ ] Test VTE internal structures accessibility
- [ ] Verify minimal patch can export all required fields
- [ ] Test VTE internal snapshot round-trip (A → serialize → B → compare)
- [ ] Test restore order: feed → g_main_context_iteration() → spawn_shell
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

## 6. Key Files Modified

| File | Changes |
|------|---------|
| `src/gui/terminal/terminal_pane.hpp` | `defer_spawn` param, `initialize_fresh()`, `runtime_restore()` |
| `src/gui/terminal/terminal_pane.cpp` | Constructor defer_spawn, `runtime_restore()` fix, debug logs |
| `src/gui/window/terminal_tab_view.cpp` | `defer_spawn=true` for restore, `initialize_fresh()` for new panes |
| `src/core/workspace_core.cpp` | Scrollback load/store debug logs |
| `src/storage/storage.cpp` | SQLite scrollback store/load debug logs |

---

## 10. Next Actions (Investigation First)

1. **Immediate**: Audit VTE 0.76 internal structures for snapshot feasibility
2. **Immediate**: Test round-trip capture/restore in isolated experiment
3. **Immediate**: Test restore order - feed → `g_main_context_iteration()` → `spawn_shell()`
4. **Then**: Determine feasible snapshot representation given VTE API limits

---

*Report generated: 2026-09-07*  
*Author: Remin Development Team*  
*Status: Architecture APPROVED — Implementation NOT YET APPROVED — Phase 0 Investigation In Progress*