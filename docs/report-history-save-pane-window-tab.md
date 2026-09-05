# Report — History & Khả năng Lưu Pane/Window/Tab (2026-09-06)

> Báo cáo điều tra **chỉ đọc code** (không sửa gì). Mục tiêu: xác định thực trạng
> hiện tại của (1) lưu layout pane/window/tab, (2) khái niệm **history**, và
> (3) settings **3 mức độ** theo spec — vì đây là phần tiên quyết trước khi làm
> các tính năng tiếp theo. Spec tham chiếu: `docs/ui-audit.md`, `docs/architecture/*.md`,
> `docs/design/ui-principles.md`, `docs/work-2026-09-04.md`.

---

## 0. TL;DR

| Chủ đề | Hiện trạng |
|--------|-----------|
| Lưu **layout** window/tab/pane | Đã lưu được **cấu trúc** (tree, ratio, focus) vào `workspaces.json`. |
| Lưu **nội dung** terminal (scrollback, cwd, shell…) | **KHÔNG** — model có field nhưng không bao giờ populate; scrollback ghi nhưng **không bao giờ đọc lại**. |
| Khái niệm **history** | **Chưa có khái niệm thật sự.** Chỉ có 1 list command toàn cục. Struct `Workspace` ghi chú "history" nhưng không có field. |
| **Window history** (window đã đóng) | **Không có.** (D-3, D-7 option 2) |
| **Snapshot / Restore** | API + bảng + CLI có, nhưng **GUI không dùng** (không UI trigger). |
| **Settings 3 mức độ** (D-7: 3 chính sách persistence) | **Chưa có.** Chỉ có struct `PersistencePolicy` không được wire. Settings hiện tại là **flat global** `settings:*`. |
| Restore khi mở lại app | Restore được **terminal tab**, nhưng **không** phục hồi scrollback/cwd; **note tab không restore**. |
| Flush cuối khi thoát | **Không có** handler `close-request`/shutdown gọi `flush_now()`. |
| Command history sidebar | Hoạt động (toàn cục, 1 list, tối đa 2000), nhưng `clear_history` **không persist**. |

---

## 1. Kiến trúc thực tế (đã verify trong code)

```text
Application (application.cpp)
├── ThemeManager (CSS light/dark)
├── WorkspaceSession (workspace_session.cpp)
│   ├── SqliteStorage  → ~/.local/share/remin/remin.db
│   │                    (workspace_session.cpp:30)
│   ├── WorkspaceCore  → ws_current_ = workspace in-memory duy nhất
│   ├── Autosaver      → 250ms poll (application.cpp:102), debounce 2s terminal
│   │                    / idle 10s note (session_controller.cpp:37-38)
│   └── SessionController → bridge UI ↔ core
└── MainWindow (main_window.cpp) → TerminalTabView / NoteTabView
```

- **Dòng lưu**: mutation `WorkspaceCore.mark_dirty()` → emit `StateDirty` →
  `autosaver.note_workspace_activity()` (workspace_session.cpp:45-50) → poll flush →
  `workspace_provider` = `core_->persist()` → `save_workspace()` (workspace_core.cpp:368-375).
- **2 bảng chính**: `workspaces` (json = toàn bộ workspace) và `scrollbacks`
  (blob key→string, vốn để ghi scrollback nhưng đang bị tận dụng để chứa cả
  settings, note body, note path, command history). Schema: `sqlite_db.cpp`,
  docs `architecture/storage.md`.

---

## 2. Những gì ĐANG hoạt động (đã lưu được)

### 2.1 Layout pane/window/tab — phần đã lưu
- `Workspace → Window(x,y,width,height) → Tab(title, PaneTree) → Pane(id, state)`
  được JSON-hoá trọn vẹn, gồm **kind split + ratio + focus ids**
  (`workspace.hpp`, `window.hpp`, `pane.hpp`, `serialization.cpp`).
- Mọi mutation STRUCTURE: `add_window/add_tab/split_pane/remove_pane/set_pane_ratio/
  focus_pane/focus_tab` đều `mark_dirty()` + emit → flush trong ~2s.
- **Restore layout**: `MainWindow::restore_workspace()` (main_window.cpp:504-565)
  tái tạo `TerminalTabView` dùng lại đúng core IDs; divider ratio được restore
  qua `restore_paned_ratio` (terminal_tab_view.cpp:16-45). ✅

### 2.2 Note body autosave
- `note_provider` (application.cpp:91-98) → `store_scrollback(noteId)` sau 10s idle.
- Note path lưu riêng dưới key `path:` (session_controller.cpp:135-144).

### 2.3 Command history (phiên bản "history" hiện tại)
- `add_command_history()` (session_controller.cpp:271-300) — ghi **eager** (ngay khi
  chạy lệnh), 1 list toàn cục `settings:command-history`, max 2000, dạng text từng dòng.
