# Design — Workspace Persistence/Recovery Pipeline (REMIN CORE FEATURE)

> **Status**: IMPLEMENTATION-GATED — đã qua review của user với 5 điểm sửa + thêm
> schema_version/P2.5 (xem chốt ở cuối doc). Sau các chỉnh sửa này user cho **GO
> implement P1–P5**. Không viết code từng gap rời rạc; mỗi phase có gate + test.
> Tiền đề: report `docs/report-history-save-pane-window-tab.md` xác nhận ta mới có
> foundation; capture/restore chưa đi qua một full end-to-end cycle nào.
> Doc này chốt **canonical state model + capture/restore pipeline + atomic
> checkpoint** trước khi code.

---

## 0. Mục tiêu

Một vòng lặp end-to-end duy nhất chạy được:

```text
LIVE WORKSPACE
   → CAPTURE (mọi thứ user đang nhìn)
   → ATOMIC PERSIST (1 transaction, không half-written)
   → PROCESS TERMINATION
   → RECREATE
   → RESTORE
   → VERIFY (không "looks okay")
```

**Demo 1 đáng giá nhất cả project**: đóng Remin → mở lại → scrollback text về
**y nguyên (EXACT)** và màn hình căn về **prompt/bottom**. 

> ⚠️ **Wording cam kết (chốt với reviewer)**: ta restore *scrollback = EXACT*,
> còn *viewport = bottom/prompt trong V1*. Không tuyên bố "restore đúng chỗ tuyệt
> đối" khi đang scroll lên — VTE 0.76 không expose `get/set_scrollback_offset`
> (§5.4). Mọi acceptance test phải theo đúng 2 mức này, không ghi viewport ✓ khi
> user đang scroll lên.

---

## 1. Định nghĩa lại "Remin State" = Workspace Checkpoint

Không nghĩ "save window/tab/pane riêng lẻ". Checkpoint = chụp **toàn bộ những gì
user đang nhìn + context để tiếp tục công việc**:

```text
Workspace
├── identity (id, name, created_at, last_saved_at, schema_version, generation)
├── ui state
│   ├── active window
│   └── directory-tree state (current_dir, expanded[], selected, filter, scroll anchor)
├── windows[]
│   ├── window id, title
│   ├── geometry (width, height; x, y nếu compositor cho phép)
│   ├── active tab
│   ├── tabs[]
│   │   ├── tab id, title, kind (Terminal | Note)
│   │   │
│   │   │  ── Terminal ─────────────────────────────
│   │   │  ├── active pane
│   │   │  ├── pane tree (kind + ratio)
│   │   │  │    └── pane[]
│   │   │  │        ├── cwd                      (shell context cwd)
│   │   │  │        ├── shell
│   │   │  │        ├── cols / rows
│   │   │  │        ├── environment              (V1: KHÔNG persist — §3.2)
│   │   │  │        ├── scrollback (full text)
│   │   │  │        ├── viewport  (V1: prompt/bottom — xem §5.4)
│   │   │  │        ├── command_history[]        (per-pane, canonical)
│   │   │  │        └── interrupted_command      (metadata)
│   │   │  │
│   │   │  ── Note ───────────────────────────────
│   │   │  └── note state
│   │   │      ├── document_id, path
│   │   │      ├── content
│   │   │      ├── modified
│   │   │      ├── cursor
│   │   │      ├── scroll
│   │   │      ├── preview_enabled
│   │   │      ├── split_ratio
│   │   │      └── sync_scroll
│   │   └── (surface contract: capture_state()/restore_state())
```

**Source of truth (bất biến)** — boundary Capture/GUI ≠ Core (xem §12):

```text
VTE/PTY  →  TerminalPane.runtime_capture adapter  →  TerminalRuntimeSnapshot
        →  SessionController / WorkspaceSnapshotBuilder  →  WorkspaceCore::apply_runtime_state
        →  PaneState (canonical, thuộc Core)
WorkspaceCore  →  canonical state  →  restore adapter (GUI)  →  TerminalPaneView  →  VTE + new PTY/shell
```

KHÔNG có: VTE → SQLite, VTE → callback rời rạc, MainWindow → history vector 🔥,
và **KHÔNG có Core gọi thẳng GUI widget**. `WorkspaceCore` là authoritative cho
**canonical state + giao dịch + generation**; capture adapter + dựng lại view là
trách nhiệm của **GUI/runtime**. Widget GTK chỉ là runtime representation.

---

## 2. Điều tra hiện trạng (căn cứ verify trong code)

