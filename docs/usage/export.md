# Export

Every note can be exported from the preview header:

| Button | Output                       |
|--------|------------------------------|
| `HTML` | a self-contained `.html` document |
| `PDF`  | a Pango/Cairo-rendered `.pdf`    |

## HTML export

- Self-contained: the full document (body + styles) is written to one file.
- Images resolve from `~/remin-image/` (the `remin://images/…` store) and
  relative to the note's folder.
- Pagebreak marker `<!-- pagebreak -->` and heading/TOC handling follow
  [`docs/design/markdown-html-pagebreak-toc.md`](../design/markdown-html-pagebreak-toc.md).
- The CSS is the same one used by the preview (theme-aware). If a custom CSS
  file is loaded for preview, it is inlined into the export.

## PDF export

- Same parsing/layout engine as HTML, rendered to PDF with Pango/Cairo.
- Links and image URLs are resolved the same way as HTML; CSS is inlined.
- Page metadata is set (`author = "remin"`) and page size follows the
  layout engine's page model (see
  [`docs/design/pdf-links-image-resolution-inline-css.md`](../design/pdf-links-image-resolution-inline-css.md)).

## Where files go

The export dialogs ask for a target path; exports never overwrite unchecked
files without a confirmation.