# Implementation Note #1 — Remin

> Giai đoạn: Lên kế hoạch triển khai (pre-implementation)

---

## 2.5. ĐIỀU CHỈNH CONCEPT QUAN TRỌNG (rev-a)

> Cập nhật sau khi chốt lại với chủ dự án. Có một lệch concept mạnh trong
> bản gốc: doc cũ coi **CLI = V1, GUI/compositor = V2**. Sai.

### Nguyên tắc mới

**GUI không phải phần phụ. GUI chính là cách Remin thể hiện workspace model.**

```
                 REMIN
                   │
          Workspace Engine
                   │
       ┌───────────┼───────────┐
       │           │           │
    Window        Tab         Pane
       │           │           │
       └───────────┼───────────┘
                   │
              Terminal
                   │
                  PTY
                   │
             bash/zsh/fish
```

### V1 = Nested CLI-oriented workspace environment (chạy trong DE hiện có)

Remin **chưa cần** là một Desktop Environment hoàn chỉnh ngay ngày đầu.

V1 là:

> **Nested CLI-oriented workspace environment chạy bên trong DE hiện có.**

Ví dụ trên Kali Xfce — host DE vẫn quản lý **một cửa sổ Remin**, Remin
quản lý toàn bộ thế giới **bên trong cửa sổ đó**:

```
┌──────────── Kali Xfce ────────────────────────┐
│ panel                                         │
│                                               │
│   ┌────────────── Remin ──────────────────┐   │
│   │ GitLab Audit                          │   │
│   │ ┌───────────┬───────────────────────┐ │   │
│   │ │ nmap      │ ffuf                  │ │   │
│   │ │           │                       │ │   │
│   │ ├───────────┴───────────────────────┤ │   │
│   │ │ shell                             │ │   │
│   │ └───────────────────────────────────┘ │   │
│   └───────────────────────────────────────┘   │
└────────────────────────────────────────────────┘
```

### V2 = Remin Desktop Environment thực sự

Sau này mới tiến tới:

```
Login
  ↓
Remin session
  ↓
Remin compositor
  ↓
CLI workspace
```

tức **Remin Desktop Environment thực sự** (Wayland-native compositor,
standalone session, login integration).

### Điểm khởi đầu đúng

Không bắt đầu bằng `CLI → save → restore`.

Mà bắt đầu bằng **Workspace State**:

```
                 Workspace State
                       │
          ┌────────────┼────────────┐
          ↓            ↓            ↓
       Window         Tab          Pane
          │            │            │
          └────────────┼────────────┘
                       ↓
                 Terminal State
                       │
              ┌────────┼────────┐
              ↓        ↓        ↓
             CWD     History  Scrollback
                       │
                       ↓
                      PTY
```

**GUI render cái model này. CLI cũng điều khiển chính cái model này.**

```bash
remin workspace list
remin workspace open "GitLab Audit"
remin window rename "GitLab Audit"
remin snapshot create
remin history
remin restore
```

GUI:

```
click Rename   →  cùng WorkspaceCore
click Snapshot →  cùng WorkspaceCore
click Restore  →  cùng WorkspaceCore
```

→ **GUI và CLI đều gọi cùng `WorkspaceCore`** (core không biết mình bị điều khiển bởi GUI hay CLI).

### `restore` không còn "spawn shell trong terminal hiện tại"

Với Remin GUI:

```
remin-gui
   │
   ├── Window Manager
   ├── Tab Manager
   ├── Pane/Layout Manager
   └── Terminal Manager
             │
             └── PTY
```

`restore` = **workspace reconstruction**:

```
Snapshot
   ↓
Workspace reconstruction
   ↓
Window
   ↓
Tabs
   ↓
Pane tree
   ↓
PTY instances
   ↓
shell
```

PTY vẫn là **OS/library territory** — Remin không reinvent PTY.

### Chia lại version

