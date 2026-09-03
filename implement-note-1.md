# Implementation Note #1 — Remin

> Giai đoạn: Lên kế hoạch triển khai (pre-implementation)
> Dành cho: cuộc trao đổi QA giữa người build và chủ dự án (a).

---

## 1. Mục đích file này

File này ghi lại kế hoạch triển khai Remin bước đầu, cùng với các câu hỏi
QA (questions & answers) mà chủ dự án cần trả lời trước khi chốt công nghệ
và bắt tay vào code. **Không được code trước khi trả lời hết những câu này.**

---

## 2. Tóm tắt phạm vi (từ SPEC.md)

| Mục | Giá trị |
|-----|---------|
| Ngôn ngữ | C++17, Linux-first |
| License | MIT |
| Binary | `remin` (CLI), `remin-gui` (v2, tùy chọn) |
| Mục tiêu v1 | Save/restore workspace CLI. Zero external deps ở core. |
| V2 mơ ước | GUI terminal emulator + nested compositor (Wayland) |

---

## 3. Kế hoạch triển khai theo pha

### Phase 0 — Nền tảng dự án
- [ ] CMake project skeleton
- [ ] VSCode / clangd + compile_commands.json
- [ ] `.clang-format`, `.gitignore`, CONTRIBUTING.md
- [ ] CI cơ bản (GitHub Actions: build + test)
- [ ] Wiring CLI arg-parsing tối giản

### Phase 1 — Core model (không phụ thuộc GUI/PTY)
- [ ] Cấu trúc dữ liệu: `Workspace`, `Window`, `Tab`, `Pane`, `Snapshot`
- [ ] Serialize/Deserialize ra JSON
- [ ] Storage layer: ghi/đọc workspace index + snapshot files
- [ ] Unit test cho pure logic (không cần PTY, không cần terminal)

### Phase 2 — PTY & shell integration (Linux)
- [ ] Mô-đun `pty` dùng `forkpty` / `posix_openpt`
- [ ] Phát hiện shell (`$SHELL`, `/etc/passwd` fallback)
- [ ] Capture env vars (filter secret)
- [ ] Capture scrollback buffer
- [ ] Capture command history (đọc HISTFILE hoặc hook prompt)

### Phase 3 — CLI frontend
- [ ] `remin save / restore / list / delete / export / import / info / diff`
- [ ] `--json`, `--quiet`, `--verbose`
- [ ] In bảng workspace (manually, không cần lib)
- [ ] Confirm prompt trước restore

### Phase 4 — V1 hardening
- [ ] Error handling + exit codes có ý nghĩa
- [ ] Xử lý lock (tránh 2 process ghi cùng lúc)
- [ ] Tests tích hợp (end-to-end trên PTY thật)
- [ ] Package tối thiểu (tarball / CMake install)

### V2 (để sau)
- [ ] Terminal emulator GUI (libvterm hoặc tự viết)
- [ ] Nested compositor (Wayland)
- [ ] Export/import portable .remin file
- [ ] Plugin system

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

### 4.5 GUI (nếu v1 cần, không thì bỏ)

| Thư viện | Ghi chú |
|----------|---------|
| libvterm | terminal emulator cấp thấp, C |
| Nested compositor | Wayland — cực nặng, để v2 |

**Q5: GUI có vào v1 không, hay v1 chỉ CLI thuần?** (SPEC đề xuất v1 bỏ GUI)

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
| Q5 | GUI có vào v1? | bỏ, v1 CLI thuần |
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

### AQ1 — Mô hình Control flow

**Câu hỏi:** Khi `remin restore` chạy, nó phải làm gì?

- **A)** `remin` spawn shell mới trong terminal hiện tại, tự nó đóng vai trò
  "wrapper" — tức `remin` là PID 1 của session, sau đó fork ra shell thật.
  → `remin` phải biết quản lý PTY (phức tạp, nhưng kiểm soát được layout).

- **B)** `remin restore` chỉ tạo file script `.remin-session`, rồi gọi
  `exec $SHELL` với các env vars đã restore. User tự nhìn script để biết
  cần làm gì. → Đơn giản, nhưng chỉ "gợi ý", không restore thật.

- **C)** `remin restore` viết ra tmux socket script / screen config rồi
  launch `tmux new-session` từ state đã lưu. → tận dụng tmux, nhưng
  `"remin ≠ tmux"` (SPEC nói sẽ khác tmux, nhưng cách này rất thực dụng).

**AQ1a:**
- Nếu (A): cần fork PTY, quản lý terminal emulator internally → cần xác
  nhận dùng `forkpty()` hay viết abstraction layer.
- Nếu (C): chỉ cần generate tmux config → v1 cực nhanh, nhưng phụ thuộc
  tmux.

---

### AQ2 — Scrollback capture: "thật" hay "giả"?

**Câu hỏi:** Khi `remin save` chạy, scrollback (nội dung text đã hiển thị
trên terminal) được capture như thế nào?

- **A) Thật:** Remin wrap mỗi terminal, khi user chạy `remin save`, nó
  đọc buffer trong pty master fd → chính xác những gì user thấy.

- **B) Giả:** Remin chỉ ghi lại `HISTFILE` (command history) + `cwd` +
  `env`. Scrollback "visual" mất, nhưng command history đủ để gợi nhớ.

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
- Nếu (B): cần format binary rõ ràng (version field, endian约定).
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

### AQ5 — Terminal emulator trong GUI mode

**Câu hỏi:** v1 có cần terminal emulator GUI hay chỉ cần CLI?

- **A) CLI thuần:** v1 chỉ CLI, `remin save/restore` chạy trên terminal
  hiện có. → Đơn giản nhất.

- **B) Terminal emulator đầu tiên:** v1 viết terminal emulator tối giản
  (1 tab, 1 pane), sau đó v2 thêm workspace logic. → Phức tạp nhưng
  có thể demo.

- **C) Dùng `vte` (GTK terminal widget) hoặc `libvterm` + SDL2:** tér
  máquina evaporar na GUI. → Có thể dùng GTK/VTE để render terminal,
  nhúng vào app. Thư viện VTE của GNOME làm terminal widget, load
  được terminal info, render UTF-8 OK. Hoặc dùng xterm.js nếu viết
  app Electron (nhưng SPEC nói không Electron).

**AQ5a:** Nếu (C): cần link `libvte-2.91-dev` (GTK3) hoặc `vte-2.91` (GTK4).
Đây là dependency lớn nhất nhưng đáng giá nếu muốn GUI thật.

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
| AQ1 | Control flow | Remin wrap shell hay chỉ generate script? | (A) wrap shell |
| AQ2 | Scrollback | Capture scrollback thật hay chỉ history? | (C) history v1 |
| AQ3 | Snapshot format | JSON / Binary / Hybrid? | (A) JSON |
| AQ4 | File lock | Lock, atomic, hay bỏ qua? | (B) atomic rename |
| AQ5 | GUI v1 | CLI thuần hay terminal emulator? | (A) CLI thuần |
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