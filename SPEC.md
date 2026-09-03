# Remin

> Remember your work.

**Remin** — A CLI workspace manager for Linux. Save, restore, and carry your
terminal sessions, windows, tabs, panes, and history across time and machines.

---

## 1. Overview

Remin lets you **snapshot** your entire CLI working environment — open terminals,
running processes, scrollback history, working directories, environment state —
and **restore** it later, either on the same machine or a different one.

Think of it as "git stash" but for your terminal workspace.

### 1.1 Goals

- Save and restore complete terminal workspaces (windows, tabs, panes, history)
- Run standalone or nested inside existing desktop environments (Kali Xfce,
  Fedora Xfce, KDE, etc.)
- Lightweight native Linux binary, no Electron
- Fast, POSIX-native PTY/process management
- Clean architecture: core logic is GUI/CLI agnostic
- Small binary, minimal dependencies

### 1.2 Non-Goals (v1)

- macOS/Windows support (core is portable, but v1 is Linux-only)
- Remote session sync (planned for future)
- Container/VM integration (planned for future)

---

## 2. Product Identity

| Field        | Value                                      |
|--------------|--------------------------------------------|
| Name         | Remin                                      |
| Tagline      | Remember your work.                        |
| Alt tagline  | Your CLI work, remembered.                 |
| License      | MIT                                        |
| Language     | C++ (Linux-first)                          |
| Binary name  | `remin`                                    |
| Config dir   | `~/.config/remin/`                         |
| Data dir     | `~/.local/share/remin/`                    |

---

## 3. Architecture

### 3.1 Directory Structure

```
remin/
├── core/
│   ├── workspace/          # Workspace lifecycle (create, save, load, delete)
│   ├── window/             # Window management (create, focus, close, layout)
│   ├── tab/                # Tab management within a window
│   ├── pane/               # Pane splitting within a tab
│   ├── history/            # Command history capture and replay
│   ├── snapshot/           # Workspace snapshot (serialize state to disk)
│   └── restore/            # Workspace restore (deserialize + re-launch)
│
├── terminal/
│   ├── pty/                # PTY creation and I/O (forkpty, POSIX)
│   ├── shell/              # Shell detection, env capture
│   └── scrollback/         # Scrollback buffer management
│
├── ui/
│   ├── gui/                # GUI frontend (terminal emulator + workspace UI)
│   └── cli/                # CLI frontend (remin save/restore/list/...)
│
├── platform/
│   └── linux/              # Linux-specific: PTY, X11/Wayland, process mgmt
│
├── storage/
│   ├── snapshot/           # Snapshot file format (JSON/binary)
│   └── index/              # Workspace index DB
│
└── tools/
    ├── remin               # Main CLI binary
    └── remin-gui           # GUI binary (optional, v1 may skip)
```

### 3.2 Dependency Diagram

```
                 Linux Kernel
                      │
             ┌────────▼────────┐
             │  Wayland / X11  │
             └────────┬────────┘
                      │
             ┌────────▼────────┐
             │  Host Compositor│
             │ (KDE/Xfce/GNOME)│
             └────────┬────────┘
                      │
               ┌──────▼──────┐
               │  Remin GUI  │   ← optional nested environment
               │ (compositor)│
               └──────┬──────┘
                      │
           ┌──────────┼──────────┐
                  ▼                ▼                 ▼
       Terminal     Apps       Shell
```

### 3.3 UI Architecture

```
GUI ───────┐
           ├──> Remin Core ──> PTY ──> bash/zsh/fish
CLI ───────┘
```

Core C++ logic does not know whether it is driven by GUI or CLI.

### 3.4 Nested Mode

Remin can run as a **nested desktop environment** inside an existing session:

```
┌──────────────────────────────────────┐
│             Host Desktop             │
│                                      │
│  ┌────────────────────────────────┐  │
│  │       REMIN ENVIRONMENT        │  │
│  │                                │  │
│  │  [Terminal] [Files] [Apps]     │  │
│  │                                │  │
│  └────────────────────────────────┘  │
│                                      │
└──────────────────────────────────────┘
```

