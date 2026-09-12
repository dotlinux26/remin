# History System — Phase A Audit (History panel + persistence pipeline hiện tại)

Status: AUDIT (2026-09-06) — input cho Phase B–G của `docs/design/history-system-spec.md`.

## 1. History panel hiện tại (frozen baseline)

- Sidebar mode switcher `History | Files` (.remin-sidebar-tab) — `main_window.cpp:739-752`.
- History page = `history_scroller_` (ScrolledWindow) → `history_list_` (Gtk::Box VERTICAL, spacing 2) — `main_window.cpp:760-765`.
- `update_history_sidebar()` — `main_window.cpp:812-844`:
  - Nguồn: `controller_->get_command_history()` → `std::vector<std::string>` (đã mất provenance).
  - Mỗi entry = `.remin-history-item` Button; click → `p->feed(cmd)` vào **pane đang focus** (insert, không execute — đã đúng spec §2).
  - Chỉ show 500 entry mới nhất, cuộn xuống đáy. KHÔNG search, KHÔNG timestamp, KHÔNG provenance display.
- `clear_history()` — `main_window.cpp:846-850`: `controller_->clear_command_history()` + refresh.
- Menu History: "Show/Hide Panel" (`win.toggle_history`), "Clear History" (`win.clear_history`) — `main_window.cpp:251-260`.
- Toolbar toggle: `sidebar-toggle` button "sidebar-show/hide-symbolic", tooltip "Toggle Sidebar (Ctrl+P)" — `main_window.cpp:200-210`.
- `toggle_history_sidebar()` — `main_window.cpp:1745-1765`: `main_paned_->set_position(220|0)`; `apply_initial_sidebar_state()` (1767-1783) theo setting `settings:autoshow-panel`.

## 2. Keyboard hiện tại

- Global key controller CAPTURE trên window → `on_find_key_pressed` (`main_window.cpp:1529-1602`).
- `Ctrl+F` → find; `Ctrl+H` → find bar replace mode (`1560-1563`) — **không phân biệt Shift**, nên `Ctrl+Shift+H` hiện bị nhét vào nhánh Ctrl+H → lỗi baseline cho Phase B.
- `Ctrl+P` → `toggle_history_sidebar()` (1580).
- ⇒ Phase B: chèn nhánh `Ctrl+Shift+H` (chạy trước nhánh Ctrl+H) → open/focus History panel (sidebar + history mode).

## 3. Command history — đã canonical per-pane (hầu hết "làm sẵn")

- Nguồn: VTE "commit" → `TerminalPane::on_commit_trampoline` (`terminal_pane.cpp:264-298`): phát hiện `Ctrl+C \x03` (interrupted_command), tách nhập theo `\n`, trim, gọi `on_command_(line)`.
- `TerminalTabView::set_command_callback` → `add_history(pid, cmd)` → `controller_->add_command_to_pane(tab, pane, cmd)` → core (`terminal_tab_view.cpp:220-222, 302-304, 474-476`).
- `WorkspaceCore::add_command_to_pane` (`workspace_core.cpp:367-389`): per-pane `command_history` **vector<string>**, dedupe adjacent, cap 1000 (`kMaxCommandHistoryPerPane`), `mark_dirty()`.
- `clear_command_history` (`391-404`): xóa toàn bộ pane. Test: `tests/unit/history_test.cpp` (isolation/adjacent/cap 1000/clear persist) — đều pass.
- **Gap:** `command_history[]` là `vector<std::string>` — **thiếu timestamp**; spec §2 yêu cầu `CommandRecord { command · timestamp · pane · tab · window }`.
  `HistoryEntry{window,tab,pane,command}` (`workspace.hpp:67-72`) đã có identity nhưng KHÔNG timestamp và bị flatten thành string khi qua `get_command_history()` (`session_controller.cpp:407-416`).
