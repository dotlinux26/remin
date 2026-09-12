# Notes

The note tab is a markdown editor with a live preview. Notes autosave through
the same workspace pipeline as terminal state.

## Basics

| Shortcut         | Action                        |
|------------------|-------------------------------|
| `Ctrl+Shift+N`   | new note tab                  |
| `Ctrl+S`         | save the active note          |
| Toolbar / tree   | open a text file → note tab   |

Double-clicking (or opening) a text file in the [file tree] opens it as a note.

[file tree]: terminal.md

## Editor & preview

Each note tab has an editor and an optional **live preview** split. Toggling
preview splits the note horizontally: `editor | preview`.

- The preview renders the markdown as you type (deferred to idle so the first
  open is laid out at the real width).
- **Sync Scroll** keeps the editor and preview in lockstep; toggling it on
  immediately realigns from the editor position.
- Images referenced in a note resolve from `~/remin-image/` (shared image
  store) and relative to the note's folder.
- Inline code, GFM-style tables, headings, lists, checkboxes, blockquotes, and
  code blocks are supported. See [markdown](markdown.md) for style details.

## Autosave & unsaved notes

Notes persist through the unified edge-triggered autosave pipeline. Unsaved
scratch notes are kept under `~/.local/share/remin/unsaved-notes/` (XDG data
home) so a fresh note that was never saved to disk still comes back on restart.

## Export

The preview header has **HTML** and **PDF** export buttons (see
[export](export.md)).