- Load lúc startup vào `MainWindow::history_` (main_window.cpp:616-623); sidebar
  History hiện 500 lệnh gần nhất; click → feed lại vào pane đang focus.

### 2.4 Settings hiện có (toàn cục)
- Danh sách key `settings:*` lưu trong bảng `scrollbacks` (session_controller.cpp:14-22):
  `autosave-temp`, `autoreload`, `autoshow-panel`, `unsaved-close`, `theme-dark`,
  `color-profile-fg/bg`, `terminal-fg/bg`, `command-history`.
- Được đọc lại khi ứng dụng khởi động: theme (application.cpp:63-74), sidebar + history.

### 2.5 Snapshot API — tồn tại nhưng không được dùng
- `WorkspaceCore::create_snapshot/restore_snapshot` (workspace_core.cpp:332-359),
  bảng `snapshots`, CLI `snapshot.create` (request_dispatcher.cpp:64).
- **GUI không gọi** — không UI, không lifecycle gắn snapshot.

---

## 3. VẤN ĐỀ THỰC TẾ (gaps — phần chưa có)

### A. Không có khái niệm "History" thật sự
1. **Struct `Workspace` ghi chú "Contains windows, snapshots, history" nhưng
   KHÔNG có field history nào** (workspace.hpp:13-14). Đó chỉ là comment.
2. **History = 1 list command toàn cục duy nhất**, không scoped theo pane →
   tab → window → workspace (audit §3-5 / PHASE 20). `PaneState.command_history`
   (per-pane, pane.hpp:21) tồn tại nhưng **không bao giờ được dùng**.
3. **Không có Window History** (window đã đóng được lưu + đặt tên + restore bằng
   Ctrl+H) — yêu cầu D-3/D-7 option 2, **chưa implement**.
4. **History không tích hợp snapshot**: đúng ra "History" nên là chuỗi trạng thái
   theo thời gian (snapshot), nhưng snapshot không được gắn vào UI/lifecycle nào.
5. `clear_history()` (sidebar) chỉ xoá bản in-memory, **không persist xoá**
   (audit §3-1).

### B. Khả năng "lưu pane/window/tab" mới có 1 nửa
1. **`PaneState` chết (dead)**: cwd / shell / cols / rows / environment /
   command_history / scrollback / interrupted_command đều được serialize nhưng
   **không từng được populate từ terminal sống** (audit §3-4). Terminal mới luôn
   `TerminalPane(shell, "")` → cwd mặc định `$HOME` (terminal_pane.cpp:10-36).
2. **Scrollback: ghi nhưng không bao giờ đọc lại.** `store_scrollback` được gọi
   khi autosave (autosave.cpp:49-60, application.cpp:81-88), nhưng `load_scrollback`
   **chỉ được gọi cho note/settings** — KHÔNG có code nào load scrollback vào VTE
   để phục hồi terminal (grep toàn `src/`). → Tắt máy, mở lại: **terminal trắng, mất scrollback**.
3. **Window geometry x/y/width/height** được serialize (window.hpp:32-35) nhưng
   **không apply cho GTK window khi mở** (D-6 chưa làm).
4. **Note tab KHÔNG được restore** — `restore_workspace()` có TODO rõ ràng
   (main_window.cpp:543). Note body có được autosave, nhưng không có code tạo lại
   NoteTabView từ lưu trữ.
5. **Không flush khi thoát**: không có handler `close-request`/`on_shutdown`
   gọi `autosaver_->flush_now()`. `close_workspace()` (workspace_core.cpp:41-48)
   comment là "final checkpoint handled by shutdown flush" — **nhưng shutdown flush
   đó không tồn tại trong GUI**. Thay đổi cấu trúc phút cuối có thể mất.
6. `persist()` **không cập nhật `last_saved_at`** (workspace_core.cpp:368-375) —
   trường này luôn bằng `created_at`.
7. `restore_snapshot()` set `ws_current_` **nhưng không emit event / không mark_dirty**
   (workspace_core.cpp:351-359) → GUI không được báo, UI widget không rebuild theo state mới.

### C. Settings "3 mức độ" chưa có
1. **Flat global**: mọi key `settings:*` là blob toàn cục duy nhất, không có
   phạm vi app / workspace / per-pane. Không có chỗ cho "3 mức độ".
2. **D-7 — "3 chính sách persistence"** (Default Recovery / Window History /
   Auto-save by Input): đã có struct `PersistencePolicy{default_recovery,
   closed_window_history, input_checkpoint}` + JSON round-trip (persistence_policy.{hpp,cpp}),
   **nhưng không được wire ở đâu**: SessionController không đọc/ghi nó, Autosaver
   không dùng, Settings dialog không hiển thị, CLI không có.
3. **Settings dialog thiếu Persistence page**: dialog chỉ có Appearance (dark),
   Terminal (stub/info), Editor (stub), Behavior (autoreload / autoshow-panel /
   unsaved-close). Không có 3 chính sách (settings_dialog.cpp:31-197).
