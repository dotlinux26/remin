# Remin — PDF TOC Links + Image Resolution + Inline HTML/CSS Research

> Research note (2026-09-12). Chưa implement — design-first, gate với user trước khi code.
> Mục tiêu: sửa 3 lỗi user báo trên bản v0.0.9rc:
>   (1) PDF không tạo link nội bộ cho các dòng TOC (ấn không nhảy, HTML thì nhảy)
>   (2) ảnh trong PDF bị nát (độ phân giải thấp), HTML nét
>   (3) thuộc tính thẻ HTML/CSS chưa được render qua PDF, chỉ HTML hiển thị tốt

---

## 1. Lỗi 1 — PDF không có link nội bộ cho TOC

### 1.1 Hiện trạng (đã verify bằng code)

- HTML export tạo `<a href="#intro">1. Tóm tắt</a>` bên trong `.toc-row` → browser nhảy tới
  heading khi click. (`markdown_html.cpp:287`)
- PDF export vẽ text thuần bằng `draw_blocks_range()` (`markdown_pdf_export.cpp:199`).
  Không có annotation nào được viết vào PDF → PDF viewer không biết vùng nào là link.
- Layout đã có sẵn mọi dữ liệu cần thiết:
  - TocRow có `link_href = "#" + anchor` (`markdown_layout.cpp:630`)
  - Heading block có `link_href = "#" + anchor` (`markdown_layout.cpp:326`)
  - Two-pass TOC đã tính sẵn `anchor_page` (heading → số trang) (`markdown_pdf_export.cpp:159-167`)

### 1.2 Khả năng của cairo (đã probe verify)

Cairo ≥ 1.16 (đang có 1.18) hỗ trợ đầy đủ cho PDF:

| API | Mục đích |
|-----|----------|
| `cairo_tag_begin(cr, "Link", "dest='anchor'")` / `cairo_tag_end` | tạo vùng click được (internal link) |
| `cairo_tag_begin(cr, "cairo.dest", "name='anchor'")` / `cairo_tag_end` | tạo destination (named dest) |
| `cairo_pdf_surface_add_outline(surface, parent, title, "dest='anchor'", flags)` | PDF bookmarks (sidebar) |

Probe `/tmp/opencode/pdf_tags.cpp` đã chạy thật và xác nhận PDF tạo ra:

```text
/Annots  /Subtype /Link  /Rect [56 784.89 137 797.89]  /Dest [2 0 R /XYZ 57 704.89 0]
/Outlines  /Title (Intro)  /Dest [...]           (bookmark sidebar)
```

→ clickable internal link + bookmarks đều hoạt động ở cairo 1.18.

### 1.3 Vấn đề triển khai

- **cairomm không wrap `cairo_tag_begin` / `cairo_pdf_surface_add_outline`.** Phải gọi
  trực tiếp C API qua `cr->cobj()` (`Context::cobj()` trả `cairo_t*`,
  `surface->cobj()` trả `cairo_surface_t*`) — đã xác nhận tồn tại trong cairomm 1.16.
- **`draw_blocks_range()` dùng chung cho preview + PDF.** Preview điều hướng bằng
  hit-regions (`collect_hit_regions`), không cần tag. Nên thêm cờ `emit_links` (mặc
  định `false`, PDF bật `true`) — khi bật, block có `has_link_hit && link_href[0] == '#'`
  được bọc `Link` tag quanh vẽ text; heading được bọc `Dest` tag `name=<anchor>`.
- **TocRow PDF vẽ title/leader/page-name tách riêng** (`markdown_draw.cpp:252-297`).
  Chỉ phần title layout cần nằm trong `Link` tag (leader dots + số trang không link).
- Video: Cairo dùng extents của drawing ops trong tag để làm clickable area; vì ta vẽ
  title text ngay trong tag nên rect tự rộng đúng — không cần chỉ định `rect=` thủ công.

### 1.4 Đề xuất

- Bật `emit_links` = true trong `export_pdf()`.
- TocRow + mọi block link nội `#anchor` → `Link` tag `dest='<anchor>'` quanh vẽ text.
- Heading → `Dest` tag `name='<anchor>'` quanh vẽ nội dung heading (chung cho mọi
  heading, kể cả không có trong TOC — để link nội khác cũng nhảy được).
- Ngoài ra: `cairo_pdf_surface_add_outline` cho mỗi heading (theo thứ tự) → bookmarks
  sidebar trong PDF viewer. Tạo thứ bậc theo heading level (H1 → root, H2 → child của
  H1 đang mở). **Bonus — quyết định ở Q1.**

---

## 2. Lỗi 2 — Ảnh PDF nát, HTML nét

### 2.1 Hiện trạng (đã verify)

