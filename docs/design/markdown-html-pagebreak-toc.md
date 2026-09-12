# Remin - Markdown Raw HTML + Page Breaks + TOC Numbering Research

> Research note (2026-09-11). Not yet implemented - design-first, gate with user
> before coding. Goals: (1) let users write raw HTML in Markdown to force CSS and
> pagination, (2) TOC with dotted leader + book-style page numbers.

---

## 1. Current state (verified with code + probe)

### 1.1 md4c parser already tracks raw HTML

Probe `ast_probe.cpp` on `MD_DIALECT_GITHUB` (no `MD_FLAG_NOHTMLBLOCKS`/`NOHTMLSPANS`):

```text
# T
Hello <span style="color:red">world</span>!
<div style="page-break-after: always;"></div>
```

-> AST:
```
Paragraph children:
  Text "Hello "
  HtmlSpan "<span style="color:red">"     (type 21)
  Text "world"
  HtmlSpan "</span>"                        (type 21)
  Text "!"
HtmlBlock "<div style="page-break-after: always;"></div>\n"   (type 5)
```

- `MD_BLOCK_HTML` -> `NodeType::HtmlBlock` (`markdown_ast.cpp:193`)
- `MD_TEXT_HTML` -> `NodeType::HtmlSpan` (`markdown_ast.cpp:324`)

=> **The parser keeps all the information.** The renderers are what drop it:

| Stage | Current behavior |
|-------|------------------|
| `markdown_html.cpp:179` | `HtmlBlock` -> `return;` (drop) |
| `markdown_html.cpp:109-113` | `HtmlSpan` -> drop |
| `markdown_layout.cpp:620` | `HtmlBlock` -> `return;` (drop) |
| `markdown_layout.cpp:134-136` | `HtmlSpan`/`HtmlBlock` in RunBuilder -> `return;` (drop) |
| PDF draw | built on layout blocks -> HTML has no blocks |

### 1.2 Current TOC

- `[[TOC]]` -> `NodeType::Toc` (`markdown_ast.cpp:345 mark_toc_nodes`).
- Layout `case NodeType::Toc` (`markdown_layout.cpp:570`) creates one `Block::TocRow` per heading.
- TocRow has: `toc_indent` (14pt/level), `toc_number` ("1.", "1.1."), `toc_text`, `toc_anchor`, `has_link_hit = true`, `link_href = "#anchor"`.
- Preview click -> `markdown_preview.cpp:75` hit-test -> scroll to the run whose `anchor` matches (`markdown_preview.cpp:80-90`) => **click-to-navigate works**.
- PDF export: TocRow drawn as plain text, **no dotted leader, no page number**.

---

## 2. Feature goals

### 2.1 Raw HTML -> CSS styling

Users want to write HTML to "force" styling. Two levels:

**Level A (required for HTML export):** pass HtmlBlock/HtmlSpan through to the HTML verbatim. The browser understands the CSS itself - the CommonMark/GitHub standard, "the common and most compatible approach".

```html
<div style="page-break-after: always;"></div>
```
-> kept as-is (verbatim block div).

**Level B (preview + PDF - Pango/Cairo):** Pango does not parse CSS, so take a pragmatic approach:
- **Page break markers**: detect `HtmlBlock`/`HtmlSpan` containing CSS `page-break-after: always` (or `break-after: page`, `break-before: page`, `page-break-before: always`) -> create a special `Block::Kind::PageBreak` -> PDF export breaks the page there.
- **Other HTML**: two choices:
  - (i) Drop (as today) - safe, but "forced CSS" does not work in preview/PDF.
  - (ii) Shallow-parse a basic CSS set (`color`, `font-size`, `font-weight`, `text-align`) applied to runs. **Not recommended for V1** - fragile, diverges between preview/PDF/HTML.

> Level B V1 conclusion: **only the page-break marker is supported**. Other HTML stays dropped in preview/PDF (current behavior), pass-through in HTML export - the GitHub standard (raw HTML is only useful on export).

### 2.2 PDF TOC numbering (dotted leader + page number)

Requirement:
```
1. Summary........................................12
2. Details.......................................16
```

Needs a **two-pass** in export_pdf:
1. Layout the whole doc (TOC without page numbers yet).
2. **Paginate** -> know which block lands on which page (`PageSlice` in `markdown_pdf_export.cpp:98-145`).
3. Map anchor -> page number (1-based) via heading blocks (`link_href="#anchor"`).
4. **Re-layout** with `TocPageResolver` -> each TocRow receives `toc_page`.
5. Draw: title left, dotted leader, page number right-aligned.

