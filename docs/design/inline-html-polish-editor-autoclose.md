# Inline HTML Polish + Editor Auto-Close — Implementation Plan

> Sprint plan (2026-09-12). User feedback sau khi xem report test `~/Pictures/aster-pentest-report.md`.
> Implement theo đúng thứ tự; từng hạng mục có file chạm + cách verify.
> Nguyên tắc: không viết lại file nếu không cần, edit tối thiểu, giữ pass-through HTML export.

---

## A. Code fence language — DONE ✅ (đã fix trong phiên này)

**Bug:** `markdown_ast.cpp` (`MD_BLOCK_CODE`) copy `code->info` (chuỗi ngôn ngữ `cpp`) vào
`n->data.text`, nên `cpp` lọt vào body code (`text='cpp\nhey\n\n'`) thay vì chỉ `info`.

**Fix (1 edit):** bỏ `n->data.text.assign(code->info.text, ...)`; body tích lũy từ sự kiện
`MD_TEXT_*` qua `verbatim()`. Giữ `n->data.info.assign(code->lang...)`.

**Verify:** probe `/tmp/opencode/fence_probe` — ` ```cpp\ney\n``` ` ⇒ `info='cpp'`, `text='hey\n'`. OK.

Ghi chú: ` ```cpp` + blank line đầu ⇒ CommonMark giữ leading blank line (đúng chuẩn, giữ nguyên).

---

## B. Table render polish — PENDING

**Hiện trạng:** `markdown_draw.cpp` Table vẽ: header fill `b.bg` (surface), grid 1px `p.border`.
Cảm giác nhạt, không tương phản (nhất là dark theme).

**Mục tiêu:** bảng gọn, nét, có phân cấp; vẫn dùng màu từ `StyleSheet` (không hardcode).

**Cách làm** (chạm riêng `markdown_draw.cpp` case `Block::Kind::Table`):
1. Header band: fill `b.bg`; nếu `b.bg` invalid thì fallback = `style.bg_color` pha alpha ~0.5.
2. Header text: `weight 700` (đã có từ layout), thêm đã đủ.
3. Đường **phân tách dưới header** đậm hơn (1.5pt, `outer_border` hoặc `hr_color`).
4. Grid: đường ngang giữa các row + đường dọc giữa các column giữ 1pt; kẻ **outline ngoài** 1.2pt
   cho nguyên bảng (viền trong/ngoài rõ ràng hơn).
5. Không zebra / không tô thêm (giữ rule "calm, no clutter").

**Verify:** mở report preview light + dark; export PDF xem 2 cái.

---

## C. Preview margin — PENDING (chỉ 1 chút)

**Hiện trạng:** preview vẽ từ x=0 → dính mép trái/đường split; click hit-region lại trừ `page_margin`
(tọa độ lệch sẵn).

**Cách làm** (chạm `src/gui/note/markdown_preview.cpp`):
1. Hằng `kPreviewMargin = 20.0` (pt).
2. `relayout_if_needed()`: `content_w = w - 2*kPreviewMargin` truyền vào `layout_document`;
   canvas height request `+ kPreviewMargin` (đáy thoáng).
3. `on_canvas_draw()`: `cr->translate(kPreviewMargin, kPreviewMargin)` trước `draw_blocks_range`.
4. Đổi 3 chỗ `style_.page_margin_pt` trong click/motion handler thành `kPreviewMargin` để tọa độ khớp.

**Verify:** preview không dính mép, click link/anchor vẫn đúng.

---

## D. Preview: `[[TOC]]` hiển thị dạng text — PENDING

**Hiện trạng:** preview render TOC đầy đủ giống PDF.

**Yêu cầu:** preview chỉ hiện literal `[[TOC]]`; chỉ PDF render TOC thật.

**Cách làm**:
1. `markdown_layout.hpp/.cpp`: thêm param `bool render_toc = true` vào `layout_document`.
2. `case NodeType::Toc` trong layout: nếu `render_toc == false` ⇒ emit 1 P block text `[[TOC]]`
   (plain run), return.
3. `markdown_preview.cpp` gọi `layout_document(..., render_toc=false)`.
4. PDF (`markdown_pdf_export.cpp`) không đổi (default true), HTML export không đổi.

**Verify:** preview thấy `[[TOC]]` nguyên văn; PDF vẫn có TOC + page number + link.

---

## E. HTML `<ul>/<ol>` trong raw block — PENDING

**Hiện trạng:** chunk chứa `<ul>/<ol>` + `<li>` ⇒ `render_html_block_runs` thấy
`li` là block-level lồng nhau ⇒ drop cả chunk (`return false`).

**Mục tiêu:** render danh sách đơn cấp từ chunk HTML nguyên cục (case B).

**Cách làm** (chạm riêng `markdown_inline_html.cpp` / `render_html_block_runs`):
1. Nhận biết wrapper block-level đầu là `ul` hoặc `ol` ⇒ vào "list mode":
   - `ul`: marker `"• "` đứng đầu mỗi `<li>`.
   - `ol`: marker `"<N>. "`, N tăng dần (bắt đầu 1, hoặc `start` attr nếu có).
2. `<li>` mở: nếu không phải item đầu ⇒ push run `"\n"`; push marker run; set `in_li`.
3. `</li>`: reset `in_li`. `</ul>/</ol>`: pop wrapper.
4. Trong item: text collapse + style inherit như bình thường (dùng `emit_text` sẵn có).
5. Vẫn fail-safe: lồng ul/ol (2 cấp) hoặc block-level khác ⇒ `return false` (giữ cũ, không crash).
6. Bonus nhỏ: `<br>` / `<br/>` trong chunk ⇒ push run `"\n"` (line break) thay vì bỏ trống.

Kết quả là **1 P block** nhiều dòng với marker (đủ dùng cho v1; hanging indent đa cấp là V2).

**Verify:** unit test mới trong `markdown_inline_html_test.cpp` (ul 2 items, ol counter,
lồng nhau ⇒ false, có `<br/>`).

---

## F. Editor auto-close HTML tag (kiểu VS Code) — PENDING

**Yêu cầu (đính chính từ user):** không phải `self-closing` `<br/>`, mà là trong **editor**,
khi gõ `<h1>` và gõ `>` ⇒ tự chèn `</h1>` ngay sau con trỏ (con trỏ vẫn nằm giữa).
Giống Emmet/VS Code trong file markup.

**Cách làm** (chạm `src/gui/note/note_editor.{hpp,cpp}` + test):
1. Helper **thuần** (test được):
   ```cpp
   // note_editor.hpp (free function, namespace remin::gui)
   std::string suggest_closing_html_tag(const std::string& line, std::size_t cursor);
   ```
   Logic: nhìn ngược từ `cursor` về `<` gần nhất trên cùng dòng:
   - substring từ `<`+1 tới `cursor` ⇒ lấy **tên tag** (token đầu, bỏ whitespace/attr).
   - bỏ qua nếu: bắt đầu bằng `/` (closing), là comment `<!--`, tên rỗng/không hợp lệ,
     đang sau `<` thuộc `a > b` kiểu so sánh (không có `<name` liền trước), tên là **void element**
     (`br, img, hr, input, meta, link, area, base, col, embed, source, track, wbr`).
   - có attr cũng được: `<h1 style="x"` ⇒ `</h1>`.
   - trả `"</name>"` hoặc `""`.
2. NoteEditor ctor: connect `text_buffer_->signal_insert_text(...)`:
   - nếu text_inserted == `">"`:
     - `line` = toàn bộ dòng trước vị trí insert; `cursor` = vị trí đó.
     - `suggest = suggest_closing_html_tag(...)`; nếu rỗng ⇒ thôi.
     - `iter` sau `>` ⇒ `buffer->insert(iter, suggest)` rồi `place_cursor(iter_before_suggest)`.
     - reentrancy: insert `"</name>"` lại chạy signal nhưng text `!= ">"` ⇒ bỏ qua. Không đệ quy.
3. Test mới `note_editor_autoclose_test`: gọi helper thuần (không cần GTK): các case
   `<h1` / `<h1 ` / `<h1 a="b"` ⇒ `</h1>`; `</h1` / `<!--` / `a > b` / `a<b` / `<br/` ⇒ `""`.
   Register trong `tests/CMakeLists.txt` (link `remin_gui`).

**Verify:** mở note, gõ `<h1>` ⇒ thấy `</h1>` tự sinh, con trỏ ở giữa; undo 2 bước; không crash.

---

## Order & verify tổng

1. (xong) A
2. B → C → D → E (đều thuộc pipeline render, verify chung qua preview + PDF + ctest)
3. F (editor riêng, verify GUI thủ công + test thuần)
4. `cmake --build build -j4` + `ctest --test-dir build --output-on-failure`
   (baseline 19/21; 2 failure pre-existing: `markdown_document_test`, `vte_critical_validation_test`)
5. Báo user kiểm tra report với preview (margin, table, TOC dạng text, list ul/ol) + gõ thử autoclose.