- ⇒ Phase C: nâng thành `CommandRecord` (thêm timestamp + identity), giữ nguyên dedupe/cap/isolation; pipeline capture hiện tại là đúng.

## 4. Transcript — CHƯA có gì tách biệt

- `TerminalPane::capture_scrollback()` (`terminal_pane.cpp:188-202`): `vte_terminal_get_text_range_format(VTE_FORMAT_TEXT, -(rows+10000), 0, rows, 0, &len)` = **screen-state snapshot**, KHÔNG phải journal (mất nội dung sau `clear`, spec §3/§4).
- VTE tự spawn PTY qua `vte_terminal_spawn_async` — hiện **không giữ PTY fd**, chỉ thấy input qua "commit" (không thấy program output). Không có chỗ quan sát output để tee transcript.
- ⇒ Phase D (spec §13 audit trước khi chọn journal): phải **Remin-owned recorder** độc lập với VTE current screen. Con đường khả thi nhất: tự `openpty` → spawn shell gắn slave → vòng đọc master feed cả `vte_terminal_feed` lẫn transcript recorder. Việc này thay cách spawn hiện tại (giữ VTE render, giữ HISTFILE/env/cwd logic). Cần cân nhắc interference/PTY/window-size; UI thread không block (spec §14).
- VTE `scrollback_lines=10000`, capture bound `-(rows+10000)` — tránh hang VTE 0.76 (đã note trong code `194`).

## 5. Window History — CHƠI CONTEXT CHƯA CÓ

- Recovery đã có: `WorkspaceCore::checkpoint(reason)` ("recovery"/"autosave"/"window_history"/"manual") → transactions tạo snapshot rows (`storage.cpp:25-153`); shutdown qua `signal_close_request` → `checkpoint_recovery()` (`main_window.cpp:139-144`); restore qua `restore_workspace()`.
- **Chưa có:** capture closed-window final state + label + timestamp thành `ClosedWindowSnapshot` khi đóng Window với policy ON (`Workspace` không giữ closed-window history; `snapshot_ids` chỉ là recovery/manual). Policy setting (`settings:persist-open-windows`) đã tồn tại (mặc định ON) — dùng chung hay thêm key riêng cần quyết định ở Phase E.
- ⇒ Phase E: model `ClosedWindowSnapshot`, storage riêng (spec §15), lifecycle đóng window (GUI V1 chỉ có 1 window đang dùng; "đóng window" = đóng app/workspace — cần gắn với close path hiện tại).

## 6. Storage hiện tại (SQLite)

- Schema: `workspaces`, `scrollbacks(pane_id,…)`, `snapshots(id,workspace_id,timestamp,revision,size_bytes,state_json,schema_version,generation,reason)` — `storage.cpp`.
- Blob generic: `store_scrollback/load_scrollback` keyed `PaneId` — hiện dùng cho cả note bodies + settings (metadata ở `session_controller.cpp:24-28`).
- Spec §15: transcript/closed-window nên có **dedicated APIs** (record/chunk riêng), không nhét vào scrollbacks/settings generic.
- SqliteDb có txn RAII (`begin_transaction`/COMMIT/ROLLBACK) — dùng lại cho chunked transcript.

## 7. Hệ quả cho các phase

- **Phase B**: 1 branch Ctrl+Shift+H. (không đụng UI).
- **Phase C**: `CommandRecord` thay `vector<string>`; serialize round-trip update (`kSchemaVersion` bump + migration); GUI vẫn dùng CommandRecord.
- **Phase D**: Remin-owned transcript recorder + chunked storage (dedicated), không block UI, buffered qua checkpoint pipeline.
- **Phase E**: `ClosedWindowSnapshot` + dedicated storage + lifecycle.
- **Phase F**: 3 subview [Commands][Transcripts][Windows] trong History panel — wire vào panel hiện có (không restyle).
- **Phase G**: golden test theo spec §18 + acceptance §19 (7 điều đồng thời).