- `draw_pixbuf()` (`markdown_draw.cpp:15-60`):
  - gọi `gdk_pixbuf_scale_simple(source, (int)w, (int)h, GDK_INTERP_BILINEAR)` → tự
    scale ảnh xuống đúng kích thước hiển thị **trước khi vẽ**;
  - tạo `Cairo::ImageSurface` ở kích thước đã scale;
  - `cr->set_source(surface, x, y); cr->paint()` — vẽ 1:1.
- Với `Cairo::PdfSurface` (72 dpi, 1 pt = 1 px), khi hiển thị ảnh 2000×1000 trong khung
  400×200 pt, ảnh bị scale xuống còn 400×200 px rồi mới nhúng vào PDF → PDF chỉ chứa
  `400×200` pixel. Khi PDF viewer zoom/phóng to hoặc in, ảnh bị nhòe/nát.
- HTML export dùng `<img src="file.png">` với src trỏ file gốc → browser render ảnh
  **native resolution**, nét ở mọi zoom. (`markdown_html.cpp:95-102`)
- Probe `/tmp/opencode/imgres.pdf` đã xác nhận:
  - Cách hiện tại: PDF chứa `/Width 400 /Height 200` (mất 5x thông tin).
  - Cách fix: PDF chứa `/Width 2000 /Height 1000` + transform matrix → nét.

### 2.2 Cách sửa (đã probe verify)

Không scale pixbuf thành surface nhỏ nữa; giữ **native resolution** và để Cairo scale
bằng transform matrix khi paint:

```cpp
auto surface = Cairo::ImageSurface::create(ARGB32, native_w, native_h);  // từ pixbuf gốc
cr->save();
cr->translate(x, y);
cr->scale(w / native_w, h / native_h);
cr->set_source(surface, 0, 0);
cr->paint();
cr->restore();
```

→ PDF nhúng ảnh full-res (`/Width 2000`) cùng một ma trận scale; viewer tự quyết định
độ phân giải render → giống HTML.

### 2.3 Cân nhắc

- File PDF lớn hơn (đúng bằng dữ liệu ảnh gốc) — cùng trade-off như HTML embed; chấp
  nhận được theo yêu cầu "ảnh phải nét như HTML".
- Ảnh đã được scale-to-fit trong layout (`resolve_image_block`, không upscale) — giữ
  nguyên hành vi; chỉ đổi cách **render**.
- Transform scale ảnh native → khung hiển thị: nếu scale là phóng to (không có do
  không-upscale) hoặc thu nhỏ (có) đều chính xác.

---

## 3. Lỗi 3 — Thuộc tính HTML/CSS chưa được render qua PDF

### 3.1 Hiện trạng (đã verify bằng probe `sp.cpp`)

- `RunBuilder::emit` **bỏ hoàn toàn** `HtmlSpan`/`HtmlBlock`:
  ```cpp
  case NodeType::HtmlSpan:
  case NodeType::HtmlBlock:
      return;  // raw HTML dropped for safety      (markdown_layout.cpp:135-137)
  ```
- HTML export pass-through verbatim → browser render CSS. Preview+PDF (cùng pipeline
  layout) mất toàn bộ style của inline HTML.
- md4c chunk inline HTML thành nhiều node:
  ```text
  x <span style="color:red">RED</span> y
      ^htmlspan(open)          ^text    ^htmlspan(close)
  ```
  → cần duy trì **style stack** trong `RunBuilder` (giống save/restore đã có cho
  emphasis/strong/link).

### 3.2 Đề xuất phạm vi (cần gate ở Q2)

Chỉ inline HTML subset — không phải browser engine:

- **Tags**:
  - span với attribute `style="..."` (nội dung CSS)
  - b / strong, i / em, u, s / del (map sang weight=700 / italic / underline / strike
    — đã có sẵn trong `StyledText`)
  - code (map sang mono font + background — style Code đã có)
  - mark (background nhạt)
  - font, sub/sup (size), small/big (size tương đối) — tùy Q2
- **CSS properties trong `style=""`** (ngăn cách bởi `;`):
  - `color` (named / `#hex` / `rgb()`)
  - `background-color`
  - `font-weight` (normal / bold / 100-900)
  - `font-style` (normal / italic)
  - `text-decoration` (underline / line-through)
  - `font-size` (pt / px / %) — tùy Q2
  - KHÔNG hỗ trợ layout: margin/padding/position/float/display/flex — nằm ngoài phạm vi.
- **HtmlBlock** có `style` (khối) đứng riêng, không phải page-break marker: hiện tại bị
  bỏ. Đề xuất V1 vẫn bỏ (khối layout phức tạp) — chỉ xử lý inline. Tùy Q3.

