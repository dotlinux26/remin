# Release Hardening Plan (v1.0.0rc → v1.0.0)

> Plan gating mọi công việc chuẩn hóa tài liệu + distribution/packaging.
> **1.0.0rc đã feature-frozen** — codebase ở trạng thái "release hardening".
> Không vừa sửa docs vừa thêm feature mới (trừ khi packaging phát hiện vấn đề
> dependency/installation thực sự).

## Mục tiêu

Chuyển Remin từ "project đang phát triển" sang **"sản phẩm có thể phát hành"**.
Tách hẳn **2 track**:

```text
1. Release documentation   — docs chuẩn hóa, user-facing landing
2. Distribution            — AppImage / .deb / source tarball + CI release
```

Câu chuyện **VTE patch** phải được làm rất tử tế — đó là phần đặc biệt nhất
của Remin và là một "technical story" có giá trị, không phải câu "we patched VTE
to save terminal state".

---

## 1. Chuẩn hóa repository trước

Đưa `docs/` về cấu trúc rõ ràng, **không lẫn research note / design note /
end-user documentation**:

```text
docs/
├── README.md
├── getting-started.md
├── installation.md
├── configuration.md
├── usage/
│   ├── workspaces.md
│   ├── terminal.md
│   ├── history.md
│   ├── notes.md
│   ├── markdown.md
│   └── export.md
│
├── architecture/
│   ├── overview.md
│   ├── terminal.md
│   ├── history.md
│   ├── workspace.md
│   ├── markdown.md
│   └── persistence.md
│
├── design/
│   ├── ...
│
├── patches/
│   └── vte/
│       ├── README.md
│       ├── architecture.md
│       ├── snapshot-format.md
│       ├── integration.md
│       ├── upstream-delta.md
│       ├── restoration-semantics.md
│       ├── compatibility.md
│       └── testing.md
│
├── development/
│   ├── build.md
│   ├── testing.md
│   ├── contributing.md
│   └── release.md
│
└── packaging/
    ├── appimage.md
    ├── debian.md
    └── desktop-integration.md
```

Nguyên tắc phân lớp:

```text
docs/design/       = tại sao + quyết định kỹ thuật
docs/architecture/ = hệ thống hoạt động thế nào
docs/usage/        = người dùng dùng ra sao
docs/patches/      = Remin thay đổi upstream dependency thế nào
docs/development/  = developer
docs/packaging/    = release engineer
```

---

## 2. VTE patch — một "technical story" đầy đủ

Entry point: `docs/patches/vte/README.md`.

Không viết sơ sài kiểu *"We patched VTE to save terminal state."* — mà giải thích
động cơ + thiết kế:

```text
# Remin VTE Extension

Remin requires persistence of terminal emulator state across application
restarts.

Instead of replacing VTE's terminal engine or introducing a PTY proxy,
Remin extends VTE with a minimal snapshot/restore interface...
```

Kèm diagram vị trí của snapshot trong pipeline:

```text
Terminal application
       │
       │ VTE API
       ▼
┌───────────────────────┐
│        VTE            │
│                       │
│ canonical terminal    │
│ state                 │
│                       │
│  snapshot_capture()   │
│  snapshot_restore()   │
└───────────────────────┘
       │
       ▼
   Remin snapshot
```

Ghi rõ **Remin không fork terminal emulator architecture**:

```text
Remin does NOT:
- implement its own terminal emulator
- proxy PTY traffic
- replay terminal output
- reconstruct terminal state from visible text
- replace VTE's parser/rendering engine
```

Sau đó mới vào cấu trúc API snapshot:

```cpp
vte_terminal_snapshot_capture()
vte_terminal_snapshot_restore()
vte_terminal_snapshot_has_pending_data()
```

### `snapshot-format.md`

Mô tả logical state (KHÔNG dump từng struct private của VTE):

```text
Header
Version
Geometry
Screen state
Cursor state
Saved cursor
Rows
Cells
Attributes
Colors
Hyperlinks
Palette
Modes
Scroll region
Tab stops
Charset state
Active screen
...
```

### `restoration-semantics.md`

Đây là phần quan trọng nhất — dùng equation để nói rõ snapshot là **save
terminal state**, không phải "save màn hình":

```text
RESTORE(snapshot(A)) + X
        ==
state(A) + X
```

cho tập thao tác `X` được hỗ trợ (feed input, resize, scroll …).

