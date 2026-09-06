# History System Implementation Spec — 3-Mode History (Commands / Transcripts / Windows)

Status: SPEC (chốt với user 2026-09-06) — CHƯA triển khai
Authoritative docs (READ FIRST):
1. `docs/design/terminal-history-semantics.md`
2. `docs/design/workspace-persistence-pipeline.md` (spec gốc viết
   `workspace-persistence-recovery-pipeline.md` — file thật là `workspace-persistence-pipeline.md`)
3. `docs/report-history-save-pane-window-tab.md`
4. `docs/ui-audit.md`
5. `AGENTS.md`

> **FROZEN GUI/UX**: KHÔNG redesign, restyle, resize, recolor, replace icons,
> thay đổi tab geometry, hay thay đổi trình bày context-menu. Chỉ implement
> behavior + state. Nếu cần sửa UI → DỪNG và báo lý do.

> `implement-note-1.md` (concept cũ) đã bị **supersede** bởi design + semantics
> doc mới — KHÔNG nên coi nó là spec cuối cùng.

---

## 1. Tổng quan — History = "historical workspace browser" (hệ 3 MODE)

> History tab chính thức trở thành một **"historical workspace browser"** — không
> phải một list command đơn thuần. Là **ký ức của workspace ở ba cấp độ**:
> **tôi đã chạy gì** (Commands) · **terminal đã trải qua gì** (Transcripts) ·
> **tôi từng có những window làm việc nào** (Windows).

```text
History
├── Commands       # command history theo pane
├── Transcripts    # terminal transcript/output history theo pane
└── Windows        # closed-window history / restore
```

Ba loại là **3 khái niệm khác nhau** — KHÔNG bao giờ lưu/implements như cùng một
thứ. Ban đầu có ý "mỗi loại một shortcut riêng", nhưng **CHỐT: không phát minh
quá nhiều phím** — user không phải nhớ `Ctrl+Shift+M` / `Ctrl+Shift+T` /
`Ctrl+Shift+W`.

**Quyết định phím (chốt):**
- `Ctrl+Shift+H` → History panel tổng
- Trong panel: `[Commands] [Transcripts] [Windows]` là **filter/subview** — không
  bắt buộc mỗi mode một global shortcut.
- Sau này: nếu tần suất dùng một loại đủ cao → MỚI thêm shortcut riêng.

**Nguyên tắc UI:** không nhồi thêm UI mới phá baseline hiện tại (frozen); chỉ wire
behavior vào control/panel đang có.
**Restore screen ≠ resurrect process:** khi mở lại, pane hiển thị screen state
cuối, NHƯNG shell/pane được spawn mới — không phục hồi process cũ
(chi tiết: `terminal-history-semantics.md`).

---

## 2. Command History (mode 1)

**Canonical source** (per-pane, giữ nguyên):

```text
Workspace
 → Window
 → Tab
 → Pane
 → command_history[]
```

Mỗi **CommandRecord** tối thiểu giữ:

- `command`
- `timestamp`
- `pane identity`
- `tab identity`
- `window identity`

History UI có thể aggregate nhiều pane/window, nhưng canonical underlying vẫn
là **per-pane**.

**User flow:**

```text
Focus pane
→ open History
→ Commands
→ chọn command
→ INSERT command vào input của pane đang focus
```

- **KHÔNG** tự execute chạy ngay khi user click (an toàn: insert vào cmd line,
  user ấn Enter mới chạy).
- **↑↓ native của shell VẪN shell-owned.** Remin KHÔNG thay thế readline của
  bash/zsh/fish.

**Shell-native history vs Remin command history — hai thứ KHÔNG loại trừ nhau:**

```text
Shell-native history   = bash/zsh/fish tự quản (readline navigation, ↑↓)
Remin command history  = Remin ghi nhận command commit theo pane (command_history[])
```

- ↑↓: PTY → shell readline. Remin KHÔNG thay thế.
- History UI: `Pane.command_history[]`. Remin quản lý.
- Kết quả: ↑↓ hoạt động **y hệt terminal bình thường**, NHƯNG đồng thời Remin có
  **historical browser tốt hơn terminal bình thường** (search/provenance/per-pane).

**Provenance mỗi record:**

```text
timestamp
workspace/window/tab/pane
command
```

**Display:**

```text
Today
15:21  ls -la
15:18  cd ~/remin
15:12  cmake --build build
Yesterday
...
```

---

## 3. Transcript History (mode 2)

**KHÔNG phải command history.** Represents historical terminal output/context
generated trong vòng đời pane.

**Mô hình tối thiểu mỗi TerminalPane:**