Structure to reuse:
- `Block.toc_anchor` (existing), `Block.link_href="#anchor"` (existing).
- New fields: `Block.toc_has_page bool`, `Block.toc_page int` (already proposed in layout.hpp - not committed).
- `PageSlice` has `first/last` index - map heading block -> slice index => page = slice_index + 1.
- Keep-with-next heading (`block_keeps_with_next`) - a heading at page end moves to the next page; the page number must match its final post-pagination position.

---

## 3. Detailed design

### 3.1 Page-break detection (layout)

Add `Block::Kind::PageBreak` (or a `force_break_before` flag):

```cpp
// new Block kind
PageBreak,   // height = 0, marker only
```

In `markdown_layout.cpp`, `case NodeType::HtmlBlock:`:

```cpp
if (is_page_break_marker(n.text)) {
    Block b;
    b.kind = Block::Kind::PageBreak;
    b.height = 0; b.margin_before = b.margin_after = 0;
    b.y = y;
    push_block(std::move(b));
}
return;
```

`is_page_break_marker(const std::string& html)`:
- lowercase
- look for `page-break-after` or `break-after` or `page-break-before` or `break-before`
- value contains `always` or `page`
- or the tag is `<div>`/`<section>`/`<p>`/`<span>` with a style attribute

```cpp
bool is_page_break_marker(const std::string& html) {
    const std::string s = to_lower(trim(html));
    if (starts_with(s, "<div") || starts_with(s, "<section") ||
        starts_with(s, "<p") || starts_with(s, "<span")) {
        const bool aft = (s.find("page-break-after:") != npos ||
                          s.find("break-after:") != npos);
        const bool bfr = (s.find("page-break-before:") != npos ||
                          s.find("break-before:") != npos);
        const bool always = (s.find("always") != npos ||
                             s.find(": page") != npos);
        return (aft || bfr) && always;
    }
    return false;
}
```

### 3.2 Paginator recognizes PageBreak

Current paginate (`markdown_pdf_export.cpp:104-145`):

```cpp
struct PageSlice { std::size_t first; std::size_t last; double start_y; };
while (cursor < n) {
    last = cursor;
    candidate = cursor;
    while (candidate < n) {
        const Block& b = blocks[candidate];
        rel_bottom = (b.y - page_top) + b.height;
        if (rel_bottom > content_bottom - content_top) { ... break; }
        last = candidate; ++candidate;
        if (candidate < n && block_keeps_with_next(b)) { ... }
    }
    pages.push_back({cursor, last, page_top});
    next = last + 1;
    if (next < n) page_top = blocks[next].y;
    cursor = next;
}
```

The loop advances `last` through each matching block. With PageBreak (height = 0, y = current position) the marker matches and is swallowed into the current page - **no natural break**. Insert:

```cpp
while (candidate < n) {
    const Block& b = blocks[candidate];
    if (b.kind == Block::Kind::PageBreak) {
        if (candidate > cursor) {
            // break the page right before the marker
            last = candidate - 1;
            break;
        }
        // marker is the first block of a page: let it "flow" past
        // (page_top = b.y, rel==0, takes no space, harmless)
        last = candidate; ++candidate; continue;
    }
    ...
}
```

Marker semantics: a current page that has content closes right before the marker (the marker occupies no slice; `draw` skips the empty run). A marker at the page top is `blocks[next]`, `page_top = marker.y`, `rel = 0`, height 0 -> the new page continues immediately. No blank page.

**Note**: PageBreak.y is set to the current y at layout time; height/margin = 0 so it does not push following content; content AFTER the marker has y > marker.y -> next page. Correct semantics.

**Preview** (no pagination): PageBreak is a zero-height block - invisible, no effect. Draw: `case PageBreak: continue;`.

**HTML export**: the marker div passes through verbatim -> the browser breaks the page on print itself.

### 3.3 PDF TOC numbering (two-pass)

