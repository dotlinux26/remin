# VẤN ĐỀ P0 — Terminal Transcript Restoration (Capture đang fail, chưa phải Restore)

Date: 2026-09-06
Status: **FAILED** (chưa đạt acceptance tiêu chuẩn transcript)
Branch: chưa commit

---

## 1. Yêu cầu sản phẩm (explicit — user)

Khi user thoát Remin, pane terminal phải **giữ lại transcript hiển thị** để lần sau
mở lại, pane vẫn hiển thị lại output cũ y nguyên:

```text
/app$ ls
audit_methodology.md.md  CMakeFiles  cmake_install.cmake  Makefile  remin

nguyenduccanh@dotlinux:~/remin/build-asan/src/app$ ls
...
nguyenduccanh@dotlinux:~/remin/build-asan/src/app$
```

Thoát → mở lại → nhìn vào pane **phải thấy lại đúng transcript đó**.

Bao gồm:
- command text như hiển thị trong terminal
- command output
- prompts
- scrollback tích lũy mà VTE còn giữ
- terminal dimensions

**Lưu ý**: shell command ↑/↓ (command_history) là feature RIÊNG, phải tách bạch,
per-pane, không lẫn với scrollback.

---

## 2. Bằng chứng hiện tại (DB dump thật)

Pipeline hiện tại:

```text
VTE
 ↓ get_text_range(...)
"capture"
 ↓
SQLite (scrollbacks table)
 ↓
restore
```

Query DB `~/.local/share/remin/remin.db`:

```text
pane = a1d0db47783e747af2870f60fa6a8131
len  = 10090
content ≈ "\n\n\n\n....\nnguyenduccanh@dotlinux:~$ \n\n..."
```

→ **blob tồn tại (~10KB), NHƯNG nội dung gần như blank + prompt cuối, KHÔNG chứa
output `ls`.**

- STORAGE WRITE = WORKING ✅ (ghi được blob)
- CAPTURE FIDELITY = FAILING ❌ (nội dung capture thiếu output)
- RESTORE = CHƯA ĐƯỢC VALIDATE (vì capture đã sai từ gốc)

**Kết luận quan trọng**: "blob không rỗng" KHÔNG chứng minh capture đúng.
Phải có **deterministic marker** trong blob.

---

## 3. Blocker độc lập

```text
P0-A: lifecycle crash khi đóng window (app abort/core dump khi windowclose)
P0-B: terminal transcript capture không chứa output (đang ở đây, ưu tiên)
```

Không để P0-B bị che bởi P0-A. Không claim "scrollback đã lưu" vì chỉ thấy blob.

---

## 4. VTE có tự persist không?

**KHÔNG.**

VTE giữ terminal state trong RAM khi instance còn tồn tại.
Khi Remin thoát, VTE object + PTY chết → buffer chết.
Remin phải CHỦ ĐỘNG capture rồi persist. Không có cơ chế tự ghi disk.

---

## 5. Nghi vấn capture API (điều tra trước khi kết luận)

Hàm production: `TerminalPane::capture_scrollback()` (`src/gui/terminal/terminal_pane.cpp`):

```cpp
constexpr glong kScrollbackLines = 10000;
const glong rows = vte_terminal_get_row_count(vte_);
char* text = vte_terminal_get_text_range_format(vte_, VTE_FORMAT_TEXT,
                                                -(rows + kScrollbackLines), 0, rows, 0, &len);
```

Phải verify (đừng mặc định `-(rows+N)` trả "toàn bộ scrollback"):

- `vte_terminal_set_scrollback_lines()` — số dòng thực tế VTE giữ
- row indices của `get_text_range` — start/end có đúng không
- terminal row/col count thực tế
- terminal có ở alternate screen không
- output đã bị evict bởi scrollback limit chưa
- nếu VTE chỉ giữ 10000 dòng mà output vượt quá → không thể capture phần đã discard
  (giới hạn thật, không phải bug)

So sánh TẠI THỜI ĐIỂM capture:
- A. cái user nhìn thấy trong VTE widget
- B. cái `get_text_range()` trả về

Dùng marker deterministic để loại bỏ ambiguity:
```
printf 'REMIN_CAPTURE_A\n'
ls
printf 'REMIN_CAPTURE_B\n'
```

