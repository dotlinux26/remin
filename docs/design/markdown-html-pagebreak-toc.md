# Remin — Markdown Raw HTML + Page Breaks + TỌC Numbering Research

> Research note (2026-09-11). Chưa implement — design-first, gate với user trước khi code.
> Mục tiêu: (1) cho phép user viết raw HTML trong Markdown để ép CSS + phân trang,
> (2) TOC có dotted leader + page number kiểu sách.

---

## 1. Hiện trạng (đã verify bằng code + probe)

### 1.1 Parser md4c đã theo dõi raw HTML

Probe `ast_probe.cpp` trên `MD_DIALECT_GITHUB` (không có `MD_FLAG_NOHTMLBLOCKS`/`NOHTMLSPANS`):

```text
# T
Hello <span style="color:red">world</span>!
<div style="page-break-after: always;"></div>
```

→ AST:
```
Paragraph children:
  Text "Hello "
  HtmlSpan "<span style="color:red">"     (type 21)
  Text "world"
  HtmlSpan "</span>"                        (type 21)
  Text "!"
HtmlBlock "<div style="page-break-after: always;"></div>\n"   (type 5)
```

- `MD_BLOCK_HTML` → `NodeType::HtmlBlock` (`markdown_ast.cpp:193`)
- `MD_TEXT_HTML` → `NodeType::HtmlSpan` (`markdown_ast.cpp:324`)

⇒ **Parser giữ đủ thông tin.** Vấn đề nằm ở renderers đang drop:

| Stage | Hành vi hiện tại |
|-------|------------------|
| `markdown_html.cpp:179` | `HtmlBlock` → `return;` (drop) |
| `markdown_html.cpp:109-113` | `HtmlSpan` → drop |
| `markdown_layout.cpp:620` | `HtmlBlock` → `return;` (drop) |
| `markdown_layout.cpp:134-136` | `HtmlSpan`/`HtmlBlock` trong RunBuilder → `return;` (drop) |
| PDF draw | dựa layout blocks → HTML không có block |

### 1.2 TOC hiện tại

- `[[TOC]]` → `NodeType::Toc` (`markdown_ast.cpp:345 mark_toc_nodes`).
- Layout `case NodeType::Toc` (`markdown_layout.cpp:570`) tạo 1 `Block::TocRow` cho mỗi heading.
- TocRow có: `toc_indent` (14pt/level), `toc_number` ("1.", "1.1."), `toc_text`, `toc_anchor`, `has_link_hit = true`, `link_href = "#anchor"`.
- Preview click → `markdown_preview.cpp:75` hit-test → scroll tới run có `anchor` match (`markdown_preview.cpp:80-90`) ⇒ **click-to-navigate hoạt động rồi**.
- PDF export: TocRow được vẽ như text trơn, **không có dotted leader, không có page number**.

---

## 2. Mục tiêu feature

### 2.1 Raw HTML → CSS styling

Người dùng muốn viết HTML để "ép" style. Hai mức:

**Mức A (bắt buộc cho export HTML):** pass-through các HtmlBlock/HtmlSpan ra HTML nguyên văn. Browser tự hiểu CSS. Đây là chuẩn CommonMark/GitHub — "cách phổ biến và tương thích nhất".

```html
<div style="page-break-after: always;"></div>
```
→ giữ nguyên trong `<div>`... (hoặc block div nguyên văn).

**Mức B (preview + PDF — Pango/Cairo):** Pango không parse CSS. Nên tiếp cận thực dụng:
- **Page break markers**: nhận diện `HtmlBlock`/`HtmlSpan` chứa CSS `page-break-after: always` (hoặc `break-after: page`, `break-before: page`, `page-break-before: always`) → tạo `Block::Kind::PageBreak` đặc biệt → PDF export tách trang ngay tại đây.
- **Các HTML khác**: có 2 lựa chọn:
  - (i) Drop (như hiện tại) — an toàn, nhưng "ép CSS" không chạy được trong preview/PDF.
  - (ii) Shallow-parse một tập CSS cơ bản (`color`, `font-size`, `font-weight`, `text-align`) áp lên runs. **Không khuyến nghị V1** — dễ vỡ, sai lệch giữa preview/PDF/HTML.

> Kết luận mức B V1: **chỉ hỗ trợ page-break marker**. Các HTML khác vẫn drop trong preview/PDF (giữ hành vi hiện tại), pass-through trong HTML export. Đây là chuẩn GitHub (raw HTML chỉ hữu dụng khi export).

