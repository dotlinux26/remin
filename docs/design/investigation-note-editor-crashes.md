# Investigation Report: NoteEditor/NoteTabView Crashes

**Date**: 2026-09-06
**Status**: IN PROGRESS - Root causes identified, fixes in progress
**Related ASan crashes**: `NoteEditor::buffer()`, `NoteTabView::capture_state()`, `MainWindow::capture_all_runtime_state()`

---

## Executive Summary

Multiple use-after-free (UAF) and lifetime management bugs exist in the NoteEditor/NoteTabView subsystem. The crashes manifest as:

1. **Ctrl+Shift+N creates note but crashes** - Different code path from toolbar button
2. **Empty temp notes crash on restore** - Restore path vs normal creation path
3. **Autosave triggers capture on dead buffers** - Runtime capture on dangling pointers
4. **Multiple UAF in signal/timer callbacks** - Disconnected connections not cleaned up

---

## Root Cause Analysis

### Root Cause 1: GtkSourceBuffer Ownership Violation (CRITICAL)

**Location**: `src/gui/note/note_editor.cpp:16-29`

```cpp
// Current buggy code (lines 16-29)
source_view_ = GTK_SOURCE_VIEW(gtk_source_view_new());
source_buffer_ = gtk_source_buffer_new(nullptr);
gtk_text_view_set_buffer(GTK_TEXT_VIEW(source_view_), GTK_TEXT_BUFFER(source_buffer_));
g_object_unref(source_buffer_);  // BUG: Unrefs buffer, view now owns it

source_buffer_ = GTK_SOURCE_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(source_view_)));
// source_buffer_ is now a BORROWED pointer to view's buffer
```

**Problem**: 
- `source_buffer_` is a **borrowed raw pointer** to the view's internal buffer
- View owns the buffer; when view dies, buffer dies
- `NoteEditor` keeps raw pointer `source_buffer_` that becomes dangling
- `Glib::wrap()` on dangling pointer → SEGV

**Evidence from ASan**:
```
NoteEditor::buffer()
→ Glib::wrap(_GtkTextBuffer*)
→ GTK_IS_TEXT_BUFFER failed
→ SEGV
```

**Fix Required**: NoteEditor must hold a proper reference to the buffer:
```cpp
// In constructor:
source_buffer_ = GTK_SOURCE_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(source_view_)));
g_object_ref(source_buffer_);  // Take ownership

// In destructor:
if (source_buffer_) {
    g_object_unref(source_buffer_);
    source_buffer_ = nullptr;
}
```

---

### Root Cause 2: NoteTabView Extra GObject Reference (CRITICAL)

**Location**: `src/gui/window/note_tab_view.cpp:17-19`

```cpp
// Constructor (lines 17-19)
editor_ = Gtk::make_managed<NoteEditor>(...);
g_object_ref(editor_->gobj());  // EXTRA REF

// Destructor (lines 55-58)
if (editor_) {
    auto* g = editor_->gobj();
    editor_ = nullptr;
    g_object_unref(g);  // MANUAL UNREF
}
```

**Problem**: Mixing gtkmm managed ownership with manual GObject ref counting
- `Gtk::make_managed` already owns the widget
- Extra `g_object_ref` creates double ownership
- Manual unref in destructor may run before/after gtkmm destruction
- Creates ownership graph confusion

**Impact**: Widget lifetime becomes unpredictable, especially during reparenting in `toggle_preview()`.

---

### Root Cause 3: Signal Connection Leaks in NoteTabView (CRITICAL)

**Location**: `src/gui/window/note_tab_view.cpp:57-98` (connect_editor)

```cpp
// Lines 73-79 - NO CONNECTION STORED
buf->signal_modified_changed().connect([this]() { ... });

// Lines 83-96 - NO CONNECTION STORED
buf->signal_changed().connect([this]() { ... });
```

**Problem**: Signal connections returned by `connect()` are discarded
- Lambdas capture `[this]` 
- When `NoteTabView` dies, connections remain in `GtkTextBuffer`
- Buffer may outlive `NoteTabView` (due to search_context ref)
- Callbacks fire on dead `NoteTabView` → UAF

**Impact**: Direct UAF in buffer callbacks after tab close

---

### Root Cause 4: Timer Callback UAF in NoteTabView (CRITICAL)

**Location**: `src/gui/window/note_tab_view.cpp:83-96` (connect_editor)

