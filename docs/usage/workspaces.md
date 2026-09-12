# Workspaces

Remin's mental model is a single tree:

```text
Workspace
 └── Windows
      └── Tabs
           └── Panes
                └── Shell sessions
```

## Windows

The core engine models multiple windows and **persists each one** across
restarts. The V1 GUI is a **single-window** interface: it attaches to the most
recently used window and shows it full-screen. Multiple on-screen windows are a
V2 feature.

Rules that hold no matter how many windows exist:

- Autosave/checkpoints only *update* the state of existing windows — a save
  never creates a new window.
- Restarting never increases the window count.
- Windows are identified by a stable id; reopening the app restores the same
  window entities, not new copies.

## Tabs and panes

A tab is a **surface**. Today the surfaces are:

- **Terminal tab** — holds one or more shell panes (splits).
- **Note tab** — a markdown editor (+ optional preview split).

| Shortcut      | Action                                  |
|---------------|-----------------------------------------|
| `Ctrl+T`      | new terminal tab                        |
| `Ctrl+Shift+N`| new note tab                            |
| `Ctrl+W`      | close the active tab                    |
| `Alt+V`       | split terminal pane vertically          |
| `Alt+H`       | split terminal pane horizontally        |
| `Alt+K`       | close the focused terminal pane/tab     |

## What persists

At shutdown (and every autosave) Remin captures:

- the window/tab/pane structure and split ratios,
- terminal state — working directory, size, and the **full scrollback snapshot**,
- open notes and their content,
- the directory tree and file tree state,
- window geometry and focus.

On startup the last valid checkpoint is reconstructed first; only when no valid
state exists is a fresh default window created.