| Khoản mục | Hiện trạng |
|-----------|-----------|
| PaneState (cwd, shell, cols, rows, env, history, scrollback, interrupted) | tồn tại (pane.hpp:15-23), serialize được (serialization.cpp) nhưng **dead** — chưa từng được populate từ terminal |
| Scrollback | GHI được (autosave.cpp:49-60 → scrollbacks table), **không bao giờ đọc lại** (grep: load_scrollback chỉ cho note/settings) |
| Command history | 1 list global `settings:command-history` (session_controller.cpp:271-314); MainWindow giữ bản copy `history_`; per-pane `PaneState.command_history` không dùng |
| Note tab | KHÔNG nằm trong domain `Tab` (chỉ terminal + PaneTree); body lưu dưới note id nhưng restore tab không có (main_window.cpp:543 TODO) |
| Directory tree | `DirectoryTreePanel::State` đã có đủ field (directory_tree_panel.hpp:59-68) nhưng private, chưa có capture/restore API |
| Window geometry | `Window.x/y/width/height` serialize (window.hpp:32-35) nhưng không apply khi mở; `set_default_size(1024,768)` cố định (main_window.cpp:21) |
| Atomicity | `SqliteDb` **không có** transaction helper (sqlite_db.hpp:16-36) — các statement lẻ |
| Shutdown flush | **Không có** handler close/shutdown gọi `flush_now()`; `close_workspace()` comment trông cậy "shutdown flush" không tồn tại (workspace_core.cpp:41-48) |
| Snapshot | API + CLI `snapshot.create` có, GUI không dùng; `restore_snapshot` không emit event (workspace_core.cpp:351-359) |
| VTE 0.76 API (đã kiểm tra header) | ✅ `get_current_directory_uri`, `get_text_range`, `feed`, `set_size`, `get_column/get_row_count`, `get_cursor_position` — ❌ **KHÔNG có** `get/set_scrollback_offset`, ❌ **KHÔNG có** `vte_pty_get_child_process_id` |
| Shell spawn | `vte_terminal_spawn_async` (terminal_pane.cpp:29-36); có thể lấy PID shell qua `GChildWatchFunc` callback |
| CWD mặc định | `$HOME` (terminal_pane.cpp:22-24) |

---

## 3. Make PaneState LIVE

### 3.1 Contract mới của TerminalPane

```cpp
// Adapter thuộc GUI/runtime: đọc trạng thái VTE hiện tại → snapshot thuần data
// (KHÔNG phải PaneState — trung gian để Core không phụ thuộc GUI, xem §12).
remin::core::TerminalRuntimeSnapshot runtime_capture();

// Adapter thuộc GUI/runtime: dựng lại VTE theo canonical PaneState.
void runtime_restore(const remin::core::PaneState& state);
```

- Capture gọi **khi checkpoint** (không phải mỗi keystroke) — một lần đọc.
- `WorkspaceSnapshotBuilder` (tầng SessionController) ráp `TerminalRuntimeSnapshot`
  thành `PaneState` canonical và đẩy vào `WorkspaceCore::apply_runtime_state(...)`.
- Restore chạy một lần ở đầu đời pane (startup restore hoặc pane mới).

### 3.2 Nguồn dữ liệu từng field

| Field | SOURCE (cơ chế thực tế) |
|-------|--------------------------|
| `cwd` | §4 — shell context cwd |
| `shell` | shell_ đang dùng (shell.hpp.detect_default_shell hoặc PaneState đã lưu) |
| `cols`, `rows` | `vte_terminal_get_column_count/row_count` |
| `environment` | **V1: KHÔNG persist.** Shell mới spawn inherit env mặc định bình thường; blocklist (TOKEN/PASSWORD/SECRET/KEY) không đủ an toàn. Acceptance về sau chỉ qua explicit allowlist / user opt-in |
| `command_history` | §6 — stack từ commit của pane này (bắt buộc ở P2, không trễ) |
| `scrollback` | §5.1 — `vte_terminal_get_text_range` full scrollback + visible |
| `interrupted_command` | §6.2 — `InterruptedCommand{command, timestamp, source}` với `source = CtrlC | process_exit | unknown`; V1 CHỈ gán `CtrlC` khi dựng được từ commit `\x03` thực tế, không suy diễn |

Không populate field bằng default chỉ để serialization test pass.

---

## 4. Pane CWD — "shell context cwd"

### 4.1 Định nghĩa

> **Pane cwd = thư mục mà interactive shell đang đứng ($PWD), tức context người
> dùng muốn tiếp tục** — KHÔNG phải cwd của bất kỳ child process đang chạy
> (ssh/vim/python/sudo…).

Ví dụ restore đúng: Pane A → `~/research/gitlab`, B → `~/research/gitlab/poc`,
C → `/tmp/testing`; mỗi pane spawn shell mới **đúng thư mục đó**.

