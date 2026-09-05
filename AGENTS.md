# Remin — Agent Context & Vision

> This file is the **single source of truth** for Remin's long-term vision,
> architecture decisions, open issues, and active work. Every agent working on
> this codebase should read this first.

---

## Vision: CLI-First Workspace Platform

Remin is **not** a terminal emulator with tabs. It is a **workspace platform**
for CLI/desktop-oriented tools, where each plugin is a new "work surface".

### Design rule (the project's primary rule)

> **Remin does not need to be "prettier"; it needs *less clutter* and everything
> in the right place.**

Concretely, every change must move toward:
- **Simple, compact, calm, professional, consistent, native.**
- **Light-first**, with dark mode as a first-class, genuinely-dark theme.
- **No excessive decoration** — no decorative cards, heavy borders, gradients,
  oversized rounded containers, shadows, icon soup, or emoji.
- **A control visually exists only when its affordance requires it.** No button
  chrome where a plain text/icon control suffices.
- **No arbitrary pixel hacks** — no magic margins/offsets, no font-shrinking to
  solve overflow, no ellipsis as a substitute for navigation, no scroll-polling.
- **Structural honesty:** a control's look must come from *one shared
  style/component*, never hand-patched widget-by-widget.

If a subsystem is structurally wrong, **refactor it** — do not patch symptoms.

### Evolution roadmap

```text
V1: Terminal + Notes + Workspace/History/Snapshot
V2: Terminal + Notes + Browser
V3: Terminal + Notes + Browser + C2 + BloodHound + Docker/K8s + Git + ...
```

### Example engagement workspace

```text
GitLab Audit
├── Terminal
├── Browser
├── Notes
├── BloodHound
└── C2
```

### Workspace Snapshot (long-term)

```text
Workspace Snapshot
├── windows
├── tabs
├── pane tree
├── terminal state
├── note documents
├── browser state
├── plugin state
└── metadata
```

---

## Architecture: Work Surface Abstraction

Every tab is a **Surface** — the same abstraction regardless of domain:

```text
Tab
└── Surface
      ├── TerminalSurface
      ├── NoteSurface
      ├── BrowserSurface
      └── PluginSurface
```

GUI doesn't know what "C2 tab" is. It only knows:

```text
Surface
├── title
├── icon
├── state
├── view
└── lifecycle
```

### Current TabView interface (V1)

```cpp
class TabView {
public:
    virtual ~TabView() = default;
    virtual TabKind kind() const = 0;
    virtual const std::string& title() const = 0;
    virtual void set_title(const std::string&) = 0;
    virtual void activate() = 0;
    virtual void deactivate() = 0;
    virtual bool focus_search() = 0;
};
```

This will evolve into the `ISurface` contract.

---

## Plugin Architecture (design-first, V1+)

### Core manages

```text
Window / Tab / Pane / Workspace / Snapshot / History / Lifecycle / Plugin registry
```

### Plugin manages

```text
domain state / domain UI / domain actions / domain persistence / domain runtime
```

### Plugin API contract

```cpp
class IPlugin {
public:
    virtual ~IPlugin() = default;
    virtual PluginInfo info() const = 0;
    virtual void activate(PluginContext&) = 0;
    virtual void deactivate() = 0;
};

class PluginContext {
public:
    WorkspaceService& workspace();
    StorageService& storage();
    TerminalService& terminal();
    UiService& ui();
};
```

Plugins **request capabilities from host**, never access internals directly.

### Plugin manifest

```toml
plugins/
└── plugin-id/
    ├── manifest.toml
    ├── plugin.so
    └── resources/
```

```toml
id = "bloodhound"
name = "BloodHound"
version = "0.1"
api = "1"
```

### Safety: V1 is design-only

- No arbitrary native C++ plugins in-process yet (ABI fragility, crash risk).
- Plugin API/manifest designed early, runtime limited.
- Out-of-process plugins preferred for security tools.

---

## Layered Boundary (the core contract)

```text
View → Controller → Core → Storage/Runtime
```