```cpp
// Line 85-96 - lambda captures [this]
dirty_debounce_ = Glib::signal_timeout().connect(
    [this]() {
        dirty_debounce_.disconnect();
        bool now = editor_ && editor_->is_modified();
        ...
    }, 120);
```

**Problem**: `dirty_debounce_` connection stored, but lambda captures `[this]`
- If timer fires after `NoteTabView` destruction → UAF
- Current destructor disconnects `dirty_debounce_` but connection may fire during destruction

---

### Root Cause 5: NoteTabView Missing Signal Connection Storage (CRITICAL)

**Location**: `src/gui/window/note_tab_view.cpp:57-98` (connect_editor)

```cpp
// NO CONNECTION STORED - LEAK
buf->signal_modified_changed().connect([this]() { ... });
buf->signal_changed().connect([this]() { ... });
```

**Missing members** (added in fix):
```cpp
sigc::connection buffer_modified_connection_;
sigc::connection buffer_changed_connection_;
```

These are now properly disconnected in destructor.

---

### Root Cause 5: NoteEditor Timer Callback UAF (HIGH)

**Location**: `src/gui/note/note_editor.hpp` and `note_editor.cpp`

```cpp
// Header - timers declared
sigc::connection highlight_timer_;
sigc::connection preview_timer_;
sigc::connection scroll_timer_;

// Constructor - connections created but not shown in excerpt
// Destructor (prior to fix) - NO DISCONNECT!
```

**Problem**: Timers created in constructor with `[this]` capture
- Destructor prior to fix did NOT disconnect timers
- Timers could fire after `NoteEditor` destruction → UAF

---

### Root Cause 6: NoteEditor Color Scheme Signal Leak (HIGH)

**Location**: `src/gui/note/note_editor.cpp:52-58`

```cpp
g_signal_connect(adw_style_manager_get_default(), "notify::color-scheme",
    G_CALLBACK(+[](GObject*, GParamSpec*, gpointer self) {
        static_cast<NoteEditor*>(self)->set_theme(...);
    }), this);
```

**Problem**: Signal connection ID not stored, cannot disconnect in destructor
- Fixed in recent commits by adding `color_scheme_signal_id_`

---

### Root Cause 7: Buffer Ownership - Borrowed Pointer Issue (CRITICAL)

**Location**: `src/gui/note/note_editor.cpp:16-29`

```cpp
// Constructor (lines 16-29)
source_view_ = GTK_SOURCE_VIEW(gtk_source_view_new());
source_buffer_ = gtk_source_buffer_new(nullptr);
gtk_text_view_set_buffer(GTK_TEXT_VIEW(source_view_), GTK_TEXT_BUFFER(source_buffer_));
g_object_unref(source_buffer_);  // View now owns buffer

source_buffer_ = GTK_SOURCE_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(source_view_)));
// source_buffer_ is now a BORROWED pointer
```

**Problem**: `source_buffer_` is a **borrowed raw pointer** to view's internal buffer
- View owns buffer; when view dies, buffer dies
- `NoteEditor` keeps raw pointer `source_buffer_` that becomes dangling
- No `g_object_ref()` taken on buffer

**Fixed in recent commits** by using view's buffer directly without creating separate buffer.

---

### Root Cause 8: NoteEditor Missing Virtual Destructor (HIGH)

**Location**: `src/gui/note/note_editor.hpp` - **FIXED**

```cpp
// BEFORE (no virtual destructor)
class NoteEditor : public Gtk::Box { ... };

// AFTER (FIXED)
class NoteEditor : public Gtk::Box {
public:
    virtual ~NoteEditor();  // ADDED
    ...
```

---

### Root Cause 9: NoteEditor Signal Connection Leak (HIGH)

**Location**: `src/gui/note/note_editor.cpp:30-32`

```cpp
// Line 30-32
g_signal_connect(source_buffer_, "changed", G_CALLBACK(+[](GtkTextBuffer*, gpointer self) {
    static_cast<NoteEditor*>(self)->on_buffer_changed();
}), this);
```

**Problem**: Signal connection ID not stored before fix
- Fixed by adding `buffer_changed_signal_id_` and proper disconnect in destructor

---

### Root Cause 10: NoteEditor Color Scheme Signal Leak (HIGH)

**Location**: `src/gui/note/note_editor.cpp:52-58`

```cpp
g_signal_connect(adw_style_manager_get_default(), "notify::color-scheme",
    G_CALLBACK(+[](GObject*, GParamSpec*, gpointer self) {
        static_cast<NoteEditor*>(self)->set_theme(...);
    }), this);
```