### 2.2 PDF TOC numbering (dotted leader + page number)

Yêu cầu:
```
1. Tóm tắt........................................12
2. Chi tiết......................................16
```

Cần **two-pass** trong export_pdf:
1. Layout cả doc (TOC chưa có page number).
2. **Paginate** → biết block nào rơi vào page nào (`PageSlice` trong `markdown_pdf_export.cpp:98-145`).
3. Map anchor → page number (1-based) qua heading blocks (`link_href="#anchor"` ⇒ anchor → page).
4. **Re-layout** với `TocPageResolver` → mỗi TocRow nhận `toc_page`.
5. Draw: title bên trái, dotted leader, page number canh phải.

Ký hiệu cấu trúc hiện có để tái dùng:
- `Block.toc_anchor` (đã có), `Block.link_href="#anchor"` (đã có).
- Cần thêm field: `Block.toc_has_page bool`, `Block.toc_page int` (đã đề xuất trong layout.hpp — chưa commit).
- Paginator `PageSlice` có `first/last` index — map heading block → slice index ⇒ page = slice_index + 1.
- Keep-with-next heading (`block_keeps_with_next`) — heading rơi cuối trang sẽ đẩy xuống page sau; page number phải khớp vị trí cuối cùng sau paginate.

---

## 3. Thiết kế chi tiết

### 3.1 Page-break detection (layout)

Thêm `Block::Kind::PageBreak` (hoặc dùng flag `force_break_before`):

```cpp
// Block kind mới
PageBreak,   // height = 0, chỉ là marker
```

Trong `markdown_layout.cpp`, `case NodeType::HtmlBlock:`:

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
- tìm `page-break-after` hoặc `break-after` hoặc `page-break-before` hoặc `break-before`
- value chứa `always` hoặc `page`
- hoặc tag chính là `<div>`/`<section>`/`<p>`/`<span>` với style attribute

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

### 3.2 Paginator nhận biết PageBreak

Paginate hiện tại (`markdown_pdf_export.cpp:104-145`):

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

Vòng lặp đang đẩy `last` qua từng block khớp. Với PageBreak (height = 0, y = vị trí hiện tại), marker tự khớp và bị nuốt vào page hiện tại — **không tự ngắt**. Cần chèn:

```cpp
while (candidate < n) {
    const Block& b = blocks[candidate];
    if (b.kind == Block::Kind::PageBreak) {
        if (candidate > cursor) {
            // ngắt trang ngay trước marker
            last = candidate - 1;
            break;
        }
        // marker là block đầu tiên của page: cho "trôi" qua
        // (page_top = b.y, rel==0, không chiếm chỗ, vô hại)
        last = candidate; ++candidate; continue;
    }
    ...
}
```

Marker image: với page hiện tại có nội dung → đóng page ngay trước marker (marker không chiếm slice nào cả, `draw` bỏ qua run rỗng). Marker nằm đầu trang → nó là `blocks[next]`, `page_top = marker.y`, `rel = 0`, chiều cao 0 → page mới tự nhiên tiếp tục ngay. Không sinh page trắng trống.

**Note**: PageBreak y được set = y hiện tại tại thời điểm layout; height/margin = 0 nên không đẩy content tiếp theo; content SAU marker vẫn có y > marker.y → thuộc page sau. Đúng semantics.

**Preview** (không phân trang): PageBreak là block height 0 — vô hình, không ảnh hưởng gì. Trong draw: `case PageBreak: continue;`.

**HTML export**: marker div được pass-through y nguyên → browser tự ngắt trang in.

### 3.3 PDF TOC numbering (two-pass)

```cpp
// export_pdf (markdown_pdf_export.cpp)
LayoutResult layout = layout_document(ast, style, content_w, resolve_image);
if (layout.blocks.empty()) return false;

// ... (hàm paginate được tách riêng ra) ...
auto pages = paginate(layout, content_top, content_bottom);

// Bước 2: có TOC không?
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
    // RE-LAYOUT TOÀN BỘ
    layout = layout_document(ast, style, content_w, resolve_image, resolver);
    if (layout.blocks.empty()) return false;
    pages = paginate(layout, ...);
}
```

> **Lưu ý quan trọng**: thêm page number làm thay đổi height của TocRow (thêm runs) nhưng content_width vốn đã đủ rộng; số "12" nhỏ nên không gây wrap → height giữ nguyên ⇒ pagination của các block sau KHÔNG đổi. Vẫn an toàn RE-LAYOUT và RE-PAGINATE lại lần 2. Nếu future phát hiện shift, cần lặp đến fixed-point (small loop).

