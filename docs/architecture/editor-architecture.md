# Editor Architecture — GtkSourceView 5 via thin C++ adapter

> Decision D-1. This describes the **concrete** editor layout strategy required
> by the audit gate before implementation. It replaces the previous two-TextView
> + manual-scroll-sync model.

## Why GtkSourceView 5

GtkSourceView 5 inherits `GtkTextView` but adds the pieces Remin's editor
actually needs, all natively sharing the text layout:

- `GtkSourceGutter` + `GtkSourceGutterRendererText` → **line numbers** drawn
  from the view's own layout/line metrics.
- `GtkSourceGutterLines` → cached/shared per-visual-line info between gutter
  renderers and the main view (this is the key to soft-wrap correctness).
- current-line highlight, line markers (future), `GtkSourceBuffer` (undo,
  file I/O), `GtkSourceSearchContext` (find/replace), syntax highlighting via
  `GtkSourceLanguage`.

Philosophy: same as VTE for the terminal — use the battle-tested library; Remin
builds only its differentiators (workspace, tabs/panes, persistence, snapshot,
restore, plugin surfaces, UX).

## Stack

```text
NoteEditor (C++)                       [src/gui/note/note_editor.*]
   │  talks only to the adapter
   ▼
GtkSourceAdapter (C++)                 [src/gui/editor/source_view_adapter.*]
   │  thin C++ wrapper over the C API
   ▼
GtkSourceView 5 C API                  [gtksourceview-5, v5.12.0]
   ├── GtkSourceBuffer
   ├── GtkSourceGutter / GutterLines
   ├── GtkSourceGutterRendererText (or custom)   [gutter_renderer.* if needed]
   └── GtkTextView layout  (single source of truth)
   ▼
GTK4
```

**Hard rules**
- Raw `GtkSourceView*` / `GtkSourceGutter*` C structs **must not** leak into
  `WorkspaceCore`, `SessionController`, `NoteDocument`, or `NoteTabView`.
- **No** `TextView(editor) + TextView(gutter) + manual vertical-adjustment sync`.
- **No** guessed line heights / magic offsets / vadjustment polling.
- Gutter renderer reads its geometry from `GtkSourceGutterLines` / the view
  layout; we never build a second text model just to draw numbers.

## Required invariant (acceptance)

```
Logical line 42
   | very long text...
   | continues...
   | continues...
```

The single gutter entry `42` must span the **entire visual extent** of logical
line 42 (all wrapped rows). Must hold under:

- window resize
- soft-wrap on/off
- font change
- theme change
- large documents
- empty lines

## Click-to-line navigation

Clicking a line number must:
1. move the caret to that **logical** line,
2. scroll it into view,
3. apply a **transient** navigation highlight (current-line style / mark).

This is a navigation/focus marker, **not** a clipboard selection.

## Proposed adapter interface (C++)

```cpp
// editor/source_view_adapter.hpp (sketch)
class SourceViewAdapter {
public:
    explicit SourceViewAdapter(GtkTextView* text_view); // wraps gtk_source_view_new()
    GtkWidget* widget();

    void set_wrap_mode(GtkWrapMode);
    void set_monospace(bool);

    // line numbers
    void show_line_numbers(bool);
    void set_line_number_click_cb(std::function<void(int logical_line)>);

    // buffer access
    void set_text(const std::string&, bool clear_undo);
    std::string text() const;

    // find / replace
    // via GtkSourceSearchContext or plain TextIter search
};
```

Non-goals in this stage: keep the adapter minimal; fold find/replace and syntax
highlighting in later stages (E / follow-ups) but design the interface so they
slot in.

## Files

- `src/gui/editor/source_view_adapter.hpp/.cpp` — NEW thin adapter
- `src/gui/editor/gutter_renderer.hpp/.cpp` — NEW (custom renderer if needed)
- `src/gui/note/note_editor.hpp/.cpp` — REWRITE to use adapter; remove gutter
  TextView + manual scroll-sync + internal find bar
- `src/gui/CMakeLists.txt` — add new files + `PkgConfig::GTKSOURCE`
- `cmake/Dependencies.cmake` — add `gtksourceview-5` pkg_check_modules
- `docs/architecture/notes.md` — update the editor description (currently
  describes the old two-view model)
