# Remin Inline HTML/CSS Polish + PDF Links + Table Fixes — Implementation Plan (COMPLETED)

> Sprint plan (2026-09-12). All items implemented and tested.

---

## 1. Table render polish — ✅ COMPLETED

**Fixed:**
- Table căn giữa khung hình (cr->translate với offset)
- Header bottom line (1.6pt) thay vì double line với row separator (1.0pt)
- Cell vertical padding: text căn giữa dọc trong cell (`ty = row_y + (row.height - m.height)/2`)
- Cell horizontal padding: measure với width = `col_width - 2*pad` (pad=3pt)

**Files:** `markdown_draw.cpp`, `markdown_layout.cpp`

---

## 2. UL/OL + `<br>` trong raw HTML block — ✅ COMPLETED

**Fixed:**
- `<ul><li>first</li><li>second</li></ul>` render đúng với bullet `• ` và newline giữa các item
- `<ol><li>one</li><li>two</li></ol>` render đúng với counter `1. `, `2. `
- Nested list `<ul><li>a<ul><li>b</li></ul></li></ul>` → return false (unsupported)
- `<br/>` self-closing và `<br>` plain đều tạo newline trong runs

**Files:** `markdown_inline_html.cpp`, `markdown_inline_html_test.cpp`

---

## 3. PDF external links ([Google](https://google.com)) — ✅ COMPLETED

**Fixed:** Export PDF giờ tạo link click được cho external URLs (http/https) dùng Cairo `uri='...'` attribute, internal anchors dùng `dest='...'`.

**Files:** `markdown_draw.cpp` (`open_link_tags`, `close_link_tags`)

---

## 4. Table cell horizontal padding (| helo | thay vì | helo|) — ✅ COMPLETED

**Fixed:** Measure text với available width = `col_width - 2*pad`, draw với horizontal padding đều 2 bên.

**Files:** `markdown_layout.cpp`, `markdown_draw.cpp`

---

## 5. Preview margin + TOC literal + Fence language fix — ✅ COMPLETED (đã xong trước)

---

## 6. Editor auto-close tag — ✅ COMPLETED

**Fixed:** VS Code-style auto-close HTML tags. Gõ `<h1>` + `>` → tự chèn `</h1>` sau con trỏ. Void elements (br, img, hr...) không auto-close. Có unit test.

**Files:** `note_editor.hpp`, `note_editor.cpp`, `note_editor_autoclose_test.cpp`, `tests/CMakeLists.txt`

---

## 7. Fidelity gate test — ✅ NOW PASSING (was failing before)

---

## 8. VTE critical validation test — IGNORE (pre-existing permission issue)

---

## 9. Markdown document test — IGNORE (pre-existing 4 asset failures)

---

## 10. Code fence language badge — ✅ COMPLETED

**Added:** Badge nhỏ ở góc trên phải code block show ngôn ngữ (bash, cpp, http...). Background từ style Code/Pre, fallback auto light/dark.

**Files:** `markdown_draw.cpp`

---

## 11. Editor scrollbar overlap — ✅ COMPLETED

**Fixed:**
- Disable overlay scrolling (`gtk_scrolled_window_set_overlay_scrolling = FALSE`)
- Increase right margin từ 8px → 20px
- Scrollbar CSS: `margin-left: 4px` tạo gap giữa editor và scrollbar

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

| Hạng mục | Files |
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