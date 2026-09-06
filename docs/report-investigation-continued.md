# GObject Ownership & Widget Lifecycle Investigation — Continuing Audit

**Project:** Remin  
**Date:** 2026-09-07  
**Status:** INVESTIGATION IN PROGRESS — NO IMPLEMENTATION YET  
**Purpose:** Document confirmed findings, identify unresolved questions, plan instrumentation

---

## Executive Summary

Two independent crash paths identified. Both involve `NoteEditor::is_modified()` unsafe raw buffer access, but root cause of **why buffer becomes invalid** remains unproven. Previous report incorrectly claimed "double-unref" — not confirmed. Current investigation must trace exact object lifecycles.

---

## Confirmed Findings (Evidence-Backed)

### 1. `is_modified()` — Unsafe Callsite ✅ CONFIRMED
```cpp
// note_editor.cpp:592-594
bool NoteEditor::is_modified() const {
    return gtk_text_buffer_get_modified(GTK_TEXT_BUFFER(source_buffer_));
}
```
- **No guards**: no `alive_`, no null check, no `GTK_IS_TEXT_BUFFER`
- **Called from**: 
  - `MainWindow::capture_all_runtime_state()` line 701 (PATH A - autosave)
  - `MainWindow::refresh_tab_widget()` line 1658 (PATH B - tab bar refresh)
- **Log match**: `gtk_text_buffer_get_modified: assertion 'GTK_IS_TEXT_BUFFER (buffer)' failed`

### 2. `buffer()` — Has Guards ✅ CONFIRMED
```cpp
// note_editor.cpp:604-609
Glib::RefPtr<Gtk::TextBuffer> NoteEditor::buffer() const {
    if (!alive_ || !source_buffer_) return {};
    if (!GTK_IS_TEXT_BUFFER(source_buffer_)) return {};
    return Glib::wrap(GTK_TEXT_BUFFER(source_buffer_));
}
```
- Inconsistent with `is_modified()` — same object, different safety

### 3. Constructor Creates Own Buffer ✅ CONFIRMED
```cpp
// note_editor.cpp:16-18
source_view_ = GTK_SOURCE_VIEW(gtk_source_view_new());
source_buffer_ = GTK_SOURCE_BUFFER(gtk_source_buffer_new(nullptr));
gtk_text_view_set_buffer(GTK_TEXT_VIEW(source_view_), GTK_TEXT_BUFFER(source_buffer_));
```
- NoteEditor owns +1 ref, GtkTextView gets +1 ref via set_buffer

### 4. Search Tags Fixed ✅ CONFIRMED
```cpp
// note_editor.cpp:103-107 - destructor
// "Search tags are owned by the buffer's tag table... so we do NOT unref them here."
search_match_tag_ = nullptr;
search_current_tag_ = nullptr;
```

### 5. Tab Close Triggers Both Paths ✅ CONFIRMED
- PATH A: Autosaver calls `capture_all_runtime_state()` periodically
- PATH B: `finish_close_tab()` → `update_tab_bar()` → `refresh_tab_widget()` → `is_modified()`

### 6. Crash Sites Identified ✅ CONFIRMED
| Path | Crash Location | Stack Evidence |
|------|---------------|----------------|
| PATH A | `gtk_text_buffer_get_modified()` | `NoteEditor::is_modified()` → `capture_state()` → autosave |
| PATH B | `gtk_text_buffer_get_iter_at_mark()` | `gtk_widget_map/realize` → `build_tab_widget()` lambda |

---

## Unproven Claims (Must Verify via Instrumentation)

| Claim | Status | Why Unproven |
|-------|--------|--------------|
| `source_buffer_` double-unref | ❌ UNPROVEN | GTK may hold ref; no refcount trace |
| `editor_ = nullptr` destroys NoteEditor | ❌ FALSE | Raw pointer — doesn't destroy GTK widget |
| GtkStack remove/re-add destroys buffer | ❌ UNPROVEN | remove ≠ finalize; unmap ≠ buffer death |
| Buffer finalizes before NoteEditor dtor | ❌ UNPROVEN | Need destruction order trace |
| Ctrl+Shift+N capture phase related | ❌ UNPROVEN | Same function, different event path |

