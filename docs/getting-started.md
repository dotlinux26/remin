# Getting Started

Remin is a single window that combines a **terminal workspace** and a
**markdown note editor**. Everything you do — tabs, splits, scrollback,
command history, notes, the directory tree — is persisted and restored on the
next launch.

## Launch

```bash
remin gui
```

That's it. On first run Remin creates its data directory and a fresh workspace
with one terminal tab. Every subsequent run restores the workspace from the
last shutdown (or the latest autosave checkpoint).

There is no separate daemon, no project manager to configure, and no setup
wizard. The whole app is one binary.

## The three areas

| Area       | What it is                                              |
|------------|---------------------------------------------------------|
| Terminal   | real shell session(s) using VTE, with splits and tabs   |
| Notes      | markdown editor with a live preview and HTML/PDF export |
| Sidebar    | command history + file tree, toggled per tab            |

## First commands to try

| Shortcut      | Action                                    |
|---------------|-------------------------------------------|
| `Ctrl+T`      | new terminal tab                          |
| `Ctrl+Shift+N`| new note tab                              |
| `Alt+V`       | split the terminal vertically             |
| `Alt+H`       | split the terminal horizontally           |
| `Alt+K`       | close the focused terminal pane/tab       |
| `Ctrl+Shift+H`| open the command-history panel            |
| `Ctrl+P`      | toggle the sidebar                        |
| `Ctrl+F` / `Ctrl+H` | find / find-and-replace              |
| `Ctrl+W`      | close tab                                 |

## Closing

Close the window (or quit the app). Remin writes a final checkpoint just
before teardown, so the next launch restores exactly what you left on screen.

## Next steps

- Read about the [workspace model](usage/workspaces.md).
- Learn what survives a restart in the [terminal guide](usage/terminal.md).
- Try the [markdown editor](usage/notes.md) and [export](usage/export.md).