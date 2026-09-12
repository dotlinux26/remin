# Inline HTML Polish + Editor Auto-Close - Implementation Plan

> Sprint plan (2026-09-12). User feedback after reviewing the report test `~/Pictures/aster-pentest-report.md`.
> Implement in order; each item lists touched files + how to verify.
> Principle: do not rewrite files unnecessarily, minimal edits, keep pass-through HTML export.

---

## A. Code fence language - DONE (fixed in this session)

**Bug:** `markdown_ast.cpp` (`MD_BLOCK_CODE`) copies `code->info` (language string `cpp`) into
`n->data.text`, so `cpp` ends up in the code body (`text='cpp\nhey\n\n'`) instead of only `info`.

**Fix (1 edit):** remove `n->data.text.assign(code->info.text, ...)`; body accumulates from
`MD_TEXT_*` events via `verbatim()`. Keep `n->data.info.assign(code->lang...)`.

**Verify:** probe `/tmp/opencode/fence_probe` - ` ```cpp\ney\n``` ` -> `info='cpp'`, `text='hey\n'`. OK.

Note: ` ```cpp` + blank first line -> CommonMark keeps the leading blank line (spec-correct, unchanged).

---

## B. Table render polish - PENDING

**Current state:** `markdown_draw.cpp` Table draws: header fill `b.bg` (surface), grid 1px `p.border`.
Feels pale, low contrast (especially in dark theme).

**Goal:** compact, crisp table with hierarchy; still use colors from `StyleSheet` (no hardcode).

**Approach** (touch only `markdown_draw.cpp` case `Block::Kind::Table`):
1. Header band: fill `b.bg`; if `b.bg` invalid, fallback = `style.bg_color` blended with alpha ~0.5.
2. Header text: `weight 700` (already from layout), nothing more needed.
3. **Header separator** line darker (1.5pt, `outer_border` or `hr_color`).
4. Grid: horizontal lines between rows + vertical lines between columns stay 1pt; draw an **outer outline** 1.2pt
   around the whole table (clearer inner/outer borders).
5. No zebra / no extra fill (keep the "calm, no clutter" rule).

**Verify:** open report preview in light + dark; export PDF and check both.

---

## C. Preview margin - PENDING (just a bit)

**Current state:** preview draws from x=0 -> sticks to left edge/split line; click hit-region subtracts `page_margin`
(coordinates already offset).

**Approach** (touch `src/gui/note/markdown_preview.cpp`):
1. Constant `kPreviewMargin = 20.0` (pt).
2. `relayout_if_needed()`: `content_w = w - 2*kPreviewMargin` passed to `layout_document`;
   canvas height request `+ kPreviewMargin` (breathing room at bottom).
3. `on_canvas_draw()`: `cr->translate(kPreviewMargin, kPreviewMargin)` before `draw_blocks_range`.
4. Replace the 3 uses of `style_.page_margin_pt` in click/motion handlers with `kPreviewMargin` so coordinates match.

**Verify:** preview no longer touches the edge; clicking links/anchors still correct.

---

## D. Preview: show `[[TOC]]` as literal text - PENDING

**Current state:** preview renders full TOC like PDF.

**Requirement:** preview shows only the literal `[[TOC]]`; only PDF renders the real TOC.

**Approach**:
1. `markdown_layout.hpp/.cpp`: add param `bool render_toc = true` to `layout_document`.
2. `case NodeType::Toc` in layout: if `render_toc == false`, emit 1 P block with text `[[TOC]]`
   (plain run), return.
3. `markdown_preview.cpp` calls `layout_document(..., render_toc=false)`.
4. PDF (`markdown_pdf_export.cpp`) unchanged (default true), HTML export unchanged.

**Verify:** preview shows literal `[[TOC]]`; PDF still has TOC + page number + link.

---

## E. HTML `<ul>/<ol>` inside raw block - PENDING

**Current state:** a chunk containing `<ul>/<ol>` + `<li>` -> `render_html_block_runs` sees
`li` as nested block-level -> drops the whole chunk (`return false`).

**Goal:** render a single-level list from a raw HTML chunk (case B).

**Approach** (touch only `markdown_inline_html.cpp` / `render_html_block_runs`):
1. Detect first wrapper block-level tag as `ul` or `ol` -> enter "list mode":
   - `ul`: marker `"• "` at the start of each `<li>`.
   - `ol`: marker `"<N>. "`, N increments (starts at 1, or `start` attr if present).
2. `<li>` open: if not the first item, push run `"\n"`; push marker run; set `in_li`.
3. `</li>`: reset `in_li`. `</ul>/</ol>`: pop wrapper.
4. Inside item: text collapse + style inherit as usual (use existing `emit_text`).
5. Still fail-safe: nested ul/ol (2 levels) or other block-level tags -> `return false` (keep old behavior, no crash).
6. Small bonus: `<br>` / `<br/>` inside the chunk -> push run `"\n"` (line break) instead of dropping it.

Result is **1 P block** with multiple lines and markers (good enough for v1; multi-level hanging indent is V2).

**Verify:** new unit tests in `markdown_inline_html_test.cpp` (ul 2 items, ol counter,
nested -> false, `<br/>` present).

---

## F. Editor auto-close HTML tag (VS Code style) - PENDING

**Requirement (corrected by user):** not `self-closing` `<br/>`; rather, **in the editor**,
when typing `<h1>` and typing `>` immediately insert `</h1>` right after the cursor (cursor stays in the middle).
Like Emmet/VS Code in markup files.

**Approach** (touch `src/gui/note/note_editor.{hpp,cpp}` + test):
1. **Pure** helper (testable):
   ```cpp
   // note_editor.hpp (free function, namespace remin::gui)
   std::string suggest_closing_html_tag(const std::string& line, std::size_t cursor);
   ```
   Logic: scan backward from `cursor` to the nearest `<` on the same line:
   - substring from `<`+1 to `cursor` -> take the **tag name** (first token, skip whitespace/attrs).
   - skip if: starts with `/` (closing), is a comment `<!--`, name empty/invalid,
     is a `<` of `a > b` comparison style (no `<name` immediately before), name is a **void element**
     (`br, img, hr, input, meta, link, area, base, col, embed, source, track, wbr`).
   - attrs are fine: `<h1 style="x"` -> `</h1>`.
   - return `"</name>"` or `""`.
2. NoteEditor ctor: connect `text_buffer_->signal_insert_text(...)`:
   - if text_inserted == `">"`:
     - `line` = entire line before the insert position; `cursor` = that position.
     - `suggest = suggest_closing_html_tag(...)`; if empty, stop.
     - position `iter` after `>` -> `buffer->insert(iter, suggest)` then `place_cursor(iter_before_suggest)`.
     - reentrancy: inserting `"</name>"` re-runs the signal but text `!= ">"` -> ignored. No recursion.
3. New test `note_editor_autoclose_test`: call the pure helper (no GTK needed): cases
   `<h1` / `<h1 ` / `<h1 a="b"` -> `</h1>`; `</h1` / `<!--` / `a > b` / `a<b` / `<br/` -> `""`.
   Register in `tests/CMakeLists.txt` (link `remin_gui`).

**Verify:** open a note, type `<h1>` -> `</h1>` auto-generated, cursor in the middle; undo 2 steps; no crash.

---

## Overall order & verify

1. (done) A
2. B -> C -> D -> E (all in the render pipeline, verify together via preview + PDF + ctest)
3. F (editor-only, verify manually in GUI + pure test)
4. `cmake --build build -j4` + `ctest --test-dir build --output-on-failure`
   (baseline 19/21; 2 pre-existing failures: `markdown_document_test`, `vte_critical_validation_test`)
5. Report to user: check report with preview (margin, table, literal TOC, ul/ol list) + try typing autoclose.