```text
V1
├── Remin Core
├── Linux PTY integration
├── Workspace model
├── GUI workspace
├── Window / Tab / Pane
├── History
├── Search
├── Snapshot / Restore
└── Linux nested environment

V2
├── Wayland-native compositor
├── standalone session
├── login/session integration
└── full Remin desktop environment
```

**GUI là V1. DE/compositor độc lập là V2.**

> Remin remembers your workspace.
> Browser nhớ `Window→Tabs→Pages`; Remin nhớ `Workspace→Windows→Tabs→Panes→Shell sessions`.

---

---

## 1. Mục đích file này

File này ghi lại kế hoạch triển khai Remin bước đầu, cùng với các câu hỏi
QA (questions & answers) mà chủ dự án cần trả lời trước khi chốt công nghệ
và bắt tay vào code. **Không được code trước khi trả lời hết những câu này.**

---

## 2. Tóm tắt phạm vi (từ SPEC.md)

| Mục | Giá trị |
|-----|---------|
| Ngôn ngữ | C++17/20, Linux-first |
| License | MIT |
| Binary | `remin` (CLI) + `remin-gui` (V1 — nested GUI workspace) |
| Mục tiêu V1 | Nested CLI-oriented workspace environment với GUI + CLI cùng dùng `WorkspaceCore` |
| V2 mơ ước | Remin Desktop Environment: Wayland-native compositor + standalone session |

---

## 3. Kế hoạch triển khai theo pha (rev-a — GUI-first)

> **GUI là V1. DE/compositor độc lập là V2.**

### Phase 0 — Nền tảng dự án
- [ ] CMake project skeleton
- [ ] VSCode / clangd + compile_commands.json
- [ ] `.clang-format`, `.gitignore`, CONTRIBUTING.md
- [ ] CI cơ bản (GitHub Actions: build + test)

### Phase 1 — Core model + Workspace Engine (không phụ thuộc GUI/PTY)
- [ ] Cấu trúc dữ liệu: `Workspace`, `Window`, `Tab`, `Pane`, `Snapshot`
- [ ] `WorkspaceCore` — API duy nhất mà cả GUI lẫn CLI cùng gọi
- [ ] Serialize/Deserialize ra JSON
- [ ] Storage layer: ghi/đọc workspace index + snapshot files
- [ ] Unit test cho pure logic (không cần PTY, không cần terminal)

### Phase 2 — PTY & shell integration (Linux)
- [ ] Mô-đun `pty` dùng `forkpty` / `posix_openpt`
- [ ] Phát hiện shell (`$SHELL`, `/etc/passwd` fallback)
- [ ] Capture env vars (filter secret)
- [ ] Capture scrollback buffer
- [ ] Capture command history (đọc HISTFILE hoặc hook prompt)

### Phase 3 — GUI workspace (GIAI ĐOẠN CHÍNH CỦA V1)
- [ ] Terminal emulator (libvterm hoặc VTE) render Pane
- [ ] Window / Tab / Pane layout renderer
- [ ] Window Manager (tạo/focus/close/rename window)
- [ ] Tab Manager (tạo/focus/close tab)
- [ ] Pane/Layout Manager (split vertical/horizontal, resize)
- [ ] History panel + Search
- [ ] Snapshot / Restore UI (button + dialog)
- [ ] Chạy nested: Remin là 1 cửa sổ bên trong DE host

### Phase 4 — CLI frontend (điều khiển CÙNG WorkspaceCore)
- [ ] `remin workspace list / open / rename`
- [ ] `remin window ...`, `remin tab ...`, `remin pane ...`
- [ ] `remin snapshot create / list / restore`
- [ ] `remin history`, `remin search`
- [ ] `--json`, `--quiet`, `--verbose`

### Phase 5 — V1 hardening
- [ ] Error handling + exit codes có ý nghĩa
- [ ] Xử lý lock (tránh 2 process ghi cùng lúc)
- [ ] Tests tích hợp (end-to-end trên PTY thật)
- [ ] Package tối thiểu (tarball / CMake install)

