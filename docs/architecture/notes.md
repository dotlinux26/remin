# Notes & Markdown

The note editor is a first-class tab kind alongside the terminal. It behaves
like a plain notes app (type, line numbers, find/replace, open/save, 10 s idle
autosave) with an optional **live Markdown preview** shown when the tab is split.

## Layering

```text
NoteDocument
     ↓
MarkdownParser (md4c, CommonMark)
     ↓
MarkdownDocument   (intermediate representation)
     ↓
MarkdownRenderer   (native GTK block widgets)
     ↓
NoteTabView
```

* `NoteDocument` holds the semantic state: id, title, markdown body, timestamps,
  dirty flag. It belongs to the core/session domain.
* `NoteEditor` is the edit surface (Gtk::TextView + line-number gutter + find /
  replace bar). It only *reports* changes to the session autosaver; it never
  writes storage directly.
* `MarkdownPreview` is a separate presentation of the same document; it does
  **not** own the document.
* The parser stays decoupled from the editor so the renderer can be replaced
  without touching edit code.

## Markdown pipeline

* **Parser**: `md4c` (CommonMark), a tiny C library (~100 KB). No WebKit.
* **Model**: `MarkdownDocument` is Remin's intermediate representation produced
  from md4c callbacks, decoupling the UI from the C library.
* **Renderer**: native GTK4 block widgets — `HeadingView`, `ParagraphView`,
  `ListView`, `QuoteView`, `CodeBlockView`, `TableView`, `ImageView`,
  `SeparatorView` — laid out in a scrolled container. Pango markup is used only
  for *inline* text runs, never for a whole document, so tables/lists/images
  don't degenerate into markup hacks.

### Scope (V1)

| Blocks            | Inline                |
|-------------------|-----------------------|
| H1–H6             | Bold                  |
| Paragraph         | Italic                |
| Blockquote        | Bold + italic         |
| Ordered list      | Inline code           |
| Unordered / nested| Link (clickable)      |
| Task list         | Local image           |
| Code block        | Strikethrough         |
| Horizontal rule   |                       |
| Table (header/align) |                    |

Syntax highlighting is **out of scope** for V1.

## Performance

The preview does **not** re-render on every keystroke. The editor debounces the
parse ~150–300 ms after the last change, then full re-renders. Incremental
rendering is deferred until profiling shows it is necessary.

## Security

Markdown preview is treated as **untrusted input**:

* No HTML execution, no JavaScript, no WebKit.
* No remote resource loading by default. `http(s)://` images are not fetched;
  only local images are resolved (relative to the note or absolute local paths).
* URI / image paths are validated before touching the filesystem; external links
  open through a controlled handler.

## Autosave

The note editor reports `buffer changed` to the shared session `Autosaver`,
which applies a **10 s idle** policy (vs the terminal's 2 s debounce) — see
[`autosave-lock.md`](autosave-lock.md). There is no independent autosave inside
the editor.
