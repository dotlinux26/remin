# Remin - PDF TOC Links + Image Resolution + Inline HTML/CSS Research

> Research note (2026-09-12). Not implemented yet - design-first, gate with user before coding.
> Goal: fix 3 bugs reported by user on v0.0.9rc:
>   (1) PDF does not create internal links for TOC lines (clicking does not jump; the HTML version jumps)
>   (2) images embedded in PDF are blurry (low resolution); HTML is sharp
>   (3) HTML tag/CSS attributes are not rendered through PDF; only HTML display handles them

---

## 1. Bug 1 - PDF has no internal links for TOC

### 1.1 Current state (verified by code)

- HTML export creates `<a href="#intro">1. Summary</a>` inside `.toc-row` -> browser jumps to the heading on click. (`markdown_html.cpp:287`)
- PDF export draws plain text via `draw_blocks_range()` (`markdown_pdf_export.cpp:199`). No annotation is written into the PDF -> the PDF viewer cannot know which region is a link.
- Layout already has all required data:
  - TocRow has `link_href = "#" + anchor` (`markdown_layout.cpp:630`)
  - Heading block has `link_href = "#" + anchor` (`markdown_layout.cpp:326`)
  - Two-pass TOC has computed `anchor_page` (heading -> page number) (`markdown_pdf_export.cpp:159-167`)

### 1.2 Cairo capability (probe verified)

Cairo >= 1.16 (currently 1.18) supports this fully for PDF:

| API | Purpose |
|-----|---------|
| `cairo_tag_begin(cr, "Link", "dest='anchor'")` / `cairo_tag_end` | creates clickable region (internal link) |
| `cairo_tag_begin(cr, "cairo.dest", "name='anchor'")` / `cairo_tag_end` | creates a destination (named dest) |
| `cairo_pdf_surface_add_outline(surface, parent, title, "dest='anchor'", flags)` | PDF bookmarks (sidebar) |

Probe `/tmp/opencode/pdf_tags.cpp` ran against the real library and confirmed the produced PDF:

```text
/Annots  /Subtype /Link  /Rect [56 784.89 137 797.89]  /Dest [2 0 R /XYZ 57 704.89 0]
/Outlines  /Title (Intro)  /Dest [...]           (bookmark sidebar)
```

So clickable internal links + bookmarks both work on cairo 1.18.

### 1.3 Implementation issues

- **cairomm does not wrap `cairo_tag_begin` / `cairo_pdf_surface_add_outline`.** Must call the C API directly via `cr->cobj()` (`Context::cobj()` returns `cairo_t*`, `surface->cobj()` returns `cairo_surface_t*`) - confirmed present in cairomm 1.16.
- **`draw_blocks_range()` is shared by preview + PDF.** Preview navigates via hit-regions (`collect_hit_regions`), it needs no tags. Add a flag `emit_links` (default `false`, PDF sets `true`) - when on, a block with `has_link_hit && link_href[0] == '#'` gets wrapped in a `Link` tag around the text drawing; a heading gets wrapped in a `Dest` tag `name=<anchor>`.
- **TocRow PDF draws title/leader/page-name separately** (`markdown_draw.cpp:252-297`). Only the title part of the layout belongs inside the `Link` tag (leader dots + page number are not linked).
- Note: Cairo uses the extents of the drawing ops inside the tag as the clickable area; since the title text is drawn inside the tag, the rect auto-sizes correctly - no manual `rect=` needed.

### 1.4 Proposal

- Enable `emit_links` = true in `export_pdf()`.
- TocRow + every internal `#anchor` link block -> `Link` tag `dest='<anchor>'` around the text drawing.
- Heading -> `Dest` tag `name='<anchor>'` around the heading content drawing (for every heading, including ones not in the TOC - so other internal links can also jump to them).
- Additionally: `cairo_pdf_surface_add_outline` for each heading (in document order) -> bookmarks sidebar in the PDF viewer. Build hierarchy by heading level (H1 -> root, H2 -> child of the currently open H1). **Bonus - decided at Q1.**

---

