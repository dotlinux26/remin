# Remin VTE Extension Design Report

**Project:** Remin  
**Date:** 2026-09-07  
**Status:** Architecture Decision - VTE Extension Approach Approved  

---

## Executive Summary

This document captures the architectural decision to build a **VTE Extension** (`remin-vte-extension`) for Remin's terminal visual state restore feature, instead of using HTML/ANSI reconstruction or PTY proxy approaches.

### Core Philosophy
> **Don't reimplement terminal. Add a serialization door to the existing terminal engine.**

VTE remains the terminal engine. Remin adds a thin extension layer for visual state persistence.

---

## Problem Statement

When Remin restarts, terminal panes lose their visual state (output history, colors, layout, scrollback). Current approaches fail:

| Approach | Problem |
|----------|---------|
| `VTE_FORMAT_TEXT` | Loses grid geometry, colors, wrapping → diagonal whitespace artifacts |
| `VTE_FORMAT_HTML` | No cell coordinates, only flow layout |
| `feed()` ANSI reconstruction | Loses terminal modes, cursor, alternate screen, soft-wrap |
| PTY proxy | Requires reimplementing terminal emulator |

---

## Solution: VTE Extension (`remin-vte-extension`)

### Core Philosophy
> **Don't reimplement terminal. Add a serialization door to the existing terminal engine.**

### Architecture
```
┌─────────────────────────────────────────────────────────────┐
│                        Remin                                │
│  ┌─────────────────────────────────────────────────────┐   │
│  │                   TerminalPane                      │   │
│  │  ┌──────────────┐    ┌──────────────────────────┐  │   │
│  │  │    VTE       │    │   Remin VTE Extension    │  │   │
│  │  │  (runtime)   │───►│   Snapshot/Restore API   │  │   │
│  │  └──────────────┘    └─────────────┬────────────┘  │   │
│  └───────────────────────┬─────────────────────────────┘   │
└───────────────────────────┼──────────────────────────────────┘
                            │
                      Persistence (SQLite)
```

### Key Principle
> **VTE remains the terminal engine. Remin adds a thin extension for visual state persistence.**

---

## Extension Design

### Repository Structure
```
remin/
├── src/
├── docs/
├── ...
└── vte-extension/
    ├── include/
    │   └── vte-snapshot.h
    ├── src/
    │   ├── vte-snapshot.c
    │   ├── vte-internal-access.c
    │   └── tests/
    ├── tests/
    ├── CMakeLists.txt
    └── README.md
```

### Extension API (Conceptual)
```c
// Opaque snapshot object
typedef struct VteVisualSnapshot VteVisualSnapshot;

// Capture current visual state
VteVisualSnapshot* remin_vte_snapshot_capture(VteTerminal *terminal);

// Restore visual state
gboolean remin_vte_snapshot_restore(VteTerminal *terminal, 
                                     const VteVisualSnapshot *snapshot);

// Serialize to binary/JSON for storage
gboolean remin_vte_snapshot_serialize(const VteVisualSnapshot *snap,
                                       GBytes **out_bytes);

// Deserialize from binary/JSON
VteVisualSnapshot* remin_vte_snapshot_deserialize(const guchar *data, 
                                                   gsize size);

// Cleanup
void remin_vte_snapshot_free(VteVisualSnapshot *snap);
```

### Snapshot Structure (Opaque to Remin)
```c
typedef struct _VteVisualSnapshot {
    uint32_t rows;
    uint32_t cols;
    int64_t scrollback_lines;
    
    // Cursor state
    struct { long row, col; VteCursorShape shape; } cursor;
    
    // Terminal modes
    VteTerminalMode modes;
    
    // Scrollback
    GArray *scrollback_lines;  // VisualLine[]
    
    // Viewport (visible area)
    VisualLine *viewport_lines;  // rows × cols
    
} VteVisualSnapshot;

struct VisualLine {
    GArray *fragments;  // CellFragment[]
    gboolean is_wrapped;
};

struct CellFragment {
    char *text;           // UTF-8
    uint32_t start_col;
    uint32_t end_col;
    VteCellAttributes attrs;  // fg, bg, bold, underline, etc.
};
```

---

## Extension Internals

