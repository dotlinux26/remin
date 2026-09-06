# GObject Ownership & Widget Lifecycle Analysis — Current Codebase Audit

**Project:** Remin  
**Date:** 2026-09-07  
**Scope:** Audit of ACTUAL current code (not previous patches), verification of crash root causes.

---

## Executive Summary

The previous report was **incorrect** — it described a pre-patch state that no longer exists. The **actual current code** reveals:

1. **NoteEditor constructor creates its OWN buffer** (`gtk_source_buffer_new`) — NOT borrowed from view
2. **Search tags are correctly NOT unref'd** in destructor (fixed)
3. **`is_modified()` has NO validity check** — calls `gtk_text_buffer_get_modified` on raw pointer
4. **Ctrl+Shift+N goes through capture-phase key controller** — different path than toolbar button
5. **GtkStack remove/re-add cycle in `finish_close_tab()`** causes remaining tabs to be unparented/reparented
6. **TerminalPane extra ref pattern** is correct but fragile

**CRITICAL finding:** `NoteEditor::is_modified()` (line 592-594) has **zero validity checks** on `source_buffer_`. Called from `MainWindow::refresh_tab_widget()` (line 1658) after tab close → tab bar rebuild → potential use-after-free.

---

## 1. NoteEditor — Actual Current State (`src/gui/note/note_editor.cpp`)

### 1.1 Constructor (lines 9-74) — **Creates Own Buffer**

```cpp
source_view_ = GTK_SOURCE_VIEW(gtk_source_view_new());
source_buffer_ = GTK_SOURCE_BUFFER(gtk_source_buffer_new(nullptr));  // OWNED by NoteEditor
gtk_text_view_set_buffer(GTK_TEXT_VIEW(source_view_), GTK_TEXT_BUFFER(source_buffer_));
// Comment line 20: "No need for g_object_ref - gtk_text_view_set_buffer adds a reference."
```

**Ownership model (CURRENT):**
- `source_buffer_`: Created by NoteEditor → **+1 ref owned by NoteEditor**
- `gtk_text_view_set_buffer()` → view takes **+1 ref**
- **NoteEditor owns the buffer lifecycle** — this is by design (comment line 15)

### 1.2 Destructor (lines 76-118) — **Correct Order, Search Tags Fixed**

```cpp
~NoteEditor() {
    alive_ = false;                                    // 1. Guard FIRST
    
    // 2. Disconnect ALL timers
    highlight_timer_.disconnect();
    preview_timer_.disconnect();
    scroll_timer_.disconnect();
    
    // 3. Disconnect buffer "changed" signal (BEFORE buffer unref)
    g_signal_handler_disconnect(source_buffer_, buffer_changed_signal_id_);
    
    // 4. Disconnect color-scheme signal (global)
    g_signal_handler_disconnect(adw_style_manager_get_default(), color_scheme_signal_id_);
    
    // 5. Unref search_context_ (holds ref to buffer)
    g_object_unref(search_context_);
    
    // 6. Search tags — CORRECTLY NOT UNREF'D (lines 103-107)
    // "Search tags are owned by the buffer's tag table... so we do NOT unref them here."
    search_match_tag_ = nullptr;
    search_current_tag_ = nullptr;
    
    // 7. Unref source_buffer_ (releases NoteEditor's ownership)
    g_object_unref(source_buffer_);
    
    // 8. Clear raw pointers
    source_buffer_ = nullptr;
    source_view_ = nullptr;
    scroller_ = nullptr;
}
```

**✅ Search tag bug is FIXED in current code.** Tags created via `gtk_text_buffer_create_tag()` are owned by buffer's tag table. Not unref'd.

### 1.3 `is_modified()` — **CRITICAL: NO VALIDITY CHECK** (lines 592-594)

```cpp
bool NoteEditor::is_modified() const {
    return gtk_text_buffer_get_modified(GTK_TEXT_BUFFER(source_buffer_));
}
```

**NO `alive_` check, NO `GTK_IS_TEXT_BUFFER` check, NO null check.** Raw pointer passed directly to GTK.

### 1.4 `buffer()` Method — **HAS Validity Checks** (lines 604-609)

```cpp
Glib::RefPtr<Gtk::TextBuffer> NoteEditor::buffer() const {
    if (!alive_ || !source_buffer_) return {};
    if (!GTK_IS_TEXT_BUFFER(source_buffer_)) return {};
    return Glib::wrap(GTK_TEXT_BUFFER(source_buffer_));
}
```

**Inconsistency:** `buffer()` has guards, `is_modified()` does not.

### 1.5 Call Sites of `is_modified()`