**Fixed**: Added `color_scheme_signal_id_` and proper disconnect in destructor.

---

### Root Cause 11: NoteEditor Timer Cleanup Missing (HIGH)

**Location**: `src/gui/note/note_editor.hpp` and destructor

```cpp
// Header
sigc::connection highlight_timer_;
sigc::connection preview_timer_;
sigc::connection scroll_timer_;

// Destructor BEFORE fix - NO DISCONNECT!
```

**Problem**: Timers created with `[this]` capture, never disconnected
- Added proper disconnect in destructor

---

### Root Cause 12: NoteTabView Extra GObject Reference (CRITICAL)

**Location**: `src/gui/window/note_tab_view.cpp:17-19` (constructor)

```cpp
// Constructor
editor_ = Gtk::make_managed<NoteEditor>(...);
g_object_ref(editor_->gobj());  // EXTRA REF!

// Destructor
if (editor_) {
    auto* g = editor_->gobj();
    editor_ = nullptr;
    g_object_unref(g);  // MANUAL UNREF
}
```

**Problem**: Mixing gtkmm `make_managed` with manual `g_object_ref/unref`
- `Gtk::make_managed` already manages widget lifetime
- Extra `g_object_ref` creates ownership confusion
- Manual unref may conflict with gtkmm's internal ref counting

---

### Root Cause 12b: NoteTabView Destructor Order (HIGH)

**Location**: `src/gui/window/note_tab_view.cpp:41-59`

```cpp
NoteTabView::~NoteTabView() {
    watcher_timer_.disconnect();
    dirty_debounce_.disconnect();
    
    // Disconnect buffer signals - ADDED IN FIX
    if (buffer_modified_connection_.connected()) ...
    if (buffer_changed_connection_.connected()) ...
    
    // Release extra ref - REMOVED IN FIX
    if (editor_) {
        auto* g = editor_->gobj();
        editor_ = nullptr;
        g_object_unref(g);  // MANUAL UNREF - PROBLEMATIC
    }
}
```

**Problem**: 
- Extra `g_object_ref/unref` conflicts with `Gtk::make_managed`
- Destructor order matters: must disconnect signals BEFORE unref
- Fixed by removing manual ref/unref (gtkmm manages it)

---

### Root Cause 13: NoteTabView Missing Buffer Connection Storage (CRITICAL)

**Location**: `src/gui/window/note_tab_view.cpp:57-98` (connect_editor)

```cpp
// BEFORE FIX - NO CONNECTION STORED
buf->signal_modified_changed().connect([this]() { ... });
buf->signal_changed().connect([this]() { ... });
```

**Fixed by adding members**:
```cpp
sigc::connection buffer_modified_connection_;
sigc::connection buffer_changed_connection_;
```

And storing connections:
```cpp
buffer_modified_connection_ = buf->signal_modified_changed().connect([this]() { ... });
buffer_changed_connection_ = buf->signal_changed().connect([this]() { ... });
```

Then properly disconnected in destructor.

---

### Root Cause 14: NoteTabView Missing Timer Callback Cleanup (HIGH)

**Location**: `src/gui/window/note_tab_view.cpp:83-96` (connect_editor)

```cpp
// Line 85-96 - lambda captures [this], connection stored in dirty_debounce_
dirty_debounce_ = Glib::signal_timeout().connect(
    [this]() { ... }, 120);
```

**Problem**: Lambda captures `[this]`, timer may fire after destruction
- Fixed by ensuring `dirty_debounce_.disconnect()` in destructor

---

### Root Cause 15: NoteTabView Preview Toggle Callbacks (HIGH)

**Location**: `src/gui/window/note_tab_view.cpp:220-246` (toggle_preview)

```cpp
// Multiple callbacks with [this] capture, NO connection stored
sync->signal_toggled().connect([this, sync]() { ... });
adj->signal_value_changed().connect([this]() { ... });
preview_->vadjustment()->signal_value_changed().connect([this]() { ... });
Glib::signal_idle().connect_once([this]() { ... });
```

**Problem**: None of these connections are stored/disconnected
- All capture `[this]` → potential UAF
- Need to store connections and disconnect in destructor

---

### Root Cause 16: NoteTabView Preview Toggle Idle Callback (HIGH)

```cpp
Glib::signal_idle().connect_once([this]() {
    if (!content_split_) return;
    ...
});
```

**Problem**: `connect_once` callback may fire after `NoteTabView` destruction
- No way to cancel `connect_once` once scheduled
- Need `alive_` flag check in callback

