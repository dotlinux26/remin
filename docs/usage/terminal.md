# Terminal

Each terminal tab (and each split pane inside it) is a real VTE shell session.

## Splitting panes

| Shortcut | Action                      |
|----------|-----------------------------|
| `Alt+V`  | split the focused pane vertically |
| `Alt+H`  | split the focused pane horizontally |
| `Alt+K`  | close the focused pane/tab  |

Recursive splits are supported — a pane can be split again, forming a pane
tree.

## What survives a restart

Per pane, Remin persists its **canonical state**, not a replay of output:

- working directory (resolved from OSC7, falling back to `/proc`, then the
  cached value, then `$HOME`)
- columns/rows
- the **full scrollback snapshot** via the VTE extension
  ([snapshot](../patches/vte/README.md))
- per-pane command history (see [history](history.md))

On restore, the pane is rebuilt in this order: configure size →
restore outside snapshot → display-only CR-LF → spawn the shell. The result is
behavioural continuity — the shell continues inside the terminal it was using
before the app exited.

## clear and scrollback

`clear` is a **screen-state operation**. It clears the visible screen but does
**not** erase command history or transcripts. Canonical history is owned by
Remin (per pane), not by `~/.bash_history`.

## Input handling

- **↑/↓ arrow keys live in the shell** — they are not hijacked by Remin for
  history navigation.
- The History panel inserts a command into the input line without executing it
  ([history](history.md)).
- OSC 7 is honored for working-directory tracking.