### V2 (Remin Desktop Environment — để sau)
- [ ] Wayland-native compositor
- [ ] Standalone session (login → Remin)
- [ ] Login/session integration
- [ ] Full Remin desktop environment

---

## 4. Quyết định công nghệ cần xác nhận

Bảng dưới liệt kê **toàn bộ thư viện / kỹ thuật** sẽ dùng ở v1. Chủ dự án
cần trả lời theo từng dòng.

### 4.1 Build system

| Công nghệ | Lựa chọn đề xuất | Câu hỏi QA |
|-----------|------------------|------------|
| CMake | ✅ chọn CMake | Q1: Anh dùng CMake hay muốn đổi sang Meson/ninja thuần? |
| gcc/clang | Clang (dev), gcc (CI) | Q2: Anh có chuộng 1 compiler cụ thể không? |

### 4.2 JSON (bắt buộc — snapshot format)

| Thư viện | Ghi chú | Câu hỏi QA |
|----------|---------|------------|
| A) Tự viết JSON parser | zero-dep, nhưng tốn công + dễ bug | Q3: Chấp nhận dựa 1 lib JSON (header-only) hay bắt buộc tự viết để "zero dep"? |
| B) nlohmann/json | header-only, phổ biến, MIT | |
| C) simdjson | nhanh nhưng nặng dep | |

**Nếu chọn B/C:** phá vỡ cam kết "zero external dependencies" ở core. Cần anh chốt.

### 4.3 CLI arg-parsing

| Lựa chọn | Ghi chú |
|----------|---------|
| Tự viết (getopt) | đủ dùng cho 8 lệnh, 0 dep |
| CLI11 | header-only, gọn |

### 4.4 Terminal emulation (scrollback capture)

Scrollback KHÓ nhất. Muốn đọc nội dung trên màn hình phải parse escape
sequences. Chiến lược:

| Chiến lược | Mô tả |
|------------|-------|
| A) Wrapper PTY | Remin làm middle layer giữa shell và pty, tự parse ANSI |
| B) Đọc HISTFILE | Chỉ lấy command history (không phải scrollback thật) |
| C) Ptys via cgo | không dùng (không phải Go) |

**Q4: Scrollback "thật" (A) rất tốn công. Anh chấp nhận v1 chỉ lưu command
history (B), còn scrollback đầy đủ để v2 không?**

### 4.5 GUI — CỐT LÕI CỦA V1

> Rev-a: GUI **không phải** tùy chọn. GUI là cách Remin thể hiện workspace model.

| Thư viện | Ghi chú |
|----------|---------|
| VTE (libvte-2.91) | GTK terminal widget, render + emulation sẵn, battle-tested |
| libvterm | terminal emulation cấp thấp (C), tự render bằng toolkit khác |
| SDL2 / GTK3 / Qt6 | toolkit vẽ giao diện |

**Q5 (rev-a): Chốt stack GUI:** VTE/GTK hay libvterm + toolkit khác?
Đề xuất: **VTE + GTK3** cho terminal emulation + widget đầy đủ (nhanh, ít code).

### 4.6 Portable format (.remin export file)

| Lựa chọn | Ghi chú |
|----------|---------|
| Tarball (tar.gz) | tự nhiên, có sẵn nhiều file, compress |
| JSON single file | đơn giản, nhưng scrollback lớn thì phồng |
| Zip | thuận phổ biến |

**Q6: Export .remin dùng format nào? Đề xuất: tarball.**

### 4.7 C++ standard & toolchain

**Q7: Confirm C++17 vs C++20?** (đề xuất C++20 nếu toolchain mới, C++17 nếu muốn portable rộng)

### 4.8 Concurrency / PTY I/O

| Lựa chọn | Ghi chú |
|----------|---------|
| epoll | Linux-native, 1 thread |
| poll/select | đơn giản hơn, kém scale |