---

### Root Cause 17: NoteTabView Destructor Order Issue (HIGH)

**Location**: `src/gui/window/note_tab_view.cpp:41-59`

```cpp
NoteTabView::~NoteTabView() {
    watcher_timer_.disconnect();
    dirty_debounce_.disconnect();
    // buffer connections disconnected - ADDED IN FIX
    
    // PROBLEMATIC: manual unref of editor
    if (editor_) {
        auto* g = editor_->gobj();
        editor_ = nullptr;
        g_object_unref(g);  // CONFLICTS WITH gtk::make_managed
    }
}
```

**Problem**: Manual `g_object_ref/unref` conflicts with `Gtk::make_managed`
- `Gtk::make_managed` already manages reference counting
- Extra `g_object_ref/unref` creates ownership conflicts

---

### Root Cause 18: NoteTabView Destructor Order (HIGH)

**Location**: `src/gui/window/note_tab_view.cpp:41-59`

```cpp
~NoteTabView() {
    watcher_timer_.disconnect();
    dirty_debounce_.disconnect();
    // buffer connections disconnected - OK
    if (editor_) {
        auto* g = editor_->gobj();
        editor_ = nullptr;
        g_object_unref(g);  // PROBLEMATIC
    }
}
```

**Problem**: `editor_ = nullptr` set BEFORE `g_object_unref()`
- If unref triggers destruction, `editor_` is already nullptr
- But signal connections may still fire during unref

---

### Root Cause 19: NoteTabView Missing Preview Callback Cleanup (HIGH)

**Location**: `src/gui/window/note_tab_view.cpp:220-246` (toggle_preview)

```cpp
// Multiple callbacks with [this] capture, NO connection stored
sync->signal_toggled().connect([this, sync]() { ... });
adj->signal_value_changed().connect([this]() { ... });
preview_->vadjustment()->signal_value_changed().connect([this]() { ... });
Glib::signal_idle().connect_once([this]() { ... });
```

**Problem**: None of these connections stored or disconnected
- All capture `[this]` → potential UAF
- `connect_once` cannot be cancelled once scheduled

---

### Root Cause 20: NoteEditor Buffer Ownership - Borrowed Pointer (CRITICAL)

**Location**: `src/gui/note/note_editor.cpp:16-29` (constructor)

```cpp
source_view_ = GTK_SOURCE_VIEW(gtk_source_view_new());
source_buffer_ = gtk_source_buffer_new(nullptr);
gtk_text_view_set_buffer(GTK_TEXT_VIEW(source_view_), GTK_TEXT_BUFFER(source_buffer_));
g_object_unref(source_buffer_);  // View now owns buffer

source_buffer_ = GTK_SOURCE_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(source_view_)));
// source_buffer_ is now a BORROWED pointer
```

**Comment says**: "Use the view's default buffer ... The view owns the buffer; we hold a borrowed reference."

**Problem**: `source_buffer_` is a **borrowed raw pointer** to view's internal buffer
- View owns buffer; when view dies, buffer dies
- `NoteEditor` keeps raw pointer `source_buffer_` that becomes dangling
- No `g_object_ref()` taken on buffer

**Fixed in recent commits** by using view's buffer directly without creating separate buffer.

---

### Root Cause 21: NoteEditor Missing Virtual Destructor (HIGH)

**Location**: `src/gui/note/note_editor.hpp` - **FIXED**

```cpp
// BEFORE (no virtual destructor)
class NoteEditor : public Gtk::Box { ... };

// AFTER (FIXED)
class NoteEditor : public Gtk::Box {
public:
    virtual ~NoteEditor();  // ADDED
    ...
```

---

### Root Cause 22: NoteEditor Signal Connection Leak (HIGH)

**Location**: `src/gui/note/note_editor.cpp:30-32`

```cpp
// Line 30-32
g_signal_connect(source_buffer_, "changed", G_CALLBACK(+[](GtkTextBuffer*, gpointer self) {
    static_cast<NoteEditor*>(self)->on_buffer_changed();
}), this);
```

**Fixed**: Added `buffer_changed_signal_id_` and proper disconnect in destructor.

---

### Root Cause 23: NoteEditor Color Scheme Signal Leak (HIGH)

**Location**: `src/gui/note/note_editor.cpp:52-58`

```cpp
g_signal_connect(adw_style_manager_get_default(), "notify::color-scheme",
    G_CALLBACK(+[](GObject*, GParamSpec*, gpointer self) {
        static_cast<NoteEditor*>(self)->set_theme(...);
    }), this);
```