## 2. Bug 2 - PDF images blurry, HTML sharp

### 2.1 Current state (verified)

- `draw_pixbuf()` (`markdown_draw.cpp:15-60`):
  - calls `gdk_pixbuf_scale_simple(source, (int)w, (int)h, GDK_INTERP_BILINEAR)` -> scales the image down to display size **before drawing**;
  - creates a `Cairo::ImageSurface` at the scaled size;
  - `cr->set_source(surface, x, y); cr->paint()` - draws 1:1.
- With `Cairo::PdfSurface` (72 dpi, 1 pt = 1 px), showing a 2000x1000 image in a 400x200 pt frame scales it down to 400x200 px before embedding -> the PDF holds only `400x200` pixels. Zooming or printing makes the image blurry/pixelated.
- HTML export uses `<img src="file.png">` with src pointing at the original file -> the browser renders at **native resolution**, sharp at any zoom. (`markdown_html.cpp:95-102`)
- Probe `/tmp/opencode/imgres.pdf` confirmed:
  - Current way: PDF contains `/Width 400 /Height 200` (5x information lost).
  - Fixed way: PDF contains `/Width 2000 /Height 1000` + a transform matrix -> sharp.

### 2.2 Fix (probe verified)

Do not scale the pixbuf into a small surface; keep **native resolution** and let Cairo scale via a transform matrix on paint:

```cpp
auto surface = Cairo::ImageSurface::create(ARGB32, native_w, native_h);  // from original pixbuf
cr->save();
cr->translate(x, y);
cr->scale(w / native_w, h / native_h);
cr->set_source(surface, 0, 0);
cr->paint();
cr->restore();
```

-> PDF embeds the full-resolution image (`/Width 2000`) together with one scale matrix; the viewer decides the render resolution -> same as HTML.

### 2.3 Considerations

- The PDF file gets larger (exactly the original image data) - same trade-off as HTML embed; acceptable per the requirement "images must be sharp like HTML".
- Images are already scale-to-fit in layout (`resolve_image_block`, no upscale) - keep that behavior; only change how they are **rendered**.
- Transform native image -> display frame is accurate whether scaling up (never happens, due to no-upscale) or down (does happen).

---

## 3. Bug 3 - HTML/CSS attributes not rendered through PDF

### 3.1 Current state (verified by probe `sp.cpp`)

- `RunBuilder::emit` **drops `HtmlSpan`/`HtmlBlock` entirely**:
  ```cpp
  case NodeType::HtmlSpan:
  case NodeType::HtmlBlock:
      return;  // raw HTML dropped for safety      (markdown_layout.cpp:135-137)
  ```
- HTML export passes through verbatim -> the browser renders the CSS. Preview + PDF (same layout pipeline) lose all styling from inline HTML.
- md4c chunks inline HTML into several nodes:
  ```text
  x <span style="color:red">RED</span> y
      ^htmlspan(open)          ^text    ^htmlspan(close)
  ```
  -> need a **style stack** in `RunBuilder` (like the existing save/restore for emphasis/strong/link).

### 3.2 Proposed scope (gate at Q2)

Inline HTML subset only - not a browser engine:

- **Tags**:
  - span with the `style="..."` attribute (CSS content)
  - b / strong, i / em, u, s / del (map to weight=700 / italic / underline / strike - already present in `StyledText`)
  - code (map to mono font + background - the Code style already exists)
  - mark (light background)
  - font, sub/sup (size), small/big (relative size) - TBD at Q2
- **CSS properties inside `style=""`** (separated by `;`):
  - `color` (named / `#hex` / `rgb()`)
  - `background-color`
  - `font-weight` (normal / bold / 100-900)
  - `font-style` (normal / italic)
  - `text-decoration` (underline / line-through)
  - `font-size` (pt / px / %) - TBD at Q2
  - NO layout support: margin/padding/position/float/display/flex - out of scope.
- **HtmlBlock** carrying `style` (a block) that stands alone, not a page-break marker: currently dropped. V1 proposal: still drop it (complex block layout) - handle inline only. TBD at Q3.

