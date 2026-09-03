# Architecture Overview

Remin is a **Linux-native CLI workspace application**: save, restore, and carry
your terminal workspace (windows, tabs, panes, history, scrollback) across time
and machines.

## A clean-core design

The central idea is that the **core is GUI/CLI/IPC agnostic**. Every frontend
(GUI, CLI, and the IPC server embedded in the GUI process) drives the *same*
`WorkspaceCore` API. There is no terminal logic inside the UI, and no UI logic
inside the domain.

## View → Controller → Core → Storage/Runtime

Within the GUI, data and control flow downhill through exactly one boundary:

```
View (GTK widgets) → SessionController → WorkspaceCore → Storage / Runtime
```

- **View** — `MainWindow` + `TabView`s. GTK/VTE/Markdown widgets. Never the
  source of truth.
- **SessionController** — orchestrates multi-step application operations
  (open/close/rename/split/note/snapshot) between the UI and the domain.
- **Core** — holds state and enforces invariants.
- **Storage/Runtime** — SQLite + PTY/md4c adapters.

Without the controller layer, UI commands would accumulate in either
`MainWindow` or `WorkspaceCore` and turn one of them into a God Object. GTK/VTE
stays strictly in the View layer; it never owns pane/terminal/note state.

```
                 ┌──────────────────────────────────────────┐
                 │                 Frontends                 │
                 │  ┌───────┐   ┌───────┐   ┌─────────────┐  │
                 │  │  GUI  │   │  CLI  │   │  IPC server │  │
                 │  │(gtkmm)│   │(remin)│   │ (UDS socket)│  │
                 │  └───┬───┘   └───┬───┘   └──────┬──────┘  │
                 └──────┼───────────┼──────────────┼─────────┘
                        └───────────┼──────────────┘
                                    ▼
                 ┌──────────────────────────────────────────┐
                 │              WorkspaceCore                │
                 │   Workspace / Window / Tab / Pane model   │
                 │        events · snapshots · dirty         │
                 └───────────────┬──────────────────────────┘
                                 ▼
                 ┌──────────────────────────────────────────┐
                 │      Storage  (interface)                 │
                 └───────┬──────────────┬───────────────────┘
                         ▼              ▼
                 ┌─────────────┐  ┌─────────────────────┐
                 │   SQLite    │  │ PTYProvider (Linux) │
                 │ canonical   │  │ forkpty + poll loop │
                 └─────────────┘  └─────────────────────┘
```

## Process model

- **No daemon in V1** (ADR-0006). The **GUI process is the single authority**.
  It owns `WorkspaceSession`: storage, core, workspace lock, and autosaver.
- A `remin <command>` CLI process is *short-lived*: it opens the same storage,
  drives `WorkspaceCore` directly, prints JSON, and exits.
- Because the last writer wins and SQLite gives us locking, the CLI and GUI can
  coexist on the same database. Embedding the IPC server inside the GUI lets
  future CLI→GUI forwarding target a live session.

## Build-time divisions

Each area is a static library so the dependency direction is explicit and
enforceable:

| Library        | Depends on            | Role                                   |
|----------------|-----------------------|----------------------------------------|
| `remin_core`   | —                     | domain model, engine, autosave, lock   |
| `remin_storage`| `remin_core`, sqlite  | SQLite persistence                     |
| `remin_terminal`| `remin_core`          | PTY, shell, event loop                 |
| `remin_gui`    | core, storage, terminal, GTK/VTE, md4c | GUI frontend + session + notes |
| `remin_cli`    | `remin_core`          | CLI frontend + request dispatcher      |
| `remin_ipc`    | `remin_core`          | Unix domain socket client/server       |

The final product is a **single binary** `remin` (built only when GUI support is
present); otherwise it is a headless CLI binary.

## Data flow (one command)

1. `remin workspace create pentest-lab`
2. `main` builds `SqliteStorage` + `WorkspaceCore` (`ensure_core`).
3. `RequestDispatcher` converts the subcommand into a JSON request.
4. `WorkspaceCore::create_workspace` mutates the in-memory `Workspace`, persists
   it, and emits a `WorkspaceOpened` event.
5. The CLI prints the JSON result.

In the GUI, the same steps happen inside the `WorkspaceSession`, with events
feeding the autosaver instead of stdout.

## Realm of this architecture

- **Core** (`src/core/`): model + engine, serialization, autosave, lock.
- **Storage** (`src/storage/`): the SQLite backend behind the `Storage` contract.
- **Terminal** (`src/terminal/`): the PTY abstraction used by the GUI.
- **GUI** (`src/gui/`): the embedded shell, the owning session, terminal/note tabs.
- **IPC/CLI** (`src/ipc/`, `src/cli/`): remote/local driving of the core.

See [`workspace-model.md`](workspace-model.md) next for the domain model, then
[`notes.md`](notes.md) for the note/markdown subsystem.
