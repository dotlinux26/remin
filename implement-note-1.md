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

*File này sẽ được cập nhật sau khi anh trả lời QA. Không viết code cho tới khi chốt.*