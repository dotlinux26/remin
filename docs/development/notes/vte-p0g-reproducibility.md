# VTE Patch Reproducibility & External-Consumer Verification (P0-G)

> Mục tiêu: chứng minh patch package P0-F thực sự usable như dependency — pristine
> tarball + patch series → build → consumer bên ngoài VTE tree có thể gọi API snapshot.
> **KHÔNG** tích hợp vào Remin TerminalPane ở bước này.

---

## Tổng quan

| Gate | Kết quả | Bằng chứng |
|------|---------|------------|
| G1 dọn sạch clone cũ | PASS | xóa `vte-0.76.0/`, `vte-0.76.0-audit/`, `vte-extension/` |
| G2 pristine tree + verify | PASS | tarball + `git status`/`git diff` empty |
| G3 apply patch + verify footprint | PASS | `git diff --stat` = 4 files, 724 insert, 0 delete; `--check` = 1 EOF whitespace only |
| G4 fresh build + symbols + header | PASS | 171/171; nm 3 `vte_terminal_snapshot_*` T-entries; `vteterminal.h` 3 decls; reachable via `<vte/vte.h>` |
| G5 0C + 0E standalone pass | PASS | 0C `OK`; 0E `ALL TESTS PASSED / READY_FOR_IMPLEMENTATION` (14/14) |
| G6 external consumer test | PASS | `CONSUMER: PASS` (consumer chỉ dùng `<vte/vte.h>`公众API) |
| G7 build script + manifest | PASS | `scripts/build-vte.sh` reproduces full pipeline; `VTE_MANIFEST` pins all hashes |

```text
P0-G VERDICT = READY_FOR_INTEGRATION
```

---

## Chi tiết từng bước

### G1 — Dọn clipboard VTE thực nghiệm

Xóa toàn bộ working clone / audit / extension thử nghiệm:

- `vte-0.76.0/` (working patched + build 19 MB)
- `vte-0.76.0-audit/` (pristine clone 5 MB)
- `vte-extension/` (empty prototype)

**Giữ lại**:
- `vte-0.76.0.tar.gz` (pristine provenance anchor)
- `patches/vte-0.76.0/` (patch series)
- `docs/vte-*.md`

### G2 — Tạo pristine tree từ tarball

```sh
tar xzf vte-0.76.0.tar.gz -C vte-0.76.0-pristine --strip-components=1
git init -q -b main && git add -A && git commit -qm "pristine VTE 0.76.0"
```

Verification:
- `git status --porcelain` → empty
- `git diff HEAD` → empty
- Pristine commit: `c72c24f` (re-runnable, pinned)

### G3 — Clone → apply patch series → verify footprint

```sh
git clone vte-0.76.0-pristine vte-0.76.0-patched
git apply --check ../patches/vte-0.76.0/000[1-4]*.patch
git apply ../patches/vte-0.76.0/000[1-4]*.patch
```

Kết quả:
- `git diff --stat` = exactly 4 files (+724 insert, 0 delete)
- `git diff --check` = 1 known cosmetic: `src/vtegtk.cc:7378: new blank line at EOF` (0004 EOF blank — matching P0-F)
- Patched commit: `92c7c2b` / tag `p0g-patched-v1`

### G4 — Fresh build + verify symbols + header

Meson config + ninja (identical to P0-F):
```sh
meson setup build --buildtype=release --default-library=shared --prefix=/usr \
  -Dwarning_level=0 -Dgtk3=true -Dgtk4=true -Dgir=true -Dvapi=true -Da11y=true \
  -Dglade=true -Dgnutls=true -Dicu=true -Dfribidi=true -D_systemd=true \
  -Ddocs=false -Ddbg=false
ninja -C build
```

Kết quả:
- 171/171 targets PASS
- `nm -D --defined-only build/src/libvte-2.91-gtk4.so.0 | grep snapshot` → 3 T-entries
- `grep "vte_terminal_snapshot_" src/vte/vteterminal.h` → 3 decls (lines 661, 665, 669)
- `grep "vteterminal" src/vte/vte.h` → `#include "vteterminal.h"` (line 32) → consumer dùng `<vte/vte.h>` OK

### G5 — Chạy 0C + 0E standalone

