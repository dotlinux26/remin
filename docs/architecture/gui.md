# GUI Frontend

The GUI (`src/gui/`) is the primary Remin shell. It is a **nested**
GTK4 + gtkmm4 + VTE application (it runs inside the host desktop; a full Remin
DE is the V2 story).

> Note: the gtkmm C++ bindings for libadwaita (`Adw::` namespace) are **not**
> shipped by the distro packages, so the window/headerbar are plain gtkmm
> (`Gtk::HeaderBar`, `Gtk::Window`), while libadwaita stays linked for future
> use.

## Layered boundary (the core contract)

The whole GUI obeys one rule — **View → Controller → Core → Storage/Runtime**.
GTK/VTE widgets live only in the View layer and are **never** the source of
state; all persistent truth lives in the core domain model.

```text
                   Application
                       │
             ┌─────────┴──────────┐
             │                    │
        ThemeManager        WorkspaceSession
                                  │
                          SessionController   (orchestration)
                                  │
                           WorkspaceCore       (state + invariants)
                       ┌──────────┼──────────┐
                       │          │          │
                    Storage    Terminal      Notes
                       │          │          │
                    SQLite       PTY     md4c
                                  │
                                VTE

                       UI
                       │
                  MainWindow       (composition root only)
                       │
                ┌──────┴──────┐
                │             │
        TerminalTabView   NoteTabView
```

* **MainWindow** is only a composition/root view: overall layout, header/menu,
  tab bar, `Gtk::Stack`, status bar, and dispatch of UI-level commands.
  It never owns the PTY, pane tree, terminal emulator state, note document,
  markdown parser, persistence, or autosave implementation.
* **SessionController** sits between the UI and `WorkspaceCore`. Core keeps
  state and invariants; the controller performs *orchestration* for multi-step
  application operations (`open_workspace`, `restore_snapshot`, `close_tab`,
  `create_note`, `split_pane`, `rename_window`). Without it, UI commands would
  pile straight into `WorkspaceCore` and turn it into a God Object too.
* **Autosave** belongs to `WorkspaceSession`, not to widgets — see
  [`autosave-lock.md`](autosave-lock.md).

## WorkspaceSession

`session/workspace_session.cpp` is the single owning "application core" for the
process (the *authority*, since there is no daemon):

1. Computes the XDG data dir and creates it.
2. Opens the SQLite `SqliteStorage`.
3. Builds the `WorkspaceCore`.
4. Creates or reopens a default workspace.
5. Takes the `WorkspaceLock` (second instance is refused).
6. Builds the unified `Autosaver`.

The `Application` (`application.cpp`) owns a `WorkspaceSession` and hands the
session (controller + core + autosaver) to each `MainWindow`.

## Tab / Surface abstraction

Every tab type shares a common lifecycle so new kinds (terminal, note, later
browser, C2, BloodHound, Docker/K8s, Git) can be added without rewriting
`MainWindow`. The long-term abstraction is **Work Surface**:

```text
Tab
└── Surface
      ├── TerminalSurface
      ├── NoteSurface
      ├── BrowserSurface
      └── PluginSurface
```

Current V1 interface (will evolve into `ISurface`):

```cpp
enum class TabKind { Terminal, Note /*, future... */ };

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

GUI doesn't know what "C2 tab" is. It only knows:

```text
Surface
├── title
├── icon
├── state
├── view
└── lifecycle
```

Plugin (or built-in surface) manages everything inside.

`TabKind` is *semantic* state. The tab icon is only its **presentation** — a
mapper `TabKind → icon-name` (`remin-terminal` / `remin-note`) rendered as a
`Gtk::Image + Gtk::Label`, never via Pango markup. Dropping an icon later would
not change the architecture.

## TerminalTabView

```text
TerminalTabView ─ PtrSharedPaneTreeView
   └── (recursive Gtk::Paned tree mirroring core::PaneTree)
        ├── TerminalPaneView → wraps TerminalPane → VteTerminal
        └── ...