### 3.4 Layout TocRow với page number

Trong `markdown_layout.cpp` `case NodeType::Toc`:

```cpp
// sau khi tạo num_run + txt_run, nếu toc_pages có page:
if (toc_pages) {
    if (auto pg = toc_pages(h.anchor)) {
        b.toc_has_page = true;
        b.toc_page = *pg;
        StyledText pg_run;
        pg_run.text = std::to_string(*pg);
        pg_run.font_family = style.base_font;
        pg_run.size_pt = style.base_font_pt;
        pg_run.color = style.text_color;
        runs.push_back(pg_run); // thêm vào cuối
    }
}
```

Lưu ý: `measure_runs(b.run, avail, ...)` sẽ đo cả page number — không gây wrap vì avail lớn. Height = max line.

### 3.5 Draw TocRow dotted leader + page number

**Draw path** (`markdown_draw.cpp`, phần text blocks):

Tách riêng main-title text và page number:

```cpp
if (b.kind == Block::Kind::TocRow) {
    text_x = b.toc_indent;
    text_w  = b.content_width - b.toc_indent;
    wrap_w  = text_w;

    if (b.toc_has_page) {
        // vẽ title (tất cả run trừ run cuối = page number)
        // đo title width = measure_runs(title_runs, -1)
        double title_w = ...;
        // dotted leader: từ title_end + 6 tới page_x
        double page_x = text_w + b.content_width...; // canh phải
        draw_dotted_leader(cr, x0=text_x+title_w+6, x1=page_x - page_number_w - 6,
                           y=baseline);
        // page number vẽ canh phải
        cr->move_to(page_x - page_number_w, text_y);
        // layout page number show
    } else {
        // bình thường (preview)
    }
}
```

**Leader**: vẽ bằng text "." lặp lại hoặc Cairo dash:

```cpp
void draw_dotted_leader(cr, x0, x1, y) {
    if (x1 - x0 < 6) return;
    cr->save();
    cr->set_source_rgba(0.4, 0.4, 0.4, alpha);
    const double dot = 1.4, gap = 2.6;
    for (double x = x0; x < x1; x += dot + gap)
        cr->rectangle(x, y, dot, 0.5);  // hoặc arc
    cr->fill();
    cr->restore();
}
```

Nếu free dot y = baseline - nhỏ.

### 3.6 HTML TOC 

`render_toc_html` đã có `<a href="#anchor">`. Muốn thêm số trang kiểu sách cần pass `anchor→page` custom function — nhưng HTML không phân trang nên **không làm**. Giữ nguyên (đã đẹp).

---

## 4. Edge cases & rủi ro

1. **PageBreak trong preview**: height 0, vô hình — OK. Nhưng `draw_blocks_range` cần `case PageBreak: continue;` để không draw gì.
2. **PageBreak cuối doc**: paginate xử lý — không để phát sinh page trắng.
3. **Nhiều TOC trong 1 doc**: mỗi [[TOC]] lặp đầy đủ headings — hiện tại đã thế, giữ.
4. **Heading trùng anchor**: `headings()` đã dedup (`-2`, `-3`) — page map dùng `link_href.substr(1)` khớp anchor dedup. OK.
5. **TOC page number và keep-with-next**: heading đẩy xuống page sau → map đúng vì ta map sau khi paginate lần 1. OK.
6. **Re-layout thay đổi pagination**: (xem 3.3) nếu wrap không xảy ra thì ổn định. Kiểm tra bằng unit test 2-layout: page numbers không đổi giữa pass 1 và pass 2.
7. **Raw HTML nguy hiểm (script)**: HTML export pass-through nguyên văn → **browser sẽ chạy**. Giống GitHub behavior. Document rõ. (Không làm sanitize V1.)
8. **`<div>` giữa văn bản inline**: `<span>` mid-paragraph → HtmlSpan nằm trong Paragraph → layout drop (như cũ). Preview không bị lệch. OK.

---

## 5. Phạm vi implement (ĐÃ DUYỆT 2026-09-11)

Decisions của user:
- **Q1**: Pass-through nguyên văn (không sanitize) — giống GitHub.
- **Q2**: Preview hiện **gạch ngang mờ** cho page-break marker (không phải vô hình).
- **Q3**: Dotted-leader **cho CẢ PDF lẫn HTML export** (HTML không có số trang, chỉ dotted leader).