```cpp
// export_pdf (markdown_pdf_export.cpp)
LayoutResult layout = layout_document(ast, style, content_w, resolve_image);
if (layout.blocks.empty()) return false;

// ... (paginate extracted into its own function) ...
auto pages = paginate(layout, content_top, content_bottom);

// Step 2: is there a TOC?
bool has_toc = any block.kind == TocRow;
if (has_toc) {
    // map anchor -> page
    std::map<std::string,int> anchor_page;
    for (page_idx, PageSlice& slice : pages)
        for (block : layout.blocks[slice.first..slice.last])
            if (block.kind == Heading)
                anchor_page[block.link_href.substr(1)] = page_idx + 1;
    TocPageResolver resolver = [&](const std::string& a) -> std::optional<int> {
        auto it = anchor_page.find(a);
        return it == end ? nullopt : optional(it->second);
    };
    // FULL RE-LAYOUT
    layout = layout_document(ast, style, content_w, resolve_image, resolver);
    if (layout.blocks.empty()) return false;
    pages = paginate(layout, ...);
}
```

> **Important note**: the added page number changes the TocRow height (extra runs), but content_width is already wide enough; a number like "12" does not wrap -> height stays -> pagination of later blocks does NOT change. Re-layout + re-paginate a second time is safe. If a future shift appears, iterate to a fixed point (small loop).

### 3.4 Layout TocRow with page number

In `markdown_layout.cpp` `case NodeType::Toc`:

```cpp
// after creating num_run + txt_run, if toc_pages has a page:
if (toc_pages) {
    if (auto pg = toc_pages(h.anchor)) {
        b.toc_has_page = true;
        b.toc_page = *pg;
        StyledText pg_run;
        pg_run.text = std::to_string(*pg);
        pg_run.font_family = style.base_font;
        pg_run.size_pt = style.base_font_pt;
        pg_run.color = style.text_color;
        runs.push_back(pg_run); // append at the end
    }
}
```

Note: `measure_runs(b.run, avail, ...)` measures the page number too - no wrap since avail is large. Height = max line.

### 3.5 Draw TocRow dotted leader + page number

**Draw path** (`markdown_draw.cpp`, text blocks section):

Separate the title text and the page number:

```cpp
if (b.kind == Block::Kind::TocRow) {
    text_x = b.toc_indent;
    text_w  = b.content_width - b.toc_indent;
    wrap_w  = text_w;

    if (b.toc_has_page) {
        // draw title (all runs except the last = page number)
        // measure title width = measure_runs(title_runs, -1)
        double title_w = ...;
        // dotted leader: from title_end + 6 to page_x
        double page_x = text_w + b.content_width...; // right-aligned
        draw_dotted_leader(cr, x0=text_x+title_w+6, x1=page_x - page_number_w - 6,
                           y=baseline);
        // page number drawn right-aligned
        cr->move_to(page_x - page_number_w, text_y);
        // layout the page number
    } else {
        // normal (preview)
    }
}
```

**Leader**: drawn with repeated "." text or a Cairo dash:

```cpp
void draw_dotted_leader(cr, x0, x1, y) {
    if (x1 - x0 < 6) return;
    cr->save();
    cr->set_source_rgba(0.4, 0.4, 0.4, alpha);
    const double dot = 1.4, gap = 2.6;
    for (double x = x0; x < x1; x += dot + gap)
        cr->rectangle(x, y, dot, 0.5);  // or arc
    cr->fill();
    cr->restore();
}
```

Dot bottom sits just below baseline.

### 3.6 HTML TOC

`render_toc_html` already emits `<a href="#anchor">`. Book-style page numbers would need an `anchor->page` custom function - but HTML is not paginated, so **do not do it**. Keep as-is (already good).

---

## 4. Edge cases & risks

1. **PageBreak in preview**: height 0, invisible - OK. But `draw_blocks_range` needs `case PageBreak: continue;`.
2. **PageBreak at end of doc**: handled by paginate - no blank page.
3. **Multiple TOCs per doc**: each `[[TOC]]` repeats the full heading list - already the case, keep.
4. **Duplicate heading anchors**: `headings()` already dedups (`-2`, `-3`); page map uses `link_href.substr(1)` matching the deduped anchor. OK.
5. **TOC page number vs keep-with-next**: a heading pushed to the next page maps correctly because mapping happens after the first paginate. OK.
6. **Re-layout changes pagination**: (see 3.3) stable if no wrap. Verify with a 2-layout unit test: page numbers unchanged between pass 1 and pass 2.
7. **Dangerous raw HTML (script)**: HTML export passes through verbatim -> **the browser will run it**. Same as GitHub. Document clearly. (No sanitize in V1.)
8. **`<div>` mid-inline-text**: `<span>` mid-paragraph -> HtmlSpan inside Paragraph -> layout drops it (as before). Preview not misaligned. OK.

