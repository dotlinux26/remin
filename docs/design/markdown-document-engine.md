# Design - Remin Markdown Document Engine (MD4C -> Document Model -> Native Cairo)

Date: 2026-09-09
Status: DESIGN - architecture decided with user (no WebKit). Implementation in
progress by phases below. This doc records the REQUIREMENTS + ENGINE SOLUTION
in one source.

---

## 0. Goals

The Remin Editor/Markdown milestone (12 features) runs on a **single pipeline**
with no browser dependency:

```text
        Markdown Source
              |
              v
            MD4C              (CommonMark + extensions)
              |
              v
   +---------------------+
   | Remin Document Model|   (semantic, NOT HTML-shaped)
   |                     |
   | heading/paragraph/  |
   | span/link/image/    |
   | list/table/quote/   |
   | code/toc/footnote/  |
   | admonition/pagebreak|
   +---------+-----------+
             |
             v
  +--------------------+
  | Remin Style Engine |  ("Remin Document Style" - CSS-like subset)
  +---------+----------+
            |
            v
  +--------------------+
  | Remin Layout Engine|  (Pango measure -> line breaking / page breaking)
  |                    |
  |  Display List      |  (DrawCommand: Text/Rect/Line/Image)
  +---------+----------+
          |            |
          v            v
  GTK/Cairo Preview    Pango/Cairo PDF
  (Gtk::DrawingArea)   (Cairo::PdfSurface)
```

**Cardinal rule**: Preview and PDF must **not** have two different layouts.
One Display List, one `draw(display_list, cairo_ctx)` function - only the
surface differs (screen vs PDF). "Sames" = same layout/rendering pipeline, no
promise of pixel-perfect output in every PDF viewer.

---

## 1. Architecture decisions (agreed with user)

| Decision | Value | Why |
|---|---|---|
| No WebKit | no | Compact, self-contained, 1 binary + assets, no WebProcess/JS Core/ICU/... |
| No Python / Pandoc / WeasyPrint | no | No external runtime; PDF rendered internally. |
| MD4C | yes | Small parser, CommonMark + tables/tasklists/strikethrough/permissive autolinks. |
| Document Model | yes | Semantic (heading/paragraph/...), NOT designed around DOM. |
| Layout Engine + Display List | yes | Preview/PDF/export share the same layout. |
| GTK/Cairo preview | yes | `Gtk::ScrolledWindow -> Gtk::DrawingArea`, Cairo + Pango. |
| PDF = Cairo::PdfSurface | yes | Pagination/header/footer/TOC built on Display List ourselves. |
| Static friendly | yes | md4c embeddable; Cairo static possible; Pango static heavy (runtime still needs font/glib...) |

Final dependency footprint:

```text
core document pipeline:  MD4C + GLib/GTK + PangoCairo + Cairo
```

---

## 2. Requirements (milestone, 12 features)

1. **Image paste**: Ctrl+V in the note editor, clipboard has image -> save
   `assets/asset-NNN.png` (scan 001 up) -> insert ref `![alt](assets/asset-003.png)`.
2. **Full Markdown preview** (native): headings 1-6, paragraph, bold/italic/
   strikethrough, inline code, fenced code, links, images (local/relative/
   absolute/data URI), lists (ordered/nested), task lists, blockquote, hr,
   tables (alignment), TOC, footnote (if the md4c version supports it).
3. **Custom CSS**: "Remin Document Style" - CSS-like subset (colors, font, size,
   margin, padding, border, background, alignment, page). NOT a full CSS engine.
4. **CSS setting**: pick a `.css`/`.rcss` file in Settings; Reset to default;
   preview + PDF + HTML export all use that file (preview/PDF parse the subset,
   HTML export embeds the raw text).
5. **Preview toolbar**: [Sync Scroll] + [Export HTML] + [Export PDF] (kept in
   the existing `preview_host` header).