**Q8: Dùng epoll (Linux-only, v1 OK vì Linux-first)?**

---

## 5. Môi trường build mục tiêu

**Q9: Anh build ở đâu?**

- [ ] Kali (Debian-based) — `apt install ...`
- [ ] Fedora (RPM-based) — `dnf install ...`
- [ ] Cả hai — cần both toolchains

Điều này ảnh hưởng script setup + hướng dẫn CI.

---

## 6. Bảng QA tổng hợp để anh trả lời

| # | Câu hỏi | Đề xuất mặc định |
|---|---------|------------------|
| Q1 | Build system? | CMake |
| Q2 | Compiler? | clang dev / gcc CI |
| Q3 | JSON zerodep hay dùng lib? | dùng nlohmann/json |
| Q4 | Scrollback thật hay chỉ history ở v1? | chỉ history v1 |
| Q5 | GUI có vào V1? | Có — GUI là cốt lõi V1 (rev-a) |
| Q6 | Format .remin export? | tarball |
| Q7 | C++17 hay C++20? | C++20 |
| Q8 | PTY I/O dùng epoll? | epoll |
| Q9 | Build trên OS nào? | cần a xác nhận |

---

## 7. Ghi chú rủi ro / điểm cần cân nhắc

- **Zero-dep vs đúng hạn:** tự viết JSON + ANSI parser tốn rất nhiều thời
  gian và dễ sinh bug bảo mật (terminal parsing rất dễ bị tấn công — cvmlike
  CVE chuỗi escape). Đề xuất dùng lib đã battle-tested cho phần terminal.
- **Restore process:** không thể "restore" một process đang chạy (Unix không
  checkpoint được process). Remin chỉ restore *layout + history + cwd + env*,
  rồi spawn shell mới. Cần anh hiểu giới hạn này.
- **Secrets:** env vars có thể chứa token. Mặc định KHÔNG capture env, hoặc
  có danh sách blocklist (AWS_SECRET, TOKEN, PASSWORD...).

---

## 8. Bảng tóm tắt những câu hỏi then chốt (AIQ — Asking Important Questions)

Trước khi bắt tay vào code, chủ dự án **phải** trả lời những câu hỏi cốt
lõi dưới đây. Những câu này quyết định toàn bộ kiến trúc v1, không chỉ
"thư viện" mà là **cách tiếp cận**.

---

### AQ1 — Mô hình Control flow (rev-a: GUI-based)

**Câu hỏi:** Khi `remin restore` chạy trong **GUI**, nó hoạt động thế nào?

- **A)** `remin-gui` khởi động, tự nó là Window/Tab/Pane manager, tự fork
  PTY cho mỗi pane, và `WorkspaceCore` tái lập cây pane từ snapshot.

- **B)** `remin-gui` chỉ render, còn restore delegate xuống tmux/screen
  backend. → Phụ thuộc tmux.

**Đề xuất:** (A) — Remin quản lý PTY trực tiếp, không dựa tmux. `restore`
= workspace reconstruction (Snapshot → Window → Tab → Pane tree → PTY → shell).

**AQ1a:** Nếu (A) cần xác nhận dùng `forkpty()` trực tiếp hay abstraction
`PTYProvider` interface (để sau này port).

---

### AQ2 — Scrollback capture: "thật" hay "giả" (rev-a: GUI)

**Câu hỏi:** Trong **GUI**, việc capture scrollback được làm thế nào?

Vì V1 là GUI, nếu dùng **VTE** thì:
- VTE tự quản lý scrollback buffer (text đã hiển thị trong widget).
- `remin-gui` chỉ cần đọc buffer từ VTE widget rồi lưu vào snapshot.
- Không cần tự viết ANSI parser — VTE lo phần đó.

Nếu dùng **CLI** thuần (không tab GUI):
- Phải wrap PTY, tự parse ANSI escape.