### 3.3 Vị trí triển khai

Trong `RunBuilder::emit`, thêm xử lý `NodeType::HtmlSpan`:
- Duy trì 1 stack `[css_override]` song song cơ chế save/restore hiện có.
- Open tag → parse `style`/attr, push lên stack, áp dụng override vào state (color, bg,
  weight, italic, underline, strike, size, font).
- Close tag → pop.
- Text node giữa → push với state đang active.
- Đảm bảo HTML inline state được apply giống emphasis/strong (nested đúng).

→ Preview + PDF cùng nhận style mới; HTML export vẫn pass-through (browser tự render).

### 3.4 Kết quả probe `blk.cpp` — md4c xử lý HtmlBlock theo 3 kiểu

```text
case A: <div style="color:red">\n\n**Bold** text\n\n</div>
  → HtmlBlock(<div style="color:red">)  +  Paragraph(**Bold** text)  +  HtmlBlock(</div>)
case B: <div style="color:blue">Hello <b>world</b></div>   (trong dòng, sát dòng liền)
  → 1 HtmlBlock chứa nguyên cục '<div ...>Hello <b>world</b></div>\n'
case C/D: khối lồng nhau / nhiều khối liền → 1 HtmlBlock nguyên cục (HTML block type 6)
```

→ **Case A** (có blank line ngăn cách giữa các phần) md4c tự tách open-tag / Paragraph /
close-tag; markdown bên trong (**Bold**) vẫn được parse. Đây là đường render rẻ nhất:
mở style context ở open-tag block, para con được style, đóng context ở close-tag block.
Xếp chồng được (div lồng div) bằng 1 context stack trong `emit_node`.

→ **Case B/C/D** gộp nguyên cục (HTML block type 6). Muốn render cần tokenize mini:
bóc `<tagname style="...">` mở, nội dung (text/inline tags), đóng `</tagname>`. Nội dung
hỗ trợ giới hạn = inline subset (Q2). Nếu khối chứa block-level HTML lồng nhau
(div/table/section...) → drop khối đó, giữ nguyên an toàn (hành vi cũ).

### 3.5 Chú ý

- Parser CSS nhỏ gọn: split `;`, split `:`, bỏ whitespace; nhận diện keyword; không cần
  unit đầy đủ. Phải an toàn (không crash khi style sai).
- Reuse `decode_entity` / helpers có sẵn nếu cần.
- `StyledText` đã có đủ field: `color`, `background`, `weight`, `italic`, `strike`,
  `underline`, `size_pt`, `font_family` — không cần đổi struct.

---

## 4. Phạm vi / ngoài phạm vi

