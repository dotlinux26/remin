# Configuration

Remin is designed to work with **zero configuration**. Everything below is a
description of how it behaves and where state lives, not a promise of a large
settings surface.

## Data directory

Remin stores one canonical SQLite database plus its resources under the XDG
data home:

```text
$XDG_DATA_HOME/remin/remin.db            (default: ~/.local/share/remin/remin.db)
```

The database holds the workspace (windows, tabs, panes), terminal scrollback
snapshots, note bodies, command history, and settings — written atomically.

There is also a single-instance lock so two Remin processes do not race on the
same database.

## Settings

Settings are stored in the database under the `settings:` key namespace.

| Key                            | Default | Meaning                                      |
|--------------------------------|---------|----------------------------------------------|
| `settings:persist-open-windows`| `1`     | restore the last open workspace on startup   |
| `settings:command-history`     | `1`     | track per-pane command history               |

Turning `persist-open-windows` off clears old window state on startup so the
app always launches fresh (it never silently duplicates windows).

## Autosave

Remin saves continuously, not on a blind timer:

- **Edge-triggered** — a save fires once per typing/session burst rather than
  on every keystroke or on a fixed clock tick.
- **Atomic checkpoint** — each save is a single SQLite transaction: workspace
  state, scrollback snapshots, and notes are committed together or not at all.
- **Shutdown flush** — a final checkpoint is written right before teardown.

A checkpoint is a *version* of the workspace state (a generation), not a new
window. Restarting never grows the window count in the database.

## Terminal behavior

- Each pane keeps its own canonical `command_history` (capped at 1000 entries).
- `clear` in the shell is a screen-state operation and does **not** erase
  command history or transcripts.
- Scrollback is preserved across restarts through the VTE snapshot mechanism
  ([VTE extension](patches/vte/README.md)).

## Notes

- Notes autosave through the same unified pipeline.
- Opening a text file from the file tree opens it in a note tab.
- Markdown rendering, preview, and export are covered in
  [Notes](usage/notes.md) and [Export](usage/export.md).