### VTE Internal Access Strategy
```
VteTerminal
    │
    ├─ VteScreen (visible area)
    │   ├─ rows × cols grid
    │   └─ cursor position
    │
    ├─ VteRing (scrollback ring buffer)
    │   ├─ rows
    │   └─ lines (VteRowData[])
    │
    ├─ VteTerminalPrivate
    │   ├─ cursor (row, col, shape)
    │   ├─ modes (insert, origin, bracket-paste, etc.)
    │   ├─ colors (palette)
    │   └─ alternate screen
    │
    └─ VteRowData[]
        ├─ cells (VteCell[])
        ├─ attr (attributes)
        └─ length
```

### Extension Entry Points (Minimal Patch to VTE)
```c
// In vte-terminal.c (minimal patch)
VteVisualSnapshot* remin_vte_snapshot_capture(VteTerminal *terminal) {
    VteTerminalPrivate *priv = terminal->priv;
    VteVisualSnapshot *snap = g_new0(VteVisualSnapshot, 1);
    
    // Geometry
    snap->rows = vte_terminal_get_row_count(terminal);
    snap->cols = vte_terminal_get_column_count(terminal);
    snap->scrollback_lines = vte_terminal_get_scrollback_lines(terminal);
    
    // Cursor
    vte_terminal_get_cursor_position(terminal, &snap->cursor.col, &snap->cursor.row);
    snap->cursor.shape = vte_terminal_get_cursor_shape(terminal);
    
    // Modes
    snap->modes = priv->mode;
    
    // Scrollback
    snap->scrollback_lines = capture_scrollback_internal(priv);
    
    // Viewport
    snap->viewport_lines = capture_viewport_internal(priv);
    
    return snap;
}

gboolean remin_vte_snapshot_restore(VteTerminal *terminal, 
                                     const VteVisualSnapshot *snap) {
    VteTerminalPrivate *priv = terminal->priv;
    
    // 1. Geometry
    vte_terminal_set_size(terminal, snap->cols, snap->rows);
    
    // 2. Feed scrollback (feed-before-spawn)
    for (int i = 0; i < snap->scrollback_lines->len; i++) {
        VisualLine *line = &g_array_index(snap->scrollback_lines, VisualLine, i);
        // Feed each line with explicit \r\n
    }
    
    // 2b. Viewport
    for (int i = 0; i < snap->viewport_lines->len; i++) {
        VisualLine *line = &g_array_index(snap->viewport_lines, VisualLine, i);
        // Feed with CUP(row, col) + SGR + text
    }
    
    // 3. Cursor
    vte_terminal_set_cursor_position(terminal, snap->cursor.col, snap->cursor.row);
    vte_terminal_set_cursor_shape(terminal, snap->cursor.shape);
    
    // 4. Modes
    apply_modes(priv, snap->modes);
    
    return TRUE;
}
```

---

## Snapshot Format (Wire Format)

### Binary/JSON Schema
```json
{
  "version": 1,
  "geometry": {
    "rows": 40,
    "cols": 120,
    "scrollback_lines": 10000
  },
  "cursor": {
    "row": 39,
    "col": 2,
    "shape": "block"
  },
  "modes": {
    "insert": false,
    "origin": false,
    "bracketed_paste": true,
    "application_cursor": true
  },
  "scrollback": [
    {"fragments": [{"text": "$ ls", "col": 0, "attrs": {...}}], "wrapped": false},
    {"fragments": [{"text": "Desktop", "col": 0}, {"text": "  ", "col": 8}, {"text": "Documents", "col": 10}], "wrapped": false}
  ],
  "viewport": [
    {"fragments": [{"text": "$ ", "col": 0, "attrs": {...}}, {"text": "ls", "col": 2}], "wrapped": false}
  ]
}
```

### Persistence Neutral
- Extension only knows: `VteVisualSnapshot*`
- Remin handles: JSON serialization → SQLite storage
- Decouples extension from Remin's storage layer

---

## Compatibility Strategy

### Version Compatibility
```
VTE 0.76     VTE 0.78     VTE 0.80
     │           │           │
     └───────────┼───────────┘
                 │
      remin-vte-extension
      (compat layer)
```

### Compatibility Layer
```c
// vte-compat.h
#if VTE_CHECK_VERSION(0, 78, 0)
#define vte_terminal_get_scrollback_lines_v2(t) vte_terminal_get_scrollback_lines(t)
#else
static inline glong vte_terminal_get_scrollback_lines_v2(VteTerminal *t) {
    return vte_terminal_get_scrollback_lines(t);
}
#endif
```