4. **Lạm dụng bảng `scrollbacks` làm kho settings/history/note metadata**
   (`meta_id()` bọc key thành PaneId để nhét vào blob store — session_controller.cpp:135-144;
   audit §10 kêu gọi tách API settings/content/metadata riêng, ngừng lạm dụng).

### D. Kiến trúc tách lớp chưa hoàn tất
- `MainWindow::history_` (main_window.hpp:155) **duplicate** command history đã
  persist — 2 nguồn sự thật (audit §2, main_window.cpp:616-623).
- `MainWindow` vẫn đọc thẳng `core_->current_workspace()` và giữ sidebar/history/
  find/toolbar/settings (audit verdict §1).
- IPC server (UDS) primitives compile nhưng **chưa nối vào vòng lặp GUI**
  (ipc-cli.md:35-38) — CLI không thể lệnh cho session sống.

---

## 4. Đối chiếu Spec → Hiện trạng

| Spec (ui-audit / work-log) | Hiện trạng code |
|----------------------------|-----------------|
| PHASE 15 A: core mutation ≠ persistence, dirty+autosaver | ✅ Làm xong (workspace_core.cpp `mark_dirty/persist`, autosave.cpp) |
| PHASE 15 B: MainWindow slim, history single-source | ⚠️ `DirectoryTreePanel`/`ContextMenu` tách, nhưng `history_/find/...` vẫn trong MainWindow |
| PHASE 15 C: Persistence/Recovery; Startup restore | ⚠️ Restore terminal-tab layout ✅; scrollback/cwd/note-tab ❌; shutdown flush ❌ |
| PHASE 18 / D-7: 3 persistence policies trong Settings | ❌ Struct có, wire + UI không có |
| PHASE 20 / §3-5: history scoped pane→tab→window→workspace | ❌ 1 list global; per-pane field không dùng |
| D-3: Recovery vs Window History tách bạch | ❌ Window History không tồn tại |
| D-6: Window geometry restore | ❌ Serialize nhưng không apply |
| D-4: PaneState live (cwd/cols/rows/scrollback) | ❌ PaneState dead, không populate |
| Snapshot lifecycle | ⚠️ API + CLI có, GUI không dùng |
| Settings thêm/hiện trong SettingsDialog | ⚠️ Flat keys đọc/ghi được; không 3 mức, không persistence page |
| Notes: 10s idle autosave, path persistence | ✅ Body + path có lưu; restore tab ❌ |

---

## 5. Khuyến nghị (thứ tự tiên quyết)

1. **Định nghĩa "History" thật sự** ở core: thêm field vào domain (VD: per-pane
   `command_history` đã có sẵn → bắt đầu populate; `Window` thêm trạng thái đã đóng;
   `Workspace` có thể thêm `history` — hoặc đưa snapshot thành dòng công cụ thật).
2. **Populate PaneState từ terminal sống** (cwd qua `vte_terminal_get_current_directory_uri`,
   cols/rows, lịch sử lệnh per-pane) — hết "PaneState dead".
3. **Đọc lại scrollback khi restore** — thêm `load_scrollback` vào luồng tạo pane
   (hiện là dead write).
4. **Thêm flush cuối khi thoát**: handler close/shutdown gọi `autosaver_->flush_now()`.
5. **Restore note tab + window geometry** để "lưu window/tab" hoàn chỉnh.
6. **Settings 3 mức theo D-7**: wire `PersistencePolicy` vào Storage/Controller/Autosaver,
   thêm Persistence page vào SettingsDialog, tách bảng settings riêng khỏi `scrollbacks`.
7. Sau cùng: gắn snapshot vào UI (History/Checkpoint) và hoàn tất IPC wiring.

---

## 6. File map đã đọc

- `src/core/{workspace,window,pane}/...hpp` — model (điểm chết chính: pane.hpp:15-23)
- `src/core/workspace_core.cpp` — mark_dirty/persist/snapshot
- `src/core/autosave.cpp` — write_entry/provider, dead scrollback write
- `src/core/serialization.cpp` — JSON round-trip
- `src/storage/{storage.cpp,sqlite_db.cpp}` — persist layer
- `src/gui/application.cpp` — providers + theme + poll (thiếu shutdown flush)
- `src/gui/session/{workspace_session.cpp, session_controller.cpp}` — session + settings/history keys
- `src/gui/window/{main_window.cpp, terminal_tab_view.cpp, settings_dialog.cpp}` — UI restore/history/settings
- `src/gui/terminal/terminal_pane.cpp` — capture_scrollback, spawn cwd
- Docs: `architecture/{overview,storage,autosave-lock,workspace-model,notes,gui,ipc-cli}.md`,
  `ui-audit.md`, `work-2026-09-04.md`