### 4.2 Cơ chế (theo thứ tự ưu tiên, dựa trên API VTE 0.76 đã verify)

1. **Primary — `vte_terminal_get_current_directory_uri()`** (OSC 7): chính là
   "$PWD của shell" do shell báo qua OSC 7 (`\e]7;file://host/path\a`). Đúng ngữ
   nghĩa "shell context", không đụng /proc. Giới hạn: chỉ hoạt động khi shell có
   OSC 7 (zsh/fish có mặc định; bash cần PROMPT_COMMAND — ta **không** sửa rc của
   user; ship kèm một shell-integration opt-in về sau).
2. **Fallback — `/proc/<shell_pid>/cwd`**: shell_pid lấy qua `GChildWatchFunc`
   của `vte_terminal_spawn_async` (đã dùng spawn nên cộng thêm callback). CHỈ đọc
   PID **của shell** (interactive `cd` mới đổi), không phải child foreground →
   đúng semantics, không "blind read kid process".
3. **Fallback cùng — cached cwd**: cập nhật mỗi lần `commit`/checkpoint (giá trị
   cuối từng đọc được), rồi `$HOME` nếu rỗng.

Restore: kiểm tra đường dẫn còn tồn tại → spawn tại đó; nếu mất (volume chưa
mount) → log + rơi về `$HOME`.

---

## 5. Scrollback — "linh hồn" của Remin

### 5.1 Capture (đã verify khả dụng)

- `vte_terminal_get_text_range(vte, start_row_âm, 0, end_row, 0, VTE_FORMAT_TEXT, …)`
  với `start_row` âm để phủ toàn bộ scrollback + vùng visible. Row hiện tại =
  `get_row_count()`. (Kỹ thuật quét scrollback chuẩn của các terminal có restore.)
- `cols/rows` bên cạnh để dựng đúng kích thước grid.

### 5.2 Restore — deterministic lifecycle KHÔNG sleep/paste hacks

```text
1. Tạo VTE (chưa spawn shell), set size theo captured cols/rows,
   set scrollback_lines = max(captured length, 10000)
2. vte_terminal_feed(captured_text)      ← dựng lại toàn bộ visual + scrollback
                                            từ buffer widget (không qua PTY)
3. vte_terminal_spawn_async(shell, captured cwd)   ← env INHERIT mặc định (§3.2);
                                            shell prompt in xuống, dưới đúng
                                            nội dung đã restore
4. Viewport: căn về prompt/bottom (§5.4)
```

Thứ tự feed-trước-spawn đảm bảo không bị "bash in prompt rồi paste lệch".
Không có `sleep(100ms)` ở bất kỳ đâu.

**Ta KHÔNG restore quá trình** (không hồi sinh vim/ssh). Ta restore: shell
context (cwd) + scrollback + kích thước + viewport, rồi spawn shell mới với env
mặc định.

### 5.3 Fidelity (phân biệt rõ)

| Loại | Có restore không |
|------|------------------|
| PTY runtime state (process đang chạy) | ❌ vĩnh viễn ngoài scope |
| Scrollback text | ✅ EXACT (get_text_range → feed) |
| Visible screen (đang ở prompt / output giữa chừng) | ✅ text restore; ANSI/color/format fidelity phải qua §5.5 gate |
| Viewport khi user đang scroll lên | V1: chỉ bottom/prompt — xem §5.4 (không claim scroll-đúng-chỗ) |

### 5.4 Viewport — giới hạn trung thực của VTE 0.76

Đã kiểm tra header: **VTE 0.76 không expose `get/set_scrollback_offset`** và không
có `vte_pty_get_child_process_id`. Vì `set_scroll_on_keystroke/output = TRUE` nên
trạng thái "đang scroll lên" chỉ xảy ra khi idle.

**Acceptance rõ ràng (chốt với reviewer)**:
- `scrollback` → **EXACT**
- `viewport` → **bottom/prompt trong V1** (không claim "đúng chỗ tuyệt đối" khi
  đang scroll lên)

V1: viewport restore = **căn về prompt/bottom** — đúng y cho kịch bản demo
(user close lúc đang ở prompt/keyboard). Scrollback text vẫn có đủ, user cuộn
lên là thấy.
- Ghi rõ vào Known Limitations; enhancement tương lai = OSC-based scroll tracking
  (shell-integration) hoặc bản VTE khác.

### 5.5 Capture/Restore Fidelity Gate (bắt buộc — KHÔNG coi là "đã chứng minh" vì API tồn tại)

`get_text_range()` trả **text**, nhưng runtime terminal còn cursor, attributes,
alternate screen, hyperlinks, colors… "feed text lại" chưa chắc tái tạo
byte-for-byte visual state. → Cần gate kỹ thuật riêng, test nhỏ:

```text
VTE live terminal
   ↓ generate ANSI/control sequences      (color, bold, cursor move, alt-screen, NL/CR, wide char)
↓ capture (get_text_range VTE_FORMAT_TEXT)
↓ new VTE  →  feed
↓ compare visual/text semantics:  dòng output từng phần còn đúng thứ tự + nội dung,
                                   ANSI color/format có giữ được như kỳ vọng không
```

**Mục tiêu V1 (ghi vào doc, khỏi tự đánh lừa acceptance)**:
> restore terminal *textual scrollback faithfully*, không restore arbitrary
> runtime terminal state (cursor vị trí tuyệt đối, alt-screen buffer, hyperlink
> GRID mảnh…). Nếu gate này fail với thứ đang claim → hạ claim, không mở rộng scope.

---

## 6. Per-Pane Command History (canonical)

```text
Workspace → Window → Tab → Pane → command_history[]
```

### 6.1 Dòng chảy mới

- TerminalPane đã có `on_command_` (commit → tách theo `\n`, trim —
  terminal_pane.cpp:162-182) — **đây là nguồn per-pane đúng chỗ**.
- Thay vì đẩy lên global, `TerminalTabView::add_history` (terminal_tab_view.cpp:102-108)
  đổi thành: `controller_->add_command_to_pane(tab, pane, cmd)` → `WorkspaceCore`
  append vào `PaneState.command_history` (dedupe lệnh liền kề, cap ~1000).
- **Bỏ** `settings:command-history` làm canonical (giữ nếu cần chỉ để migrate cũ).

### 6.2 Interrupted command

```cpp
struct InterruptedCommand {
    std::string command;
    std::int64_t timestamp_us;           // khi phát hiện
    enum class Source { CtrlC, ProcessExit, Unknown } source;
};
```

- `source = CtrlC` **CHỈ** khi thực sự quan sát được commit chứa `\x03` ngay sau
  command đó (heuristic cũ "lệnh cuối + `\x03`" là suy diễn — SIGTERM, app crash,
  user đóng pane, tool tự exit, process con nhận Ctrl+C mà shell không nhận đều
  KHÔNG phải Ctrl+C).
- `ProcessExit` = spawn callback báo shell/chương trình thoát (GChildWatchFunc).
- `Unknown` = mọi case còn lại. V1 guarantee tối đa: **chỉ ghi `CtrlC` khi
  quan sát được**, không gọi mọi interrupted process là Ctrl+C.

### 6.3 Workspace History = view/query, không phải store thứ hai

- Sidebar History đổi thành **aggregate query trên toàn bộ panes** của workspace
  hiện tại, giữ provenance `(window → tab → pane → command)`.
- `clear_history()` phải persist (xoá các `PaneState.command_history`), không xoá
  bản nhớ tạm (main_window.hpp history_ bị bỏ).

---

## 7. Note Tab thành first-class domain state

- Domain `Tab` thêm `TabKind kind` + `std::optional<NoteTabState>` bên cạnh
  `PaneTree` (window.hpp:13-26). Serialization có **migration**: JSON cũ (không có
  kind) → mặc định terminal.
- `NoteTabState`: document_id, path, title, content, modified, cursor (iter),
  scroll, preview_enabled, split_ratio, sync_scroll.
- Restore: `restore_workspace()` dựng `NoteTabView` từ state, set content/cursor/
  scroll/preview/split — không phải "chỉ lưu được body".

---

## 8. Workspace UI State (directory tree + active window)

- `Workspace` thêm `UiState ui`:
  ```cpp
  struct DirectoryTreeState {
      std::filesystem::path current_dir;
      std::vector<std::filesystem::path> expanded;
      std::filesystem::path selected;
      std::string filter;
      std::filesystem::path scroll_anchor;
      double anchor_offset{0.0};
  };
  ```
- `DirectoryTreePanel` expose `capture_state()` / `apply_state()` (field đã có sẵn
  trong `State`, directory_tree_panel.hpp:59-68 — giờ public hóa).
- Restore: apply trước khi map, **không refresh-to-top mạnh** (panel hiện đã giữ
  được việc này qua scroll anchor; chỉ cần nạp state đầu vào).
- Active window: `focus_window_id` đã có — apply khi restore.

---

## 9. Window geometry

- Capture (khi checkpoint/close): đọc size + vị trí từ `Gdk::Toplevel`
  (`get_width/height`; position đọc theo Wayland) → `Window.x/y/width/height`.
- Restore: `set_default_size(captured w/h)` luôn; `move(x,y)` **chỉ khi nền hỗ trợ**
  (D-6: Wayland compositor có thể từ chối) — fail-safe về default.