6. **HTML export**: `render_html_body()` from the same AST -> 1 self-contained
   .html file (embedded CSS, image refs -> file:// or relative), opens a Save dialog.
7. **PDF export**: `Cairo::PdfSurface`, A4/letter pagination, header/footer,
   page number `{page}/{pages}`, TOC page, embedded images.
8. **Print header/footer**: left / center / right + tokens `{page}{pages}{date}
   {time}{title}{author}{filename}`.
9. **TOC**: `[[TOC]]` = Remin extension -> auto TOC (nested, numbered 1 / 1.1),
   toc_max_depth.
10. **Page numbering**: footer `Page N of M` + tokens.
11. **Document model**: one single semantic AST (md4c parses once), everything
    reads the same model. Image asset mapping, per-note print config (JSON blob).
12. **Testing**: unit tests for parse/ast/html/style/layout/document/pdf and the
    whole ctest suite still passes.

---

## 3. Document Model (semantic, not HTML-shaped)

Current state: `src/gui/markdown/markdown_ast.{hpp,cpp}` - md4c callbacks ->
`Node` tree (arena -> value):

```cpp
enum class NodeType {
    Root, Heading(level), Paragraph, BlockQuote, CodeBlock(lang,text),
    HtmlBlock, ThematicBreak, List(ordered,start,tight), ListItem(task,checked),
    Table(aligned cells), TableRow, TableCell(header,align),
    Link(href,title), Image(src,title,{alt}), Emphasis, Strong, Del,
    CodeSpan, Text, SoftBreak, HardBreak, HtmlSpan, Toc
};
```

- Single parse (md4c / MD_DIALECT_GITHUB), semantic node tree.
- `[[TOC]]` is recognized as a `Toc` node.
- `headings()` -> flatten + deterministic anchor dedup (intro, intro-2, ...),
  used for TOC and `#anchor` links.
- HTML renderer already exists (`markdown_html.*`) for export.

Future (if the md4c version supports it): footnote/comment/admonition/highlight/
sup/sub -> add the corresponding NodeType + parser flag. **No promise of a math
renderer** (separate engine, see section 8).

---

## 4. Remin Style Engine ("Remin Document Style")

State: `src/gui/markdown/markdown_style.hpp` (parser under implementation).

- CSS-like subset parser: `selector { prop: value; }`, multiple selector names,
  values with units (`12pt`, `18mm`, `0.5cm`, `16px`, plain numbers).
- Selectors: `document, h1..h6, p, code, pre, blockquote, a, ul, ol, li, table,
  th, td, tr, hr, .toc, .toc-title, .toc-link, page`.
- Props: color, background(-color), font-family, font-size, font-weight,
  font-style, text-decoration(strike/underline), margin[+-top/bottom/left/right],
  padding, border, border-left, border-color, border-width, line-height,
  page{size|width|height|margin}.
- Palette tokens `@text @accent @bg @surface @border @text-muted
  @red @orange @amber @green @blue` -> resolved against the theme at parse time ->
  preview/PDF follow dark mode without modifying the file.
- Builtin `default_style(palette)`; `apply_style(css, base, palette)`.
- A parse failure must not crash; every unknown is ignored.

Definition: this is a **Remin Style Sheet**, NOT a CSS engine (no flexbox/
grid/position/animation/DOM).

---

## 5. Layout Engine -> Display List

**`src/gui/markdown/markdown_layout.*`** (under implementation).

Input: `MarkdownAst + StyleSheet + content_width (+ page_height for PDF)`.
Pango measures & wraps; the result is **block boxes** with drawable primitives:

```cpp
struct DrawText   { std::string text; double x,y; std::string font; double size_pt;
                    int weight; bool italic,strike,underline; Color color, background; }; // code bg
struct DrawRect   { double x,y,w,h; Color fill; double border_w; Color border; };
struct DrawLine   { double x1,y1,x2,y2; double width; Color color; };
struct DrawImage  { std::filesystem::path path; double x,y,w,h; };
struct DrawCmd    { enum Kind { Text, Rect, Line, Image } kind; DrawText t; DrawRect r;
                    DrawLine l; DrawImage img; };

struct BlockBox {
    enum Kind { Paragraph, Heading, Quote, Code, List, Table, Hr, Image, Toc } kind;
    double y, height;                 // in flow (margins applied before/after)
    double margin_before, margin_after;
    std::vector<DrawCmd> cmds;
    bool splittable;                  // paragraph/code/... can be cut between pages
    // for the page breaker:
    ... bounding region to clip/translate when splitting lines
};
```

- **Inline**: text runs (bold/italic/strike/underline/color/bg) represented as a
  Pango attr list on one layout/paragraph; links record their position
  (Underline + accent color). Inline image -> block Image (V1 simplification,
  acknowledged).
- **Tables**: grid, column width from content + available, alignment, header bg,
  border.
- **Sticky keep rules**: heading keeps-with-next, no orphan heading at page end.
- **Preview**: a single continuous flow (no page breaks), width = widget width.
- **PDF**: width = content_width + **page breaking** (line-granular for
  splittable, keep for heading/listitem/table-row).

Shared Display List -> **the same draw code** for preview and PDF (section 6).

---

## 6. Draw (shared Cairo)

**`src/gui/markdown/markdown_draw.*`** - `draw_blocks(cr, blocks, ...)`.

- PangoCairo draws text (create_layout on the current cr -> wrap matches layout).
- HtmlDest TOC/heading anchors drawn as text (with number + indent).
- Images: `cairo_set_source_surface` (load GdkPixbuf -> Cairo surface); scale
  preserving aspect ratio, bounded by width.
- Code block: background + border; quote: left border + light background padding.
- Called by: `MarkdownPreview::on_draw` (screen cr) and `markdown_pdf_export`
  (PDF cr). **Same code path.**

---

## 7. Native GTK Preview

**`src/gui/note/markdown_preview.*`** - rewrite (keep the public API so
`NoteTabView` does not change):

```text
Gtk::ScrolledWindow
  `-- Gtk::DrawingArea        (Cairo renderer, no more GtkLabel)
```

Public API unchanged:
```cpp
MarkdownPreview();  void render(const std::string&);
void set_scroll_fraction(double);   vadjustment();
+ void set_style_path(const std::string&);   // user CSS (subset)
+ void set_dark(bool);                          // change palette, re-layout
```

- Native GTK scrolling -> scroll sync uses `vadjustment()` as before.
- Link click: GestureClick hit-test on the Display List link rect -> open in
  browser (xdg-open / Gtk show-uri). *(later phase)*
- Light-first, dark mode = palette switch + re-render (no WebView reload).

---

## 8. PDF Export

**`src/gui/markdown/markdown_pdf_export.*`** - `Cairo::PdfSurface`.

- Page geometry: `page.size` (a4/letter or width/height), `page.margin`.
- Pipeline: AST -> StyleSheet(+user) -> LayoutEngine (paginate with
  content_height) -> TOC 2 pass (if show_toc) -> draw each page:
  - page background
  - header (left/center/right, tokens)
  - content
  - footer (left/center/right, `{page}/{pages}`)
- Optional TOC page first: headings at level <= max_toc_depth, page numbers,
  dotted leaders; internal links via `cairo_pdf_surface_add_outline` / annotation.
- Images: embedded into the PDF (cairo image surface per image).
- Code block split line-granular; heading keeps-with-next.
- Pass 1 = layout without TOC to know the total `{pages}`; pass 2 = render TOC +
  page numbers.

Header/footer tokens: `{page} {pages} {date} {time} {title} {author} {filename}`.

**Does not use**: PrintOperation screenshot, WeasyPrint, wkhtmltopdf.

---

## 9. Feature matrix (md4c 0.4.8 on the current machine)

| Feature | md4c | Remin |
|---|---:|---:|
| H1-H6, paragraph, bold/italic, inline & fenced code, links, images, ordered/unordered/nested lists, tasklist, blockquote, hr, tables, strikethrough | yes | yes (layout + draw) |
| permissive wikipedia/wikilink/underline extension | yes flag | partial (simple renderer) |
| footnote / highlight / sup / sub / admonition | no (0.4.8) | waits for an md4c version with the flag - not a milestone blocker |
| math (`$...$`) | yes (syntax recognized) | needs a separate math renderer (outside milestone) |
| TOC ([[TOC]]), heading IDs, text colors, background, custom theme, custom stylesheet, page size, margins, page break, header/footer, page numbers, left/center/right, PDF, clickable links, image scaling, watermark, cover page | parser at heading level | yes - Remin layout/display list |

---

## 10. Static linking (notes)

- **md4c**: easy to embed the source (`md4c.c/h`) or `libmd4c.a`.
- **Cairo**: static OK (`pkg-config --static --libs cairo` to see the chain on the
  build machine).
- **Pango**: build/static per chain (glib, harfbuzz, freetype, fontconfig) -
  feasible but still needs font/backend at runtime; not "100% standalone binary".
- **GTK**: a GTK4 app still needs glib/gio runtime; **no WebKit** removes a
  huge footprint (no WebProcess/JS Core/libsoup/ICU...).

Long-term decision: target **1 binary + a few assets**, no extra browser runtime
to install.

---

## 11. Implementation gating (phases)

Direction (each phase has a build + tests before moving on):

- Phase A: done - Document Model (`markdown_ast`) + HTML body renderer
  (`markdown_html`) + document wrapper/assets/print config (`markdown_document`).
- Phase B: pending - Style Engine (`markdown_style`) - parser + defaults + palettes.
- Phase C: pending - Layout Engine (`markdown_layout`) -> BlockBox + Display List;
  page breaker.
- Phase D: pending - Draw layer (`markdown_draw`) + rewrite `MarkdownPreview`
  (DrawingArea).
- Phase E: pending - NoteEditor paste-image + NoteTabView toolbar/export + assets
  + sync scroll + Settings CSS + SessionController keys.
- Phase F: pending - PDF export (`markdown_pdf_export`): header/footer/TOC/
  pagination tokens.
- Phase G: pending - Tests (ast/html/style/layout/document/pdf) + golden accept
  + AGENTS.md.

**Overall acceptance**: preview renders cleanly with Cairo (no WebKit); PDF
exports with the same layout; all 12 features complete; all old + new ctest pass;
app runs clean.

---

## 12. Non-goals (keep the boundary)

- No full CSS engine (flex/grid/position/animation/DOM/JS).
- No fancy math renderer (a separate engine would be needed for true LaTeX).
- No heavy syntax highlighting (add later as an independent layer).
- No WebKit/Python/Pandoc/WeasyPrint in the core document pipeline.