---

## The Critical Destruction Race (Hypothesis to Test)

### Widget Hierarchy & Ownership
```
MainWindow
 └── content_stack_ (Gtk::Stack)
      └── NoteTabView (unique_ptr in tabs_, managed by GTK as child of stack)
           └── content_host_ (Gtk::Box, managed, child of NoteTabView)
                └── editor_ = NoteEditor (Gtk::Box, managed, child of content_host)
                     └── scroller_ (Gtk::ScrolledWindow, managed, child of NoteEditor)
                          └── source_view_ (GtkSourceView, child of scroller)
                               └── source_buffer_ (ref'd by NoteEditor + source_view_)
```

### Hypothesized Destruction Order When `tabs_.erase(index)` Runs

| Step | Who | Action | Buffer Refcount |
|------|-----|--------|-----------------|
| 1 | C++ | `unique_ptr` destroys `NoteTabView` | — |
| 2 | GTK | `NoteTabView` (Box) finalize → destroy children | — |
| 3 | GTK | `content_host_` finalize → unparent `editor_` | — |
| 4 | GTK | `editor_` (NoteEditor Box) finalize → destroy children | — |
| 5 | GTK | `scroller_` finalize → unparent `source_view_` | — |
| 6 | GTK | `source_view_` finalize → `gtk_text_view_set_buffer(NULL)` → **unrefs buffer** | 2 → 1 |
| 7 | C++ | `NoteEditor::~NoteEditor()` body executes | — |
| 8 | Dtor | `g_signal_handler_disconnect(source_buffer_, ...)` [line 86] | **UAF if step 6 already happened!** |
| 9 | Dtor | `g_object_unref(source_buffer_)` [line 111] | **Double-unref if step 6 happened!** |

**This hypothesis explains GLib logs:**
```
g_signal_handler_disconnect: assertion 'G_TYPE_CHECK_INSTANCE' failed  ← step 8
g_object_unref: assertion '!object_already_finalized' failed (2x)     ← step 9
invalid (NULL) class pointer                                         ← step 8/9
g_hash_table_foreach: assertion 'version == hash_table->version' failed  ← corrupted tag table
```

---

## Required Instrumentation (No Fixes Yet)

Add `g_printerr` at these exact locations to trace object identity and refcounts:

### NoteEditor Constructor (note_editor.cpp:9-20)
```cpp
g_printerr("EDITOR_CTOR this=%p view=%p buffer=%p buffer_ref=%d\n",
    this, source_view_, source_buffer_,
    source_buffer_ ? G_OBJECT(source_buffer_)->ref_count : -1);
```

### NoteEditor Destructor Start (note_editor.cpp:76)
```cpp
g_printerr("EDITOR_DTOR_START this=%p alive=%d buffer=%p view=%p buffer_ref=%d\n",
    this, alive_, source_buffer_, source_view_,
    source_buffer_ ? G_OBJECT(source_buffer_)->ref_count : -1);
```

### Before g_signal_handler_disconnect (note_editor.cpp:86)
```cpp
g_printerr("EDITOR_DTOR_DISCONNECT_BUFFER this=%p buffer=%p GTK_IS_TEXT_BUFFER=%d ref=%d\n",
    this, source_buffer_,
    source_buffer_ ? GTK_IS_TEXT_BUFFER(source_buffer_) : 0,
    source_buffer_ ? G_OBJECT(source_buffer_)->ref_count : -1);
```

### Before g_object_unref(source_buffer_) (note_editor.cpp:111)
```cpp
g_printerr("EDITOR_DTOR_UNREF_BUFFER this=%p buffer=%p ref=%d\n",
    this, source_buffer_,
    source_buffer_ ? G_OBJECT(source_buffer_)->ref_count : -1);
```