---

## Extension Boundaries (Critical)

### What Extension DOES
- ✅ Read VTE internal grid/cell state
- ✅ Capture visual snapshot
- ✅ Restore visual state to VTE
- ✅ Serialize/deserialize snapshot

### What Extension Does NOT
- ❌ No GTK application logic
- ❌ No SQLite/Storage
- ❌ No Remin Workspace concepts
- ❌ No shell management
- ❌ No PTY proxy
- ❌ No shell spawning
- ❌ No keyboard/mouse handling
- ❌ No PTY lifecycle management

### Runtime Boundary
```
Runtime (unchanged):
Shell ↔ PTY ↔ VTE

Persistence (new):
VTE ──► Snapshot ──► Remin Storage
          │
          └──► Restore
```

---

## Why This Approach?

### Comparison

| Approach | VTE Touched | Rewrite Terminal | Keeps VTE Behavior |
|----------|-------------|------------------|-------------------|
| HTML → ANSI | Low | No | ✅ |
| PTY Proxy + Core | High | Almost | ⚠️ |
| **VTE Extension** | **Minimal** | **No** | **✅** |

### Key Benefits

1. **VTE remains source of truth** - No behavior changes
2. **All VTE behaviors preserved** - Ctrl+Shift+C/V, selection, scroll, ANSI parsing, vim, tmux, mouse, etc.
3. **No whitespace artifacts** - Direct grid access → no text serialization loss
4. **Colors/attributes preserved** - Direct cell attribute access
5. **Geometry preserved** - Direct cell coordinate access
4. **Maintainable** - Small patch, upstream compatible
5. **Reusable** - Other projects can use `remin-vte-extension`

---

## Roadmap

### Phase 1: Investigation (Current)
- [ ] Audit VTE 0.76 source tree
- [ ] Identify internal grid structure locations
- [ ] Verify snapshot/restore feasibility with minimal patch
- [ ] Prototype capture/restore in isolation

### Phase 2: Extension Prototype
- [ ] Create `vte-extension/` subdirectory
- [ ] Implement `VteVisualSnapshot` capture/restore
- [ ] Test with real terminal (colors, wrapping, scrollback)
- [ ] Verify no VTE behavior regression

### Phase 3: Remin Integration
- [ ] Integrate extension into `TerminalPane`
- [ ] Replace HTML/ANSI pipeline with extension calls
- [ ] Remove double-spawn workaround (extension handles restore)

### Phase 4: Hardening
- [ ] Automated tests for snapshot round-trip
- [ ] Compatibility layer for VTE versions
- [ ] Performance benchmarks

---

## Repository Structure
```
remin/
├── src/
│   ├── gui/
│   │   ├── terminal/
│   │   │   ├── terminal_pane.cpp       # Uses extension
│   │   │   └── terminal_tab_view.cpp
│   │   └── ...
├── vte-extension/
│   ├── include/
│   │   └── vte-snapshot.h
│   ├── src/
│   │   ├── vte-snapshot.c
│   │   ├── vte-internal-access.c
│   │   └── compat.c
│   ├── tests/
│   │   ├── test_snapshot_capture.c
│   │   └── test_snapshot_restore.c
│   ├── CMakeLists.txt
│   └── README.md
├── docs/
│   └── vte-extension-design.md
└── CMakeLists.txt
```

---

## Decision Log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-09-07 | VTE Extension approach | Solves VTE API gap without reimplementing terminal |
| 2026-09-07 | Extension, not fork | Maintains upstream compatibility |
| 2026-09-07 | No PTY/Terminal reimplementation | VTE keeps all behaviors |
| 2026-09-07 | Extension = capture/restore only | Clear boundaries |

---

## Next Steps

1. **Investigation**: Audit VTE 0.76 source tree for grid structure
2. **Prototype**: Build minimal capture/restore in isolation
3. **Validation**: Test with real terminal (colors, wrapping, scrollback)
4. **Integration**: Replace HTML/ANSI pipeline with extension calls

---

*Report generated: 2026-09-07*  
*Author: Remin Development Team*  
*Status: Architecture Decision - VTE Extension Approach Approved*