---

## 5. Implementation scope (APPROVED 2026-09-11)

User decisions:
- **Q1**: Verbatim pass-through (no sanitize) - same as GitHub.
- **Q2**: Preview shows a **faint strikethrough line** for the page-break marker (not invisible).
- **Q3**: Dotted leader for **BOTH PDF and HTML export** (HTML has no page numbers, only the leader).

When implementing:

1. `markdown_layout.hpp`: `Block::Kind::PageBreak`; fields `toc_has_page`, `toc_page` (already added); `TocPageResolver` + parameter for `layout_document` (decl added, **not yet in the impl**).
2. `markdown_layout.cpp`:
   - `is_page_break_marker(const std::string&)`: lowercase, recognizes `page-break-after: always` / `break-after: page` / `page-break-before` / `break-before`, tags `<div>`/`<section>`/`<p>`/`<span>`.
   - `case NodeType::HtmlBlock:` -> if marker: `Block b; b.kind = PageBreak; b.height = 18.0; margin ~6`. **Do not push a run** (so draw paints the line).
   - `case NodeType::Toc:` -> if `toc_pages` returns a page: append the page number run, set `toc_has_page/toc_page`, re-measure height/content_width.
   - signature `layout_document(..., const TocPageResolver& toc_pages = {})` kept in sync between decl and impl.
3. `markdown_draw.cpp`:
   - `case Block::Kind::PageBreak:` draw a faint dashed line (`@text-muted` alpha ~0.35, mid-block, x from `b.padding` to `b.content_width`). **Preview + PDF**.
   - TocRow: split the page-number run off the title -> draw title, dotted leader from title end to near the page number, page number right-aligned.
   - `draw_dotted_leader(cr, x0, x1, baseline)` - repeating "." text or cairo dash.
   - `draw_flow` + `draw_blocks_range` handle PageBreak without crash (explicit case).
4. `markdown_pdf_export.cpp`:
   - Extract a `paginate()` helper (reads blocks + breaks) - unit-testable.
   - Paginate: PageBreak block -> break the page (height 18 but pushed to the new page; at page top it flows past). No blank page.
   - Two-pass TOC: layout pass 1 -> paginate -> map anchor->page -> RE-LAYOUT with resolver -> re-paginate. Heading blocks already have `link_href="#anchor"` (`markdown_layout.cpp:287`).
5. `markdown_html.cpp`:
   - `render_block`: `HtmlBlock` -> emit `n.text` verbatim (for `<div style="page-break-after: always"></div>` this yields the CSS break) - `out += n.text; return;`. The file will include a trailing newline (handled deliberately) - accepted.
   - `render_inline`: `HtmlSpan` -> emit verbatim.
   - `render_toc_html`: add dotted leader (CSS `text-align: right` + `::after` dots or `<span class="toc-leader">...</span>` + page number if present).
6. New unit tests: `is_page_break_marker` (4 valid forms + negatives), PageBreak in paginate (no blank page, correct break), TOC page number correctness (heading->page map), HtmlBlock pass-through losing no content. Run `ctest`.
7. Update the `markdown_ast.hpp` comment: HtmlBlock "not rendered" -> "emitted raw in HTML; page-break markers recognized in PDF/preview".

## 6. Edge cases & risks (reviewed)

1. PageBreak in preview: faint line drawn - user approved. No pagination impact.
2. PageBreak at end of doc: no blank page (paginate closes the previous page).
3. Multiple TOCs: each `[[TOC]]` pulls the full heading list - keep as-is.
4. Duplicate heading anchors: `-2`/`-3` dedup exists; resolver maps by the deduped anchor.
5. keep-with-next + page number: mapped after the first paginate - correct final position.
6. **Re-layout changes pagination**: page number adds a short run, content_width suffices -> no wrap -> height stable -> pagination stable. Verified by test: page-number pass 1 == pass 2.
7. Raw HTML script: pass-through (GitHub-style, user approved). No sanitize.
8. `<span>` inline mid-paragraph: HtmlSpan inside Paragraph -> pass-through in HTML; preview/PDF drop inline.

## 7. Open questions - RESOLVED (2026-09-11)

- **Q1**: Verbatim pass-through - approved.
- **Q2**: Preview shows a faint strikethrough line - approved.
- **Q3**: Both PDF and HTML export - approved.