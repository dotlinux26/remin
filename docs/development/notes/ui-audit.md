# Remin — Deep UI/UX, Editor, State & Persistence Audit

> **Session**: 2026-09-04 (round 2, design-first)
> **Method**: codebase inspection, no code changes until this audit is approved.
> **Design rule** (see `AGENTS.md`): *Remin does not need to be "prettier"; it
> needs less clutter and everything in the right place.* No patch symptoms —
> find root causes, refactor structurally wrong subsystems.

---

## 1. Current Architecture

```text
Application (app.cpp)
├── ThemeManager            (loads dark.css / light.css)
├── WorkspaceSession        (DB, WorkspaceCore, Autosaver, SessionController, lock)
│   ├── WorkspaceCore       (ws_current_ = the one in-memory workspace; saves on every mutation)
│   ├── Storage (SQLite)    (workspaces / snapshots / scrollbacks tables)
│   └── Autosaver           (activity-triggered debounce/idle flush)
└── MainWindow              (layout composition + toolbar + tabs + sidebar + find + status)  ← ~1500 lines
    ├── HeaderBar / PopoverMenuBar
    ├── TabBar (tab_scroller_)
    ├── ContentStack
    │   ├── TerminalTabView  (owns panes_ map of TerminalPane widgets)
    │   └── NoteTabView      (NoteEditor + optional MarkdownPreview)
    └── StatusBar
```

Layer rule intended: `View → SessionController → WorkspaceCore → Storage/Runtime`.
**Verdict: the layered boundary is NOT respected.** `MainWindow` reads
`core_->current_workspace()` directly (main_window.cpp:1235, 1250), owns the
command-history vector, the directory-tree state, the find bar, the toolbar, and
settings dialogs. It is a God Object.

---

## 2. Responsibility Map

| Concern | Owner today | Should own |
|---------|-------------|------------|
| Workspace tree (windows/tabs/panes) | WorkspaceCore `ws_current_` | WorkspaceCore ✓ |
| Note modified/saved state | `Gtk::TextBuffer::modified` (widget) | NoteTabView/document (surfaced, not widget-only) |
| Note body persistence | Autosaver → blob store | Autosaver/Storage ✓ |
| Command history | **MainWindow `history_` vector** + SessionController | SessionController/core (single source) |
| Directory tree nav (cwd, filter, rows) | **MainWindow** (current_dir_, directory_filter_) | View/`DirectoryService` (not MainWindow) |
| Active tab | MainWindow `active_tab_` | Workspace session/core |
| Focus pane | **TerminalTabView `active_pane_`** + core `focus_pane_id` (duplicated) | Core only |
| Theme | ThemeManager ✓ | ThemeManager ✓ |
| Settings persistence | SessionController blob-store keys | SessionController/core (dedicated API) |
| Find/search state | MainWindow find bar + NoteEditor find bar (**duplicated**) | One shared find surface |

---

## 3. State Ownership Concerns

1. **`MainWindow::history_`** (main_window.hpp:155) duplicates persisted command
   history. Loaded once at startup, then kept only in memory; also fed by
   `on_history_` callback. `clear_history()` (main_window.cpp:525) only clears
   the in-memory copy — **does not persist the clear**. Two sources of truth.
2. **`TerminalTabView::active_pane_`** duplicates core `Window::focus_pane_id`.
   Both updated on click (terminal_tab_view.cpp:260-274) — drift risk.
3. **`TerminalTabView::panes_`** holds `TerminalPane` widget wrappers (owning
   live VTE + PTY + process) keyed by PaneId — domain state embedded in widgets,
   driven by core rebuild. Acceptable as view mirror, but never shrinks on
   stale rebuilds.
4. **`PaneState` is dead**: cwd/shell/cols/rows/environment/command_history/
   scrollback fields are serialized but never populated from the live terminal.
   Snapshots therefore capture an empty terminal model.
5. **Scrollback/history scoping is broken**: command history is a *single global
   list* (`settings:command-history`), not scoped to pane → tab → window →
   workspace as required (PHASE 20).
