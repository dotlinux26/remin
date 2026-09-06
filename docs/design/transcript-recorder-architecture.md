# Transcript Recorder — Architecture Investigation (Phase D: D0 + D1 gate)

Status: D0 COMPLETE · D1 DECISION PENDING (2026-09-06)
Authoritative: `docs/design/history-system-spec.md` §3–§14 + `docs/problem-terminal-transcript-capture.md` (P0-B).

> Objective: add a **Remin-owned TerminalTranscriptRecorder** (lịch sử output, độc
> lập với VTE current screen/scrollback, sống sót qua `clear`) **mà KHÔNG
> reimplement PTY** (D8 out-of-scope) và KHÔNG thay `vte_terminal_spawn_async`.
> Theo yêu cầu user: KHÔNG triển khai recorder (D2) cho đến khi D0/D1 hiểu rõ.
> Nếu không có tee point an toàn với integration hiện tại → **STOP và báo
> constraint chính xác**.

---

## 1. D0 — PTY/VTE OUTPUT OWNERSHIP (verified VTE 0.76/0.78 source)

| Câu hỏi | Kết luận |
|---------|----------|
| Ai giữ master PTY fd? | `VtePty` (`vte::base::Pty::m_pty_fd`), mở bằng `posix_openpt(..., O_NONBLOCK|O_CLOEXEC)` + **TIOCPKT packet mode**. `vte_pty_get_fd()` trả **chính fd đó** (borrowed, không dup, không được close/flags đổi). `vte_terminal_get_pty()` trả cùng object pty. |
| Ai đọc master fd? | **VTE luôn là reader duy nhất.** Sau `vte_terminal_spawn_async` → callback `vte_terminal_set_pty()` → `connect_pty_read()` → `g_unix_fd_add_full(...)` trên master (G_IO_IN/PRI/HUP/ERR) → `pty_io_read()` vòng `read()` đến EAGAIN, đẩy vào `m_incoming_queue` → parser → screen. Không có path output nào bypass được reader này. |
| VTE có output hook an toàn? | **KHÔNG.** Trong GTK4 KHÔNG có signal chứa raw output bytes. `contents-changed` = notification **không payload**, bị coalesce (idle `emit_pending_signals`), dành cho a11y. `text-inserted/text-scrolled/text-modified` **chỉ tồn tại ở GTK3** (`#if _VTE_GTK == 3`) và không được emit. Không có `output/received/data`/`log/tee/record` API. |
| `get_pty()+get_fd()` + GIO watch riêng có an toàn? | **KHÔNG — race dual-reader bất hợp pháp.** `get_fd()` trả đúng fd/open-file-description mà VTE đã watch. Đọc master (O_NONBLOCK + TIOCPKT) **tiêu thụ bytes** (không peek); hai source trong cùng main loop `read()` tranh nhau, reader nào trước ăn bytes của reader kia → corrupt cả screen lẫn recorder. Đúng race mà design cấm. |

### 1a. Raw byte tồn tại ở đâu?

Chỉ có **một** chỗ sau khi read: `m_incoming_queue` được `pty_io_read()` tiêu thụ
→ parser → screen buffer. Không có callback/emitter giữa read và parse. Do đó
**không có raw output bytes nào ở ngoài VTE parser** trong API 0.76/0.78 public.
Capture trước đây của Remin dùng `vte_terminal_get_text_range_format()` = snapshot
post-parse (đã decode, mất escape/format, mất nội dung bị `clear`) — đây chính là
nguyên nhân P0-B.

---

## 2. D1 — DECISION GATE

### Yêu cầu từ directive (D0/D1/D8)

Recorder phải đặt ở tee point **single-reader** quanh integration hiện tại
(giữ `Shell → OS PTY → VTE → TerminalPane`), không hai reader shared master.

### Kết luận D1

**KHÔNG có tee point an toàn nếu GIỮ NGUYÊN `vte_terminal_spawn_async` (VTE là
single reader) và không có output-bytes hook.** Lựa chọn duy nhất để có bytes
không-race là phải "làm reader chính mình":

```
tự tạo VtePty (vte_pty_new_sync / vte_pty_spawn_async — KHÔNG spawn_async của terminal)
+ KHÔNG gọi vte_terminal_set_pty()    (nếu gọi → VTE lại cài reader của nó = dual-reader)
+ GIO watch riêng trên vte_pty_get_fd() → đọc (xử lý TIOCPKT) → vte_terminal_feed(bytes)
```

