# Implementation Note #1 — Remin

> Giai đoạn: Kế hoạch triển khai (chốt công nghệ)
> Trạng thái: **APPROVED** — sẵn sàng implement

---

## 1. Tóm tắt concept đã chốt (rev-b)

**Remin** là một **Linux-native workspace application** viết bằng C++.

- **GUI là deliverable chính của V1** — GUI chính là cách Remin thể hiện
  workspace model, không phải phần phụ.
- Nhưng về kiến trúc, **Workspace Engine + Storage phải tồn tại trước GUI**.
  GUI và CLI đều điều khiển **cùng `WorkspaceCore`**.
- V1 = **nested GUI workspace environment** chạy bên trong DE host hiện có
  (một cửa sổ Remin trong Kali/Fedora Xfce...).
- V2 = **Remin Desktop Environment** (Wayland compositor + standalone session).
- **Không** daemon V1, **không** Electron, **không** tự viết terminal
  emulator, **không** icon soup, **không** dependency buffet.

---

## 2. Final decisions (APPROVED)

| ID | Quyết định | Ghi chú |
|----|-----------|---------|
| Language | **C++20** | concepts, ranges, coroutines |
| Build | **CMake + CMakePresets** | |
| Compiler | Clang (dev) + GCC (CI) | |
| GUI | **GTK4 + gtkmm4 + libadwaita** | gtkmm = C++ binding chính thức |
| Terminal | **VTE GTK4** (`vte-2.91-gtk4`) | scrollback/PTY/selection có sẵn |
| Storage | **SQLite** | canonical state, 1 file, transaction ACID |
| JSON | **nlohmann/json** | interchange/export/debug, KHÔNG phải storage |
| PTY | **PTYProvider** abstraction + Linux `forkpty()` | |
| I/O | **poll()** trước, đổi `epoll` sau khi profiling | qua `EventLoop` abstraction |
| IPC | **Unix Domain Socket** | CLI ↔ GUI process |
| Linking | **Dynamic** | ecosystem-native Linux app |
| License | **MIT** | |
| Terminal compat | **Remin owns terminal** — host `$TERM` irrelevant | Remin set `$TERM` cho child shell |
| User model | Single-user UX, **XDG-compliant storage** | |
| Binary | **1 binary `remin`**, nhiều internal CMake target | |
| Daemon | **Không** V1 | GUI process là authority |
| Deps | system via `pkg-config`, embedded pinned under `third_party/` | |
| Tests | CTest + test executables nhỏ; GoogleTest chỉ khi suite lớn | |
| CI | format → configure → build → unit → ASan → UBSan → PTY integration | |

### Không dùng

```
Qt, SDL, Electron, Boost, spdlog, fmt, CLI11, tmux,
libvterm, JSON tự viết, ANSI parser tự viết, icon framework, font bundle
```

---

## 3. Stack thành phần

**Runtime / system (distro cung cấp):**

```
GTK4, gtkmm4, VTE GTK4, libadwaita, GLib, Pango, Graphene
```

**Embedded (vendor, pin version + license):**

```
third_party/sqlite/    (sqlite3.c, sqlite3.h, LICENSE)
third_party/nlohmann/  (json.hpp, LICENSE)
```

---

## 4. Architecture

### 4.1 Tổng quan

```
                         ┌───────────────┐
                         │    remin      │
                         │ single binary │
                         └───────┬───────┘
                                 │
                    ┌────────────┴────────────┐
                    │                         │
                   CLI                       GUI
                    │                         │
                    └────────────┬────────────┘
                                 │
                           Unix IPC / direct
                                 │
                         ┌───────▼───────┐
                         │ WorkspaceCore │
                         └───────┬───────┘
                                 │
          ┌──────────────┬───────┼───────────┐
          ↓              ↓       ↓           ↓
       Storage        Terminal   IPC       Snapshot
          │              │
       SQLite           VTE
                         │
                       PTY
                         │
                    bash/zsh/fish
```

### 4.2 Core độc lập với GUI/IPC/CLI

```
                    WorkspaceCore
                  /       |       \
                 /        |        \
               GUI       CLI       IPC
                │         │         │
                └─────────┴─────────┘
```

Mọi frontend gọi cùng API:

```cpp
core.rename_window(window_id, "GitLab Audit");
```