### 3.3 Implementation location

In `RunBuilder::emit`, add handling for `NodeType::HtmlSpan`:
- Maintain one stack `[css_override]` parallel to the existing save/restore mechanism.
- Open tag -> parse `style`/attrs, push onto the stack, apply the override to state (color, bg, weight, italic, underline, strike, size, font).
- Close tag -> pop.
- Text nodes in between -> pushed with the currently active state.
- Make sure inline HTML state applies the same way as emphasis/strong (nesting correct).

-> Preview + PDF both receive the new styles; HTML export stays pass-through (the browser renders itself).

### 3.4 Probe `blk.cpp` results - md4c handles HtmlBlock in 3 ways

```text
case A: <div style="color:red">\n\n**Bold** text\n\n</div>
  -> HtmlBlock(<div style="color:red">)  +  Paragraph(**Bold** text)  +  HtmlBlock(</div>)
case B: <div style="color:blue">Hello <b>world</b></div>   (inline, on a continuous line)
  -> 1 HtmlBlock containing the whole chunk '<div ...>Hello <b>world</b></div>\n'
case C/D: nested blocks / several adjacent blocks -> 1 HtmlBlock whole chunk (HTML block type 6)
```

-> **Case A** (blank lines separate the parts) md4c auto-splits open-tag / Paragraph / close-tag; the markdown inside (**Bold**) is still parsed. This is the cheapest render path: open the style context at the open-tag block, the child paragraph is styled, close the context at the close-tag block. Nestable (div inside div) with one context stack in `emit_node`.

-> **Case B/C/D** merge the whole chunk (HTML block type 6). Rendering needs a mini tokenizer: peel the `<tagname style="...">` open, the content (text / inline tags), the closing `</tagname>`. Supported content is limited to the inline subset (Q2). If the block contains nested block-level HTML (div/table/section...) -> drop that block, keep the safe old behavior.

### 3.5 Notes

- Small CSS parser: split on `;`, split on `:`, strip whitespace; recognize keywords; no full unit support. Must be safe (no crash on malformed style).
- Reuse `decode_entity` / existing helpers if needed.
- `StyledText` already has all needed fields: `color`, `background`, `weight`, `italic`, `strike`, `underline`, `size_pt`, `font_family` - no struct change needed.

---

## 4. Scope / out of scope

