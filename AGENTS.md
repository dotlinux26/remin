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
- [x] P1 Domain state + migration (TabKind, NoteTabState, UiState + serialization round-trip)
- [x] P2 TerminalPane live: `runtime_capture()/runtime_restore()` (cwd §4 OSC7→/proc→cached→$HOME, cols/rows, full scrollback §5.1, Ctrl+C `\x03` detection §6.2), per-pane canonical `command_history` (`WorkspaceCore::add_command_to_pane`, cap 1000) + aggregate sidebar (§6.3, `settings:command-history` migrated once)
- [x] P2.5 Fidelity gate (§5.5): VTE live → ANSI/control → capture → new VTE → feed → compare (textual scrollback EXACT round-trip, semantic content preserved, color/format NOT preserved — recorded)
- [x] P3 Atomic checkpoint: SqliteDb txn RAII (BEGIN IMMEDIATE/COMMIT/ROLLBACK), `checkpoint(reason)` writes workspace + scrollbacks + snapshot atomically, generation + schema_version + reason persisted, autosave → checkpoint, shutdown flush via `signal_close_request()`. **Window identity: checkpoint UPDATE các Window đang tồn tại; generation = version, KHÔNG tạo Window mới.**
- [x] P4 Restore workspace đầy đủ (terminals, notes, dir tree, geometry, focus) — restore binds `focus_window_id` (most recent), KHÔNG prune các window khác, KHÔNG tạo default window khi có state hợp lệ
- [x] P5 Golden acceptance test + report (§16) — runtime-verified 2026-09-06 trên binary release với DB cô lập (chi tiết: `tests/golden/GOLDEN_ACCEPTANCE_CHECKLIST.md` → "Window Identity & Persistence Semantics"). 8/8 invariant pass: identity ổn định qua autosave (generations = versions), restart không tăng window count, W42+W51 sống sót không duplicate, most-recent = focus_window_id, setting OFF cleanup tường minh + fresh single window. Bug tìm được lúc verify: `Autosaver::flush()` không gọi workspace provider → checkpoint autosave chết (generation 0) — đã fix, verify checkpoints bump generation.

### Refs (cập nhật 2026-09-06)

- `docs/design/terminal-history-semantics.md` — Terminal History Semantics (chốt
  với user 2026-09-06): **3 khái niệm tách bạch — Screen State / Command History /
  Terminal Transcript**. `clear` là screen-state op, KHÔNG xóa history/transcript.
  `command_history[]` là canonical per-pane (không dựa `~/.bash_history`);
  History UI search từ Workspace → Windows → Tabs → Panes. ↑↓ vẫn shell-owned.
  VTE scrollback = screen state, KHÔNG = "toàn bộ lịch sử pane".
- `docs/design/history-system-spec.md` — **History System SPEC (3-mode)**: History =
  Commands (command_history[] per-pane, click = insert vào input KHÔNG execute) +
  Transcripts (Remin-owned transcript path, KHÔNG phụ thuộc VTE current screen,
  clear không xóa) + Windows (closed-window snapshots, recovery ≠ window history).
  UI History panel FROZEN — wire behavior + state only, khác: `Ctrl+Shift+H`.
  Model: CommandRecord / TranscriptChunk / ClosedWindowSnapshot riêng biệt.
  Implementation order A–G; acceptance yêu cầu đủ 7 thứ đồng thời.
  `implement-note-1.md` đã superseded bởi semantics + spec này.
- `docs/problem-terminal-transcript-capture.md` — **P0-B CAPTURE FIDELITY FAILING**:
  blob scrollback tồn tại (~10KB) nhưng nội dung gần như blank + prompt, thiếu
  output thật. PHẢI chứng minh capture chứa marker deterministic
  (`printf 'REMIN_CAPTURE_A\n'` / `ls` / `printf 'REMIN_CAPTURE_B\n'`) trước khi
  nói scrollback hoạt động. Acceptance: per-pane transcript + command_history
  tách biệt, no cross-contamination, no crash.

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

## Window Identity & Persistence Semantics (P5 contract — READ FIRST)

> Đây là hợp đồng bất biến của feature workspace persistence/recovery. Mọi thay đổi
> liên quan Window/checkpoint/restore phải khớp đúng các quy tắc dưới đây.