**AQ2a:** Confirm GUI dùng **VTE** (scrollback "thật" miễn phí) → v1
capture scrollback dễ dàng.

- **C) Hybrid:** v1 dùng (B), v2 mới làm (A).

**AQ2a:** Nếu (A) → cần ANSI escape sequence parser. Thư viện `libvterm`
làm tốt việc này nhưng là C lib (có thể link). Viết tay = hàng nghìn
dòng code + bug-filled.

**AQ2b:** Nếu command history (B): làm sao đọc `HISTFILE` nếu shell đang
chạy? (Cần hook hoặc đọc từ disk.)

---

### AQ3 — Snapshot format: JSON hay Binary?

**Câu hỏi:** Khi lưu state workspace ra disk, dùng JSON hay binary?

- **A) JSON:** human-readable, dễ debug, dùng nlohmann/json. Cute nhưng
  scrollback lớn (hàng triệu byte) sẽ tạo file JSON khổng lồ.

- **B) Binary:** compact, nhanh, cần custom serialization (có thể dùng
  flatbuffers / capnp hoặc tự viết struct packing).

- **C) Hybrid:** metadata = JSON, scrollback binary (file riêng).

**AQ3a:**
- Nếu (A): chấp nhận file có thể lớn, giảm bug serialization.
- Nếu (B): cần format binary rõ ràng (version field, endianness).
- Nếu (C): phức tạp nhất nhưng tối ưu nhất.

---

### AQ4 — Lock & concurrent access

**Câu hỏi:** Nếu user mở 2 terminal cùng lúc, cả 2 đều đang `remin save`
thì sao?

- **A) File lock:** dùng `flock()` trên lock file, tránh ghi đè.
- **B) Copy-on-write:** mỗi snapshot ghi ra file mới (UUID), index chỉ
  update atomic → không cần lock.
- **C) Không xử lý ở v1:** user tự biết không chạy cùng lúc.

**AQ4a:** Nếu (B) thì cần cam kết file system hỗ trợ atomic rename
(ext4, xfs OK, tmpfs thì OK).

---

### AQ5 — Terminal emulator trong GUI mode (rev-a)

**Câu hỏi:** GUI V1 dùng terminal emulator thế nào?

- **A) VTE (GTK terminal widget):** render + emulation sẵn, scrollback
  "thật" miễn phí. Battle-tested. → Đề xuất.

- **B) libvterm + toolkit khác (SDL2/Qt):** tự render lại, thêm việc.

- **C) Tự viết terminal emulator:** rất tốn công, bug-ridden.

**AQ5a:** Confirm dùng **VTE + GTK3** (link `libvte-2.91-dev`). Đây là
dependency lớn nhất nhưng đáng giá — GUI là cốt lõi V1.

> Lưu ý rev-a: phương án (A) CLI thuần đã bị loại — GUI không phải phần phụ.

---

### AQ6 — Static hay dynamic linking?

**Câu hỏi:** Build binary v1 link static hay dynamic?

- **A) Static binary:** `g++ -static` → binary lớn (5-10MB) nhưng
  portable, copy sang máy khác chạy được ngay.

- **B) Dynamic:** binary nhỏ (500KB), nhưng cần libstdc++ / glibc
  trên máy đích.

- **C) Mostly static:** link static C++ runtime, dynamic libc → portable
  hơn nhưng vẫn nhỏ hơn static hoàn toàn.

**AQ6a:** Nếu anh muốn distribute binary compact, (C) hay (B). Nếu
muốn "copy 1 file chạy được everywhere", (A).

---

### AQ7 — MIT hay BSD hay GPL?

**Câu hỏi:** SPEC nói MIT. Confirm chưa?

MIT:
- ✅ Cho phép binary closed-source
- ✅ Rất tự do, nhiều dự án OSS dùng
- ⚠️ Không bắt buộc contribute-back (nếu anh muốn)

GPL v3:
- ⚠️ Nếu distribute binary phải share source
- ✅ Bảo vệagainst proprietary forks
- ✅ Phổ biến trong Linux ecosystem