```text
TerminalPane
├── current screen state
├── command history
└── transcript history
```

- `clear` chỉ ảnh hưởng **current screen state**.
- `clear` KHÔNG được xóa: `command history` + `transcript history`.

**Ví dụ:**

```text
$ ls
file-a
file-b

$ clear
```

Sau clear: current screen = prompt. Nhưng:
- Command history vẫn: `ls` · `clear`
- Transcript vẫn: `ls` · `file-a` · `file-b` · `clear`

**Transcript UI (độc đáo — "xem lại terminal thực sự đã diễn ra thế nào"):**

```text
Window: GitLab Audit
Tab: Recon
Pane: 2

15:12
$ nmap ...

15:13
PORT 22/tcp open
PORT 80/tcp open
...

15:14
$ ffuf ...
```

Click transcript → focus đúng pane/workspace hoặc mở transcript view tùy mode.

> **Transcript ≠ Screen restore.** Current Screen = state cuối cùng của pane;
> Transcript History = những gì pane đã render trong quá trình làm việc.
> clear: screen → clear; transcript → KHÔNG xóa; command history → KHÔNG xóa.

---

## 4. TRANSCRIPT KHÔNG ĐƯỢC phụ thuộc chỉ vào VTE current screen

VTE = terminal emulator state hiện tại, **KHÔNG phải historical journal**.

Nếu cần retention transcript sau `clear`, phải có **Remin-owned transcript path**
riêng:

```text
PTY / terminal runtime
      ├──> VTE current screen/scrollback
      └──> Remin transcript recorder
```

**KHÔNG** đơn thuần dựa vào final `vte_terminal_get_text_range()` snapshot cho
transcript history. Với immediate persistence MVP: **giữ nguyên** VTE snapshot
restoration riêng, tách biệt khỏi transcript journaling. Không âm thầm gộp chúng.

---

## 5. Window History (mode 3)

= những Window **cố ý đóng** khi Window History policy đang ON.

**Ví dụ:**

```text
Window: W42, label = "GitLab Audit"
```

**Khi đóng với Window History ON:**

```text
W42
→ capture final state
→ tạo closed-window history entry
→ giữ label + timestamp
→ đóng runtime Window
```

**Khi Window History OFF:**

```text
W42 → đóng → KHÔNG tạo reopenable history entry
```

**Restore Window (click "GitLab Audit"):**

- restore Window snapshot đó.
- **KHÔNG** tạo window mới trước rồi restore.
- **KHÔNG** clone thành `GitLab Audit (2)` — trừ khi user explicitly dupplicate.

---

## 6. RECOVERY ≠ WINDOW HISTORY

```text
Recovery:        latest valid open-workspace checkpoint
Window History:  historical snapshots của closed Windows (explicit)
```

**KHÔNG merge hai cái này vào một collection.**

---

## 7. TÁCH "Current Workspace" và "Historical Data" (model)

```text
Workspace
│
├── current state
│   ├── windows
│   ├── tabs
│   └── panes
│
└── history
    ├── command records
    ├── transcript records
    └── closed-window snapshots
```

History **KHÔNG** phải một blob khổng lồ duy nhất. Đặc biệt transcript có thể
lớn — storage nên theo **record/chunk hoặc blob riêng**, không nhét vào
`settings` hay `scrollbacks` generic.

---

## 8. History UI (frozen)

- **KHÔNG redesign** History panel hiện tại. Dùng panel hiện có.
- Bên trong cung cấp 3 mode logic: `[Commands] [Transcripts] [Windows]`.
- Reuse visual language hiện có. **No new decorative cards, no large buttons,
  no new chrome** trừ khi đã có trong baseline.

---

## 9. Keyboard access

- `Ctrl+Shift+H` → open/focus History panel.
- Trong panel: `Commands / Transcripts / Windows` = selectable modes.
- KHÔNG thêm nhiều global shortcut trừ khi có nhu cầu UX cụ thể.

---

## 10. Per-pane isolation (bắt buộc)

Command history và Transcript đều **per-pane**:

```text
Pane A ≠ Pane B
```

**Test:**

```text
Pane A:  printf 'TRANSCRIPT_A\n'
Pane B:  printf 'TRANSCRIPT_B\n'
```

History phải giữ provenance, không bao giờ mix data sai.

---

## 11. Clear semantics (test chốt)

```text
$ printf 'BEFORE_CLEAR\n'
$ clear
$ printf 'AFTER_CLEAR\n'
```

**Sau clear:**