---

## 10. Atomic checkpoint (snapshot = transaction)

### 10.1 Semantics

```text
BEGIN IMMEDIATE
  → capture toàn bộ workspace (json + scrollback mỗi pane)
  → validate (ids hợp lệ, không half tree, generation tăng)
  → write: workspaces + n×(scrollbacks) + 1 snapshot row (reason, generation)
COMMIT
```

Một checkpoint lỗi = rollback, **không bao giờ** thành "latest recovery".

### 10.2 Storage changes

- `SqliteDb` thêm **transaction RAII**: `begin()/commit()/rollback()` (giữ mutex).
- Bảng `snapshots` thêm:
  `schema_version INTEGER NOT NULL`, `generation INTEGER NOT NULL`,
  `reason TEXT NOT NULL` (`recovery|autosave|window_history|manual`).
- `WorkspaceCore::checkpoint(reason)` — API hợp nhất autosave/close/manual:
  chụp + validate + ghi trong 1 transaction + set `generation`, `last_saved_at`.
- **Recovery = latest valid committed checkpoint** (max generation, trạng thái
  committed). Autosave hiện tại (per-resource viết lẻ, workspace_core.cpp:368-375)
  được thay bằng `checkpoint(reason=autosave)` — vì scale nhỏ, một workspace
  JSON + scrollback trong 1 transaction là rẻ và đúng atomic.

### 10.3 Schema version vs Generation (2 khái niệm RIÊNG BIỆT)

```json
{ "schema_version": 3, "generation": 108 }
```

| Khái niệm | Đổi khi | Ý nghĩa |
|-----------|---------|---------|
| `schema_version` | cấu trúc dữ liệu thay đổi (thêm TabKind, NoteTabState, PluginState…) | gắn với **shipping code**, cần migration khi tăng |
| `generation` | mỗi checkpoint thành công | số thứ tự checkpoint, monotonically tăng |

`generation 101 → 102 → 103`; nếu 104 lỗi → latest valid = 103. Recovery không
bao giờ trỏ vào half-written. Migration theo `schema_version` là bất biến khi
thêm domain state mới (TabKind/NoteTabState/PluginState sau này).

---

## 11. Ba persistence option (D-7) giờ mới có nghĩa

- **Option 1 — Recovery**: shutdown/restart → `checkpoint(recovery)`, hiển thị
  đúng trạng thái mới nhất.
- **Option 2 — Window History**: user đóng Window → snapshot window đó
  (`.closed_window_history`). **Chưa implement UI (bài sau)** — policy chỉ để
  sẵn field.
- **Option 3 — Input checkpoint**: ENTER → checkpoint ngay; typing → idle 10s →
  checkpoint. **Checkpoint TOÀN workspace** (Window A + B + C đồng thời), không
  chỉ pane vừa gõ. → Autosaver gọi `checkpoint(reason=autosave|input)`.

`PersistencePolicy` struct (persistence_policy.hpp) wire vào:
SessionController read/write + Autosaver dùng `input_checkpoint` để quyết định
flush → checkpoint vs bỏ qua.

---

## 12. Layered data flow (bất biến)

```text
GUI runtime  (TerminalPane / NoteTabView / DirectoryTreePanel — capture adapters)
 ↓ runtime_capture() → TerminalRuntimeSnapshot / NoteState / TreeState
SessionController  (orchestration duy nhất + WorkspaceSnapshotBuilder)
 ↓ apply_runtime_state(...)
WorkspaceCore  (canonical state + validate + checkpoint — authoritative)
 ↓ transaction
Storage  (SqliteDb txn RAII, schemas)
```

Restore (chiều ngược): `core canonical → SessionController → runtime adapter
(GUI) → VTE + PTY/shell / editor / tree`.

**Bất biến**: WorkspaceCore **KHÔNG import GUI** và **KHÔNG gọi
`TerminalPane::capture_state()`** — Core con không định nghĩa đám widget. Capture
adapter thuộc GUI/runtime; canonical state + giao dịch thuộc Core. Tách 2 thứ đó
là lý do tồn tại của `TerminalRuntimeSnapshot` + `WorkspaceSnapshotBuilder`.

MainWindow/NoteTabView/DirectoryTreePanel **không** đọc/ghi Storage thẳng
(hiện đang vi phạm: main_window history_ / add_history global).

---

## 13. Per-field: SOURCE → SERIALIZATION → STORAGE → RESTORE → VERIFY

