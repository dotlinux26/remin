# Design — Remin Markdown Document Engine (MD4C → Document Model → Native Cairo)

Date: 2026-09-09
Status: DESIGN — đã chốt kiến trúc với user (không WebKit). Đang implement theo
phases phía dưới. Doc này ghi lại **YÊU CẦU** + **GIẢI PHÁP ENGINE** (cùng một nguồn).

---

## 0. Mục tiêu

Remin Editor/Markdown milestone (12 tính năng) chạy trên **một pipeline duy nhất**,
không phụ thuộc trình duyệt:

```text
        Markdown Source
              │
              ▼
            MD4C              (CommonMark + extensions)
              │
              ▼
   ┌─────────────────────┐
   │ Remin Document Model│   (semantic, KHÔNG theo HTML)
   │                     │
   │ heading/paragraph/  │
   │ span/link/image/    │
   │ list/table/quote/   │
   │ code/toc/footnote/  │
   │ admonition/pagebreak│
   └─────────┬───────────┘
             │
             ▼
  ┌────────────────────┐
  │ Remin Style Engine │  ("Remin Document Style" — CSS-like subset)
  └─────────┬──────────┘
            │
            ▼
  ┌────────────────────┐
  │ Remin Layout Engine│  (Pango measure → line breaking / page breaking)
  │                    │
  │  Display List      │  (DrawCommand: Text/Rect/Line/Image)
  └─────────┬──────────┘
          ↓            ↓
  GTK/Cairo Preview    Pango/Cairo PDF
  (Gtk::DrawingArea)   (Cairo::PdfSurface)
```

**Nguyên tắc sống còn**: Preview và PDF **không được có hai layout khác nhau**.
Một Display List, một hàm `draw(display_list, cairo_ctx)` — surface khác nhau
thôi (screen vs PDF). "Giống nhau" = cùng layout/rendering pipeline, không hứa
pixel-perfect ở mọi PDF viewer.

---

## 1. Quyết định kiến trúc (chốt với user)

| Quyết định | Giá trị | Lý do |
|---|---|---|
| Không WebKit | ❌ | Gọn, tự chủ, 1 binary + assets, không kéo WebProcess/JS Core/ICU/… |
| Không Python / Pandoc / WeasyPrint | ❌ | Không runtime ngoài; PDF render nội bộ. |
| MD4C | ✅ | Parser nhỏ, CommonMark + tables/tasklists/strikethrough/permissive autolinks. |
| Document Model | ✅ | Semantic (heading/paragraph/…), KHÔNG thiết kế theo DOM. |
| Layout Engine + Display List | ✅ | Preview/PDF/export chia cùng layout. |
| GTK/Cairo preview | ✅ | `Gtk::ScrolledWindow → Gtk::DrawingArea`, Cairo + Pango. |
| PDF = Cairo::PdfSurface | ✅ | Pagination/header/footer/TOC tự xây trên Display List. |
| Static friendly | ✅ | md4c embed-able; Cairo static possible; Pango static heavy (runtime vẫn cần font/glib…) |

Dependency footprint cuối:

```text
core document pipeline:  MD4C + GLib/GTK + PangoCairo + Cairo
```

---

## 2. YÊU CẦU (milestone 12 tính năng)

1. **Image paste**: Ctrl+V trong note editor, clipboard có ảnh → lưu
   `assets/asset-NNN.png` (scan 001↑) → chèn ref `![alt](assets/asset-003.png)`.
2. **Full Markdown preview** (native): headings 1–6, paragraph, bold/italic/
   strikethrough, inline code, fenced code, links, images (local/relative/
   absolute/data URI), lists (order/nested), task lists, blockquote, hr, tables
   (align), TOC, footnote (nếu md4c version hỗ trợ).
3. **Custom CSS**: "Remin Document Style" — CSS-like subset (colors, font, size,
   margin, padding, border, background, alignment, page). KHÔNG full CSS engine.
4. **CSS setting**: chọn file `.css`/`.rcss` trong Settings; Reset về default;
   preview + PDF + HTML export cùng dùng file đó (preview/PDF parse subset, HTML
   export nhúng nguyên text).
5. **Preview toolbar**: [Sync Scroll] + [Export HTML] + [Export PDF] (giữ trong
   `preview_host` header hiện có).