```bash
remin window rename ...
```

```json
{ "method": "window.rename", ... }
```

Không cái nào bypass Core.

### 4.3 Storage: SQLite = canonical, JSON = interchange

```
                     Remin
                       │
                 WorkspaceCore
                       │
                ┌──────┴──────┐
                │             │
             Storage        Export
                │             │
             SQLite       nlohmann/json
                │             │
         canonical state    .remin
```

### 4.4 CLI hai chế độ

**GUI đang chạy:**

```
CLI → IPC → GUI/Core → SQLite
```

**GUI chưa chạy (headless):**

```
CLI → Core → SQLite
```

---

## 5. Kế hoạch theo pha

> Core/Storage trước GUI; GUI là deliverable chính V1.

### Phase 1 — Workspace Engine + Storage Contract
- [ ] CMake skeleton + CMakePresets + CI pipeline
- [ ] Core data model: `Workspace`, `Window`, `Tab`, `PaneTree`, `Pane`
- [ ] `WorkspaceCore` (rename/create/focus/close...)
- [ ] Pane tree: `Split(H|V, ratio)` layout → restore exact layout
- [ ] Storage: SQLite schema + repository layer
- [ ] Snapshot model + command history model
- [ ] Unit tests (CTest)

### Phase 2 — PTY & Terminal
- [ ] `terminal/pty/`: `PTYProvider` + `PTYProviderLinux` (`forkpty`)
- [ ] Shell detection (`$SHELL` / `/etc/passwd`)
- [ ] `EventLoop` (poll) cho PTY I/O
- [ ] VTE integration test (headless CI)

### Phase 3 — GUI (Phase chính)
- [ ] gtkmm4 + libadwaita app shell
- [ ] VTE widget per pane
- [ ] Window / Tab / Pane layout renderer
- [ ] Text-based navigation UI (không icon)
- [ ] Theme layer (light/dark CSS semantic variables)
- [ ] Nested: Remin là 1 cửa sổ trong DE host

### Phase 4 — IPC + CLI
- [ ] Unix domain socket server (trong GUI process)
- [ ] IPC client (từ CLI)
- [ ] CLI subcommands: `remin gui / workspace / window / tab / pane / snapshot / history`
- [ ] Protocol JSON

### Phase 5 — V1 hardening
- [ ] Autosave/checkpoint scheduler (dirty → Enter → 10s idle → shutdown)
- [ ] Workspace lock/ownership (logic level, không file lock thủ công)
- [ ] Error handling + exit codes
- [ ] Integration PTY test
- [ ] Packaging (distro packages trước)

---

## 6. Pane tree / Workspace model cuối

```
Workspace
│
├── metadata
│
├── Windows[]
│   │
│   ├── metadata
│   │
│   └── Tabs[]
│       │
│       └── PaneTree
│           │
│           ├── Split(H|V, ratio)
│           │   ├── Pane
│           │   └── Pane
│           │
│           └── Pane
│
├── snapshots[]
└── history[]
```

Không lưu pane dạng flat rồi tự đoán layout — lưu cây `orientation / ratio / children` để restore **exact layout**.

---

## 7. Distinction: History ≠ Scrollback

```
Shell History     Terminal Scrollback     Workspace Snapshot
bash/zsh history     VTE buffer             Remin state
```

```
Pane
├── command_history[]
├── scrollback
└── terminal_state
```

---

## 8. Snapshot model (VTE-only renderer, Remin-model snapshot)

Không snapshot toàn bộ VTE object. Snapshot là Remin model:

```
PaneState
├── cwd
├── shell
├── environment policy
├── terminal size
├── scrollback
├── command metadata
└── interrupted command
```

---

## 9. Process restore (giới hạn UNIX)

```
snapshot → new PTY → new shell
```

V1 **không** làm checkpoint/CRIU (không freeze process, save RAM, restore).

---

## 10. Autosave / checkpoint rules

```
user input      → mark dirty
Enter           → immediate checkpoint
10s idle        → checkpoint
shutdown        → final checkpoint
```

Process output **không** trigger save (ffuf spam 50k lines → KHÔNG save 50k lần).

---

## 11. Storage layout

```
$XDG_DATA_HOME/remin/remin.db     (1 database)
```