---

## 3. README — nâng cấp thành landing page

Cấu trúc đề xuất:

```text
Remin
Terminal Workspace & Markdown Desktop Environment
```

### Features (chỉ liệt kê thứ THỰC SỰ có ở 1.0.0rc)

```text
✓ Persistent terminal sessions
✓ Native VTE snapshot/restore
✓ Workspace persistence
✓ Shell history tracking
✓ Markdown editor
✓ Markdown/GFM rendering
✓ Clipboard image paste
✓ PDF/HTML export
✓ Custom Markdown styling
✓ TOC
✓ Page breaks
✓ PDF internal links/bookmarks
```

### Why Remin?

Câu chuyện sản phẩm:

> Remin treats the terminal as a persistent workspace rather than a disposable
> window.

### Các section còn lại

```text
Installation
Quick Start
Screenshots
Architecture
Known limitations
Development
License
Release
```

README phải có **link tới binary release** (GitHub Releases), đừng bắt người dùng
build từ source.

---

## 4. Packaging — làm cả 3

### AppImage (first-class portable distribution)

```text
Remin-1.0.0-linux-x86_64.AppImage
```

```bash
chmod +x Remin-1.0.0-linux-x86_64.AppImage
./Remin-1.0.0-linux-x86_64.AppImage
```

Không cần install dependency hệ thống.

### Debian package

```text
remin_1.0.0_amd64.deb
```

Cấu trúc chuẩn:

```text
/usr/bin/remin
/usr/share/applications/remin.desktop
/usr/share/icons/hicolor/.../remin.png/svg
/usr/share/metainfo/net.dotsops.Remin.metainfo.xml
/usr/share/doc/remin/...
```

Bắt buộc validate trước release:

```bash
appstreamcli validate ...
```

(GUI app Debian/AppStream cần `.desktop` + icon + AppStream metadata; thiếu
metainfo sẽ bị cảnh báo/loại trên AppStream generator.)

---

## 5. "Bộ cài đặt" — chia tầng

Release đầu **không viết installer wizard**:

```text
Linux
├── AppImage
│   └── portable
│
├── Debian / Ubuntu
│   └── .deb
│
└── Source
    └── .tar.gz
```

Sau này mới tới `Remin Installer` (GUI installer). `.deb` = package installer
native của Debian family; AppImage = portable distribution.

---

## 6. Build/release pipeline chuẩn hóa

```text
.github/
└── workflows/
    ├── ci.yml
    ├── test.yml
    └── release.yml
```

Release flow:

```text
tag v1.0.0
      ↓
GitHub Actions
      ↓
build
      ↓
ctest
      ↓
AppImage
      ↓
.deb
      ↓
SHA256SUMS
      ↓
GitHub Release
```

Assets cuối mỗi release:

```text
Remin-1.0.0-linux-x86_64.AppImage
remin_1.0.0_amd64.deb
remin-1.0.0.tar.gz
SHA256SUMS
```

---

## 7. `RELEASE.md` — release checklist dùng lại được

Để mỗi version không phải nhớ bằng đầu:

```text
1. update version
2. run tests
3. clean build
4. build AppImage
5. build .deb
6. generate checksums
7. git tag
8. push tag
9. create GitHub release
10. upload artifacts
```

Biến release từ "thao tác thủ công" thành quy trình reproducible.

---

## 8. LICENSE / THIRD-PARTY-NOTICES

```text
LICENSE
THIRD-PARTY-NOTICES
```

Các dependency liên quan:

```text
GTK4
gtkmm4
VTE
Pango
Cairo
MD4C
GLib/GIO
GDK Pixbuf
...
```

Không phải cứ MIT project là mọi dependency cũng MIT. README cần mục
**Third-party dependencies / licenses** riêng.

---

## 9. Release engineering milestone — thứ tự thực hiện

```text
1. Documentation cleanup
       ↓
2. VTE patch documentation
       ↓
3. README rewrite
       ↓
4. Installation/package documentation
       ↓
5. .desktop + AppStream + icons
       ↓
6. Debian packaging
       ↓
7. AppImage packaging
       ↓
8. CI release workflow
       ↓
9. Release checklist
       ↓
10. v1.0.0
```

Đặc biệt: **không vừa sửa docs vừa sửa code feature**. Giờ là codebase
feature-frozen, chuyển sang release hardening.