6. **HTML export**: `render_html_body()` từ cùng AST → 1 file .html tự đứng
   (nhúng CSS, image refs → file:// hoặc relative), mở Save dialog.
7. **PDF export**: `Cairo::PdfSurface`, pagination A4/letter, header/footer,
   page number `{page}/{pages}`, TOC page, images embed.
8. **Print header/footer**: left / center / right + tokens `{page}{pages}{date}
   {time}{title}{author}{filename}`.
9. **TOC**: `[[TOC]]` = Remin extension → auto TOC (nested, đánh số 1 / 1.1),
   toc_max_depth.
10. **Page numbering**: footer `Page N of M` + tokens.
11. **Document model**: một semantic AST duy nhất (md4c parse 1 lần), mọi nơi đọc
    cùng model. Image asset mapping, print config per note (JSON blob).
12. **Testing**: unit tests cho parse/ast/html/style/layout/document/pdf + toàn bộ
    ctest suite vẫn pass.

---

## 3. Document Model (semantic, không theo HTML)

Trạng thái hiện tại: `src/gui/markdown/markdown_ast.{hpp,cpp}` — md4c callbacks →
`Node` tree (arena → value):

```cpp
enum class NodeType {
    Root, Heading(level), Paragraph, BlockQuote, CodeBlock(lang,text),
    HtmlBlock, ThematicBreak, List(ordered,start,tight), ListItem(task,checked),
    Table(aligned cells), TableRow, TableCell(header,align),
    Link(href,title), Image(src,title,{alt}), Emphasis, Strong, Del,
    CodeSpan, Text, SoftBreak, HardBreak, HtmlSpan, Toc
};
```

- Một parse duy nhất (md4c / MD_DIALECT_GITHUB), node tree semantic.
- `[[TOC]]` được nhận diện thành node `Toc`.
- `headings()` → flatten + anchor deterministic dedup (intro, intro-2…), dùng cho
  TOC + link #anchor.
- Đã có renderer HTML (`markdown_html.*`) cho export.

Tiến tới (nếu md4c version support): footnote/comment/admonition/highlight/
sup/sub → thêm NodeType + parser flag tương ứng. **KHÔNG hứa math renderer**
(riêng engine, xem §8).

---

## 4. Remin Style Engine ("Remin Document Style")

Trạng thái: `src/gui/markdown/markdown_style.hpp` (đang implement parser).

- CSS-like subset parser: `selector { prop: value; }`, selector nhiều tên, value
  có unit (`12pt`, `18mm`, `0.5cm`, `16px`, số).
- Selectors: `document, h1..h6, p, code, pre, blockquote, a, ul, ol, li, table,
  th, td, tr, hr, .toc, .toc-title, .toc-link, page`.
- Props: color, background(-color), font-family, font-size, font-weight,
  font-style, text-decoration(strike/underline), margin[+-top/bottom/left/right],
  padding, border, border-left, border-color, border-width, line-height,
  page{size|width|height|margin}.
- Palette token `@text @accent @bg @surface @border @text-muted
  @red @orange @amber @green @blue` → resolve theo theme lúc parse →
  preview/PDF tự theo dark mode mà không sửa file.
- Builtin `default_style(palette)`; `apply_style(css, base, palette)`.
- Không parse sai → crash; mọi unknown bị bỏ qua.

Định nghĩa: đây là **Remin Style Sheet**, KHÔNG phải CSS engine (không flexbox/
grid/position/animation/DOM).

---

## 5. Layout Engine → Display List

**`src/gui/markdown/markdown_layout.*`** (đang implement).

Đầu vào: `MarkdownAst + StyleSheet + content_width (+ page_height cho PDF)`.
Pango đo & wrap; kết quả là **block boxes** với draw-able primitives:

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
    double y, height;                 // trong flow (xét margin trước/sau)
    double margin_before, margin_after;
    std::vector<DrawCmd> cmds;
    bool splittable;                  // paragraph/code/… cut được giữa page
    // dùng cho page breaker:
    ... bounding region để clip/translate khi tách dòng
};
```

- **Inline**: text runs (bold/italic/strike/underline/color/bg) biểu diễn bằng
  Pango attr list trên 1 layout/paragraph; link ghi vị trí (Underline + màu
  accent). Image inline → block Image (đơn giản hóa V1, ghi nhận).
- **Tables**: grid, col width theo content + available, align, header bg, border.
- **Sticky keep rules**: heading keep-with-next, không để heading trơ cuối trang.
- **Preview**: layout 1 flow liên tục (không ngắt trang), width = widget width.
- **PDF**: width = content_width + **page breaking** (line-granular cho splittable,
  keep cho heading/listitem/table-row).

Display List chung → **cùng code draw** cho cả preview lẫn PDF (§6).

---

## 6. Draw (shared Cairo)

**`src/gui/markdown/markdown_draw.*`** — `draw_blocks(cr, blocks, ...)`.

- PangoCairo vẽ text (create_layout trên cr đang vẽ → wrap khớp layout).
- HtmlDest TOC/heading anchors vẽ như text (với số + indent).
- Hình: `cairo_set_source_surface` (load GdkPixbuf → Cairo Surface); scale giữ
  tỷ lệ, giới hạn chiều rộng.
- Code block: nền + border; quote: border-trái + nền nhạt padding.
- Được gọi bởi: `MarkdownPreview::on_draw` (screen cr) và
  `markdown_pdf_export` (PDF cr). **Code path giống nhau.**

---

## 7. Native GTK Preview

**`src/gui/note/markdown_preview.*`** — rewrite (giữ public API để
`NoteTabView` không đổi):

```text
Gtk::ScrolledWindow
 └── Gtk::DrawingArea        (Cairo renderer, không GtkLabel nữa)