- **MainWindow** = composition root only (layout, header, tab bar, stack).
- **SessionController** = orchestration between UI and WorkspaceCore.
- **WorkspaceCore** = state + invariants.
- **Autosaver** = lives in WorkspaceSession, not widgets.

---

## Environment & Build

- **OS**: Linux (Kali), Wayland (`WAYLAND_DISPLAY=wayland-0`) + X11 (`DISPLAY=:0`)
- **Compiler**: g++ (C++20)
- **GUI**: gtkmm 4.10.0 / GTK 4.14.5 / VTE 0.76 / md4c 0.4.8
- **Build dirs**: `build/` (release), `build-debug/` (RelWithDebInfo), `build-asan/` (ASan+UBSan)
- **Tests**: 5 ctest suites (all must pass)
- **Remote**: `git@github.com:dotlinux26/remin.git`, branch `main`
- **SSH auth**: Agent forwarding (no password)
- **Sudo password**: `Canh0206@`
- **Entry**: `./remin gui` → `run_gui()` in `src/app/main.cpp:92`
- **Do NOT modify root `logo.svg`**. Runtime copy `resources/logo.svg` may be edited.

### GTK4 API notes

- `Gtk::Popover`: `set_child(Widget&)` / `popup()` / `popdown()`
- `Gtk::GestureClick`: replaces `signal_button_press_event`; `n_press` param for double-click; `set_button(3)` for right-click
- `get_children()` returns range (iterate with `for (auto& w : box->get_children())`)
- `Gtk::Dialog` ctor: `(title, Gtk::Window&, modal)` — NOT `Gtk::Root&`
- `Glib::ustring` ↔ `std::filesystem::path`: use `.raw()` for conversion
- No `GdkEventButton` in GTK4
- `Gdk::Clipboard` via `get_display()->get_clipboard()->set_text(text)`
- `switch->property_active().signal_changed()` NOT `signal_state_set()` (latter needs `->bool`)
- Menu items reference `win.<name>`; actions registered unprefixed in `win` group

---

## UI/UX Status — LTS STABLE (baseline: v0.0.3lts, 2026-09-06)

> UI/UX đã đẹp và ổn định theo đánh giá của user (không còn lỗi). Đây là **baseline
> LTS** — mọi thay đổi sau này phải giữ được chuẩn này, không được phá vỡ nhất quán.
> Nota: các hạng mục polish dưới đây đã hoàn tất.

### 1. Directory Tree View ✅
VS Code-style compact, search/filter real-time, file extension + size, tooltip date
on hover, `Gtk::FileChooserNative` Open File, bắt đầu từ `$HOME`, context menu
(New File/Folder, Rename, Delete, Copy Path), double-click text file → note editor,
folder/file icons từ system theme.

### 2. Broken Button Icons ✅
Symbolic icons inherit foreground color trong cả light + dark, `.remin-toolbar-btn`.

### 3. Toolbar Auto-Show ✅
Toolbar visible ngay trên first launch, `update_toolbar()` sau `new_terminal_tab()`,
compact 32-36px, icon + text label.

### 4. Find Bar Integration ✅
Find (Ctrl+F) / Replace (Ctrl+H) cùng chiều cao toolbar, ESC đóng, tự ẩn/hiện theo
context. **v0.0.3lts**: ESC / xóa find box xóa hết highlight (match + current-match)
ở **mọi** tab qua `TabView::clear_search()` (broadcast, không phụ thuộc signal chain).

### 5. Tab Focus Highlighting ✅
Active tab nổi bật (`.remin-tab` checked), chuyển tab highlight ngay lập tức.

### 6. Terminal Pane Full-Tab ✅
Pane + children `hexpand/vexpand`, lấp đầy từ toolbar tới status bar.

### 7. Line Number Gutter Scroll Sync ✅
Gutter (GtkSourceView line numbers) đồng bộ scroll với editor content.

### 8. History Sidebar Persistence ✅ (UI) — bản chất persistence theo design pipeline
`add_history()` ghi qua SessionController; restore hiển thị từ storage. Persistence
sâu (per-pane canonical, checkpoint atomic) là phần của core feature §4-6 design.