**AQ7a:** Nếu anh muốn Remin separate binary MIT + core library GPL
(ngớ ngẩn cho dự án nhỏ), hoặc MIT cho mọi thứ (đơn giản nhất).

---

### AQ8 — Terminals allowed: filesystem restrictions?

**Câu hỏi:** Remin khi `restore` chạy ở đâu? Có giới hạn terminal
nào không?

- **A) Bất kỳ terminal nào** (xterm, alacritty, kitty, gnome-terminal...)
  nhưng cần stdout == xterm-compatible.

- **B) Chỉ trong terminal đang chạy:** `remin save` ghi lại terminal
  hiện tại, `remin restore` phải chạy ở cùng terminal type.

- **C) Bất kỳ, với fallback:** detect terminal type, nếu không detected
  thì dùng xterm defaults.

**AQ8a:** Nếu (A) hoặc (C): cần detect `$TERM` (xterm-256color, linux...).

---

### AQ9 — Multi-user hay single-user?

**Câu hỏi:** Remin có cần support nhiều user trên cùng máy không?

- **A) Single-user:** workspace chỉ lưu ở `$HOME/.local/share/remin/`.
  Đơn giản.

- **B) Multi-user:** workspace ở `/var/lib/remin/<uid>/` hoặc dùng XDG
  data dir. Phức tạp hơn nhưng đúng cách Linux.

**AQ9a:** Nếu (A): chỉ cần `getenv("HOME")`. Nếu (B): cần `getuid()` +
XDG spec.

---

## 9. Danh sách system calls & Linux APIs quan trọng

Những API này **bắt buộc** phải biết khi implement Remin v1:

### PTY & Process

| System call / API | Mục đích | Head file |
|-------------------|----------|-----------|
| `posix_openpt()` | Mở PTY master | `<fcntl.h>` |
| `grantpt()` / `unlockpt()` | Unlock PTY slave | `<stdlib.h>` |
| `ptsname()` | Lấy tên `/dev/pts/N` | `<stdlib.h>` |
| `forkpty()` | Fork process trong PTY mới | `<pty.h>` |
| `waitpid()` | Chờ process con | `<sys/wait.h>` |
| `execvp()` / `execvpe()` | Launch shell | `<unistd.h>` |
| `read()` / `write()` trên master fd | I/O tới shell qua PTY | `<unistd.h>` |
| `kill(pid, signal)` | Gửi signal (SIGHUP khi logout) | `<signal.h>` |

### File system

| System call / API | Mục đích | Head file |
|-------------------|----------|-----------|
| `stat()` / `lstat()` | Kiểm tra file tồn tại | `<sys/stat.h>` |
| `getcwd()` | Lấy CWD hiện tại | `<unistd.h>` |
| `chdir()` | Đổi CWD | `<unistd.h>` |
| `opendir()` / `readdir()` | Scan workspace dir | `<dirent.h>` |
| `mkdir()` (recursive) | Tạo workspace dir structure | `<sys/stat.h>` |
| `rename()` | Atomic move file | `<stdio.h>` |

### Terminal info

| System call / API | Mục đích | Head file |
|-------------------|----------|-----------|
| `isatty()` | Kiểm tra fd có phải terminal | `<unistd.h>` |
| `ioctl(fd, TIOCGWINSZ, &ws)` | Lấy kích thước terminal | `<sys/ioctl.h>` |
| `tgetent()` / ` terminfo` | Lấy term info (width, keys) | `<curses.h>` |
| `$TERM` env | Detect terminal type | getenv |

### Process / Environment

| System call / API | Mục đích | Head file |
|-------------------|----------|-----------|
| `getenv()` / `setenv()` | Đọc/ghi env vars | `<stdlib.h>` |
| `getuid()` / `geteuid()` | UID hiện tại | `<unistd.h>` |
| `getpwuid()` | Lấy user info (home dir) | `<pwd.h>` |
| `gethostname()` | Lấy hostname (dùng export file) | `<unistd.h>` |