```

Public API giữ nguyên:
```cpp
MarkdownPreview();  void render(const std::string&);
void set_scroll_fraction(double);   vadjustment();
+ void set_style_path(const std::string&);   // user CSS (subset)
+ void set_dark(bool);                          // đổi palette, re-layout
```

- Scrolling native GTK → scroll sync dùng `vadjustment()` như cũ.
- Click link: GestureClick hit-test trên Display List link rect → mở browser
  (xdg-open / Gtk show-uri). *(phase sau)*
- Light-first, dark mode = palette đổi + re-render (không reload WebView).

---

## 8. PDF Export

**`src/gui/markdown/markdown_pdf_export.*`** — `Cairo::PdfSurface`.

- Page geometry: `page.size` (a4/letter hoặc width/height), `page.margin`.
- Pipeline: AST → StyleSheet(+user) → LayoutEngine (paginate với content_height)
  → TOC 2 pass (nếu show_toc) → draw từng page:
  - page background
  - header (left/center/right, tokens)
  - content
  - footer (left/center/right, `{page}/{pages}`)
- TOC page đầu (optional): các heading level ≤ max_toc_depth, số trang, dấu chấm
  lề; links nội bộ dùng `cairo_pdf_surface_add_outline` / annotation.
- Images: embed vào PDF (cairo image surface per image).
- Code block split line-granular; heading keep-with-next.
- Pass 1 = layout không TOC để biết tổng `{pages}`; pass 2 = render TOC + số trang.

Tokens header/footer: `{page} {pages} {date} {time} {title} {author} {filename}`.

**Không dùng**: PrintOperation screenshot, WeasyPrint, wkhtmltopdf.

---

## 9. Feature matrix (md4c 0.4.8 trên máy hiện tại)

| Feature | md4c | Remin |
|---|---:|---:|
| H1–H6, paragraph, bold/italic, inline & fenced code, links, images, ordered/unordered/nested lists, tasklist, blockquote, hr, tables, strikethrough | ✅ | ✅ (layout + draw) |
| permissive wikipedia/wikilink/underline extension | ✅ flag | ⚠️ renderer đơn giản |
| footnote / highlight / sup / sub / admonition | ❌ (0.4.8) | chờ md4c version có flag — ko chặn milestone |
| math (`$...$`) | ✅ nhận diện syntax | ⚠️ cần math renderer riêng (ngoài milestone) |
| TOC ([[TOC]]), heading IDs, text colors, background, custom theme, custom stylesheet, page size, margins, page break, header/footer, page numbers, left/center/right, PDF, clickable links, image scaling, watermark, cover page | parser cấp heading | ✅ Remin layout/display list |

---

## 10. Static linking (notes)

- **md4c**: dễ nhúng source (`md4c.c/h`) hoặc `libmd4c.a`.
- **Cairo**: static OK (`pkg-config --static --libs cairo` xem chain trên máy build).
- **Pango**: build/static theo chain (glib, harfbuzz, freetype, fontconfig) —
  khả thi nhưng vẫn cần font/backend runtime; không phải "binary độc lập 100%".
- **GTK**: app GTK4 nên vẫn cần glib/gio runtime; **không WebKit** giảm footprint
  khổng lồ (ko WebProcess/JS Core/libsoup/ICU…).

Quyết định dài hạn: đặt mục tiêu **1 binary + vài assets**, không bắt user cài
thêm runtime browser.

---

## 11. Implementation gating (phases)

Hướng đi (mỗi phase có build + test trước khi qua):

- Phase A: ✅ Document Model (`markdown_ast`) + HTML body renderer
  (`markdown_html`) + document wrapper/assets/print config (`markdown_document`).
- Phase B: ⏳ Style Engine (`markdown_style`) — parser + defaults + palettes.
- Phase C: ⏳ Layout Engine (`markdown_layout`) → BlockBox + Display List;
  page breaker.
- Phase D: ⏳ Draw layer (`markdown_draw`) + rewrite `MarkdownPreview` (DrawingArea).
- Phase E: ⏳ NoteEditor paste-image + NoteTabView toolbar/export + assets + sync
  scroll + Settings CSS + SessionController keys.
- Phase F: ⏳ PDF export (`markdown_pdf_export`): header/footer/TOC/pagination
  token.
- Phase G: ⏳ Tests (ast/html/style/layout/document/pdf) + golden accept + AGENTS.md.

**Acceptance tổng**: preview render đẹp bằng Cairo (không WebKit); PDF xuất ra
cùng layout; 12 tính năng đủ; toàn bộ ctest cũ + mới pass; app chạy clean.

---

## 12. Non-goals (giữ ranh giới)

- Không full CSS engine (flex/grid/position/animation/DOM/JS).
- Không math renderer đẹp (cần engine riêng nếu muốn LaTeX thật).
- Không syntax highlighter đậm (thêm sau như layer độc lập).
- Không WebKit/Python/Pandoc/WeasyPrint trong core document pipeline.