6. **Window x/y/width/height** are serialized in core but never applied to the
   GTK window on startup.

---

## 4. Bugs (classified P0–P3)

### P0 — data/state loss or broken core feature

| # | Bug | Location | Root cause |
|---|-----|----------|-----------|
| 1 | **Workspace recovery is broken** — tabs/panes/scrollback lost on restart | workspace_session.cpp:40-52, application.cpp:12-82, main_window.cpp:99 | `MainWindow` always calls `new_terminal_tab()` and never restores the loaded workspace tree; scrollback lives only in VTE memory captured on demand, never re-loaded; `create_snapshot/restore_snapshot` are **never called** anywhere. |
| 2 | **File/History state bug** — clicking Files makes History disappear | (older build) main_window.cpp setup_sidebar | Was: per-page tab bars / stack replacement. Mitigated by the shared-switcher fix, but must be regression-tested (PHASE 6). |
| 3 | **Ctrl+S / dirty marker bug** — save but `●` stays | main_window.cpp:1175 `update_tab_bar` | Dirty state is `Gtk::TextBuffer::modified` (note_editor.cpp:262-268); `save_now()` clears it (note_tab_view.cpp:96), but **nothing calls `update_tab_bar()` after save or after buffer change**, so the tab label never refreshes (PHASE 15). |
| 4 | **Snapshots/persistence options don't exist in UI** | settings_dialog.cpp | No Default-Recovery / Window-History / Auto-save-by-Input settings — three user-facing persistence policies unimplemented (PHASE 18). |

### P1 — major UX/architecture defect

| # | Bug | Location | Root cause |
|---|-----|----------|-----------|
| 5 | **Editor line-number gutter desyncs on soft-wrap** | note_editor.cpp:40-80, 182-218 | Gutter and editor are **two independent `Gtk::TextView` in two independent `ScrolledWindow`s**, synced only by mirroring vadjustment (note_editor.cpp:66-78). The gutter's buffer has one row per *logical* line; a soft-wrapped line has *N visual rows* in the main view → 1 row in the gutter → alignment breaks (PHASE 13). |
| 6 | **Find/replace UI duplicated** | MainWindow find_bar_ (main_window.cpp:839) **and** NoteEditor find_bar_ (note_editor.cpp:82-118) | Two separate Ctrl+F/Ctrl+H bars; `show_find_bar` toggles the top one and also calls `NoteTabView::show_find_replace` → editor's own bar. Redundant, inconsistent, hard to keep in sync (user-requested: reuse one, no duplication). |
| 7 | **Find/replace replace-at-cursor bug** | note_editor.cpp:241 `do_replace` | Uses `buffer_->insert_at_cursor()` instead of replacing the current selection; inserts at caret, not at match. |
| 8 | **Directory `..` (parent) shown as a normal folder** | main_window.cpp:543-548, 575 | Parent `..` is created via generic `create_directory_row("..", true, parent)` → folder icon, treated as ordinary dir. User wants `..` and `Home` as **special entries with distinct icons** and accurate navigation semantics (user-requested). |
| 9 | **Terminal context-menu styled as a card** | terminal_tab_view.cpp show_pane_menu, dark.css `.remin-context-menu` | `.remin-menu-item` rows use button styling → look like boxed buttons/card, not a flat native menu (PHASE 7/22). |
| 10 | **MainWindow is a God Object (~1500 lines)** | main_window.hpp/.cpp | Toolbar, tabs, sidebar, find, settings, directory, history all in one class; violates View→Controller boundary. |

### P2 — visual / interaction inconsistency