Can also run split-screen alongside host desktop applications:

```
┌──────────────────────────────────────┐
│          Host Desktop (Kali)         │
│  terminal   browser   file manager   │
├──────────────────────────────────────┤
│          Remin Environment           │
│  ┌────────┐ ┌────────┐ ┌──────────┐  │
│  │ Shell  │ │ Files  │ │ Monitor  │  │
│  └────────┘ └────────┘ └──────────┘  │
└──────────────────────────────────────┘
```

---

## 4. Workspace Model

### 4.1 Workspace

A workspace is the top-level unit:

```
Workspace
├── metadata
│   ├── name              (user-friendly name)
│   ├── id                (unique identifier)
│   ├── created_at        (timestamp)
│   ├── last_saved_at     (timestamp)
│   ├── working_directory (primary CWD)
│   ├── environment       (env vars snapshot)
│   └── tags              (optional user tags)
│
├── windows[]
│   ├── window_id
│   ├── geometry           (x, y, width, height)
│   ├── tabs[]
│   │   ├── tab_id
│   │   ├── panes[]
│   │   │   ├── pane_id
│   │   │   ├── layout     (split ratio)
│   │   │   ├── shell_pid
│   │   │   ├── cwd
│   │   │   ├── scrollback (captured buffer)
│   │   │   ├── history    (command history)
│   │   │   └── env        (pane-specific env)
│   │   └── focus_pane_id
│   └── focus_tab_id
│
├── focus_window_id
├── layout                 (tiling/floating config)
└── snapshots[]
    ├── snapshot_id
    ├── timestamp
    ├── state_file         (path to serialized state)
    └── size_bytes
```

### 4.2 Snapshot Format

Snapshots are stored as JSON + binary scrollback data:

```
~/.local/share/remin/workspaces/
├── index.json                     # workspace index
└── <workspace-id>/
    ├── meta.json                  # workspace metadata
    ├── state.json                 # full state tree
    ├── scrollback/
    │   ├── <pane-id>.scroll       # binary scrollback data
    │   └── ...
    └── history/
        └── <pane-id>.history      # command history per pane
```

---

## 5. Core Features (v1)

### 5.1 Workspace Operations

| Command | Description |
|---------|-------------|
| `remin save [name]` | Snapshot current workspace to disk |
| `remin restore <id\|name>` | Restore a saved workspace |
| `remin list` | List all saved workspaces |
| `remin delete <id\|name>` | Delete a saved workspace |
| `remin export <id> <path>` | Export workspace to a portable file |
| `remin import <path>` | Import workspace from a file |

### 5.2 Save

When saving, Remin captures:

1. All open terminal windows, their geometry
2. All tabs and panes within each window
3. Working directory of each pane
4. Shell type (bash/zsh/fish/etc.)
5. Environment variables (filtered, no secrets)
6. Scrollback buffer of each pane
7. Command history of each pane
8. Current focus state (which window/tab/pane is focused)

### 5.3 Restore

When restoring, Remin:

1. Reads the snapshot from disk
2. Creates windows/tabs/panes matching the saved layout
3. Launches shells in the saved working directories
4. Restores scrollback buffers
5. Restores command history (readline/HISTFILE integration)
6. Optionally replays recent commands (opt-in)

### 5.4 History Replay

Optional feature (disabled by default):

```bash
remin restore <workspace> --replay
```

Replays the last N commands in each pane after restore. Useful for restoring
context ("what was I doing?").

---

## 6. Platform Requirements

### 6.1 Minimum (v1)

- Linux kernel 5.4+
- glibc 2.31+ or musl
- POSIX PTY support (forkpty)
- xterm-compatible terminal for CLI mode
- Wayland or X11 for GUI/nested mode

### 6.2 Dependencies (v1)

Core: **zero external dependencies** — pure C++17, POSIX, Linux syscalls.

Optional:
- `libvterm` — terminal emulation library (for GUI mode)
- `ncurses` — terminal UI for CLI mode
- Wayland client libs — for nested compositor mode