### 9. Dark Theme Button Colors ✅
Dùng `@text` / `@text-muted` / semantic variables, không hardcode.

### 10. Overall UI Polish ✅
VS Code-inspired compact, spacing 4px/8px grid, tnum, border nhẹ, status bar tối giản.

---

## TODO Checklist (active development)

### Core Feature — Workspace Persistence/Recovery Pipeline (current sprint, THE priority)
Design: `docs/design/workspace-persistence-pipeline.md` (đã gate). Không Window History UI / Ctrl+Shift+H lúc này.
- [ ] P1 Domain state + migration (TabKind, NoteTabState, UiState + serialization round-trip)
- [ ] P2 TerminalPane live: capture/restore cwd, cols/rows, per-pane history, interrupted command
- [ ] P3 Atomic checkpoint: SqliteDb txn RAII, checkpoint(reason), autosave→checkpoint, shutdown flush
- [ ] P4 Restore workspace đầy đủ (terminals, notes, dir tree, geometry, focus)
- [ ] P5 Golden acceptance test + report (§16)

### Core Features (secondary / triage sau)
- [ ] Wire ThemeManager into Application
- [ ] Wire menu stubs (note.save/save_as, note.preview)
- [ ] Tab rename + window rename
- [ ] Custom split dialog (rows x cols)
- [ ] Fix sign-conversion warnings (CI clean)

### Plugin Platform (V2+)
- [ ] IPlugin interface design
- [ ] PluginContext service access
- [ ] Plugin manifest loader
- [ ] Plugin lifecycle management
- [ ] BrowserSurface prototype
- [ ] C2 plugin prototype

---

## Architecture Decisions (reference)

| ADR | Decision | Rationale |
|-----|----------|-----------|
| ADR-0001 | C++20 | Modern features, gtkmm bindings |
| ADR-0002 | GTK4 | Native Linux, Wayland, modern toolkit |
| ADR-0003 | VTE | Battle-tested terminal emulation |
| ADR-0004 | SQLite | Embedded, reliable, zero-config |
| ADR-0005 | IPC | CLI ↔ GUI communication |
| ADR-0006 | No daemon (V1) | Simplicity, single-instance lock |
| ADR-0007 | No icon library | Text-first design, minimal icons |

---

## File Map (key source files)

```text
src/gui/
├── application.cpp              # App entry, session init
├── session/
│   ├── session_controller.hpp   # Orchestration layer
│   └── session_controller.cpp
├── window/
│   ├── main_window.hpp          # Composition root
│   ├── main_window.cpp          # Layout, toolbar, find bar, tabs
│   ├── tab_view.hpp             # Base TabView interface
│   ├── terminal_tab_view.hpp    # Terminal tab (splits, sidebar)
│   ├── terminal_tab_view.cpp
│   ├── note_tab_view.hpp        # Note tab (editor + preview)
│   ├── note_tab_view.cpp
│   └── settings_dialog.*        # Settings dialog
├── note/
│   ├── note_editor.hpp          # GtkTextView-based editor
│   ├── note_editor.cpp
│   └── markdown_preview.*       # md4c renderer
├── terminal/
│   ├── terminal_pane.hpp        # VTE wrapper
│   └── terminal_pane.cpp
├── theme/
│   └── theme_manager.*          # CSS loader
└── view/
    └── tab_view.hpp             # TabKind enum

resources/styles/
├── dark.css                     # Dark theme (@define-color palette)
└── light.css                    # Light theme
```

---

## Conventions

- **CSS classes**: `.remin-*` prefix (`.remin-tab`, `.remin-toolbar-btn`, etc.)
- **Actions**: `win.<name>` in menu, registered unprefixed in `win` action group
- **Colors**: never hardcode; use `@accent`, `@text`, `@bg`, `@surface`, `@border`, `@text-muted`
- **Icons**: symbolic icons from system theme; icon-size 16px for buttons, 14px for tabs
- **Spacing**: 4px grid (4, 8, 12, 16, 20, 24)
- **Font**: system default, `tnum` for status areas

---

*Last updated: 2026-09-06*