Compile against clean build via meson-uninstalled .pc:
```sh
PKG_CONFIG_PATH=vte-0.76.0-patched/build/meson-uninstalled
g++ -std=c++20 ... $(pkg-config --cflags vte-2.91-gtk4-uninstalled) $(pkg-config --libs ...)
LD_LIBRARY_PATH=vte-0.76.0-patched/build/src
```

| Test | Output | RC |
|------|--------|----|
| 0C roundtrip | `snapshot A size=7612 visible=233` / `OK` | 0 |
| 0E critical | `ALL TESTS PASSED` / `READY_FOR_IMPLEMENTATION` (14/14) | 0 |

### G6 — External consumer test

Tạo `tests/unit/vte_snapshot_consumer_test.cpp` — **chỉ dùng `<vte/vte.h>`**公众API, không internal headers:
- `GtkApplication` + `VteTerminal` + `vte_terminal_feed_child`
- `vte_terminal_snapshot_capture` / `vte_terminal_snapshot_restore`
- Roundtrip: capture → restore → re-capture → byte-compare

Kết quả: `CONSUMER: PASS` (RC=0, no warnings, clean exit)

### G7 — Script build-vte.sh + VTE_MANIFEST

- `scripts/build-vte.sh`: reproduces toàn bộ pipeline pristine→apply→build→verify
  - 7 gates: tarball SHA256 → pristine git clean → patch checksum → clone → apply + footprint → meson+ninja → symbol+header
- `VTE_MANIFEST`: pin `vte_version`, `patch_series_version`, 6 SHA256 hashes, build/consumption paths

---

## Phát hiện quan trọng: System lib contamination

```
/usr/lib/x86_64-linux-gnu/libvte-2.91-gtk4.so.0 (Sep 7, 2026)
  → exports: vte_terminal_snapshot_capture / restore / has_pending_data  ✗ KHÔNG pristine
/usr/include/vte-2.91-gtk4/vte/vte.h
  → KHÔNG DECLARE snapshot API  ← hệ thống pristine header
```

Hệ thống **đã có** patched lib từ thử nghiệm trước, trong khi header **thiếu** khai báo.
Nếu Remin build bằng cách naive (`pkg-config vte-2.91-gtk4` → link `/usr/lib`):
- Linker tìm được symbol từ `.so` contaminated
- Compiler dùng header pristine → không có decl → behavior undefined / implicit declaration
- Kết quả: Remin "works" nhưng **không reproducible**, phụ thuộc artifact cũ

**Giải pháp** (đã thiết kế trong P0-G):
- BAO GIỜ cũng consume VTE bằng `PKG_CONFIG_PATH=vte-0.76.0-patched/build/meson-uninstalled`
- BAO GIỜ cũng runtime-load bằng `LD_LIBRARY_PATH=vte-0.76.0-patched/build/src`
- KHÔNG BAO GIỜ dùng `/usr/lib` libvte cho snapshot feature
- `scripts/build-vte.sh` + `VTE_MANIFEST` đảm bảo reproducibility

---

## Ghi chú kỹ thuật

- `grep -q` trong pipe + `set -o pipefail` → SIGPIPE (grep quit early) → pipeline return 141
  → "missing symbol" false-positive. Fix: capture output into variable rồi `grep <<<` thay vì pipe.
  (Đã fix trong `scripts/build-vte.sh`.)
- Khi `vte_terminal_snapshot_capture` returns `void` (public API) ≠ boolean → consumer test
  phải check `*out != nullptr` thay vì return value.
- `vte_terminal_get_text_range` (deprecated) được dùng trong 0C test; consumer test tránh
  hoàn toàn deprecated API (dùng byte-compare snapshot thay vì visible-text check).

---

## Next steps

P0-G hoàn tất → **P0-H: Integrate vào Remin TerminalPane**
- `runtime_capture()` gọi `vte_terminal_snapshot_capture()` → `GBytes` → SQLite BLOB
- `runtime_restore()` gọi `vte_terminal_snapshot_restore()` từ BLOB
- Bỏ toàn bộ ANSI/text/HTML snapshot cũ, không giữ dual persistence
- Link Remin test suite vào VTE patched build (không dùng system lib)