### 6.3 Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
# binary: build/remin
```

---

## 7. CLI Interface

### 7.1 Commands

```
remin <command> [options]

Commands:
  save [NAME]          Save current workspace
  restore <ID|NAME>    Restore a saved workspace
  list [--json]        List saved workspaces
  delete <ID|NAME>     Delete a saved workspace
  export <ID> <PATH>   Export workspace to file
  import <PATH>        Import workspace from file
  info <ID|NAME>       Show workspace details
  diff <ID1> <ID2>     Compare two workspaces

Options:
  --json               Output in JSON format
  --quiet, -q          Suppress output
  --verbose, -v        Verbose output
  --config <PATH>      Use custom config file
  --help, -h           Show help
  --version, -v        Show version
```

### 7.2 Examples

```bash
# Save current workspace as "pentest-lab"
remin save pentest-lab

# List all saved workspaces
remin list

# Restore workspace
remin restore pentest-lab

# Restore and replay last 10 commands per pane
remin restore pentest-lab --replay

# Export for sharing
remin export pentest-lab ~/pentest-lab.remin

# Import on another machine
remin import ~/pentest-lab.remin

# Show what's in a workspace
remin info pentest-lab
```

---

## 8. Configuration

### 8.1 Config File

`~/.config/remin/config.toml`

```toml
[general]
default_shell = "/bin/bash"
auto_save_interval = 300          # seconds, 0 = disabled
max_snapshots_per_workspace = 10
confirm_on_restore = true

[history]
capture_scrollback = true
max_scrollback_lines = 10000
capture_env = false               # don't capture env vars by default
replay_on_restore = false         # opt-in via --replay

[storage]
workspace_dir = "~/.local/share/remin/workspaces"
snapshot_format = "json"          # json or binary (binary = smaller)

[gui]
nested_mode = true                # allow running as nested desktop
theme = "dark"
font = "monospace"
font_size = 12

[platform]
pty_backend = "posix"             # posix (forkpty)
```

---

## 9. Future Roadmap

> Rev-a: **GUI là V1.** DE/compositor độc lập là V2.

### v1.0 (current)
- Remin Core (Workspace Engine: Workspace/Window/Tab/Pane, GUI/CLI agnostic)
- Linux PTY integration
- Workspace model + snapshot/restore
- **GUI workspace** (VTE-based terminal emulator, nested trong DE host)
- Window / Tab / Pane layout management
- History capture + Search
- CLI frontend (điều khiển cùng WorkspaceCore)

### v2.0
- Wayland-native compositor
- Standalone session (login → Remin)
- Login/session integration
- Full Remin desktop environment
- Workspace sharing (export/import)
- Plugin system (custom save hooks, auto-save triggers)

### v3.0
- Remote session sync (SSH-based)
- Cross-machine workspace transfer
- Container integration (save/restore inside Docker/Podman)
- Workspace templates
- AI-powered context restore ("what was I doing?")

---

## 10. Branding

### 10.1 Logo Concept

Simple, clean: the word **remin** in a monospace font, lowercase.
No icons needed for v1. The logo IS the wordmark.

### 10.2 Tone

- Technical but approachable
- Not trying to be "hacker aesthetic"
- Clean, minimal, functional
- Think: "git for terminal workspaces"

### 10.3 Package Names

```
remin              # main binary / package name
libremin           # if we ever expose a library
remin-gui          # GUI frontend (if separate binary)
```

---

## 11. Comparison

| Feature | tmux/screen | Remin |
|---------|-------------|-------|
| Session persistence | attach/detach | full snapshot to disk |
| Multi-window/tab | yes | yes |
| Cross-machine | no (needs SSH) | yes (export/import) |
| History preservation | limited | full scrollback + history |
| Restore context | manual | automated |
| Nested environment | no | **yes (v1)** — nested GUI workspace trong DE host |
| GUI integration | no | **yes (v1)** — GUI workspace |
| Portable snapshots | no | yes (.remin files) |

---

*Remin — Remember your work.*