**Nếu A có marker mà B không → row range/API sai.**
**Nếu A không có marker → timing/input/lifecycle sai.**

---

## 6. Test reproduction đã làm (kết quả)

Dùng `xdotool` type `ls -la` + Return, chờ, đóng window → query DB:

```text
pane = 86420efd...
content ≈ "\n\n\n\n...\n"  (~10KB blank + prompt)
```

Test này có vấn đề phụ: **app abort/core dump khi windowclose** → P0-A.
Cách dùng xdotool OK để tạo deterministic repro, nhưng phải tách rõ hai bug.

---

## 7. Kế hoạch sửa (đúng thứ tự)

```text
1. FIX lifecycle crash khi đóng (P0-A) — dùng ASan/gdb repro
2. Minimal capture probe:
   - spawn shell trong VTE
   - chạy printf 'REMIN_CAPTURE_A\n'; ls; printf 'REMIN_CAPTURE_B\n'
   - chờ command kết thúc + prompt render
   - capture bằng ĐÚNG hàm capture của Remin
   - print captured string dạng escaped
3. Verify VTE scrollback config + row range + visible vs get_text_range
4. Chỉ sau capture PASS mới tiếp tục: persist → restart → restore → visual verify
```

Index: `docs/design/workspace-persistence-pipeline.md` (thiết kế gốc), kết quả
runtime trong `tests/golden/GOLDEN_ACCEPTANCE_CHECKLIST.md` (MỤC SCROLLBACK VẪN FAIL).

---

## 8. Running-process case (semantics đã thống nhất)

```text
Pane
└── running application (top / python server)
      ↓ close window
      ↓ process bị terminate (= abort/terminate runtime)
      ↓ checkpoint
```

Persist:
```text
last visible transcript
+ last command
+ interrupted/terminated metadata
```

Restart:
```text
old screen/output
      ↓
new shell (fresh, KHÔNG resurrect process cũ)
      ↓
user@host:~$
```

Không bao giờ resurrect process từ RAM. Chỉ restore visual state + interruption
metadata.

---

## 9. Model nên có (đề xuất đổi tên cho rõ)

```text
PaneState
├── terminal
│   ├── transcript            (text do VTE còn giữ)
│   ├── cols
│   ├── rows
│   └── viewport marker       (V1: bottom/prompt nếu không offset chính xác)
├── shell
│   ├── executable
│   ├── cwd
│   └── command_history[]     (↑/↓ per-pane, RIÊNG BIỆT)
└── lifecycle
    └── interrupted_command
```

Đừng gọi command_history là "scrollback".

---

## 10. Acceptance chuẩn (bắt buộc pass)

```text
Window 1
  Tab A
    Pane A1
    Pane A2

A1: printf 'REMIN_A\n'; ls
A2: printf 'REMIN_B\n'; pwd

Checkpoint → Terminate → Restart → Restore

Verify:
- A1 transcript chứa REMIN_A + ls output
- A2 transcript chứa REMIN_B + pwd output
- A1 history != A2 history
- A1 cwd != A2 cwd (where configured)
- không cross-contamination
- không window mới dư
- không crash
```

Chỉ sau đó Window History và các consumer persistence khác mới tiếp tục.

---

## 11. CẤM

- Không sửa UI/CSS/colors/icons/layout/buttons/spacing/context menus/dir tree.
- Không paste-hack (sleeps, delayed paste, xdotool, keyboard replay, clipboard
  replay, screenshot, đọc output từ shell history).
- Không mặc định blob size = capture đúng.
- Không claim feature works khi chưa có marker trong blob.

---

## 12. File liên quan

- `src/gui/terminal/terminal_pane.cpp` — `capture_scrollback()`, `runtime_capture()`,
  `runtime_restore()`, `spawn_shell()`
- `src/gui/terminal/terminal_pane.hpp`
- `src/gui/window/terminal_tab_view.cpp` — `restore_pane_tree()`, `rebuild()`, `build_node()`
- `src/gui/window/main_window.cpp` — `restore_workspace()` (dòng 521-640),
  `capture_all_runtime_state()` (666+)
- `src/storage/storage.cpp` — `checkpoint()` (snapshot + scrollbacks)
- `src/core/autosave.cpp` — flush orchestration
- `src/core/pane/pane.hpp` — PaneState
- `docs/design/workspace-persistence-pipeline.md` — design gốc