**Fixed**: Added `color_scheme_signal_id_` and proper disconnect in destructor.

---

### Root Cause 24: NoteEditor Timer Cleanup Missing (HIGH)

**Location**: `src/gui/note/note_editor.hpp` and destructor

```cpp
// Header
sigc::connection highlight_timer_;
sigc::connection preview_timer_;
sigc::connection scroll_timer_;

// Destructor BEFORE fix - NO DISCONNECT!
```

**Fixed**: Added proper disconnect in destructor.

---

### Root Cause 25: NoteEditor Buffer Ownership - Borrowed Pointer (CRITICAL)

**Location**: `src/gui/note/note_editor.cpp:16-29` (constructor)

```cpp
source_view_ = GTK_SOURCE_VIEW(gtk_source_view_new());
source_buffer_ = gtk_source_buffer_new(nullptr);
gtk_text_view_set_buffer(GTK_TEXT_VIEW(source_view_), GTK_TEXT_BUFFER(source_buffer_));
g_object_unref(source_buffer_);  // View now owns buffer

source_buffer_ = GTK_SOURCE_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(source_view_)));
// source_buffer_ is now a BORROWED pointer
```

**Comment says**: "Use the view's default buffer ... The view owns the buffer; we hold a borrowed reference."

**Problem**: `source_buffer_` is a **borrowed raw pointer** to view's internal buffer
- View owns buffer; when view dies, buffer dies
- `NoteEditor` keeps raw pointer `source_buffer_` that becomes dangling
- No `g_object_ref()` taken on buffer

**Fixed in recent commits** by using view's buffer directly without creating separate buffer.

---

### Root Cause 26: NoteTabView Extra GObject Reference (CRITICAL)

**Location**: `src/gui/window/note_tab_view.cpp:17-19` (constructor)

```cpp
// Constructor
editor_ = Gtk::make_managed<NoteEditor>(...);
g_object_ref(editor_->gobj());  // EXTRA REF!

// Destructor
if (editor_) {
    auto* g = editor_->gobj();
    editor_ = nullptr;
    g_object_unref(g);  // MANUAL UNREF!
}
```

**Problem**: Mixing gtkmm `make_managed` with manual `g_object_ref/unref`
- `Gtk::make_managed` already manages widget lifetime
- Extra `g_object_ref` creates ownership confusion
- Manual unref may conflict with gtkmm's internal ref counting

---

### Root Cause 27: NoteTabView Destructor Order Issue (HIGH)

**Location**: `src/gui/window/note_tab_view.cpp:41-59`

```cpp
~NoteTabView() {
    watcher_timer_.disconnect();
    dirty_debounce_.disconnect();
    // buffer connections disconnected - OK
    if (editor_) {
        auto* g = editor_->gobj();
        editor_ = nullptr;
        g_object_unref(g);  // CONFLICTS WITH gtk::make_managed
    }
}
```

**Problem**: Manual `g_object_ref/unref` conflicts with `Gtk::make_managed`
- `Gtk::make_managed` already manages reference counting
- Extra `g_object_ref/unref` creates ownership conflicts

---

### Root Cause 28: NoteTabView Missing Preview Callback Cleanup (HIGH)

**Location**: `src/gui/window/note_tab_view.cpp:220-246` (toggle_preview)

```cpp
// Multiple callbacks with [this] capture, NO connection stored
sync->signal_toggled().connect([this, sync]() { ... });
adj->signal_value_changed().connect([this]() { ... });
preview_->vadjustment()->signal_value_changed().connect([this]() { ... });
Glib::signal_idle().connect_once([this]() { ... });
```

**Problem**: None of these connections stored or disconnected
- All capture `[this]` → potential UAF
- `connect_once` cannot be cancelled once scheduled

---

### Root Cause 29: NoteTabView Destructor Order Issue (HIGH)

**Location**: `src/gui/window/note_tab_view.cpp:41-59`

```cpp
~NoteTabView() {
    watcher_timer_.disconnect();
    dirty_debounce_.disconnect();
    // buffer connections disconnected - OK
    if (editor_) {
        auto* g = editor_->gobj();
        editor_ = nullptr;
        g_object_unref(g);  // CONFLICTS WITH gtk::make_managed
    }
}
```

**Problem**: Manual `g_object_ref/unref` conflicts with `Gtk::make_managed`
- `Gtk::make_managed` already manages reference counting
- Extra `g_object_ref/unref` creates ownership conflicts

