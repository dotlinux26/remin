# UI Design Principles

These are the visual/interaction rules Remin follows (ADR-0007 and the product
tone in the SPEC). They are *principles*, not a pixel spec.

## Text-first, no icon soup

- Navigate by **words, spacing, alignment, keyboard shortcuts, subtle borders,
  and selection state** — not by a toolbar of icons.
- Tabs are `remin-tab` text buttons; the active tab is marked by color +
  underline.
- The **only** icons are small tab-kind markers (`remin-terminal` /
  `remin-note`) rendered as `Gtk::Image + Gtk::Label`. They are presentation of
  the semantic `TabKind` (never logic) — a tiny, deliberate exception that aids
  scanning, not a toolbar.
- Result: a calm, dense, terminal-native interface with a hint of warmth.

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
- Both themes favor **generous whitespace**, clear typography, and a subtle
  divider treatment. No gradients/decoration beyond the required accent.
- Remin must never read as an IDE; it stays a calm text shell in either theme.

## Feedback is quiet but present

- Long-running ops (e.g. autosave) surface as a **small pill** (`#autosave-badge`)
  top-right: green `saved ✓`, red `save failed`. It auto-hides after ~2 s.
- No modal dialogs for routine transitions.

## Consistency

- The window header, tab strip, and status bar share one vertical rhythm and one
  accent language.
- A single dominant font + `tnum` figures in status areas.

The logo itself is reused as the header mark; it is the only "hero graphic" in
the shell, and it stays small (22 px) so it reads as a signature, not a splash.
