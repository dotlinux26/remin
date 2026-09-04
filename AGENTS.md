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

## Active UI Issues (current sprint)

These are the **concrete issues** reported by the user. Each must be fixed.

### 1. Directory Tree View (HIGH PRIORITY)

**Current state**: Uses `Gtk::Expander` for directories. Has many problems.

**Required**:
- VS Code-style tree view (compact, clean)
- **Only show expander arrow** for directories that actually have children (no `> > >` for empty dirs)
- **Compact sizing**: rows should be small (20-22px height), tight padding
- **File type display**: show extension (`.txt`, `.py`, `.sh`) as a subtle suffix
- **File size display**: show size on row (e.g., `4.2 KB`, `1.2 MB`)
- **Date on hover**: tooltip shows last modified date (not on row, only on hover)
- **Search/filter box**: integrated at top of directory panel, filters tree in real-time
- **OS-native file dialog**: integrate `Gtk::FileChooserNative` for "Open File" from directory
- **Starts at `$HOME`** (not `std::filesystem::current_path()`)
- **Right-click context menu**: New File, New Folder, Rename, Delete, Copy Path
- **Double-click text files** → opens in note editor
- **Icons**: proper folder/file icons from system theme (not broken red icons)

### 2. Broken Button Icons (HIGH PRIORITY)

**Current state**: All button icons show as broken/red/error.

**Root cause**: Symbolic icons need CSS color inheritance. In GTK4, `Gtk::Image` with symbolic icons needs the icon to be rendered with the correct foreground color.

**Fix needed**:
- Ensure all `Gtk::Image` widgets using symbolic icons inherit foreground color
- CSS: `image { color: @text; }` or use `icon-size` property
- Test with both light and dark themes
- Add CSS classes to toolbar buttons: `.remin-toolbar-btn`

### 3. Toolbar Auto-Show (HIGH PRIORITY)

**Current state**: Toolbar only appears when creating a new tab. On initial launch, toolbar is hidden.

**Fix needed**:
- `update_toolbar()` must be called in constructor AFTER `new_terminal_tab()`
- Ensure toolbar is visible on first launch
- Toolbar height should be compact (32-36px)
- Toolbar buttons: icon + text label, compact sizing

### 4. Find Bar Integration (HIGH PRIORITY)

**Current state**: Find bar (Ctrl+F) and replace bar (Ctrl+H) are too tall compared to toolbar.

**Fix needed**:
- Find bar height = toolbar height (same visual weight)
- Find bar should be integrated into the toolbar row, not a separate overlay
- Replace bar (Ctrl+H) same height as find bar
- ESC closes find bar
- Find bar shows/hides with toolbar context (terminal vs note)

### 5. Tab Focus Highlighting (HIGH PRIORITY)

**Current state**: Only newly created tabs are highlighted. Clicking an existing tab doesn't highlight it.

**Fix needed**:
- Active tab must have visual highlight (background color change, border, or underline)
- CSS: `tab:checked` or custom class `.remin-tab-active`
- When switching tabs, the new active tab must immediately highlight
- The previously active tab must lose its highlight

### 6. Terminal Pane Full-Tab (HIGH PRIORITY)

**Current state**: Terminal pane has fixed height, doesn't fill the tab.

**Fix needed**:
- Terminal pane (and all child widgets) must expand to fill available space
- `set_hexpand(true)` / `set_vexpand(true)` on paned, tree_host, and all containers
- No fixed `set_size_request` on terminal containers
- Test: terminal should fill from below toolbar to above status bar

### 7. Line Number Gutter Scroll Sync (HIGH PRIORITY)

**Current state**: Line numbers don't scroll with editor content.

**Fix needed**:
- Line number gutter must be synced with the TextBuffer's scroll position
- In GTK4, the gutter is part of the GtkTextView via `set_gutter()`
- Ensure the gutter scrolls with the view's adjustment
- Test: scroll editor → line numbers update

### 8. History Sidebar Persistence (HIGH PRIORITY)

**Current state**: History entries may not persist across sessions.

**Fix needed**:
- `add_history()` must trigger autosaver save
- History should be persisted to storage (blob store or dedicated table)
- On restore, history is loaded and displayed
- Test: add commands, close app, reopen → history preserved

### 9. Dark Theme Button Colors (MEDIUM PRIORITY)

**Current state**: Buttons may not have correct colors in dark theme.

**Fix needed**:
- Test all button types in dark theme
- Ensure text/buttons/labels use `@text` / `@text-muted` from theme
- No hardcoded colors in widget code
- CSS: use semantic variables only

### 10. Overall UI Polish (MEDIUM PRIORITY)

**Current state**: "Ngoại hình quá xấu" (UI is too ugly).

**Fix needed**:
- VS Code-inspired compact, clean layout
- Consistent spacing: 4px/8px grid
- Typography: single dominant font, tnum figures
- Subtle borders and dividers
- No excessive whitespace
- Status bar should be informative but minimal
- Menu bar should be clean and organized

---

## TODO Checklist (active development)

### GUI Polish (current sprint)
- [ ] Directory tree: VS Code-style compact, search, file info
- [ ] Fix all broken button icons
- [ ] Toolbar auto-show on launch
- [ ] Find bar integrated into toolbar
- [ ] Tab focus highlighting
- [ ] Terminal pane full-tab
- [ ] Line number gutter scroll sync
- [ ] History sidebar persistence
- [ ] Dark theme button colors
- [ ] Overall UI beauty pass

### Core Features (next)
- [ ] Note Save/Save As + autosave toggle
- [ ] Wire find bar → active terminal VTE search
- [ ] Wire ThemeManager into Application
- [ ] Wire terminal color profile picker
- [ ] Wire menu stubs (note.save/save_as, note.preview)
- [ ] Tab rename + window rename
- [ ] Custom split dialog (rows x cols)
- [ ] Fix sign-conversion warnings

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

*Last updated: 2026-09-04*
