# Markdown

Remin renders markdown with a **document engine** built on md4c — parsing is
separated from layout so the editor preview and HTML/PDF export share one
source of truth (see
[`docs/design/markdown-document-engine.md`](../design/markdown-document-engine.md)).

## Supported Markdown Syntax (GFM + Extensions)

### Headings
```markdown
# H1
## H2
### H3
#### H4
##### H5
###### H6
```
Headings receive deterministic anchors (`intro`, `intro-2`, ...) for TOC and
internal links.

### Text Formatting
- **Bold**: `**text**` or `__text__`
- *Italic*: `*text*` or `_text_`
- ***Bold + Italic***: `***text***`
- ~~Strikethrough~~: `~~text~~`
- `Inline code` — including spans containing spaces (e.g. `10.0.0.0/24`,
  `` `a b` ``)

### Lists
- Unordered: `- item` or `* item`
- Ordered: `1. item`
- Nested lists (indent 2–4 spaces)
- Task lists: `- [ ] pending`, `- [x] done`

### Blockquotes
```markdown
> Quote line 1
> Quote line 2
```

### Horizontal Rule
`---` or `***` or `___`

### Code Blocks
````markdown
```bash
echo "language badge shown in top-right"
```
````
Fenced code with optional language hint. Language badge renders in preview
and PDF.

### Tables (GFM)
```markdown
| Header 1 | Header 2 | Header 3 |
|----------|:--------:|---------:|
| left     | center   | right    |
| a        | b        | c        |
```
Alignment via `:` in the separator row. Tables centered in frame with
header bottom border, cell padding, and vertical centering.

### Links & Images
```markdown
[Link text](https://example.com)
[Link with title](https://example.com "Tooltip")
![Alt text](image.png)
![Alt](image.png "Title")
```
Relative paths resolve against the note's folder. Images also resolve from
the shared store `~/remin-image/` and the `remin://images/` scheme.

### TOC (Table of Contents)
```markdown
[[TOC]]
```
Inserts an auto-generated, nested, numbered TOC from headings. In preview:
clickable navigation. In PDF: dotted leader + page numbers (two-pass
pagination). In HTML export: clickable links with dotted leader.

### Page Breaks
```markdown
<!-- pagebreak -->
```
or raw HTML:
```html
<div style="page-break-after: always;"></div>
```
Recognized in PDF export (forces page break). In preview: renders a faint
dashed line. In HTML export: passed through verbatim for browser print.

### Footnotes (md4c 0.4.8 — syntax recognized, rendering pending)
```markdown
Here is a footnote reference[^1].

[^1]: Footnote content.
```
Supported in AST; full rendering depends on md4c version with footnote flag.

---

## Inline HTML & CSS Support

### Philosophy
- **HTML export**: Raw HTML blocks and spans pass through verbatim (GitHub
  standard). The browser handles CSS/layout.
- **Preview & PDF (Pango/Cairo)**: Only **page-break markers** are interpreted.
  Other raw HTML is dropped (current behavior). This keeps preview/PDF
  deterministic and secure.

### Allowed HTML in Export
Any valid HTML block or span is emitted unchanged in HTML export:
```html
<div style="page-break-after: always;"></div>
<span style="color: #d32f2f; font-weight: 600;">Critical</span>
<table><tr><td>custom</td></tr></table>
<script>alert('xss')</script>  <!-- passes through — user responsibility -->
```
**No sanitization in V1** — same as GitHub. Document this clearly.

### Page-Break Detection (Preview + PDF)
Detected in `HtmlBlock` / `HtmlSpan` nodes (case-insensitive):
- `page-break-after: always`
- `break-after: page`
- `page-break-before: always`
- `break-before: page`

On tags: `<div>`, `<section>`, `<p>`, `<span>` with a `style` attribute.

### CSS Subset ("Remin Document Style")
For preview/PDF, a CSS-like subset is parsed from a user stylesheet
(Settings → Note → Custom CSS). Not a full CSS engine.

**Selectors:**
```
document, h1..h6, p, code, pre, blockquote, a, ul, ol, li,
table, th, td, tr, hr, .toc, .toc-title, .toc-link, page
```

**Properties:**
| Property | Example Values |
|---|---|
| `color` | `#ff0000`, `@accent`, `rgb(255,0,0)` |
| `background-color` | `@surface`, `transparent` |
| `font-family` | `"Monospace"`, `system-ui` |
| `font-size` | `12pt`, `14px`, `1.2rem` |
| `font-weight` | `400`, `600`, `bold` |
| `font-style` | `italic`, `normal` |
| `text-decoration` | `underline`, `line-through`, `none` |
| `margin` / `margin-top` / ... | `12pt`, `6mm`, `8px` |
| `padding` / `padding-left` / ... | `4px`, `0.5cm` |
| `border` / `border-left` / `border-color` / `border-width` | `1px solid @border` |
| `line-height` | `1.5`, `1.6` |
| `text-align` | `left`, `center`, `right`, `justify` |
| `page { size | width | height | margin }` | `page { size: a4; margin: 25mm; }` |