### NoteTabView Constructor (note_tab_view.cpp:10)
```cpp
g_printerr("NTV_CTOR this=%p editor=%p\n", this, editor_);
```

### NoteTabView Destructor (note_tab_view.cpp:38)
```cpp
g_printerr("NTV_DTOR this=%p editor_ptr=%p\n", this, editor_);
```

### finish_close_tab — Key Points (main_window.cpp:1301-1349)
```cpp
// Before remove
g_printerr("FCT_REMOVE index=%d child=%p tabs_sz=%zu\n", index, child, tabs_.size());

// Before erase
g_printerr("FCT_ERASE_BEFORE index=%d tabs_sz=%zu\n", index, tabs_.size());

// After erase
g_printerr("FCT_ERASE_AFTER tabs_sz=%zu\n", tabs_.size());

// During rebuild
g_printerr("FCT_REBUILD_REMOVE child=%p\n", child);
g_printerr("FCT_REBUILD_ADD child=%p new_idx=%zu\n", child, i);
```

### refresh_tab_widget (main_window.cpp:1658)
```cpp
g_printerr("REFRESH_TAB index=%zu note=%p is_mod=%d\n",
    index, note, note ? note->is_modified() : -1);
```

### capture_all_runtime_state (main_window.cpp:695)
```cpp
g_printerr("CAPTURE_NOTE note=%p editor=%p\n", note, note ? note->editor() : nullptr);
```

---

## Four Questions Instrumentation Must Answer

| Question | How to Verify |
|----------|---------------|
| **Q1. Who owns GtkSourceBuffer ref?** | Trace refcount at ctor, view set_buffer, dtor, view finalize |
| **Q2. Buffer state at NoteEditor dtor?** | Log `GTK_IS_TEXT_BUFFER` and refcount before disconnect/unref |
| **Q3. Which buffer pointer does GtkTextView use at crash?** | Log `source_view_` buffer at map/realize vs ctor buffer |
| **Q4. Do GtkTextView and GtkSourceBuffer share lifecycle?** | Correlate view finalize time vs buffer finalize time |

---

## Build & Test Plan

```bash
# 1. Add instrumentation to the 10+ locations above
# 2. Build ASan
ninja -C build-asan

# 3. Run with G_DEBUG=fatal-warnings to catch assertions early
G_DEBUG=fatal-warnings ./build-asan/src/app/remin gui 2>&1 | tee trace.log

# 4. Reproduce:
#    - Open note, type, wait for autosave (PATH A)
#    - Open multiple notes, close one (PATH B)
#    - Switch tabs rapidly

# 5. Analyze trace.log for:
#    - Exact destruction order
#    - Buffer refcount at each step
#    - Whether same buffer pointer persists ctor→dtor→crash
```

---

## Files to Instrument (Priority Order)

1. `src/gui/note/note_editor.cpp` — Constructor, destructor (4 points)
2. `src/gui/window/note_tab_view.cpp` — Constructor, destructor
3. `src/gui/window/main_window.cpp` — `finish_close_tab()`, `refresh_tab_widget()`, `capture_all_runtime_state()`

---

## Next Steps After Instrumentation

1. **If Q2 shows buffer already invalid at dtor** → Fix: NoteEditor must not unref if GTK already did (use weak ref or check)
2. **If Q3 shows different buffer pointer** → Fix: Reparenting creates new buffer (investigate set_buffer calls)
3. **If Q4 shows view outlives buffer** → Fix: Ensure buffer lifetime ≥ view lifetime
4. **Then and only then** → Add guards to `is_modified()` as defensive measure

---

## Conclusion

**Do not implement fixes yet.** The investigation has identified:
- ✅ Unsafe callsite (`is_modified()`)
- ✅ Two trigger paths (autosave, tab close)
- ✅ Crash sites (buffer access during capture, map/realize)

But **has not proven**:
- ❌ Why buffer becomes invalid
- ❌ Exact destruction order
- ❌ Whether double-unref or use-after-free

**Instrumentation is the only way to get definitive answers.** Adding guards without knowing root cause risks masking the real bug.