| Phạm vi (sẽ làm sau gate) | Ngoài phạm vi |
|---------------------------|---------------|
| PDF internal link cho TOC + dest cho heading | Links ngoài (http://...) thành URI link PDF |
| PDF bookmarks sidebar (nếu Q1 đồng ý) | Preview hit-region thay đổi (giữ nguyên) |
| Ảnh PDF dùng native resolution (fix nát) | WebP/AVIF fetch mới |
| Inline HTML/CSS subset (span style + tags cơ bản) | Block HTML layout (div/table HTML chưa hỗ trợ) |
| Chạm đúng `draw_blocks_range` + `RunBuilder::emit` | Thay đổi HTML export behavior |

## 5. Câu hỏi cho user

- **Q1**: PDF bookmarks (sidebar) — có muốn không? (cùng cơ chế dest đã có, chi phí rẻ,
  nhìn chuyên nghiệp hơn). Đề xuất: có, theo thứ bậc heading level.
- **Q2**: Inline HTML subset đến đâu? Đề xuất: span+style (color, background-color,
  font-weight, font-style, text-decoration, font-size) + b/i/u/s/code/mark. Hỏi có cần
  font/sub/sup/small/big không.
- **Q3**: HtmlBlock có style (khối) trong PDF V1: bỏ qua (chỉ page-break marker) hay
  muốn render luôn? Đề xuất: bỏ qua 1 khối tự đứng (phức tạp), inline trước.

---

## 6. Design gate đã chốt (2026-09-12) — nguồn implement

> Trạng thái: **GATE PASSED**, user duyệt implement. Đây là hợp đồng triển khai.
> Mọi code phải khớp các quyết định + ràng buộc dưới đây, theo đúng phạm vi §4.

### 6.1 Quyết định đã chốt

| Câu hỏi | Quyết định |
|---------|------------|
| **Q1** — PDF bookmarks | **CÓ**. Hierarchical outline: **H1 = node gốc (con của `CAIRO_PDF_OUTLINE_ROOT`); H2 = con của H1 đang mở gần nhất; H3 = con của H2 đang mở gần nhất** (mỗi level duy trì node cha hiện hành). Dùng anchor/dest có sẵn, theo thứ tự headings trong document. |
| **Q2** — Inline HTML subset | **span `style="..."`** + **b/strong, i/em, u, s/del, code, mark**. CSS subset: `color`, `background-color`, `font-weight`, `font-style`, `text-decoration`, `font-size`. KHÔNG cần font/sub/sup/small/big (từ đề xuất §3.2). |
| **Q3** — HtmlBlock | **Render luôn**. Ogồm: (a) case A — open/close tag + Paragraph ở giữa (md4c tách sẵn), (b) case B/C/D — chunk nguyên cục qua mini tokenizer. CSS subset giống inline. KHÔNG dùng browser engine; unsupported → **fail-safe** (bỏ style/tag, giữ text), mọi trường hợp không crash. |

- **PDF internal TOC links**: giữ nguyên — click dòng TOC nhảy tới heading (CÓ làm).
- **PDF heading destinations**: giữ — mọi heading đều có dest để link nhảy vào được.
- **Ảnh native resolution**: CÓ làm (sửa hết nát như HTML).
- **TOC page-number + dotted-leader**: giữ nguyên vẹn từ v0.0.9rc.

### 6.2 Kế hoạch implement (thứ tự)

1. **Links trong PDF** — `draw_blocks_range()` thêm param `emit_links` (mặc định `false`);
   `export_pdf()` bật `true`. Bọc `Dest` tag `name=<anchor>` quanh vẽ heading; bọc `Link`
   tag `dest=<anchor>` quanh vẽ title của TocRow + block link nội `#anchor` (leader dots
   và số trang KHÔNG nằm trong tag). Gọi C API qua `cr->cobj()`.
2. **Outline (bookmarks)** — trong `export_pdf()`, sau khi có danh sách heading theo thứ tự,
   xây cây parent theo quy tắc §6.1, gọi `cairo_pdf_surface_add_outline(surface, parent,
   title, "dest='<anchor>'", flags)` cho từng heading **trước** `surface->finish()`.
   `surface` lấy qua `pdf_surface->cobj()`.
3. **Ảnh native resolution** — `draw_pixbuf()`: detect target là qua `PdfSurface`
   (`cr->get_target()->get_type() == CAIRO_SURFACE_TYPE_PDF`); khi là PDF thì tạo
   `ImageSurface` từ pixbuf **ở native resolution**, `translate(x,y)` + `scale(w/nw,h/nh)`
   + `paint()`. Preview (screen) giữ nguyên hành vi scale hiện tại.
4. **Inline HTML/CSS** — tạo `markdown_inline_html.{hpp,cpp}`:
   - `parse_css_style()`: keyword + doctr `;`/`:` + value (named/`#hex`/`rgb()`, weight,
     style, text-decoration, font-size pt/px/%); an toàn với input sai.
   - Map semantic tag → `StyledText` override (b/strong→weight 700, i/em→italic, u→underline,
     s/del→strike, code→style Code có sẵn, mark→background nhạt).
   - Chunk tokenizer mini cho HtmlBlock case B/C/D (bóc open/close + inline subset bên trong;
     block-level lồng → drop khối, giữ cũ).
   - Tích hợp vào `RunBuilder::emit`: style stack song song save/restore cho `HtmlSpan`
     (open push / close pop), `HtmlBlock` case A qua context stack open/close, page-break
     marker giữ nguyên hành vi.
5. **Test** — unit tests cho: TOC link PDF có dest khớp, outline hierarchy (H1/H2/H3 cha-con),
   ảnh PDF chứa width full-res, parse CSS inline (hợp lệ + sai không crash), render
   HtmlBlock case A + B/C/D, malformed HTML/CSS không crash.
6. **Build + ctest** — `cmake --build build -j4` + `ctest --test-dir build --output-on-failure`
   (18/20 hiện tại, 2 pre-existing failures không thuộc scope).

### 6.3 Ràng buộc (bất biến)

- **HTML export = pass-through verbatim, KHÔNG đổi behavior** (`markdown_html.cpp`). Subset
  HTML/CSS chỉ để Remin preview/PDF hiểu — KHÔNG biến thành sanitizer/rewrite.
- Không redesign layout architecture; **reuse `StyledText` fields + infra hiện có**.
- Cấu trúc giống cơ chế save/restore đã có (emphasis/strong/link) — inline HTML là một
  override nữa, không tách pipeline.
- Không hardcode màu/icon; tuân `AGENTS.md` (không emoji, không thay đổi theme).
- `draw_blocks_range` dùng chung preview+PDF → thay đổi phải giữ preview hit-region
  hoạt động như cũ (điều hướng preview không đổi).