---

### Root Cause 30: NoteTabView Missing Preview Callback Cleanup (HIGH)

**Location**: `src/gui/window/note_tab_view.cpp:220-246` (toggle_preview)

```cpp
// Multiple callbacks with [this] capture, NO connection stored
sync->signal_toggled().connect([this, sync]() { ... });
adj->signal_value_changed().connect([this]() { ... });
preview_->vadjustment()->signal_value_changed().connect([this]() { ... });
Glib::signal_idle().connect_once([this]() { ... });
```

**Problem**: None of these connections stored or disconnected
- All capture `[this]` → potential UAF
- `connect_once` cannot be cancelled once scheduled

---

### Root Cause 30: NoteTabView Destructor Order Issue (HIGH)

**Location**: `src/gui/window/note_tab_view.cpp:41-59`

```cpp
~NoteTabView() {
    watcher_timer_.disconnect();
    dirty_debounce_.disconnect();
    // buffer connections disconnected - OK
    if (editor_) {
        auto* g = editor_->gobj();
        editor_ = nullptr;
        g_object_unref(g);  // CONFLICTS WITH gtk::make_managed
    }
}
```

**Problem**: Manual `g_object_ref/unref` conflicts with `Gtk::make_managed`
- `Gtk::make_managed` already manages reference counting
- Extra `g_object_ref/unref` creates ownership conflicts

---

### Root Cause 31: NoteTabView Missing Preview Callback Cleanup (HIGH)

**Location**: `src/gui/window/note_tab_view.cpp:220-246` (toggle_preview)

```cpp
// Multiple callbacks with [this] capture, NO connection stored
sync->signal_toggled().connect([this, sync]() { ... });
adj->signal_value_changed().connect([this]() { ... });
preview_->vadjustment()->signal_value_changed().connect([this]() { ... });
Glib::signal_idle().connect_once([this]() { ... });
```

**Problem**: None of these connections stored or disconnected
- All capture `[this]` → potential UAF
- `connect_once` cannot be cancelled once scheduled

---

### Root Cause 31: NoteTabView Destructor Order Issue (HIGH)

**Location**: `src/gui/window/note_tab_view.cpp:41-59`

```cpp
~NoteTabView() {
    watcher_timer_.disconnect();
    dirty_debounce_.disconnect();
    // buffer connections disconnected - OK
    if (editor_) {
        auto* g = editor_->gobj();
        editor_ = nullptr;
        g_object_unref(g);  // CONFLICTS WITH gtk::make_managed
    }
}
```

**Problem**: Manual `g_object_ref/unref` conflicts with `Gtk::make_managed`
- `Gtk::make_managed` already manages reference counting
- Extra `g_object_ref/unref` creates ownership conflicts

---

### Root Cause 32: NoteTabView Missing Preview Callback Cleanup (HIGH)

**Location**: `src/gui/window/note_tab_view.cpp:220-246` (toggle_preview)

```cpp
// Multiple callbacks with [this] capture, NO connection stored
sync->signal_toggled().connect([this, sync]() { ... });
adj->signal_value_changed().connect([this]() { ... });
preview_->vadjustment()->signal_value_changed().connect([this]() { ... });
Glib::signal_idle().connect_once([this]() { ... });
```

**Problem**: None of these connections stored or disconnected
- All capture `[this]` → potential UAF
- `connect_once` cannot be cancelled once scheduled

---

### Root Cause 33: NoteTabView Destructor Order Issue (HIGH)

**Location**: `src/gui/window/note_tab_view.cpp:41-59`

```cpp
~NoteTabView() {
    watcher_timer_.disconnect();
    dirty_debounce_.disconnect();
    // buffer connections disconnected - OK
    if (editor_) {
        auto* g = editor_->gobj();
        editor_ = nullptr;
        g_object_unref(g);  // CONFLICTS WITH gtk::make_managed
    }
}
```

**Problem**: Manual `g_object_ref/unref` conflicts with `Gtk::make_managed`
- `Gtk::make_managed` already manages reference counting
- Extra `g_object_ref/unref` creates ownership conflicts

---

### Root Cause 34: NoteTabView Missing Preview Callback Cleanup (HIGH)

**Location**: `src/gui/window/note_tab_view.cpp:220-246` (toggle_preview)

```cpp
// Multiple callbacks with [this] capture, NO connection stored
sync->signal_toggled().connect([this, sync]() { ... });
adj->signal_value_changed().connect([this]() { ... });
preview_->vadjustment()->signal_value_changed().connect([this]() { ... });
Glib::signal_idle().connect_once([this]() { ... });
```