### 1. Window = entity có identity ổn định

```text
Window
├── id          # bất biến, sinh một lần
├── label       # tên người dùng đặt ("GitLab Audit"), rename chỉ đổi label
├── state       # tabs / panes / ui state hiện tại
└── lifecycle   # open / closed
```

- **Autosave/checkpoint chỉ UPDATE state của Window đang tồn tại.** KHÔNG BAO GIỜ
  INSERT Window mới vì autosave chạy.
- **Checkpoint là version (generation) của workspace state, không phải Window mới.**

```text
W42    ← same W42 across G101, G102, G103 (generations = versions, not windows)
```

### 2. Contract chuẩn

| Thời điểm | Hành vi bắt buộc |
|-----------|------------------|
| Autosave  | capture live state → UPDATE từng Window đang mở → +1 generation. Không W43/W44. |
| Nhiều Window | checkpoint cập nhật TẤT CẢ window đang mở (W42+W51): **không merge, không clone, không duplicate record**. |
| Shutdown   | capture live state **ngay trước teardown** → update mọi Window → `checkpoint("recovery")` → destroy. Không restore ảnh cũ hơn. |
| Startup    | **trước tiên** tìm latest valid recovery checkpoint → reconstruct → restore → **sau đó mới** cân nhắc tạo Window mặc định. KHÔNG tạo default window trước rồi restore. |
| Default window | Chỉ tạo window mới khi KHÔNG có persisted/recovery state dùng được (DB rỗng). DB có valid W42 → restore W42, **không** tạo "My Window 2". |

### 3. Programmatic invariants (giữ bằng test/DB query)

- Autosave lặp (create W42 → checkpoint ×3) ⇒ **1 Window identity + nhiều generations**.
- Restart app ⇒ **Window count trong DB không tăng** chỉ vì restart.
- Tạo W42 + W51, checkpoint, shutdown, restart ⇒ **đúng W42 + W51**, không thêm default window.

### 4. Setting: `persist_open_windows` (key `settings:persist-open-windows`, default ON)

- **ON**: window đang mở tham gia recovery checkpoint; startup restore latest open-window state.
- **OFF**: không persist/restore session window; startup **clear window state cũ** (cleanup)
  rồi app chạy fresh. Cleanup phải tường minh — tuyệt đối không INSERT window lặp khi OFF.
- Note: việc tắt setting KHÔNG được âm thầm sinh duplicate Window entity.

### 5. "Most recent window"

Là window từ **latest valid committed workspace state**, KHÔNG phải `MAX(created_at)` /
max ID / window mới nhất vừa INSERT. Triển khai V1: `Workspace::focus_window_id` (ghi mỗi
khi window được focus/add) = "window dùng gần nhất" tại checkpoint cuối.

### 6. Ba khái niệm TÁCH BẠCH — không lẫn lộn

```text
History
├── Commands      # những gì user đã chạy (sidebar hiện tại, aggregate §6.3)
└── Windows       # (future) window đã đóng, muốn gọi lại — closed-window snapshot

Recovery          # workspace cuối cùng trước shutdown/restart
```

- Closing helper: nếu đóng W51 — Window History OFF ⇒ xóa cục bộ; Window History ON ⇒
  capture final state + label + timestamp thành closed-window history, xóa live W51.
- Việc đóng window/khôi phục **không được** tạo/spawn Window mới trong core.

### 7. Triển khai V1 (GUI đơn MainWindow)

- GUI dùng 1 core window; restore binds vào `focus_window_id` (most recent) và hiển thị
  window đó. **KHÔNG prune/xóa các window khác khỏi model** — chúng là entities hợp lệ,
  phải sống sót qua restart (DB count ổn định). Multi-window GUI = V2+.
- Default label của window mới = `My Window N` (N tăng dần, tránh trùng)
  (`SessionController::default_window_label`).
- **Autosaver**: periodic `flush()` (tick 250ms) → khi có `Kind::Workspace` entry due,
  gọi workspace provider → runtime capture + `checkpoint("autosave")`. `flush_now()`
  dành cho explicit save / shutdown. Đây là fix P5 (autosave checkpoint chết vì
  `flush()` không gọi provider).
- Setting key `settings:persist-open-windows` (default "1" = ON; rỗng = "1").

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