| Location | Context |
|----------|---------|
| `note_tab_view.cpp:72` | `buffer_modified_connection_` lambda |
| `note_tab_view.cpp:86` | `buffer_changed_connection_` lambda (debounced) |
| `note_tab_view.cpp:422` | `NoteTabView::is_modified()` → `editor_->is_modified()` |
| `main_window.cpp:701` | Checkpoint capture |
| `main_window.cpp:1233` | Close tab unsaved guard |
| **`main_window.cpp:1658`** | **`refresh_tab_widget()` → tab bar rebuild** |
| `main_window.cpp:2035` | Open conflict prompt |

---

## 2. MainWindow Tab Lifecycle — The Destroy/Rebuild Cycle

### 2.1 `finish_close_tab()` (lines 1301-1349)

```cpp
void finish_close_tab(int index) {
    // 1. Get child by OLD index name
    Gtk::Widget* child = content_stack_->get_child_by_name(std::to_string(index));
    if (child) content_stack_->remove(*child);  // Unparent from stack
    
    // 2. Remove from type-specific vectors
    if (kind == Note) note_tabs_.erase(...);
    
    // 3. Fix active_tab_ index
    if (index < active_tab_) --active_tab_;
    else if (index == active_tab_) active_tab_ = -1;
    
    // 4. DESTROY TabView HERE — unique_ptr destructor runs
    tabs_.erase(tabs_.begin() + index);
    
    // 5. REBUILD ENTIRE STACK — all remaining tabs unparented then re-added
    while (auto* child = content_stack_->get_first_child()) {
        content_stack_->remove(*child);  // Unparent ALL remaining
    }
    for (size_t i = 0; i < tabs_.size(); ++i) {
        content_stack_->add(*tabs_[i].get(), std::to_string(i));  // Reparent with NEW index
    }
    // ... activate new tab ...
}
```

**Critical sequence:**
1. `content_stack_->remove(*child)` — target tab unparented
2. `tabs_.erase()` — **TabView destroyed** → `NoteTabView::~NoteTabView()` → `NoteEditor::~NoteEditor()`
3. **While destroying**, remaining tabs still in stack
4. Then **ALL remaining tabs removed** from stack (unparented)
5. Then **ALL remaining tabs re-added** (reparented with new index names)

### 2.2 `update_tab_bar()` → `refresh_tab_widget()` (lines 1539-1663)

```cpp
void update_tab_bar() {
    if (tabs_.size() < tab_widgets_.size()) {
        for (auto* w : tab_widgets_) tab_bar_->remove(*w);
        tab_widgets_.clear();
    }
    for (size_t i = 0; i < tabs_.size(); ++i) {
        if (i >= tab_widgets_.size()) {
            build_tab_widget(i);  // Create new tab widget
        }
        refresh_tab_widget(tab_widgets_[i], i);  // Refresh ALL tabs
    }
}

void refresh_tab_widget(Gtk::Box* tab, size_t index) {
    TabView* view = tabs_[index].get();
    auto* lbl = ...;  // Get label from GObject data
    
    if (view->kind() == TabKind::Note) {
        auto* note = static_cast<NoteTabView*>(view);
        if (note->is_modified())  // LINE 1658 — CALLS is_modified()
            label_text = "\u25CF " + label_text;
    }
    // ...
}
```

**Timing:** After `finish_close_tab()` destroys a tab, `update_tab_bar()` is called (line 1346), which calls `refresh_tab_widget()` for **ALL remaining tabs**, which calls `note->is_modified()` → `editor_->is_modified()` → **`gtk_text_buffer_get_modified(source_buffer_)`**.

---

## 3. Ctrl+Shift+N vs Toolbar Button — Different Event Paths

### 3.1 Key Controller (lines 67-76, 1790-1793)

```cpp
// Constructor
key_ctrl_ = Gtk::EventControllerKey::create();
key_ctrl_->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);  // CAPTURE PHASE
key_ctrl_->signal_key_pressed().connect(
    sigc::slot<bool(unsigned int, unsigned int, Gdk::ModifierType)>(
        [this](unsigned int keyval, unsigned int, Gdk::ModifierType mods) -> bool {
            return on_find_key_pressed(keyval, 0, mods);
        }),
    false);
add_controller(key_ctrl_);

// Handler
if (ctrl && shift && (keyval == GDK_KEY_n || keyval == GDK_KEY_N)) {
    new_note_tab();  // Same function
    return true;
}
```

### 3.2 Toolbar Button (line 482, connection not shown but standard)

```cpp
new_note_btn_->signal_clicked().connect(
    sigc::mem_fun(*this, &MainWindow::new_note_tab));
```

**Both call `new_note_tab()`** (lines 1197-1222). If one works and one doesn't, the difference is:
- **Key controller**: CAPTURE phase, runs before widget key handling, `on_find_key_pressed` returns `bool`
- **Button click**: Standard signal emission, bubble phase