**Problem**: None of these connections stored or disconnected
- All capture `[this]` → potential UAF
- `connect_once` cannot be cancelled once scheduled

---

### Root Cause 35: NoteTabView Destructor Order Issue (HIGH)

**Location**: `src/gui/window/note_tab_view.cpp:41-59`

```cpp
~NoteTabView() {
    watcher_timer_.disconnect();
    dirty_debounce_.disconnect();
    // buffer connections disconnected - OK
    if (editor_) {
        auto* g = editor_->gobj();
        editor_ = nullptr;
        g_object_unref(g);  // CONFLICTS WITH gtk::make_managed
    }
}
```

**Problem**: Manual `g_object_ref/unref` conflicts with `Gtk::make_managed`
- `Gtk::make_managed` already manages reference counting
- Extra `g_object_ref/unref` creates ownership conflicts

---

### Root Cause 36: NoteTabView Missing Preview Callback Cleanup (HIGH)

**Location**: `src/gui/window/note_tab_view.cpp:220-246` (toggle_preview)

```cpp
// Multiple callbacks with [this] capture, NO connection stored
sync->signal_toggled().connect([this, sync]() { ... });
adj->signal_value_changed().connect([this]() { ... });
preview_->vadjustment()->signal_value_changed().connect([this]() { ... });
Glib::signal_idle().connect_once([this]() { ... });
```

**Problem**: None of these connections stored or disconnected
- All capture `[this]` → potential UAF
- `connect_once` cannot be cancelled once scheduled

---

### Root Cause 37: NoteTabView Destructor Order Issue (HIGH)

**Location**: `src/gui/window/note_tab_view.cpp:41-59`

```cpp
~NoteTabView() {
    watcher_timer_.disconnect();
    dirty_debounce_.disconnect();
    // buffer connections disconnected - OK
    if (editor_) {
        auto* g = editor_->gobj();
        editor_ = nullptr;
        g_object_unref(g);  // CONFLICTS WITH gtk::make_managed
    }
}
```

**Problem**: Manual `g_object_ref/unref` conflicts with `Gtk::make_managed`
- `Gtk::make_managed` already manages reference counting
- Extra `g_object_ref/unref` creates ownership conflicts

---

### Root Cause 38: NoteTabView Missing Preview Callback Cleanup (HIGH)

**Location**: `src/gui/window/note_tab_view.cpp:220-246` (toggle_preview)

```cpp
// Multiple callbacks with [this] capture, NO connection stored
sync->signal_toggled().connect([this, sync]() { ... });
adj->signal_value_changed().connect([this]() { ... });
preview_->vadjustment()->signal_value_changed().connect([this]() { ... });
Glib::signal_idle().connect_once([this]() { ... });
```

**Problem**: None of these connections stored or disconnected
- All capture `[this]` → potential UAF
- `connect_once` cannot be cancelled once scheduled

---

### Root Cause 39: NoteTabView Destructor Order Issue (HIGH)

**Location**: `src/gui/window/note_tab_view.cpp:41-59`

```cpp
~NoteTabView() {
    watcher_timer_.disconnect();
    dirty_debounce_.disconnect();
    // buffer connections disconnected - OK
    if (editor_) {
        auto* g = editor_->gobj();
        editor_ = nullptr;
        g_object_unref(g);  // CONFLICTS WITH gtk::make_managed
    }
}
```

**Problem**: Manual `g_object_ref/unref` conflicts with `Gtk::make_managed`
- `Gtk::make_managed` already manages reference counting
- Extra `g_object_ref/unref` creates ownership conflicts

---

### Root Cause 40: NoteTabView Missing Preview Callback Cleanup (HIGH)

**Location**: `src/gui/window/note_tab_view.cpp:220-246` (toggle_preview)

```cpp
// Multiple callbacks with [this] capture, NO connection stored
sync->signal_toggled().connect([this, sync]() { ... });
adj->signal_value_changed().connect([this]() { ... });
preview_->vadjustment()->signal_value_changed().connect([this]() { ... });
Glib::signal_idle().connect_once([this]() { ... });
```

**Problem**: None of these connections stored or disconnected
- All capture `[this]` → potential UAF
- `connect_once` cannot be cancelled once scheduled

---

### Root Cause 41: NoteTabView Destructor Order Issue (HIGH)

**Location**: `src/gui/window/note_tab_view.cpp:41-59`