| # | Bug | Location | Root cause |
|---|-----|----------|-----------|
| 11 | Toolbar `update_toolbar()` not called on first launch (stale tab) | main_window.cpp:56-60 | Constructor appends toolbar before `new_terminal_tab()`; toolbar populated only when tabs exist. (Recent fix orders calls; verify.) |
| 12 | Find bar taller/different weight than toolbar | main_window.cpp:839-901 | Separate overlay box, not integrated into toolbar row; `On Find` vs toolbar height mismatch. |
| 13 | Tab overflow → labels shrink / no explicit scroll strip | main_window.cpp:331-361 | `tab_scroller_` auto-scrolls, but no separated navigation strip below tabs as specified (PHASE 3). `.remin-tab` has `min-width:0`. |
| 14 | `SyntaxError`: `.remin-tab-group:has(...)` unsupported by GTK CSS parser | dark.css/light.css | GTK CSS does not support that `:has()` form (runtime `Theme parser error`, now removed). |
| 15 | Toggle size bar / `+` buttons geometry not unified | CSS `.remin-tab-btn` | Sizes aligned per-widget rather than via one shared component (PHASE 9). |
| 16 | Dark theme: possible light surfaces inside dark UI | dark.css default `button` scope | Standard GTK controls not always overridden to dark (was propagated; needs final pass, PHASE 21). |

### P3 — polish / future

- Line-number click → jump & mark (PHASE 14), Ctrl+S trusted status, Esc-twice
  terminal-search semantics (PHASE 19), settings Editor page is a stub
  (settings_dialog.cpp:136-141), `Window::x/y/w/h` restore, `Workspace::
  working_directory`/`tags`/`last_saved_at` unused.

---

## 5. Editor Architecture Problems

1. **Two-view layout + scroll sync** (note_editor.cpp:40-80): gutter & editor are
   separate `TextView`s. Cannot represent soft-wrapped logical lines correctly.
   **Requires a single layout model** (PHASE 13) — e.g. a native gutter driven by
   the same line metrics, or per-logical-line visual row height sharing.
2. **`do_replace` inserts at cursor** (note_editor.cpp:241) instead of replacing
   the selection → wrong results.
3. **Find bar is inside the editor** (note_editor.cpp:82-118) — duplicates the
   MainWindow find bar; must be consolidated (PHASE 10/16).
4. `update_gutter_width` (note_editor.cpp:195) uses a Pango probe + magic
   `extra = 4` px padding — a pixel hack; brittle under font/zoom changes.

---

## 6. Lifecycle / Persistence Problems

- **No startup restore**: loaded workspace tree ignored; single fresh terminal
  tab always created (application.cpp:50, main_window.cpp:99).
- **Snapshot API unused**: `create_snapshot/restore_snapshot` never invoked.
- **Autosave poll (250 ms)** (application.cpp:78) is a polling loop; acceptable
  but could be event-driven.
- **No Window History** persistence: closed windows are not recorded/named; no
  `Ctrl+H` window-history restore (PHASE 18 Option 2).
- Command history `clear_history()` not persisted (main_window.cpp:525).

---

## 7. Resource / Icon Problems

- Root cause found & fixed last pass: symbolic SVGs hardcoded `fill`
  (invisible on dark). Converted 17 SVGs to `currentColor`, regenerated
  `icons.gresource`. Verify in debug/release/installed builds and from another
  cwd (PHASE 2).
- **`..` and Home need distinct icons** (folder-parent / home glyph) vs regular
  dirs/files (user-requested) — not yet done.
- `reset to system icon theme` is broken (empty root `index.theme`); bundled
  GResource path is the reliable source.

---

## 8. Theme Problems

- Semantic palette exists and is mostly good (dark.css `@define-color` surface/
  border/text/accent).
- Need **centralized** override of standard GTK controls (buttons/entries/dialogs)
  so dark stays genuinely dark (PHASE 21); no per-widget hardcoded colors.
- Context menus must be flat/native, not cards (PHASE 22).
- `.remin-pane-active` toggle bar should be transparent/icon-only (already
  improved; re-verify).

---

## 9. Keyboard / Interaction Problems

- Find bar: Ctrl+F (no replace) vs Ctrl+H (replace) — see duplication (P1-6).
- Terminal search Esc semantics (keep viewport on first Esc; jump to bottom on
  second Esc) **not implemented** (PHASE 19); current `on_find_key_pressed`
  (main_window.cpp:1291) hides the find bar only.
- Ctrl+S save: wired through `on_find_key_pressed` (main_window.cpp:1311-1317) →
  `save_now()` → dirty not refreshed (see P0-3).