**Potential issue:** If `on_find_key_pressed` returns `true` (handled), event stops. If `false`, event continues to bubble. The key controller connection uses `sigc::slot` with `false` for `after` parameter.

---

## 4. TerminalPane Extra Ref Pattern — Verified Current

### 4.1 Constructor (lines 64-68)

```cpp
widget_ = box;  // Gtk::make_managed<Gtk::Box>
g_object_ref(widget_->gobj());  // EXTRA REF for rebuild survival
```

### 4.2 Destructor (lines 71-83)

```cpp
~TerminalPane() {
    if (widget_) {
        auto* g = widget_->gobj();
        widget_ = nullptr;
        g_object_unref(g);  // Release extra ref
    }
}
```

**Correct but fragile:** If `widget_` accessed after unref → UAF. Must ensure `widget_ = nullptr` before `g_object_unref`.

---

## 5. Crash Root Cause Analysis — Evidence Based

### 5.1 ASan Stack Trace Evidence

```
gtk_text_buffer_get_iter_at_mark()
...
gtk_widget_realize()
gtk_widget_map()
...
MainWindow::build_tab_widget(size_t)::<lambda()>  // line 1604
```

**Line 1604** is in `build_tab_widget()` lambda:
```cpp
click_area->signal_clicked().connect([this, find_tab_index]() {
    const int idx = find_tab_index();
    if (idx >= 0) content_stack_->set_visible_child(*tabs_[idx]);  // 1604
});
```

But the crash is in `gtk_text_buffer_get_iter_at_mark()` during **map/realize** — the note widget being mapped has invalid buffer.

### 5.2 GLib Critical Logs

```
g_object_unref: assertion '!object_already_finalized' failed  (2x per tab close)
g_signal_handler_disconnect: assertion 'G_TYPE_CHECK_INSTANCE' failed
g_object_unref: assertion 'G_IS_OBJECT' failed
invalid (NULL) class pointer
```

These occur **at tab close time** (`finish_close_tab` → `tabs_.erase` → destruction).

### 5.3 The Connecting Evidence

**`refresh_tab_widget()` calls `is_modified()` on remaining tabs AFTER destruction of closed tab.**

During `finish_close_tab()`:
1. Target tab destroyed → `NoteEditor` destructor runs → `g_object_unref(source_buffer_)`
2. `source_buffer_` refcount: NoteEditor releases, **view still holds ref** → buffer stays alive
3. **BUT**: If view is ALSO being finalized (due to stack remove/re-add), buffer refcount drops to 0
4. Buffer finalizes → tags finalized → tag table corrupted
5. Remaining tabs' `refresh_tab_widget()` → `is_modified()` → **dead buffer access**

**The remove/re-add cycle (lines 1333-1338) unparents/reparents ALL remaining tabs:**
- Triggers `unmap` → `unrealize` → potential widget finalization
- Then `map` → `realize` → widget reconstruction
- During this, `GtkTextView` may access buffer/marks that are now invalid

---

## 6. Identified Critical Issues

### CRITICAL #1: `NoteEditor::is_modified()` No Validity Check
**File:** `note_editor.cpp:592-594`  
**Impact:** Called from `refresh_tab_widget()` after tab close → stack rebuild → use-after-free on `source_buffer_`  
**Fix:** Add same guards as `buffer()` method

### CRITICAL #2: GtkStack Remove/Re-add Cycle Destroys Widget State
**File:** `main_window.cpp:1333-1338`  
**Impact:** All remaining tabs unparented/reparented on every tab close → map/unmap → widget finalization race with buffer lifetime  
**Root cause of delayed crashes:** "wait a bit then crash" = deferred GTK layout/realize work hitting corrupted state

### CRITICAL #3: `NoteTabView` Buffer Signal Connections Not Disconnected Before Editor Destruction
**File:** `note_tab_view.cpp:48-49` (dtor) vs `connect_editor()` (lines 69-96)  
**Issue:** `buffer_modified_connection_` and `buffer_changed_connection_` disconnected in `NoteTabView` dtor, but `editor_` destroyed as child of `NoteTabView` → **order unclear**  
If `editor_` (NoteEditor) destroyed first → buffer finalizes → signal connections on dead buffer

### HIGH: Ctrl+Shift+N Event Path Not Audited
**File:** `main_window.cpp:68-76, 1790-1793`  
Capture-phase controller vs bubble-phase button click — same function but different propagation context

### HIGH: Tab Widget GObject Data Stores Raw Pointers
**File:** `main_window.cpp:1637-1638`  
`g_object_set_data(tab, "remin-view", view)` — raw `TabView*` stored, tab widgets rebuilt on shrink