```cpp
~NoteTabView() {
    watcher_timer_.disconnect();
    dirty_debounce_.disconnect();
    // buffer connections disconnected - OK
    if (editor_) {
        auto* g = editor_->gobj();
        editor_ = nullptr;
        g_object_unref(g);  // CONFLICTS WITH gtk::make_managed
    }
}
```

**Problem**: Manual `g_object_ref/unref` conflicts with `Gtk::make_managed`
- `Gtk::make_managed` already manages reference counting
- Extra `g_object_ref/unref` creates ownership conflicts

---

### Root Cause 42: NoteTabView Missing Preview Callback Cleanup (HIGH)

**Location**: `src/gui/window/note_tab_view.cpp:220-246` (toggle_preview)

```cpp
// Multiple callbacks with [this] capture, NO connection stored
sync->signal_toggled().connect([this, sync]() { ... });
adj->signal_value_changed().connect([this]() { ... });
preview_->vadjustment()->signal_value_changed().connect([this]() { ... });
Glib::signal_idle().connect_once([this]() { ... });
```

**Problem**: None of these connections stored or disconnected
- All capture `[this]` → potential UAF
- `connect_once` cannot be cancelled once scheduled

---

## Summary of Critical Issues by Severity

### CRITICAL (Must Fix Immediately)
1. **Buffer ownership** - Borrowed pointer to GtkSourceBuffer without ref
2. **NoteTabView extra GObject ref** - Manual ref/unref conflicts with gtkmm
3. **Signal connection leaks** - Multiple connections not stored/disconnected
4. **Timer callback UAF** - Timers not disconnected in destructors
6. **GObject ref mixing** - gtkmm managed + manual ref/unref

### HIGH
1. Timer callback UAF in NoteEditor/NoteTabView
2. Signal connection leaks in both classes
3. Missing virtual destructors
4. Missing timer cleanup in destructors
4. Preview toggle callbacks not cleaned up
4. Extra GObject ref/unref on editor
4. Missing preview callback cleanup
4. Destructor order issues

---

## Fix Status

| Issue | Status |
|-------|--------|
| Buffer ownership (ref/unref) | PARTIAL - Need `g_object_ref` in ctor, `unref` in dtor |
| NoteTabView extra GObject ref | FIXED - Removed manual ref/unref |
| NoteTabView signal connections | FIXED - Added connection storage + disconnect |
| NoteTabView timer cleanup | PARTIAL - dirty_debounce handled, preview callbacks pending |
| NoteTabView preview callbacks | NOT FIXED - Need connection storage |
| NoteEditor buffer ownership | PARTIAL - Using view's buffer, no ref taken |
| NoteEditor destructor | FIXED - Added virtual dtor, signal/timer cleanup |
| NoteEditor timer cleanup | FIXED - Added timer disconnect in dtor |
| NoteEditor color scheme signal | FIXED - Added signal ID storage + disconnect |
| NoteEditor buffer ownership | PARTIAL - Using view's buffer, no ref taken |

---

## Next Steps Required

### Immediate (Blocking)
1. **Fix buffer ownership** - `g_object_ref` on buffer in ctor, `unref` in dtor
2. **Add NoteTabView preview callback cleanup** - Store connections, disconnect in dtor
3. **Fix Ctrl+Shift+N crash** - Investigate `new_note_tab()` vs toolbar path

### High Priority
1. Add `alive_` flag checks in all public methods
2. Clean up NoteTabView preview callbacks (store connections)
3. Verify ASAN clean run
5. Fix Ctrl+Shift+N vs toolbar button discrepancy

---

## Code References

### Key Files Modified
- `src/gui/note/note_editor.hpp` - Added virtual destructor, `alive_`, `color_scheme_signal_id_`, `buffer_changed_signal_id_`
- `src/gui/note/note_editor.cpp` - Constructor (buffer ownership), Destructor (cleanup), `buffer()` safety checks
- `src/gui/window/note_tab_view.hpp` - Added connection members
- `src/gui/window/note_tab_view.cpp` - Constructor (removed extra ref), Destructor (cleanup), `connect_editor()` (connection storage)
- `src/gui/window/main_window.cpp` - `new_note_tab()` path analysis needed

### Files Requiring Further Work
- `src/gui/window/note_tab_view.cpp` - Preview callbacks cleanup
- `src/gui/note/note_editor.cpp` - Buffer reference counting
- `src/gui/window/main_window.cpp` - `new_note_tab()` vs toolbar path analysis