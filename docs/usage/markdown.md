# Markdown

Remin renders markdown with a **document engine** built on md4c — parsing is
separated from layout so the editor preview and HTML/PDF export share one
source of truth (see
[`docs/design/markdown-document-engine.md`](../design/markdown-document-engine.md)).

## Supported syntax

- headings, paragraphs, blockquotes, lists (ordered/unordered, nested),
  task lists
- fenced and indented code blocks with visible language hint
- **inline code spans** — including spans containing spaces (e.g.
  `10.0.0.0/24`, `` `a b` ``), inside table cells too
- GFM-style tables with borders on every edge, sized to content and centered
- links, images (from `~/remin-image/` or the note's folder), horizontal rules
- emphasis, strong, strikethrough

## Rendering

- **Preview** — the preview split matches the editor's light/dark theme CSS
  (`markdown-preview.css`, plus theme styles), line grids and typography stay
  consistent with the editor.
- **HTML export** — one self-contained document, images embedded/resolved, with
  pagebreak and TOC support (`<!-- pagebreak -->`).
- **PDF export** — Pango/Cairo layout with links and images resolved and CSS
  inlined (`docs/design/pdf-links-image-resolution-inline-css.md`).

Links and image URLs inside exported documents resolve against the note's
folder and the shared `~/remin-image/` store.

## Design references

- [`docs/design/markdown-document-engine.md`](../design/markdown-document-engine.md)
- [`docs/design/inline-html-polish-v2.md`](../design/inline-html-polish-v2.md)
- [`docs/design/markdown-html-pagebreak-toc.md`](../design/markdown-html-pagebreak-toc.md)