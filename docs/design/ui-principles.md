# UI Design Principles

These are the visual/interaction rules Remin follows. Inspired by VS Code's
compact, clean aesthetic but adapted for a terminal-first workspace.

## VS Code-inspired compact layout

- **Compact rows**: directory tree rows 20-22px, toolbar 32-36px, tab bar 32px
- **Tight spacing**: 4px grid (4, 8, 12, 16, 20, 24)
- **Dense information**: file type extension + size on row, date on hover only
- **No excessive whitespace**: every pixel earns its place
- **Subtle borders and dividers**: thin lines, not heavy separators

## Text-first with smart icons

- Navigate by **words, spacing, alignment, keyboard shortcuts, subtle borders,
  and selection state** — not by a toolbar of icons.
- Tabs are `remin-tab` text buttons; the active tab is marked by background
  highlight + border accent.
- Toolbar buttons use **icon + text label** for clarity (not icon-only).
- Directory tree uses proper folder/file icons from system theme.
- Symbolic icons inherit foreground color via CSS.

## One accent system

- All colors come from a small set of **semantic variables** built from the
  logo's indigo→cyan gradient (`#4f46e5 → #6366f1 → #06b6d4`).
- Themes (`resources/styles/{light,dark}.css`) map with GTK `@define-color`:
  - `@bg` / `@surface` / `@border` / `@text` / `@text-muted`
  - `@accent` / `@accent-2` / `@accent-soft`
- C++ never hard-codes colors; it only applies CSS classes.

## Light default, dark first-class

- Default theme is **light**. Dark is a first-class, fully-supported alternative
  (toggled via the View menu or system preference), not an afterthought.
- Both themes favor **compact density**, clear typography, and a subtle
  divider treatment. No gradients/decoration beyond the required accent.
- Remin must never read as a bloated IDE; it stays a calm, dense text shell.

## Feedback is quiet but present

- Long-running ops (e.g. autosave) surface as a **small pill** (`#autosave-badge`)
  top-right: green `saved ✓`, red `save failed`. It auto-hides after ~2 s.
- No modal dialogs for routine transitions.
- Active tab highlight is immediate and obvious.
- Toolbar shows contextually based on active tab type.

## Consistency

- The window header, tab strip, toolbar, find bar, and status bar share one
  vertical rhythm and one accent language.
- A single dominant font + `tnum` figures in status areas.
- All interactive elements (buttons, entries, tabs) use consistent height.
- Find bar height = toolbar height (same visual weight, integrated row).

## Directory tree (VS Code-style)

- Starts at `$HOME`
- Compact rows with folder/file icons
- File extension shown as subtle suffix (`.txt`, `.py`, `.sh`)
- File size shown on row (`4.2 KB`, `1.2 MB`)
- Date shown only on hover (tooltip)
- Search/filter box at top of directory panel
- Right-click context menu: New File, New Folder, Rename, Delete, Copy Path
- Double-click text files → opens in note editor
- Only show expander arrow for directories with children

The logo itself is reused as the header mark; it is the only "hero graphic" in
the shell, and it stays small (22 px) so it reads as a signature, not a splash.
