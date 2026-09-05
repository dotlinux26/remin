<p align="center">
  <img src="remin-logo.svg" width="96" height="96" alt="Remin logo" />
</p>

<h1 align="center">Remin</h1>

<p align="center">
  <strong>Remember your work.</strong>
</p>

<p align="center">
  <a href="#features"><img alt="Linux native" src="https://img.shields.io/badge/OS-Linux-blue?style=flat-square&labelColor=%234f46e5"></a>
  <a href="#build"><img alt="C++" src="https://img.shields.io/badge/C%2B%2B-20-blueviolet?style=flat-square"></a>
  <a href="#stack"><img alt="GUI" src="https://img.shields.io/badge/GUI-GTK4%20%2F%20VTE-cyan?style=flat-square"></a>
  <a href="#license"><img alt="License" src="https://img.shields.io/badge/License-MIT-green?style=flat-square"></a>
</p>

<p align="center">
  A <strong>Linux-native CLI workspace application</strong> that saves, restores,
  and carries your terminal workspace — windows, tabs, panes, command history,
  and scrollback — across time and machines.
</p>

<p align="center">
  <i>Think "git stash", but for your terminal workspace.</i>
</p>

---

```
Workspace
 └── Windows
      └── Tabs
           └── Panes
                └── Shell sessions
```

## Features

- **Workspace Engine** — `Workspace → Window → Tab → Pane`, GUI/CLI/IPC agnostic
- **Edge-triggered autosave** — flushes once per typing burst, never a blind timer
- **SQLite storage** — one canonical `remin.db`, transactional, crash-safe
- **Exact layout restore** — split panes with their ratios come back as they were
- **Linux PTY** — `forkpty()` through a `PTYProvider` abstraction
- **Single binary** — `remin`, one build, three frontends (GUI / CLI / IPC)
- **Text-first UI** — no icon soup; navigate by words, spacing, and keyboard

## Stack

| Component  | Choice                                |
|------------|---------------------------------------|
| Language   | C++20                                 |
| GUI        | GTK4 + gtkmm4 + VTE GTK4 + libadwaita |
| Storage    | SQLite (canonical) + nlohmann/json    |
| PTY        | forkpty() via PTYProvider abstraction |
| IPC        | Unix domain socket (CLI ↔ GUI)        |

Linux-first, MIT.

## Build

Requirements: CMake ≥ 3.24, a C++20 compiler, and the GTK/VTE dev packages
(`gtkmm-4.0`, `vte-2.91-gtk4`, `libadwaita-1`, `librsvg2-dev`).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The single binary is `build/src/app/remin`.

## CLI

```bash
remin gui                      # Launch the GUI workspace app
remin workspace list
remin workspace create <name>
remin workspace open <id>
remin workspace close
remin window add <title>
remin window rename <id> <name>
remin snapshot create
```

## Layout

```
src/
├── app/       — single `remin` binary entry
├── core/      — Workspace Engine (GUI/CLI/IPC agnostic)
├── storage/   — SQLite backend
├── terminal/  — PTY + shell + event loop
├── ipc/       — Unix domain socket
├── gui/       — GTK/VTE frontend
└── cli/       — CLI frontend + request dispatcher
```

## Documentation

- `SPEC.md` — product spec
- `implement-note-1.md` — implementation plan & decisions
- [`docs/architecture/`](docs/architecture/) — how the code is built (overview,
  workspace model, storage, autosave & locking, terminal/PTY, GUI, IPC & CLI)
- [`docs/protocols/`](docs/protocols/) — wire/protocol specs
- [`docs/design/`](docs/design/) — UI principles
- [`docs/decisions/`](docs/decisions/) — ADRs

## License

MIT