| Field | SOURCE | SERIALIZATION | STORAGE | RESTORE | VERIFY |
|-------|--------|---------------|---------|---------|--------|
| workspace id/name | core | json | workspaces | load | tên đúng |
| schema_version | core (const) | json + snapshots.schema_version | workspaces/snapshots | migration | tăng khi data schema đổi, không đổi khi checkpoint |
| generation | core counter | json + snapshots.generation | workspaces/snapshots | recovery chọn max | tăng sau mỗi checkpoint |
| last_saved_at | checkpoint() | json + column | workspaces | — | != created_at sau save |
| active window | core focus_window_id | json | workspaces | apply focus | đúng window active |
| dir tree state | DirectoryTreePanel::capture_state | json UiState | workspaces | apply_state() | expanded/selected/filter tái hiện |
| window geometry | Gdk::Toplevel | Window.x/y/w/h | workspaces | set_default_size/move | size khớp, pos nếu nền cho phép |
| window/tab ids + titles | core | json | workspaces | recreate views | đủ window/tab, đúng title |
| tab active | core focus_tab_id | json | workspaces | set_visible_child | tab activation đúng |
| pane tree + ratios | core | json (kind/ratio) | workspaces | build pane tree + restore ratio | split giống y |
| pane shell | PaneState.shell | json | workspaces | spawn shell đó | shell đúng |
| pane cwd | cwd shell-context (§4) | json | workspaces | spawn tại cwd (tồn tại) | `pwd` bằng |
| pane cols/rows | vte get_column/row_count | json | workspaces | vte set_size | grid khớp |
| pane env | — V1: KHÔNG persist (§3.2) | — | — | spawn inherit env mặc định | shell chạy được; không secret nào bị lưu |
| scrollback | get_text_range (full) | json PaneState.scrollback (hoặc scrollbacks table) | transaction | vte feed trước spawn | output cũ còn đủ (EXACT) — §5.5 gate |
| viewport | V1 = prompt/bottom | json (marker) | workspaces | align bottom | dòng prompt trên đúng screen (KHÔNG claim scroll-đúng-chỗ) |
| command_history[] | TerminalPane commit stack | json per-pane | workspaces | khôi phục per pane | mỗi pane history riêng |
| interrupted_command | InterruptedCommand{cmd, ts, source} (§6.2) | json | workspaces | hiển thị metadata; source ghi đúng sự thật | chỉ CtrlC khi quan sát `\x03` |
| note tab kind/state | NoteTabView | json NoteTabState | workspaces | recreate NoteTabView + apply | content/cursor/scroll/preview/split đúng |
| note modified | is_modified() | json | workspaces | · resync dirty-dot | đúng trạng thái unsaved |
| active/focused pane | core focus_pane_id + GUI click | json | workspaces | apply focus + CSS class | focus đúng pane |

---

## 14. Lifecycle changes

1. `Application::on_activate` mở workspace xong → **không** tự tạo tab mới nếu có
   checkpoint hợp lệ (main_window.cpp:120-123 đang bọc restore bằng
   `if (tabs_.empty()) new_terminal_tab()`); restore đầy đủ (§7, §8, §9).
2. Close/quít: `MainWindow` / `Application` handler close → `checkpoint(recovery)`
   → `flush_now()` → default close. (Bug hiện tại: không có handler này.)
3. `Autosaver::write_entry` Kind::Terminal đổi thành một phần của checkpoint
   transaction (không viết rời bảng scrollbacks ngoài transaction).
4. `restore_snapshot` bổ sung emit event + mark_dirty để GUI rebuild (bài sau khi
   gắn UI).

---

## 15. Giới hạn (known limitations — ghi rõ, không giấu)

- **Viewport chính xác khi đang scroll lên**: VTE 0.76 không có
  `get/set_scrollback_offset` → V1 căn về prompt; tăng fidelity bằng shell
  integration (OSC-based scroll tracking) ở phase sau. Acceptance: scrollback
  EXACT, viewport bottom/prompt — không hơn.
- **ANSI/color/format fidelity**: `get_text_range` → text, `feed` dựng lại grid;
  có thể không tái tạo y nguyên cursor/alt-screen/hyperlink GRID. §5.5 gate quyết
  claim; nếu fail → hạ claim, không expand scope.
- **OSC 7 cwd**: phụ thuộc shell có OSC 7; fallback /proc chỉ đọc PID shell
  (không phải child) — hiếm khi sai, nhưng vẫn là heuristic.
- **Restore không hồi sinh process** (vim/ssh đang chạy bị "ngắt"): đúng thiết kế.
- **environment KHÔNG được persist ở V1** (blocklist không đủ an toàn); về sau chỉ
  qua explicit allowlist / user opt-in.
- **interrupted_command**: V1 chỉ gán `source=CtrlC` khi quan sát `\x03` thực tế;
  mọi interrupt khác ghi `ProcessExit`/`Unknown`, không đoán.
