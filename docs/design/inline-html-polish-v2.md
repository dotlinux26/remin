# Remin Inline HTML/CSS Polish + PDF Links + Table Fixes - Implementation Plan (COMPLETED)

> Sprint plan (2026-09-12). All items implemented and tested.

---

## 1. Table render polish - COMPLETED

**Fixed:**
- Table centered in the frame (`cr->translate` with offset)
- Header bottom line (1.6pt) instead of double line with row separator (1.0pt)
- Cell vertical padding: text vertically centered in cell (`ty = row_y + (row.height - m.height)/2`)
- Cell horizontal padding: measure with width = `col_width - 2*pad` (pad=3pt)

**Files:** `markdown_draw.cpp`, `markdown_layout.cpp`

---

## 2. UL/OL + `<br>` in raw HTML block - COMPLETED

**Fixed:**
- `<ul><li>first</li><li>second</li></ul>` renders correctly with bullet `• ` and newline between items
- `<ol><li>one</li><li>two</li></ol>` renders correctly with counters `1. `, `2. `
- Nested list `<ul><li>a<ul><li>b</li></ul></li></ul>` -> return false (unsupported)
- `<br/>` self-closing and `<br>` plain both create newlines in runs

**Files:** `markdown_inline_html.cpp`, `markdown_inline_html_test.cpp`

---

## 3. PDF external links ([Google](https://google.com)) - COMPLETED

**Fixed:** PDF export now creates clickable links for external URLs (http/https) using the Cairo `uri='...'` attribute; internal anchors use `dest='...'`.

**Files:** `markdown_draw.cpp` (`open_link_tags`, `close_link_tags`)

---

## 4. Table cell horizontal padding (| helo | instead of | helo|) - COMPLETED

**Fixed:** Measure text with available width = `col_width - 2*pad`, draw with horizontal padding on both sides.

**Files:** `markdown_layout.cpp`, `markdown_draw.cpp`

---

## 5. Preview margin + TOC literal + Fence language fix - COMPLETED (done earlier)

---

## 6. Editor auto-close tag - COMPLETED

**Fixed:** VS Code-style auto-close HTML tags. Type `<h1>` + `>` -> `</h1>` auto-inserted after cursor. Void elements (br, img, hr...) are not auto-closed. Has unit test.

**Files:** `note_editor.hpp`, `note_editor.cpp`, `note_editor_autoclose_test.cpp`, `tests/CMakeLists.txt`

---

## 7. Fidelity gate test - NOW PASSING (was failing before)

---

## 8. VTE critical validation test - IGNORE (pre-existing permission issue)

---

## 9. Markdown document test - IGNORE (pre-existing 4 asset failures)

---

## 10. Code fence language badge - COMPLETED

**Added:** Small badge in the top-right corner of a code block showing the language (bash, cpp, http...). Background from style Code/Pre, auto light/dark fallback.

**Files:** `markdown_draw.cpp`

---

## 11. Editor scrollbar overlap - COMPLETED

**Fixed:**
- Disable overlay scrolling (`gtk_scrolled_window_set_overlay_scrolling = FALSE`)
- Increase right margin from 8px to 20px
- Scrollbar CSS: `margin-left: 4px` creates a gap between editor and scrollbar

**Files:** `note_editor.cpp`, `resources/styles/dark.css`, `resources/styles/light.css`

---

## Test Results

```
20/22 tests passed (91%)
- 2 pre-existing failures: vte_critical_validation_test (permission denied), markdown_document_test (4 asset failures)
- All NEW tests pass: markdown_inline_html_test, note_editor_autoclose_test, markdown_layout_test, markdown_html_test, markdown_ast_test
- fidelity_gate_test now passes
```

---

## Files Modified

| Item | Files |
|---|---|
| Table polish | `markdown_draw.cpp`, `markdown_layout.cpp` |
| UL/OL + `<br>` | `markdown_inline_html.cpp`, `markdown_inline_html_test.cpp` |
| PDF external links | `markdown_draw.cpp` |
| Editor auto-close | `note_editor.hpp`, `note_editor.cpp`, `tests/unit/note_editor_autoclose_test.cpp`, `tests/CMakeLists.txt` |
| Code fence badge | `markdown_draw.cpp` |
| Editor scrollbar | `note_editor.cpp`, `resources/styles/dark.css`, `resources/styles/light.css` |

---

## Verify Commands

```bash
cmake --build build -j4
ctest --test-dir build --output-on-failure
./build/tests/markdown_inline_html_test
./build/tests/note_editor_autoclose_test
# Manual: ./remin gui -> open report -> check preview + PDF export
```