- `Ctrl+W` closes tab, `Ctrl+P` toggles sidebar, `Shift+H/V` splits — present.

---

## 10. Proposed Architecture

```text
Application
├── ThemeManager
├── WorkspaceSession
│   ├── WorkspaceCore            ← authoritative domain state
│   ├── Storage                  ← dedicated settings/content/metadata APIs (stop abusing scrollbacks table)
│   └── Autosaver                ← activity-triggered, policy in core
├── SessionController            ← orchestration only
└── MainWindow (slim)
    ├── TabBar (with separated scroll/nav strip)   ── one shared TabButton component
    ├── SidebarView (History | Files)              ── not in MainWindow
    │   ├── DirectoryTreeView     (owns current_dir_, filter; special .. & Home rows)
    │   └── HistoryView
    ├── ContentStack
    │   ├── TerminalTabView (TerminalPane per pane, scrollback in core)
    │   └── NoteTabView (single shared find bar + native-gutter editor)
    └── StatusBar
```

Key refactors:
- **One shared FindBar** widget (Ctrl+F / Ctrl+H) used by both note & terminal;
  delete the editor's internal copy.
- **Native-gutter editor**: derive line metrics from a single layout model so
  soft-wrap keeps logical line numbers aligned (PHASE 13).
- **Directory rows**: model entries as `{type: Parent|Home|Dir|File}`, distinct
  icons, `..` navigates parent, Home row navigates `$HOME` (user-requested).
- **Persistence service** in core exposing the three policies:
  Default Recovery / Window History / Auto-save by Input; Settings surfaces them.
- **Command history** moved to SessionController as single source, scoped per
  pane/tab/window.

---

## 11. Fix Priority / Plan

| Phase | Priority | Deliverable |
|-------|----------|-------------|
| 0 | — | THIS audit (gate) |
| 2 | P2 | Verify & harden icon loading in all builds |
| 6 | P0 | Regression test: File ↔ History switching (neither disappears) |
| 15 | P0 | Single source of saved/modified/saving/failed + refresh tab on save |
| 13 | P1 | Editor single-layout native gutter (soft-wrap correct) |
| "find" | P1 | Consolidate find/replace into one shared bar; fix do_replace |
| "dir" | P1 | Special `..`/Home rows + distinct icons + accurate navigation |
| 7/22 | P1 | Flat native context menus (all surfaces) |
| 18 | P0 | Persistence policies in Settings (3 options) |
| 3/5 | P2 | Tab bar: separated scroll strip, no label shrink; toolbar on launch |
| 21 | P2 | Dark theme: centralized control palette |
| 17/20 | P0/P1 | Startup restore from workspace + scoped history |
| 19 | P2 | Terminal search Esc semantics |
| 14/16 | P3 | Line-number click nav; Open/Save/Save As/Preview toolbar |

---

## 12. Files Changed (projected, approved after this audit)