```

`PaneTree` (the split layout) lives in core. `TerminalTabView` mirrors it into a
tree of `Gtk::Paned`s: split H/V calls flow through the controller
(`split_pane`) and each draggable divider writes back to `set_pane_ratio`, so
the widget tree is only a *projection* of core state.

See [`terminal-pty.md`](terminal-pty.md) for the VTE wrapper details.

## NoteTabView

```text
NoteTabView
├── NoteEditor      (edit surface)
└── MarkdownPreview (rendered view)  — shown when split
```

Editor and preview are two presentations of the same `NoteDocument`
(id, title, markdown, timestamps, dirty). The preview does **not** own the
document — the editor feeds it through the markdown renderer.

Flow: `Editor → NoteDocument → MarkdownParser/md4c → MarkdownDocument → Renderer → Preview`.

### Note editor
- Line-number gutter, type-to-edit, Ctrl+F find / Ctrl+H replace, Ctrl+S save,
  open from storage.
- Reports changes to the session autosaver (10 s idle policy) — it never
  persists directly.

### Markdown renderer
- **Parser**: `md4c` (CommonMark), a tiny C library (~100 KB). No WebKit.
- **Model**: `MarkdownDocument` is an intermediate representation kept separate
  from md4c callbacks, so the renderer can be swapped without touching the
  editor.
- **Renderer**: native GTK4 block widgets (HeadingView, ParagraphView, ListView,
  QuoteView, CodeBlockView, TableView, ImageView, SeparatorView) laid out in a
  scrolled box. Pango markup is used only for *inline* text runs, never the
  whole document.
- **Debounce**: preview re-parses ~150–300 ms after the last keystroke (starts
  with full re-render; incremental only if profiling proves it is needed).
- **Scope (V1)**: headings H1–H6, paragraph, blockquote, ordered/unordered and
  nested lists, task lists, code block, horizontal rule, table; inline bold,
  italic, bold+italic, code, link, local image, strikethrough. Syntax
  highlighting is out of scope.
- **Security**: preview is untrusted input — no HTML execution, no JavaScript,
  no remote resource loading. Local images only; remote (`http(s)://`) is off by
  default. URI handling is validated before touching the filesystem.

## MainWindow

`window/main_window.cpp` is a thin composition root. It comprises:

- **HeaderBar** — logo (runtime copy `resources/logo.svg`) + workspace name.
- **Menu bar** — Window / Terminal / Note / View actions.
- **Tab bar** — icon + label tabs (kind-driven), split + new-note buttons.
- **Content stack** — one `TabView` per tab (`Gtk::Stack`).
- **Status bar** — context line.
- **Autosave badge** — small pill top-right (`saved ✓` / `failed`), auto-hides.

Every user action is turned into a command forwarded to `SessionController`; it
does not contain feature implementation.

## ThemeManager

`theme/theme_manager.cpp` is an application-level service: it loads
`resources/styles/{light,dark}.css` (GTK `@define-color` palette derived from
the logo's indigo→cyan gradient) and applies them via `Gtk::CssProvider`, tagged
to the `remin-window` class. The default is light; dark is first-class. UI uses
semantic CSS classes only — no colors are hard-coded in widgets. See
[`ui-principles.md`](../design/ui-principles.md).

## TerminalPane   (VTE wrapper)

`terminal/terminal_pane.cpp` wraps the native VTE GTK4 widget (`VteTerminal`):

- Spawns the shell into VTE's PTY (`vte_terminal_spawn_async`).
- Configures 10k scrollback lines.
- Captures text (`vte_terminal_get_text_format`) for scrollback persistence.
- Hooks VTE's `commit` signal as the **input** signal that drives the autosaver.

The wrapper is a gtkmm shell around the C VTE API via `gobj()`.

## Process model summary

- GUI process = owner of storage, core, lock, autosaver, controller.
- CLI = short-lived, same storage/core, prints JSON.
- IPC server (when wired) lives inside the GUI process so remote `remin`
  commands can target the live session. See [`ipc-cli.md`](ipc-cli.md).