```text
CURRENT SCREEN: chỉ AFTER_CLEAR context / prompt hiện tại
COMMAND HISTORY: BEFORE_CLEAR command · clear · AFTER_CLEAR command
TRANSCRIPT:     BEFORE_CLEAR output · clear event/context · AFTER_CLEAR output
```

> Clear là **SCREEN STATE operation**, không phải **HISTORY DELETE operation**.

---

## 12. Startup restore

```text
Nếu có valid recovery workspace → restore nó.
KHÔNG tạo default Window trước restore.
KHÔNG tạo duplicate windows.
Stable Window ID giữ ổn định qua checkpoint.
Checkpoint generation ≠ Window identity.
```

---

## 13. Transcript storage — audit trước khi chọn journal

Trước khi chọn implementation, audit code persistence hiện tại:

- path capture VTE hiện tại
- scrollback storage hiện tại
- nơi có thể quan sát PTY/VTE output **an toàn**
- recording terminal output có interfere với PTY không
- expected storage growth

> **KHÔNG** implement "append từng byte lên SQLite đồng bộ".
> Dùng buffered/chunked persistence. UI thread KHÔNG được block trên từng event.

---

## 14. Performance

Transcript recording KHÔNG được:

- block terminal rendering
- ghi một SQLite transaction mỗi output event
- rescan toàn bộ terminal content liên tục
- duplicate huge buffers không cần thiết

→ Buffer/chunk output và persist qua checkpoint/session pipeline hiện có.

---

## 15. Persisted data model (đề xuất)

```text
CommandRecord        # per-pane command history record
TranscriptChunk      # per-pane transcript chunk
ClosedWindowSnapshot # closed-window history snapshot
```

Không ép tất cả vào `settings` hay blob `scrollbacks` generic. Storage nên expose
dedicated APIs.

---

## 16. No UI regression (frozen list)

- History panel visual structure
- tab UI / tabs
- buttons
- CSS
- colors
- iconography
- layout
- editor
- terminal context menu
- directory panel visuals

Nếu implementation cần sửa UI → **DỪNG** và report tại sao.

---

## 17. Implementation order

```text
Phase A: audit History panel hiện tại
Phase B: wire Ctrl+Shift+H
Phase C: wire Command History vào per-pane canonical model
Phase D: implement transcript model/storage
Phase E: implement Window History storage/lifecycle
Phase F: wire 3 mode vào History panel hiện có
Phase G: golden tests
```

---

## 18. Golden history test

```text
Window W1: "GitLab Audit"

Tab Recon:
  Pane A:  pwd · ls · printf 'A\n'
  Pane B:  pwd · printf 'B\n'

Perform: clear trong Pane A
Close Window với Window History ENABLED.

VERIFY:

COMMANDS:  Pane A commands tách khỏi Pane B; timestamps/provenance giữ.
TRANSCRIPT: Pane A transcript chứa pre-clear output; Pane B tách biệt;
           clear KHÔNG xóa transcript.
WINDOWS:   W1 xuất hiện trong Window History; label = "GitLab Audit";
           timestamp tồn tại; chọn nó restore closed Window state.
```

Sau đó close/reopen app và verify **Recovery độc lập**.

---

## 19. Acceptance (KHÔNG kết luận vội)

KHÔNG report hoàn thành chỉ vì:
- command history tồn tại
- SQLite chứa blobs
- unit tests pass

**Acceptance thật cần ĐỒNG THỜI:**

```text
Command History works          ✓
Transcript History works       ✓
Window History works           ✓
per-pane isolation works       ✓
recovery works                 ✓
Ctrl+Shift+H works             ✓
clear semantics correct        ✓
```

---

## 20. Final report

Tạo/update: `docs/report-history-system.md`

Mỗi feature trình bày pipeline:

```text
SOURCE → CAPTURE → STORAGE → QUERY → UI → ACTION → RESTORE
```

Ghi: implementation status · tests · known limits · storage growth assumptions
· performance measurements.
**KHÔNG sửa frozen UI trong quá trình này.**

---

## Ghi chú về mối quan hệ với spec cũ

- `implement-note-1.md` (concept terminal history/capture ban đầu) — **superseded**
  bởi `terminal-history-semantics.md` + spec này.
- `docs/problem-terminal-transcript-capture.md` — P0-B capture fidelity FAILING,
  vẫn là blocker thực tế: phải chứng minh capture chứa marker deterministic
  trước khi nói transcript hoạt động. Pin lại vào Phase D (transcript storage).
- `docs/design/workspace-persistence-pipeline.md` — chỉ có tên file này, không
  có `workspace-persistence-recovery-pipeline.md`.