### IO Multiplexing (PTY I/O loop)

| System call / API | Mục đích | Head file |
|-------------------|----------|-----------|
| `poll()` | Multiplex read từ nhiều fd | `<poll.h>` |
| `select()` | Phiên bản cũ hơn poll | `<sys/select.h>` |
| `epoll` | Linux-native, nhanh nhất | `<sys/epoll.h>` |

---

## 10. Câu hỏi về kiến trúc code

### ARCH1 — Single-binary hay multi-binary?

- **A) Single binary `remin`:** mọi lệnh (`save`, `restore`, `list`...)
  là subcommands của cùng 1 binary. → Đơn giản, install 1 file.

- **B) Multi-binary:** `remin-core` (library), `remin` (CLI),
  `remin-daemon` (background autosave), `remin-gui` → modular hơn
  nhưng phức tạp hơn cho packaging.

**Đề xuất:** (A) cho v1.

### ARCH2 — Có daemon không?

- **A) Không daemon:** user tự chạy `remin save` khi muốn. → Đơn giản.

- **B) Daemon `remin daemon`:** chạy background, tự autosave mỗi N
  phút. → Tiện hơn nhưng thêm complexity (IPC, signal handling,
  service file systemd).

**Đề xuất:** (A) cho v1, (B) cho v2.

### ARCH3 — CMake structure?

```
remin/
├── CMakeLists.txt          (root)
├── src/
│   ├── core/CMakeLists.txt
│   ├── terminal/CMakeLists.txt
│   ├── ui/cli/CMakeLists.txt
│   └── main.cpp
├── tests/
│   └── CMakeLists.txt
└── extern/                 (libs nếu dùng: nlohmann, CLI11...)
```

**ARCH3a:**
- Dùng `FetchContent` để pull dependencies (nếu có)?
- Hay `git submodule`?
- Hay `pkg-config` / `find_package`?

**Đề xuất:** `FetchContent` nếu header-only, `find_package` nếu system lib.

---

## 11. Bảng tóm tắt quyết định cần a xác nhận

| # | Lĩnh vực | Câu hỏi ngắn | Đề xuất |
|---|---------|-------------|---------|
| AQ1 | Control flow | Remin render + quản lý PTY trực tiếp hay delegate tmux? | (A) PTY trực tiếp |
| AQ2 | Scrollback | VTE buffer "thật" hay chỉ history? | (A) VTE buffer thật (rev-a) |
| AQ3 | Snapshot format | JSON / Binary / Hybrid? | (A) JSON |
| AQ4 | File lock | Lock, atomic, hay bỏ qua? | (B) atomic rename |
| AQ5 | GUI V1 | Kỹ thuật render terminal emulator? | (A) VTE + GTK3 (rev-a) |
| AQ6 | Linking | Static, dynamic, hay hybrid? | (C) mostly static |
| AQ7 | License | MIT confirm? | MIT |
| AQ8 | Terminal type | Bất kỳ hay giới hạn? | (C) detect + fallback |
| AQ9 | Multi-user | Single hay multi? | (A) single-user |
| ARCH1 | Binary | Single hay multi binary? | (A) single binary |
| ARCH2 | Daemon | Có daemon autosave? | (A) không daemon v1 |
| ARCH3 | Deps mgmt | FetchContent / submodule / pkg-config? | FetchContent |

---

## 12. Gợi ý workflow cho a khi trả lời

```
Đọc file này
    │
    ├── Trả lời AQ1-AQ11 (có thể viết ngay vào file này)
    │
    └── Gửi lại cho tôi
            │
            └── Tôi sẽ update SPEC.md + implement-note-1.md
                rồi bắt tay implement Phase 0
```

---

*File này sẽ được cập nhật sau khi anh trả lời QA. Không viết code cho tới khi chốt.*
