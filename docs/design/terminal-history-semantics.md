# Terminal History Semantics — Screen State / Command History / Transcript

Date: 2026-09-06
Status: DESIGN (đã chốt semantics theo user — chưa implement)
Tài liệu gốc của vấn đề: `docs/problem-terminal-transcript-capture.md`

---

## 1. Vấn đề gốc đặt ra từ user

User thao tác:

```text
$ ls
a.txt
b.txt
c.txt

$ clear

user@host:~$
```

Sau đó thoát Remin, mở lại:

- **Câu hỏi 1**: `clear` có làm mất lịch sử Remin không?
- **Câu hỏi 2**: chúng ta có lưu toàn bộ log kể cả khi đã `clear` không? Hay chỉ lưu
  những gì thật sự hiện trên màn hình pane?
- **Câu hỏi 3**: Remin không phải bash nên không có kiểu `bash_history` — không xem
  lại được lệnh một cách tổng quát, chỉ ấn lên/xuống (↑↓) được. Có cách xử lý vấn
  đề này không?

**Đáp án chốt của user:**

> `clear` không phải là "xóa lịch sử Remin". Nó chỉ là một thao tác thay đổi trạng
> thái hiển thị của terminal.

---

## 2. Ba khái niệm TÁCH BẠCH (semantics đã chốt)

Remin không gọi tất cả mọi thứ là "history". Có 3 khái niệm riêng:

### 2.1 Screen State ("màn hình hiện tại của pane")

```text
current terminal-visible context
```

- Bị ảnh hưởng bởi `clear`.
- Là trạng thái hiển thị **mới nhất** được restore sau khởi động lại.
- Ví dụ sau `clear` thì screen state = prompt trống `user@host:~$`.

### 2.2 Command History ("những lệnh user đã nhập")

```text
canonical per-pane sequence of committed commands
```

- **`clear` KHÔNG xóa cái này.**
- Sống sót qua restart.
- Là nguồn cho History UI / search / provenance.
- ↑↓ (navigation readline) **vẫn là shell-owned** — Remin không giả lập readline.

### 2.3 Terminal Transcript ("những gì terminal đã từng render")

```text
historical terminal output available to Remin
```

- Độc lập với command history.
- `clear` không nhất thiết xóa transcript lịch sử đã persist.
- **KHÔNG nhất thiết replay lại trên màn hình hiện tại sau restore.**

### 2.4 Bảng tóm tắt

| Khái niệm | Thay đổi bởi `clear` | Restore sau restart | Phục vụ |
|-----------|----------------------|---------------------|---------|
| Screen State | Có (clear → prompt trống) | Có — restore màn hình mới nhất | màn hình pane |
| Command History | Không | Có — ↑↓ của shell + History UI | shell nav, search, provenance |
| Terminal Transcript | Không cần thiết | Tuỳ loại — lưu riêng | History viewer, log đầu ra |

**`clear` là một screen-state operation, KHÔNG phải history-deletion operation.**

---

## 3. Vì sao không dựa vào `~/.bash_history` / HISTFILE

Remin không phải shell, nhưng bên dưới vẫn có bash/zsh/fish thật:

```text
Remin
  ↓
VTE
  ↓
PTY
  ↓
bash (zsh/fish)
```

Bash vẫn có history của nó, nhưng Remin **không nên dùng `~/.bash_history` làm
nguồn canonical**, vì:

- user có thể dùng zsh / fish
- `HISTFILE` có thể bị tắt
- history có thể flush theo policy riêng của shell
- nhiều pane có thể cùng shell type → lịch sử lẫn lộn
- history scope không phản ánh Remin pane identity

**Canonical của Remin = `Pane.command_history[]`.**

Shell history (HISTFILE) chỉ là **runtime integration**, không phải nguồn dữ liệu.

---

## 4. Làm sao xem "toàn bộ lệnh" khi không có bash_history?

Đây chính là chỗ Remin tốt hơn terminal bình thường.

Sidebar **History** KHÔNG đọc `~/.bash_history`. Nó query chính cấu trúc Workspace:

```text
Workspace
 └── Windows
     └── Tabs
         └── Panes
             └── command_history[]
```

**Search ví dụ:** `nmap`

```text
Window: GitLab Audit
Tab:    Recon
Pane:   2

nmap -sCV 10.10.10.10
nmap -p- 10.10.10.10
```

**↑↓ thì vẫn giao cho shell** — hành vi shell tự nhiên, không mô phỏng readline.
Remin đồng thời ghi nhận command đã commit để canonical history riêng.

---

## 5. Hai luồng chạy song song (command recorder)

```text
User input
   ↓
PTY
   ↓
bash/zsh/fish
   ↓
VTE

                        └── Remin command recorder
                            (ghi lại command đã commit vào command_history[])
```

Điều phải TRÁNH: đừng làm ↑↓ bằng cách Remin tự lấy `Pane.command_history[]`
rồi feed ngược lại terminal. Shell đã có readline/history navigation rồi; Remin
chỉ theo dõi và lưu lại.