Schema:

```
workspaces / windows / tabs / panes / pane_layout /
terminal_state / command_history / snapshots / snapshot_items
```

---

## 12. XDG directories

```
$XDG_DATA_HOME/remin/   → ~/.local/share/remin/
$XDG_CONFIG_HOME/remin/ → ~/.config/remin/
$XDG_STATE_HOME/remin/  → ~/.local/state/remin/
$XDG_CACHE_HOME/remin/  → ~/.cache/remin/
```

Single-user UX. Không `/var/lib/remin`.

---

## 13. Repo layout (chốt)

```
remin/
├── CMakeLists.txt
├── CMakePresets.json
├── LICENSE
├── README.md
├── CONTRIBUTING.md
├── SECURITY.md
├── CODE_OF_CONDUCT.md
│
├── cmake/
│   ├── Dependencies.cmake
│   ├── CompilerWarnings.cmake
│   ├── Sanitizers.cmake
│   └── Install.cmake
│
├── src/
│   ├── app/            application.cpp/.hpp
│   ├── core/           workspace/ window/ tab/ pane/ snapshot/ commands/
│   ├── storage/        sqlite/ migrations/ repository/
│   ├── terminal/       pty/ shell/ terminal_state/
│   ├── ipc/            server/ client/ protocol/
│   ├── gui/            window/ workspace/ tab/ pane/ terminal/ dialogs/ theme/
│   └── cli/            commands/ parser/
│
├── tests/              unit/ integration/ fixtures/
├── resources/          styles/ (light.css, dark.css) schemas/
├── third_party/        sqlite/ nlohmann/
└── docs/               architecture/ design/ protocols/ decisions/
```

**1 executable `remin`**, nhiều internal libraries:

```
remin_core, remin_storage, remin_terminal, remin_ipc,
remin_gui, remin_cli → link thành `remin`
```

> single binary ≠ single CMake target.

---

## 14. Dependencies management (chốt)

```cmake
# cmake/Dependencies.cmake
find_package(PkgConfig REQUIRED)

pkg_check_modules(GTKMM   REQUIRED gtkmm-4.0)
pkg_check_modules(VTE     REQUIRED vte-2.91-gtk4)
pkg_check_modules(ADWAITA REQUIRED libadwaita-1)
```

- System GUI stack → distro cung cấp (GTK4, gtkmm4, VTE, libadwaita, GLib, Pango, Graphene).
- Embedded → vendor trong `third_party/` (SQLite, nlohmann/json).
- **Không** FetchContent cho 15 thư viện; **không** `GIT_TAG main` không pin.

---

## 15. ADR (Architecture Decision Records)

```
docs/decisions/
├── ADR-0001-cpp20.md
├── ADR-0002-gtk4.md
├── ADR-0003-vte.md
├── ADR-0004-sqlite-storage.md
├── ADR-0005-ipc.md
├── ADR-0006-no-daemon-v1.md
└── ADR-0007-no-icon-library.md
```

---

## 16. GUI style principles

- **Không** icon library, không Font Awesome, không emoji, không toolbar 30 icon.
- Navigation dựa vào: **text + spacing + alignment + keyboard + subtle borders + selection state**.
- Default = **Light**, có Dark. Không hard-code màu trong C++; dùng semantic variables / CSS theme layer:

```
--bg, --surface, --surface-hover, --border,
--text, --text-muted, --accent, --terminal-bg, --terminal-fg
```

- Terminal font = system monospace (không bundle font V1).

UI hierarchy:

```
┌─────────────────────────────────────────────┐
│ Remin   GitLab Audit                 3      │
├─────────────────────────────────────────────┤
│ Recon    Source    Exploit    Notes         │
├─────────────────────────────────────────────┤
│                 terminal                    │
├─────────────────────────────────────────────┤
│ ~/gitlab-audit                         bash │
└─────────────────────────────────────────────┘
```

---

## 17. Version roadmap

**V1 (nested GUI workspace):**
```
Remin Core, Linux PTY, Workspace model, GUI workspace,
Window/Tab/Pane, History, Search, Snapshot/Restore, nested env
```

**V2 (Remin DE):**
```
Wayland-native compositor, standalone session, login/session integration
```

---

*APPROVED. Tiến hành implement từ Phase 1.*