- **Note cursor/scroll** chính xác tới iter/offset; scroll tuyệt đối phụ thuộc độ
  cao dòng qui hồi — best-effort.

---

## 16. Golden Acceptance Test (mục tiêu của cả project)

Kịch bản script thủ công (ghi checklist, **không "looks okay"**).

**Workspace: GitLab Audit**
- Window 1: Tab Recon (2 panes: gõ `pwd`, `nmap…`, `ffuf…`, 1 lệnh CTRL+C) + Tab Source (Note)
- Window 2: Tab Testing (2 panes: `python poc.py`, `nc …`)
- Note: edit, scroll, preview ON, split
- Directory: expand project/src, scroll tới exploit/

**Sau đó**: checkpoint → terminate Remin → start Remin.

**PHẢI restore**: window 1+2 ✓, tabs ✓, pane split ✓, từng pane `cwd` ✓ (check
`pwd` thực tế), per-pane shell history ✓, terminal scrollback ✓ (**EXACT**),
viewport = prompt/bottom ✓ (không claim scroll-đúng-chỗ), ANSI/color trong
scrollback (theo §5.5 gate — phải ghi kết quả thực tế), note content ✓, note tab ✓,
note scroll/preview ✓, directory tree + expanded ✓, active window/tab/pane ✓,
layout ratios ✓.

Báo cáo cuối cùng phải có, cho từng field:
`SOURCE → SERIALIZATION → STORAGE → RESTORE → UI VERIFICATION record`.
Không có mục nào được ghi "looks okay" thay cho record thực sự.

---

## 17. Phase plan (thứ tự — mỗi phase có gate + test)

```text
            REMIN CORE FEATURE
                   │
   ┌───────────────▼───────────────┐
   │ P1 Domain: Tab kind/NoteState │ UNITS: serialization round-trip
   │       UiState, migration      │         + schema_version migration
   ├───────────────────────────────┤
   │ P2 Runtime Capture: runtime_capture│ UNIT: cwd/pwd, cols/rows,
   │   adapter + command_history   │         command_history[p] populated
   ├───────────────────────────────┤
   │ P2.5 Fidelity Gate (§5.5)    │ GATE: ANSI/color + scrollback compare;
   │                               │       nếu fail → hạ claim, không expand scope
   ├───────────────────────────────┤
   │ P3 Transaction: SqliteDb txn  │ UNIT: checkpoint atomicity,
   │       checkpoint(reason),     │       schema_version + generation,
   │       autosave→checkpoint,    │       recovery=latest valid
   │       shutdown flush          │
   ├───────────────────────────────┤
   │ P4 Restore: full restore      │ MANUAL: note + tree + geometry + focus
   │       workspace (terminals,   │
   │       notes, tree, geo)       │
   ├───────────────────────────────┤
   │ P5 Golden acceptance + report │ GOLDEN WORKFLOW §16 (recorded)
   └───────────────┬───────────────┘
                   │
      CORE PERSISTENCE WORKS
                   │
   ┌───────────────▼───────────────┐
   │ Option 1: Recovery            │ shutdown/restart → checkpoint(recovery)
   │ Option 2: Window History UI   │ bài sau, CHỈ SAU KHI P1–P5 PASS
   │ Option 3: Input checkpoint    │ ENTER → checkpoint; typing → idle → checkpoint
   └───────────────────────────────┘
```

**KHÔNG implement lúc này**: Window History UI, Ctrl+Shift+H (≠ Ctrl+H find/replace),
snapshot browser, portable export, plugin persistence. Đó là *consumer* của engine
này. TerminalTab/NoteTab/PluginTab sau này đều nói với Remin cùng một contract
`runtime_capture()/runtime_restore()` → workspace platform mới thành hình.

**Thứ tự bắt buộc**:
P1–P5 hoàn thành (**Core Persistence Works**) → mới mở Option 1–3.
Không đảo thứ tự. Không thêm Window History / UI polish trước khi pass được:
Terminal capture → SQLite transaction → process termination → workspace
reconstruction → scrollback/history/cwd restore — đây mới là khoảnh khắc Remin
bắt đầu thực sự tồn tại. UI/UX đã là LTS stable (v0.0.3lts); từ giờ chỉ làm persistence.

---

## 18. Files đổi (projected — chưa code)