Khi implement:

1. `markdown_layout.hpp`: `Block::Kind::PageBreak`; field `toc_has_page`, `toc_page` (đã thêm); `TocPageResolver` + tham số cho `layout_document` (decl đã thêm, **chưa thêm vào impl**).
2. `markdown_layout.cpp`:
   - `is_page_break_marker(const std::string&)`: lowercase, nhận diện `page-break-after: always` / `break-after: page` / `page-break-before` / `break-before`, tag `<div>`/`<section>`/`<p>`/`<span>`.
   - `case NodeType::HtmlBlock:` → nếu marker: `Block b; b.kind = PageBreak; b.height = 18.0; margin ~6`. **Không push run** (để draw vẽ gạch).
   - `case NodeType::Toc:` → nếu `toc_pages` trả page: thêm run page number cuối, set `toc_has_page/toc_page`, đo lại height/content_width.
   - signature `layout_document(..., const TocPageResolver& toc_pages = {})` đồng bộ decl+impl.
3. `markdown_draw.cpp`:
   - `case Block::Kind::PageBreak:` vẽ gạch ngang mờ (dash, `@text-muted` alpha ~0.35, đường thẳng giữa block, x từ `b.padding` tới `b.content_width`). **Preview + PDF**.
   - TocRow: tách page-number run khỏi title → vẽ title, dotted leader từ cuối title tới gần page number, page number canh phải.
   - `draw_dotted_leader(cr, x0, x1, baseline)` — dùng text "." lặp hoặc cairo dash.
   - `draw_flow` + `draw_blocks_range` có PageBreak → không crash (case rõ ràng).
4. `markdown_pdf_export.cpp`:
   - Tách `paginate()` helper (đọc blocks + breaks) — dễ unit test.
   - Paginate: PageBreak block → ngắt trang (block height 18 nhưng bị đẩy sang page mới; đầu page thì trôi qua). Đảm bảo không sinh page trắng.
   - Two-pass TOC: layout pass 1 → paginate → map anchor→page → RE-LAYOUT với resolver → re-paginate. Heading blocks có `link_href="#anchor"` sẵn (`markdown_layout.cpp:287`).
5. `markdown_html.cpp`:
   - `render_block`: `HtmlBlock` → emit `n.text` nguyên văn (với `<div style="page-break-after: always"></div>` → có CSS break) — `out += n.text; return;`. CHỦĐỘNG file sẽ có cả trailing newline — chấp nhận.
   - `render_inline`: `HtmlSpan` → emit nguyên văn.
   - `render_toc_html`: thêm dotted leader (CSS `text-align: right` + `::after` dots hoặc `<span class="toc-leader">…</span>` + số trang nếu có).
6. Tests mới (unit): `is_page_break_marker` (4 dạng hợp lệ + âm), PageBreak trong paginate (không sinh trang trắng, ngắt đúng), TOC page number đúng (map heading→page), HtmlBlock pass-through không mất nội dung. Chạy `ctest`.
7. Update `markdown_ast.hpp` comment: HtmlBlock "not rendered" → "emitted raw in HTML; page-break markers recognized in PDF/preview".

## 6. Edge cases & rủi ro (đã rà)

1. PageBreak trong preview: vẽ gạch mờ — user chốt OK. Không ảnh hưởng pagination.
2. PageBreak cuối doc: không sinh trang trắng (paginate chốt page trước).
3. Nhiều TOC: mỗi [[TOC]] kéo đủ headings — giữ nguyên.
4. Heading trùng anchor: dedup `-2`/`-3` đã có; resolver map theo anchor dedup.
5. keep-with-next + page number: map sau paginate lần 1 — đúng vị trí cuối.
6. **Re-layout đổi pagination**: page number thêm run ngắn, content_width đủ → không wrap → height giữ → pagination ổn định. Verify ở test: page-number pass 1 == pass 2.
7. Raw HTML script: pass-through (GitHub-style, user chốt). Không sanitize.
8. `<span>` inline giữa paragraph: HtmlSpan trong Paragraph → pass-through HTML; preview/PDF drop inline.

## 7. Open questions — ĐÃ GIẢI QUYẾT (2026-09-11)

- **Q1**: Pass-through nguyên văn ✅
- **Q2**: Preview hiện gạch ngang mờ ✅
- **Q3**: Cả PDF lẫn HTML export ✅