| File | Change |
|------|--------|
| src/gui/window/main_window.cpp/.hpp | Slim down: extract sidebar, directory, find, history; add on-save refresh of tab bar |
| src/gui/window/note_editor.cpp/.hpp | Single-layout native gutter; remove internal find bar; fix do_replace |
| src/gui/ui/find_bar.* | NEW shared find/replace widget |
| src/gui/ui/directory_tree_view.* | NEW (owns dir state; special .. / Home rows + icons) |
| src/gui/ui/context_menu.* | Flat native menu styling (reused everywhere) |
| src/gui/session/session_controller.* | Move command-history source-of-truth here; scope per pane/tab |
| src/gui/window/settings_dialog.* | Add Persistence page with 3 policies; fill Editor stub |
| src/core/workspace_core.cpp | Startup restore hook; snapshot lifecycle; recompute last_saved_at |
| src/storage/* | Dedicated settings/content APIs (stop abusing scrollbacks) |
| resources/styles/dark.css, light.css | Centralized control palette; flat menus; dir icons; find/tab strip |

---

## 13. User-flagged items recorded this session (must be in scope)

1. **Directory `..` / Home are special entries** — distinct icons, accurate
   parent vs home navigation, not generic folders (P1-8).
2. **Ctrl+F / Ctrl+H UI duplication** — one shared FindBar; editor must not
   build its own (P1-6).
3. **Line-number gutter + soft-wrap desync** — single layout model (P1-5 / PHASE 13).
4. **Ctrl+S / dirty `●` not refreshing** — authoritative save-state + refresh (P0-3).
5. **Terminal context menu = floating card** — must be flat native menu (P1-9).
6. **File/History disappearing** — regression test, keep both independent (P0-2).

---

## 14. Decisions Log (round 3, 2026-09-04)

### D-1. Editor engine = GtkSourceView 5 (C API via thin C++ adapter)
- **Decision**: Replace the two-`Gtk::TextView` + manual scroll-sync editor with
  GtkSourceView 5, wrapped behind a thin C++ adapter (`src/gui/editor/`).
- **Why**: GTK4 `GtkTextView` has native gutter/display-line concepts, and
  GtkSourceView 5 inherits it with `GtkSourceGutterLines` (cached/shared line
  layout), line numbers, current-line, search/replace, file I/O, syntax
  highlighting. This is the battle-tested editor stack, same philosophy as VTE.
- **Bindings**: Kali (noble) has **no `libgtksourceviewmm-4.0-dev`** (C++ binding
  for GTK4) — only `libgtksourceviewmm-3.0-dev` (GTK3). So we use the official
  **C API (`gtksourceview-5`, v5.12.0 installed) through a thin adapter**.
  No external repo added (per user).
- **Scope guards**:
  - Raw GtkSourceView/GtkSourceGutter C structs must NOT leak into
    WorkspaceCore / SessionController / NoteDocument.
  - Do not create `TextView(editor)+TextView(gutter)+scroll-sync`.
  - Do not guess line height; gutter renderer reads layout from
    GtkSourceGutterLines.
- **Acceptance**: wrapped logical line 42 → single gutter "42" spanning all
  visual rows; correct on resize/soft-wrap on-off/font/theme/large/empty;
  click line number → jump + scroll + transient highlight (not a clipboard
  selection).

### D-2. Core mutation != persistence (autosave policy applies)
- WorkspaceCore owns authoritative in-memory state; NOT writing to SQLite on
  every domain mutation. Persistence goes through session/autosave layer per
  policy (Enter immediate / idle 10s / shutdown final checkpoint).
- (To enforce in refactor — currently `WorkspaceCore` saves on every call.)

### D-3. Recovery vs Window History are distinct
- Recovery = latest live-workspace checkpoint (shutdown/restart).
- Window History = intentionally closed window snapshots (label + timestamp),
  only when enabled; reopen via Ctrl+H.

### D-4. State ownership invariant
- `Workspace → Window → Tab → Pane`, each stable identity. GTK widgets are
  runtime representations only:
  `PaneId → PaneState → TerminalPaneView → VteTerminal+PTY`.

### D-5. File/History treated as regression (not merely mitigated)
- Test: History → Files → History → Files → open file → History. Neither view
  may disappear or be recreated losing state.

### D-6. Window geometry (V1)
- Persist width/height; x/y only where compositor allows reliably; fallback to
  default; geometry is NOT a workspace invariant (Wayland restrictions).

### D-7. Settings expose 3 explicit persistence policies
- Default Recovery / Window History / Auto-save by Input — centralized policy
  config, not scattered booleans.

### D-8. Save-state model (single source)
- `clean | dirty | saving | saved | save_failed`; Ctrl+S and Save button share
  one command path; tab UI refreshes from this, never faked.

### D-9. Terminal search semantics preserved
- Ctrl+F search; ↑/↓ navigate; Esc closes & stays at match; Esc×2 closes &
  jumps to bottom. Unchanged.

### D-10. Context menus = menus, not cards (flat, Note-editor style)

Em chọn **1 — C API qua thin adapter**.

Lý do: trong stack GTK4 hiện tại, `GtkTextView` quả thật đã có gutter native (`set_gutter()` / `get_gutter()`), và GTK4 cũng có API khái niệm *display line* để xử lý wrapped lines. ([Gnome Pages][1]) Nhưng nếu Remin muốn editor có **line number + click marker + current-line + sau này syntax highlighting/search/replace** thì GtkSourceView 5 mới là stack chuyên dụng; nó kế thừa `GtkTextView`, có gutter native và `GtkSourceGutterLines` được thiết kế để cache/chia sẻ thông tin line khi render. ([Gnome Pages][2])

Quan trọng: **không cài thêm repo ngoài chỉ để kiếm C++ bindings**. GtkSourceView 5 cung cấp C API chính thức qua `gtksourceview-5`; cứ bọc nó trong một adapter C++ mỏng. ([Gnome Pages][2])

Gửi agent nguyên đoạn này:

```text
Chọn 1 — C API GtkSourceView 5 qua thin C++ adapter.

Không chọn 2.
Không thêm repository/source ngoài chỉ để tìm gtkSourceView C++ bindings.

Stack:

NoteEditor (C++)
    ↓
GtkSourceViewAdapter (C++)
    ↓
GtkSourceView 5 C API
    ↓
GtkTextView layout
    ↓
GtkSourceGutter / GtkSourceGutterLines
    ↓
GTK4

Requirements:

1. GtkSourceView 5 là editor engine, không expose C API khắp codebase.
2. Tạo một thin adapter riêng, ví dụ:

src/gui/editor/
├── note_editor.*
├── source_view_adapter.*
└── gutter_renderer.*

3. NoteEditor chỉ làm việc với adapter/abstract editor API.
4. Không để raw GtkSourceView/GtkSourceGutter C structs leak vào WorkspaceCore,
   NoteDocument hoặc SessionController.

5. Dùng native GtkSourceView gutter cho:
   - line numbers
   - current-line indicator
   - future line marks
   - clickable navigation

6. Không tạo:
   TextView(editor)
   +
   TextView(gutter)
   +
   manual scroll synchronization.

7. Không tự tính line height để giải quyết soft-wrap.
   Gutter renderer phải lấy layout/line information từ GtkSourceView /
   GtkSourceGutterLines.

Acceptance:

Logical line 42 wrapped thành nhiều visual rows:

42 | very long text...
   | continues...
   | continues...

Line number 42 phải nằm chính xác theo toàn bộ visual extent của logical line.

Phải giữ đúng khi:
- resize
- soft-wrap on/off
- font change
- theme change
- large document
- empty lines

Click line number:
- jump tới logical line
- scroll tới vị trí
- temporary navigation highlight
- đây KHÔNG phải clipboard selection

Không dùng:
- magic offsets
- guessed line heights
- polling vadjustment
- sleep/timer synchronization hacks.

TRƯỚC KHI CODE:

Kiểm tra version GTK4 + GtkSourceView 5 thực tế trên máy build.
Kiểm tra symbol/API cần dùng.
Xác nhận thin adapter interface.

Sau đó mới implement.

Bonus:
Tận dụng GtkSourceView cho các feature editor đã tồn tại thay vì tự
implement lại nếu API phù hợp:
- line numbers
- current line
- search/replace
- file loading/saving
- syntax highlighting

## STAGE C — UI/UX & CSS Refactor (2026-09-05) — CHANGELOG

All issues below were user-reported and **structurally fixed** (not patched):

| # | Issue | Root Cause | Fix |
|---|-------|------------|-----|
| 1 | Window auto-grows with tabs | ScrolledWindow policy `NEVER` propagated child natural width | `tab_scroller_` policy → `EXTERNAL` (horizontal); overflow via dedicated `Gtk::Scrollbar` |
| 2 | Scrollbar overlaps tab labels | Single-row tab+scrollbar layout | `tab_box_` = vertical box: tab row + dedicated scrollbar row (below, never overlaps) |
| 3 | + buttons not square / too tall | Box `margin-top/bottom` + default `valign=FILL` stretched buttons | `valign=CENTER`, remove margins, CSS `min-width/height: 28px, padding:0, box-shadow:none, outline:none` |
| 3b | Native window controls have black border (dark) | Global `button { border: 1px solid @border }` applied to titlebuttons | Explicit titlebutton rules in **both** themes (identical): transparent, no border/outline/shadow for all states |
| 5 | Tab close (×) hover oval/too tall | Close button `valign=FILL` stretched to tab height (24px) | `valign=CENTER` in code; CSS round hover preserved |
| 6 | Context menus have frame/bg/margins | Custom `Gtk::Popover` + `Gtk::Button` items with `margin: 4` container | Rewrote to **native `Gtk::PopoverMenu` + `Gio::MenuModel` + `SimpleAction`** — same CSS nodes as note editor's native menu (`popover.background.menu` + `modelbutton`) |
| 7 | Toolbar icons missing (red broken) | `Gtk::Image("name")` = filename constructor | All icon creation: `Gtk::Image()` + `set_from_icon_name("...")` |
| 8 | SVG red placeholder | Same as #7 | Same fix |
| 9 | Directory path not below search row | Layout not done | Header = vertical box: top row (search+reload) + bottom row (path label) |
| 10 | Directory not realtime on save | FileMonitor existed but no write-triggered refresh | Added `set_file_saved_callback` on NoteTabView → fires `directory_panel_->refresh()` only on actual file write |
| 11 | Dirty dot (●) only on tab switch | `signal_modified_changed` only; full tab rebuild expensive | Belt-and-suspenders: `signal_changed` + 120ms debounce + persistent tab widgets → in-place label update |
| 12 | Tab bar full rebuild on every change | Destroy/recreate all widgets on any change | `tab_widgets_` vector: append for new tabs, in-place refresh for label changes |
| 13 | `..` row chevron confusing | Parent dir shown as expandable dir | Single-click `..` → `set_root(parent)`; no arrow. Label "Back ..". Regular dirs: double-click → enter |
| 14 | Scrollbar hover animation/lag | CSS `transition: background-color 0.15s` + accent hover | Removed transition; slim scrollbars (8px/6px/1px); neutral hover color |
| 15 | Native menus light in dark theme | `@popover_bg_color` not remapped | Added `@define-color popover_bg_color @surface; popover_fg_color @text` to both themes |

### STAGE C2 — Additional Fixes (2026-09-05, User Feedback Round 2)

| # | Issue | Root Cause | Fix |
|---|-------|------------|-----|
| 16 | Dark theme: buttons/editor white background | Global `button` rule + missing textview background; dialogs lacked `remin-window` class | Added `textview { background: @bg }`; SettingsDialog gets `remin-window` class; all remin-* button classes properly override global rule |
| 17 | Toolbar buttons not transparent | Needed explicit transparent rule | `.remin-tool-btn` + `.remin-toolbar-btn` + `.remin-preview-toggle` all have `background-color: transparent` |
| 18 | Settings dialog: tab text changes color + double focus outline | Notebook tabs inherited theme; GTK default focus ring had 2 colors | SettingsDialog tagged `remin-window`; notebook tab focus ring forced to single `@accent` blue |
| 19 | Window title changes color in dark mode | `.remin-header-label` used `@text` (white in dark) | Hardcoded to light-theme text color `#1f2437` |
| 20 | Directory `..` row shows chevron ".." | Expander arrow + ".." label | Label changed to "Back .."; go-up-symbolic icon kept; no chevron; single-click navigates up |
| 21 | No home button in directory tree | Only reload button | Added home button (user-home-symbolic) next to reload, same style/size, navigates to $HOME |
| 22 | Search boxes have border | Global `entry` rule + focus border | `.remin-dir-search` + `.remin-find-entry`: `border: none` for all states including `:focus` |
| 23 | Context menu actions not firing | New PopoverMenu implementation | Verified: `Gio::SimpleAction` per item, action group inserted on anchor, callbacks wrapped correctly |
| 24 | Native window controls (- [] X) still have hover background | Both themes had `@surface` on hover | **All states (hover/active/focus/checked): `background-color: transparent`** in both themes |

**Verification**: Build ✅, 6/6 tests ✅, ASan launch ✅ (no leaks, no CSS warnings).

---

### STAGE C3 — New Issues (2026-09-05, Round 3) — **UNFIXED**

| # | Issue | Root Cause | Planned Fix |
|---|-------|------------|-------------|
| 25 | Dark theme: white backgrounds on editor, sidebar tabs, toolbar, preview | `textview` background not explicitly dark; sidebar tabs inherit theme; toolbar buttons/preview toggle missing explicit transparent bg | Explicit `textview { background: @bg; color: @text }`; `.remin-sidebar-tab` override to dark surfaces; `.remin-tool-btn`, `.remin-toolbar-btn`, `.remin-preview-toggle` → `background: transparent` |
| 26 | Settings dialog tabs change color with theme | SettingsDialog gets `remin-window` class but notebook tabs still inherit dark theme | Keep dialog light: either force light CSS provider on dialog, or override notebook tab colors to light palette |
| 27 | Settings dialog: double focus outline on tabs | GTK default focus ring (orange) + custom blue outline | `notebook tab:focus { outline: 2px solid @accent; outline-offset: -2px; box-shadow: none; }` remove GTK default |
| 28 | Window title color changes in dark | `.remin-header-label` uses `@text` | Hardcode to `#1f2437` (light theme text color) |
| 29 | Directory `..` button: show "< Back .." text | Current label "Back .." with go-up icon | Change label to `"< Back .."`; keep go-up-symbolic icon; no chevron |
| 30 | Directory tree: missing home button | Only reload button exists | Add home button (user-home-symbolic) next to reload, same style/size, click → `set_root($HOME)` |
| 31 | Search boxes have borders | Global `entry` rule + focus border on `.remin-dir-search`, `.remin-find-entry` | `border: none; box-shadow: none; outline: none;` for all states |
| 32 | Context menus: actions not wired | PopoverMenu actions created but callbacks not executing | Verify `SimpleAction::signal_activate` captures callback; action group inserted on anchor; `set_enabled()` for sensitive items |
| 33 | Native window controls (- [] X): hover/active background | Both themes have `@surface` on hover/active/focus | **All states: `background-color: transparent; border: none; box-shadow: none; outline: none;`** |
| 34 | Toolbar split icons: wrong symbols | Split H uses `pan-start-symbolic` (←), Split V uses `pan-end-symbolic` (→) — should be vertical/horizontal split icons | Use `view-split-vertical-symbolic` / `view-split-horizontal-symbolic` (or similar proper split icons) |
| 35 | Missing "Open File" toolbar button in note editor | Note editor toolbar lacks open file action | Add button in `update_toolbar()` for Note tabs: `document-open-symbolic` → opens file chooser → `open_note_from_path()` |
| 36 | Editor: integrate GtkSourceView 5 | Current NoteEditor uses plain GtkTextView + manual gutter | Create `src/gui/editor/` adapter: GtkSourceView 5 C API wrapper; native gutter/line numbers/syntax highlighting/search |
| 37 | Find/Replace buttons: broken, auto-close on Esc, rounded corners | Buttons don't trigger actions; ESC handler closes bar; buttons have default GTK rounding | Wire `find_next_`, `find_prev_`, `replace_btn_`, `replace_all_btn_` to editor; ESC only closes if bar focused; `.remin-find-entry`, buttons: `border-radius: 2px` |
| 38 | Sidebar panel (Ctrl+P): right border + bottom line under tabs | `.remin-sidebar` has `border-right`; `.remin-sidebar-tabs` has `border-bottom` | Remove both: `border-right: none; border-bottom: none;` |

[1]: https://gnome.pages.gitlab.gnome.org/gtkmm/classGtk_1_1TextView.html?utm_source=chatgpt.com "gtkmm: Gtk::TextView Class Reference"
[2]: https://gnome.pages.gitlab.gnome.org/gtksourceview/gtksourceview5/?utm_source=chatgpt.com "GtkSource – 5"
[3]: https://gnome.pages.gitlab.gnome.org/gtksourceview/gtksourceview5/overview.html?utm_source=chatgpt.com "GtkSource – 5: Overview"