| Area | Change |
|------|--------|
| `core/workspace/workspace.hpp` | + `schema_version`, `generation`, + `UiState` (dir tree, active window) |
| `core/window/window.hpp` | `Tab` + `TabKind` + `NoteTabState` (migration) |
| `core/pane/pane.hpp` | `PaneState` giữ field (giờ sống); `InterruptedCommand{command,timestamp,source}`; env V1 không dùng |
| `core/workspace_core.{hpp,cpp}` | `apply_runtime_state(...)` (canonical ingest, không nhận widget), `checkpoint(reason)` (validate+ghi transactional), restore hook, `restore_snapshot` emit |
| `core/serialization.{hpp,cpp}` | UiState/NoteTabState/kind/schema_version/generation |
| `storage/sqlite/sqlite_db.{hpp,cpp}` | transaction RAII (BEGIN IMMEDIATE/COMMIT/ROLLBACK) |
| `storage/sqlite/sqlite_db.cpp` (schema) | snapshots + schema_version + generation + reason (migration) |
| `core/persistence_policy` | wire (SessionController get/set; autosaver dùng input_checkpoint) |
| `core/autosave.cpp` | Terminal/Note flush đổ vào checkpoint transaction |
| `gui/session/session_controller.cpp` | WorkspaceSnapshotBuilder, add_command_to_pane, history query aggregate, persist clear, policy read/write |
| `gui/terminal/terminal_pane.{hpp,cpp}` | `runtime_capture()/runtime_restore()` adapter, cwd §4, viewport §5.4 |
| `gui/window/main_window.{hpp,cpp}` | restore đầy đủ, close→checkpoint, bỏ history_ vector, geometry capture |
| `gui/window/terminal_tab_view.cpp` | add_history → per-pane, gọi runtime adapter của pane |
| `gui/window/directory_tree_panel.{hpp,cpp}` | `capture_state()/apply_state()` public |
| `gui/window/note_tab_view.{hpp,cpp}` | capture/restore NoteTabState |
| `gui/window/settings_dialog.cpp` | Persistence page (3 policies) liên kết PersistencePolicy |
| `tests/unit/…` | serialization migration, checkpoint atomicity, per-pane history, cwd fallback logic, §5.5 fidelity gate |

---

## 19. Review Log & Approve Record (authoritative)

**Reviewer: user — Verdict: ~95% ready → sau 5 chỉnh sửa = APPROVE IMPLEMENTATION.**

| # | Điểm review | Vị trí áp dụng | Trạng thái |
|---|-------------|----------------|------------|
| 1 | Wording cam kết restore: `scrollback=EXACT`, `viewport=bottom/prompt in V1` — không tuyên bố "đúng chỗ tuyệt đối" (VTE 0.76 không expose scrollback offset) | §0, §5.3, §5.4, §16 | ✅ |
| 2 | Gate kỹ thuật riêng cho `get_text_range()→feed()`: VTE live → ANSI/control → capture → new VTE → feed → so sánh. V1 mục tiêu = **restore textual scrollback faithfully**, không restore arbitrary runtime terminal state | §5.5, P2.5 | ✅ |
| 3 | `PaneState.environment` **KHÔNG persist ở V1** (blocklist không an toàn: AWS_KEY/GITHUB_TOKEN/KRB5CCNAME/SSH_AUTH_SOCK…). Về sau chỉ allowlist/opt-in; shell new inherit env mặc định | §3.2, §5.2, §13, §15 | ✅ |
| 4 | `interrupted_command` → `InterruptedCommand{command, timestamp, source=CtrlC\|ProcessExit\|unknown}`. V1 CHỈ gán `CtrlC` khi quan sát `\x03` thực tế, không suy diễn (SIGTERM/app crash/đóng pane/tool exit ≠ Ctrl+C) | §6.2, §3.2, §13 | ✅ |
| 5 | `WorkspaceCore::checkpoint()` **không được tự capture GUI widget** — phá dependency direction. Boundary: GUI runtime → SessionController/WorkspaceSnapshotBuilder → `WorkspaceCore::apply_runtime_state(...)` → Storage. TerminalRuntimeSnapshot = trung gian thuần data | §1, §3.1, §12, §18 | ✅ |
| 6 | Tách `schema_version` (đổi khi data structure đổi → cần migration) ≠ `generation` (tăng mỗi checkpoint) | §10.3, §13, §18 | ✅ |
| 7 | Phase plan thêm **P2.5 Capture/Restore Fidelity Gate** (§5.5); `command_history[]` bắt buộc làm ngay ở **P2** (golden restore phải chứng minh Remin nhớ terminal work) | §17, P2 | ✅ |
| 8 | Thứ tự chặt: P1–P5 (**Core Persistence Works**) → Option 1 Recovery → Option 2 Window History → Option 3 Input checkpoint. KHÔNG đảo, KHÔNG làm Window History/UI polish trước khi pass vòng capture→txn→terminate→reconstruct→restore | §17 | ✅ |

**GO**: bắt đầu implement P1. Mỗi phase đóng theo gate + test của phase đó, không
"looks okay". Record từng kết quả `SOURCE → … → UI VERIFICATION`.