---

## 6. Kiến trúc đề xuất — Screen / Journal

Thay vì lấy VTE scrollback làm duy nhất một nguồn history, ta có:

```text
                    TerminalPane
                         │
              ┌──────────┴──────────┐
              │                     │
        Screen/VTE               Journal
              │                     │
         current view        terminal events/log
```

**Ví dụ:**

```text
$ ls
a
b

$ clear

$ pwd
/home/user
```

| Lớp | Nội dung |
|-----|----------|
| Screen snapshot cuối | `$ pwd` · `/home/user` |
| Command history | `ls` · `clear` · `pwd` |
| Transcript | `ls` · `a` · `b` · `clear` · `pwd` · `/home/user` |

**Sau restart:**

- Pane hiển thị: `$ pwd` / `/home/user`
- ↑↓: `pwd` → `clear` → `ls`
- History viewer: `10:32:01 ls` · `10:32:04 clear` · `10:32:07 pwd`

**Và điều này giải quyết luôn `clear`:** nếu lưu nguyên transcript, thì `clear`
chỉ là một **event** (`CLEAR`), không phải "DELETE EVERYTHING BEFORE HERE".

---

## 7. TerminalJournal (future — append-only)

```text
TerminalJournal
├── INPUT
├── OUTPUT
├── RESIZE
├── CLEAR
├── SIGNAL
└── ...
```

Không cần làm full event journal trong V1. V1 có thể là:

```text
current_screen
+
command_history
+
captured_scrollback
```

Sau đó nâng cấp lên journal khi cần retention vượt quá scrollback VTE.

---

## 8. Checkpoint behavior (chốt)

```text
current screen state  → restore current screen
command history       → restore per-pane history
transcript            → retain separately when supported
```

- `clear` chỉ thay đổi **screen state**; command history + transcript sống sót.
- Restore hiển thị **màn hình đúng như lần cuối user chủ động để lại**, không tự
  dựng lại `$ ls ... $ clear` sau một screen đã bị clear.

---

## 9. Acceptance test (bắt buộc pass)

Pane A:

```text
$ printf 'BEFORE_CLEAR\n'
BEFORE_CLEAR

$ clear

$ printf 'AFTER_CLEAR\n'
AFTER_CLEAR
```

**Sau restart:**

```text
Screen:   $ printf 'AFTER_CLEAR\n'
          AFTER_CLEAR

↑↓:       printf 'AFTER_CLEAR\n'
          clear
          printf 'BEFORE_CLEAR\n'

History UI:  BEFORE_CLEAR
             clear
             AFTER_CLEAR
```

---

## 10. Hiểu lầm quan trọng cần tránh

> VTE scrollback không nên được coi là "toàn bộ lịch sử làm việc" của pane.

Nó chỉ là phần terminal emulator còn giữ/đang hiển thị. Remin cần một lớp
state/history của riêng nó — chính lớp đó làm Remin khác terminal emulator
bình thường.

---

## 11. Trạng thái hiện tại của code (liên quan)

- `PaneState` hiện có: `scrollback`, `cwd`, `cols`, `rows`, `shell`,
  `command_history`, `interrupted_command` (`src/core/pane/pane.hpp`).
- `command_history` được ghi qua `WorkspaceCore::add_command_to_pane` (cap 1000).
- `TerminalPane::capture_scrollback()` (`terminal_pane.cpp:188`) dùng
  `vte_terminal_get_text_range_format` để lấy VTE scrollback — **đây là nguồn
  "captured screen", KHÔNG phải transcript canonical.**
- **CAPTURE FIDELITY hiện FAILING**: blob ~10KB nhưng gần như blank + prompt
  (xem `docs/problem-terminal-transcript-capture.md`). Chưa xác nhận VTE có trả
  thêm scrollback sau khi `clear` hay không.

### Mở rộng model đề xuất (để phù hợp semantics mới)

```text
PaneState
├── terminal
│   ├── transcript            (text VTE còn giữ — hiện là captured scrollback)
│   ├── cols
│   ├── rows
│   └── viewport marker       (V1: bottom/prompt nếu không offset chính xác)
├── shell
│   ├── executable
│   ├── cwd
│   └── command_history[]     (↑/↓ per-pane + History UI)
└── lifecycle
    └── interrupted_command
```

---

## 12. Câu hỏi kỹ thuật treo (cần xác minh bằng test)

1. Sau `clear`, VTE còn giữ các dòng trước clear trong scrollback buffer không?
   - Nếu CÓ → captured scrollback vẫn chứa output cũ (đúng transcript).
   - Nếu KHÔNG → Remin phải có journal/tab riêng để giữ transcript; VTE scrollback
     chỉ là screen state.
2. Current capture (row range `-(rows + 10000)..rows`) trả toàn bộ hay chỉ vùng
   hiển thị? (bằng chứng hiện tại nghiêng về "chỉ capture được ít hơn toàn bộ".)
3. Cần thêm bucket `transcript` riêng hay tái sử dụng scrollback với semantics mới?