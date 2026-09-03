# Remin

> **Remember your work.**

Remin is a **Linux-native CLI workspace application**. It saves, restores, and
carries your terminal workspace — windows, tabs, panes, history, and scrollback —
across time and machines.

```
Workspace
 └── Windows
      └── Tabs
           └── Panes
                └── Shell sessions
```

Think "git stash", but for your terminal workspace.

## Stack

| Component  | Choice                              |
|------------|-------------------------------------|
| Language   | C++20                               |
| GUI        | GTK4 + gtkmm4 + VTE GTK4            |
| Storage    | SQLite (canonical) + nlohmann/json  |
| PTY        | forkpty() via PTYProvider abstraction |
| IPC        | Unix domain socket (CLI ↔ GUI)      |

Cyc: Linux-first, MIT.

## Build

Requires: CMake ≥ 3.24, a C++20 compiler, and the GTK/VTE dev packages
(`gtkmm-4.0`, `vte-2.91-gtk4`, `libadwaita-1`).

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
- `docs/decisions/` — ADRs

## License

MIT