| In scope (after gate) | Out of scope |
|-----------------------|--------------|
| PDF internal links for TOC + dest for headings | External links (http://...) as PDF URI links |
| PDF bookmarks sidebar (if Q1 agrees) | Preview hit-region changes (keep as-is) |
| PDF images at native resolution (fix blur) | New WebP/AVIF fetch |
| Inline HTML/CSS subset (span style + basic tags) | Block HTML layout (div/table HTML not supported) |
| Touch exactly `draw_blocks_range` + `RunBuilder::emit` | Change HTML export behavior |

## 5. Questions for user

- **Q1**: PDF bookmarks (sidebar) - wanted? (same dest mechanism already present, cheap, looks more professional). Proposal: yes, hierarchical by heading level.
- **Q2**: How far should the inline HTML subset go? Proposal: span+style (color, background-color, font-weight, font-style, text-decoration, font-size) + b/i/u/s/code/mark. Ask whether font/sub/sup/small/big is needed.
- **Q3**: HtmlBlock with style (block) in PDF V1: skip (only page-break marker) or render it now? Proposal: skip standalone blocks (complex), inline first.

---

## 6. Design gate decided (2026-09-12) - implementation source

> Status: **GATE PASSED**, user approved implementation. This is the implementation contract.
> All code must match the decisions and constraints below, per scope in section 4.

### 6.1 Decided decisions

| Question | Decision |
|----------|----------|
| **Q1** - PDF bookmarks | **YES**. Hierarchical outline: **H1 = root node (child of `CAIRO_PDF_OUTLINE_ROOT`); H2 = child of the most recently opened H1; H3 = child of the most recently opened H2** (each level keeps its current parent node). Use the anchors/dests already present, in heading order within the document. |
| **Q2** - Inline HTML subset | **span `style="..."`** + **b/strong, i/em, u, s/del, code, mark**. CSS subset: `color`, `background-color`, `font-weight`, `font-style`, `text-decoration`, `font-size`. NO font/sub/sup/small/big (from proposal 3.2). |
| **Q3** - HtmlBlock | **Render it**. Includes: (a) case A - open/close tag + Paragraph in between (md4c splits it for us), (b) case B/C/D - whole chunk via the mini tokenizer. CSS subset same as inline. NO browser engine; unsupported -> **fail-safe** (drop style/tag, keep the text), never crash in any case. |

- **PDF internal TOC links**: keep - clicking a TOC line jumps to the heading (YES, do it).
- **PDF heading destinations**: keep - every heading gets a dest so links can jump into it.
- **Image native resolution**: YES, do it (fix blur like HTML).
- **TOC page-number + dotted-leader**: kept intact from v0.0.9rc.

### 6.2 Implementation plan (order)

1. **Links in PDF** - add an `emit_links` param to `draw_blocks_range()` (default `false`); `export_pdf()` sets `true`. Wrap headings in a `Dest` tag `name=<anchor>`; wrap the TocRow title + internal `#anchor` link blocks in a `Link` tag `dest=<anchor>` (leader dots and the page number NOT inside the tag). Call the C API via `cr->cobj()`.
2. **Outline (bookmarks)** - in `export_pdf()`, after the ordered heading list is available, build the parent tree per rule 6.1 and call `cairo_pdf_surface_add_outline(surface, parent, title, "dest='<anchor>'", flags)` for each heading **before** `surface->finish()`. `surface` is obtained via `pdf_surface->cobj()`.
3. **Image native resolution** - `draw_pixbuf()`: detect the target as a `PdfSurface` (`cr->get_target()->get_type() == CAIRO_SURFACE_TYPE_PDF`); when it is PDF, create the `ImageSurface` from the pixbuf at **native resolution**, `translate(x,y)` + `scale(w/nw,h/nh)` + `paint()`. Preview (screen) keeps its current scaling behavior.
4. **Inline HTML/CSS** - create `markdown_inline_html.{hpp,cpp}`:
   - `parse_css_style()`: recognize a keyword, split on `;`/`:`, parse the value (named/`#hex`/`rgb()`, weight, style, text-decoration, font-size pt/px/%); safe with malformed input.
   - Map semantic tags -> `StyledText` override (b/strong -> weight 700, i/em -> italic, u -> underline, s/del -> strike, code -> existing Code style, mark -> light background).
   - Mini chunk tokenizer for HtmlBlock case B/C/D (peel open/close + inline subset inside; nested block-level -> drop the block, keep old behavior).
   - Integrate into `RunBuilder::emit`: style stack parallel to save/restore for `HtmlSpan` (open push / close pop), `HtmlBlock` case A via an open/close context stack, page-break marker behavior unchanged.
5. **Tests** - unit tests for: PDF TOC link with a matching dest, outline hierarchy (H1/H2/H3 parent-child), PDF image containing full-res width, inline CSS parse (valid + malformed without crash), HtmlBlock render case A + B/C/D, malformed HTML/CSS no crash.
6. **Build + ctest** - `cmake --build build -j4` + `ctest --test-dir build --output-on-failure` (18/20 currently, 2 pre-existing failures out of scope).

### 6.3 Constraints (invariants)

- **HTML export = verbatim pass-through, NO behavior change** (`markdown_html.cpp`). The HTML/CSS subset is only for Remin preview/PDF understanding - NOT a sanitizer/rewrite.
- Do not redesign the layout architecture; **reuse `StyledText` fields + existing infra**.
- Structure like the existing save/restore mechanism (emphasis/strong/link) - inline HTML is just one more override, not a pipeline split.
- No hardcoded colors/icons; follow `AGENTS.md` (no emoji, no theme changes).
- `draw_blocks_range` shared by preview+PDF -> changes must keep preview hit-region working as before (preview navigation unchanged).