NHƯNG hướng đó **mất toàn bộ convenience của VTE spawn path**:
- phải tự `vte_pty_set_size` theo resize (terminal không push được size nếu pty không đính).
- `vte_terminal_watch_child` **hard-require** pty đã set trên terminal (`src/vtegtk.cc:4883`), nên không reap child được qua API chuẩn.
- phải tự quản lý spawn/process-group/EOF/HUP giống như reimplement.

Đó chính là **D8 out-of-scope** (custom PTY lifecycle / process-group / SIGWINCH /
thay spawn path) — user đã cấm.

### Gate: STOP (không code D2 bây giờ)

> Perm D1: "If a safe output tee can be established around the existing VTE
> integration → implement. If **not** → **STOP and report the exact architectural
> constraint**."

Theo khảo sát: **tee không thể lập quanh integration hiện tại mà không vi phạm D8.**
→ Báo constraint. Chờ quyết định.

---

## 3. Các lựa chọn (đệ trình user)

### (a) Snapshot-based transcript (giữ nguyên VTE spawn; KHÔNG cần PTY rewrite)

- Capture theo sự kiện `contents-changed` (no-payload, coalesced, chạy trên main
  context — không block) → gọi `vte_terminal_get_text_range_format()` và **diff** so
  với lần trước để nối phần output mới vào transcript; ghi dấu `clear` khi thấy
  scrollback giảm/xoá.
- **Ưu**: zero rủi ro PTY, giữ nguyên `spawn_async`, đúng D8 hoàn toàn.
- **Nhược**: vẫn là post-parse (không phải raw bytes); phụ thuộc VTE screen — đây
  CHÍNH là thứ spec §4 cấm ("KHÔNG được phụ thuộc VTE current screen") và có rủi ro
  tái hiện P0-B (capture decode/lossy). Tôi KHÔNG khuyến nghị vì vi phạm lõi spec.

### (b) "Own reader → feed" (single-reader tee hợp lệ duy nhất nhưng vượt D8)

- Như §2: tự VtePty + reader riêng + `vte_terminal_feed`. Có raw bytes chuẩn, sống
  qua `clear`, giải quyết luôn P0-B.
- **Nhược**: chính là những gì D8 liệt kê out-of-scope (PTY lifecycle, watch-child,
  resize, SIGWINCH, EOF/HUP, process-group). Cần **explicit approval** để vượt D8.

### (c) Defer Phase D, làm E/F/G trước

- Window History + wire 3-mode không phụ thuộc transcript. Quay lại D sau khi
  E/F/G xong (có thể có quyết định mới).

---

## 4. Transcript data model (không triển khai nếu chưa chọn hướng)

Tham chiếu (áp dụng dù chọn (a) hay (b)) — spec §3/§15/§7, tách bạch:

```text
Per-pane (cách ly D5):
  Pane → TranscriptChunk[] { seq, timestamp_us, kind (output|clear|interrupt), data }

Aggregation chỉ ở query/UI:
  Workspace → Window → Tab → Pane → Transcript[]
```

- Buffering (D4): PTY bytes → in-memory chunk buffer (ví dụ ~64KB) → flush qua
  checkpoint/session pipeline → SQLite dedicated table (`transcripts_*`), **KHÔNG**
  INSERT mỗi byte, KHÔNG nhét vào `scrollbacks`/`settings` generic (spec §15/§14).
- Clear semantics (D3): `clear` = screen-state op → current screen trống, transcript
  vẫn giữ: `ls · A · B · clear-event · pwd · /home/user`.
- Restore (D6/D7): restore **current screen + shell context** qua `runtime_restore`
  (đã có); transcript chỉ để **xem lại** lịch sử, KHÔNG replay hết lên VTE.
- Naming (D2): `TerminalTranscriptRecorder`, **không gọi** ScrollbackRecorder.

---

## 5. Chưa có (blocks D2)

- Quyết định hướng (a)/(b)/(c) từ user.
- Nếu (b): approval vượt D8 + thiết kế PTY adapter mỏng.

---

## 6. Known limitations (thực tế khảo sát)

- VTE 0.76/0.78 GTK4 **không expose raw output bytes**; không có log/tee/record hook.
- `contents-changed` không payload + coalesced → chỉ dùng được làm "trigger snapshot", không phải byte stream.
- `text-*` signals (GTK3-only) không tồn tại ở GTK4.
- Đọc master fd chung = race phá hỏng screen; bị cấm tuyệt đối.