**Palette Tokens** (resolve against current theme at parse time):
`@text`, `@accent`, `@bg`, `@surface`, `@border`, `@text-muted`,
`@red`, `@orange`, `@amber`, `@green`, `@blue`

Dark mode works automatically — same CSS file, palette swapped.

---

## Image Handling

### Storage Locations
| Scheme / Path | Resolves To |
|---|---|
| `remin://images/asset-NNN.png` | `~/remin-image/asset-NNN.png` |
| `assets/asset-NNN.png` | `<note-folder>/assets/asset-NNN.png` |
| `./image.png` | Relative to note file |
| `~/remin-image/foo.png` | Expanded home directory |
| `https://...` | External (loaded in preview/HTML; embedded in PDF) |
| `data:image/...;base64,...` | Blocked in preview/PDF; passed in HTML export |

### Paste Image (Ctrl+V)
1. Clipboard contains image data.
2. Saved to `~/remin-image/asset-NNN.png` (scans `001` upward for first gap).
3. Inserts `![alt](remin://images/asset-NNN.png)` at cursor.

### Drag & Drop
Drop image file onto editor → same as paste (copies to shared store, inserts
reference).

### Image Scaling
Images scale to fit content width preserving aspect ratio. In PDF: embedded
as raster at render resolution.

---

## Editor Features

### Live Preview
- Toggle: toolbar button or `Ctrl+Shift+P`
- Split view: `editor | preview` (horizontal)
- **Sync Scroll**: keeps editor and preview aligned (toggle in preview header)
- Deferred render on idle — first open lays out at real width

### Find / Replace
- `Ctrl+F` — Find bar (same height as toolbar)
- `Ctrl+H` — Replace bar
- `Esc` — closes find/replace, clears all highlights in **all** tabs
- Matches highlight in editor; current match distinct

### Auto-Save
- Edge-triggered (on typing burst, not timer)
- Unsaved scratch notes: `~/.local/share/remin/unsaved-notes/`
- Restored on restart via workspace pipeline

### Syntax Highlighting
- Markdown syntax in editor (headings, code, links, etc.)
- Code blocks: language-aware (via GtkSourceView)

### Line Numbers & Word Wrap
- Gutter with line numbers (synced scroll with editor content)
- Word wrap toggle in toolbar

### Keyboard Shortcuts
| Shortcut | Action |
|---|---|
| `Ctrl+Shift+N` | New note tab |
| `Ctrl+S` | Save note |
| `Ctrl+Shift+P` | Toggle preview |
| `Ctrl+F` | Find |
| `Ctrl+H` | Replace |
| `Ctrl+P` | Print / Export dialog |
| `Tab` / `Shift+Tab` | Indent / unindent selection |

### HTML Tag Auto-Close
Type `<h1>` + `>` → `</h1>` auto-inserted after cursor. Void elements
(`br`, `img`, `hr`, `input`, `meta`, `link`, `area`, `base`, `col`,
`embed`, `param`, `source`, `track`, `wbr`) are not auto-closed.

---

## Export

### HTML Export
- Self-contained single `.html` file
- Embedded CSS (theme-aware + custom stylesheet if set)
- Images: `file://` for local, relative for note-folder, `remin://images/`
  rewritten to `file://`
- Pagebreak markers and TOC rendered per design spec
- Clickable links (internal anchors + external URLs)

### PDF Export
- Pango/Cairo layout — same engine as preview
- A4 / Letter / custom page size (`page.size` in stylesheet)
- Header/footer with tokens:
  `{page}`, `{pages}`, `{date}`, `{time}`, `{title}`, `{author}`, `{filename}`
- TOC page (optional): dotted leader + page numbers (two-pass)
- Internal links: PDF destinations; External links: clickable URIs
- Images embedded as raster

### Print Header / Footer
Configured in Settings → Note → Print:
- Left / Center / Right zones
- Tokens as above

---

## HTML Sanitization

**None in V1.** HTML export passes raw HTML through verbatim. This matches
GitHub's behavior — users who write `<script>` in their markdown get
`<script>` in the export. Preview and PDF drop non-pagebreak HTML, so they
are not affected.

If you require sanitization, pre-process the markdown before handing to
Remin, or use a downstream tool on the exported HTML.

---

## Page Breaks — Summary

| Syntax | Preview | PDF | HTML Export |
|---|---|---|---|
| `<!-- pagebreak -->` | Faint dashed line | Hard page break | `<hr class="pagebreak">` |
| `<div style="page-break-after: always"></div>` | Faint dashed line | Hard page break | Passed verbatim (browser honors on print) |
| `<div style="break-after: page"></div>` | Faint dashed line | Hard page break | Passed verbatim |

---

## Design References

- [`docs/design/markdown-document-engine.md`](../design/markdown-document-engine.md)
- [`docs/design/inline-html-polish-v2.md`](../design/inline-html-polish-v2.md)
- [`docs/design/markdown-html-pagebreak-toc.md`](../design/markdown-html-pagebreak-toc.md)
- [`docs/design/pdf-links-image-resolution-inline-css.md`](../design/pdf-links-image-resolution-inline-css.md)