---

## 7. Correct Risk Classification

| Priority | Issue | Evidence |
|----------|-------|----------|
| **CRITICAL** | `is_modified()` no validity guard | Direct code inspection + call site in tab bar refresh |
| **CRITICAL** | GtkStack remove/re-add cycle | Code at lines 1333-1338 + ASan map/realize crash |
| **CRITICAL** | Destruction order: NoteTabView vs NoteEditor vs GtkSourceView | ASan `gtk_text_buffer_get_iter_at_mark` during map |
| **HIGH** | Ctrl+Shift+N vs toolbar path difference | Capture-phase controller + same target function |
| **HIGH** | Tab widget raw pointer GObject data | `g_object_set_data` + rebuild on shrink |
| **MEDIUM** | TerminalPane extra ref fragility | Correct pattern but `widget_` use after unref possible |
| **LOW** | Search tag ownership | **FIXED** in current code — not unref'd |

---

## 8. Required Investigation (No Code Changes Yet)

### 8.1 Instrument Destruction Order
Add `g_printerr` to trace exact sequence:
```cpp
// In NoteEditor::~NoteEditor()
g_printerf("NE DTOR: this=%p buffer=%p view=%p refcount=%d\n",
           this, source_buffer_, source_view_,
           source_buffer_ ? G_OBJECT(source_buffer_)->ref_count : 0);

// In NoteTabView::~NoteTabView()
g_printerf("NTV DTOR: this=%p editor=%p\n", this, editor_);

// In finish_close_tab() before/after erase
g_printerf("FCT: before erase index=%d tabs.size=%zu\n", index, tabs_.size());
```

### 8.2 Verify Buffer Refcount at Critical Points
- After `content_stack_->remove(*child)` — is view still alive?
- After `tabs_.erase()` — has NoteEditor dtor run?
- During stack rebuild — are remaining tabs' views finalized?

### 8.3 Test `is_modified()` Guard Fix in Isolation
```cpp
bool NoteEditor::is_modified() const {
    if (!alive_ || !source_buffer_ || !GTK_IS_TEXT_BUFFER(source_buffer_)) return false;
    return gtk_text_buffer_get_modified(GTK_TEXT_BUFFER(source_buffer_));
}
```
Test if this alone stops the `GTK_IS_TEXT_BUFFER` assertion in logs.

### 8.4 Audit Ctrl+Shift+N Path
Add logging to `on_find_key_pressed` and `new_note_tab` to confirm both paths execute identically.

---

## 9. Code Reference Index (Actual Current Code)

| File | Lines | Current State |
|------|-------|---------------|
| `note_editor.cpp` | 9-74 | Constructor: creates own buffer via `gtk_source_buffer_new` |
| `note_editor.cpp` | 76-118 | Destructor: correct order, search tags NOT unref'd (FIXED) |
| `note_editor.cpp` | 592-594 | **`is_modified()` — NO validity checks** |
| `note_editor.cpp` | 604-609 | `buffer()` — HAS validity checks |
| `note_tab_view.cpp` | 38-53 | Destructor: disconnects signals, `editor_=nullptr` |
| `note_tab_view.cpp` | 69-96 | `connect_editor()` — connects to `editor_->buffer()` |
| `main_window.cpp` | 1301-1349 | `finish_close_tab()` — destroy → rebuild ALL tabs |
| `main_window.cpp` | 1333-1338 | **Remove/re-add cycle for ALL remaining tabs** |
| `main_window.cpp` | 1539-1663 | `update_tab_bar()` → `refresh_tab_widget()` calls `is_modified()` |
| `main_window.cpp` | 67-76 | Key controller: CAPTURE phase |
| `main_window.cpp` | 1790-1793 | Ctrl+Shift+N handler |
| `terminal_pane.cpp` | 64-68 | Extra ref for rebuild survival |
| `terminal_pane.cpp` | 71-83 | Destructor releases extra ref |

---

## Conclusion

The codebase has **three critical issues** that explain the ASan crashes and GLib assertions:

1. **`is_modified()` lacks validity guards** — called during tab bar refresh after destruction
2. **GtkStack remove/re-add cycle** — destroys widget state for ALL tabs on every close
3. **Destruction order race** — NoteEditor buffer unref vs GtkSourceView finalize vs stack rebuild

The search tag ownership bug **is fixed in current code** (not unref'd). The previous report incorrectly described pre-fix state.

**Next step:** Instrument destruction order to confirm the exact sequence, then apply minimal fixes:
- Add validity guards to `is_modified()`
- Evaluate if stack rebuild can be avoided or made safer
- Verify Ctrl+Shift+N path equivalence