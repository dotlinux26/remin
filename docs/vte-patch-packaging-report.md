# VTE 0.76.0 — Patch Packaging & Reproducibility Report (P0-F)

> Master prompt: P0-F VTE Patch Packaging & Reproducibility. Mode: **PACKAGING /
> FORENSIC REVIEW ONLY**. Mục tiêu: chuyển implementation snapshot đã validate
> thành patch series sạch, tái tạo được, review được với invariant:
> `pristine VTE 0.76.0 + reproducible patch == exactly current working patched VTE`.

---

## 1. Verdict — PATCH_PACKAGE

```text
PATCH_PACKAGE = READY
```

Toàn bộ gate đã qua (xem §10). Patch series đặt tại `patches/vte-0.76.0/`
(0001..0004 + `SERIES.md` + `SHA256SUMS`). Đây là gói **nguồn** — patch là cách
tái tạo nguồn chính xác; KHÔNG phải binary package, KHÔNG install (Step 11 đã
giữ local-only).

## 2. Purpose

- Đóng gói implementation snapshot đã hoạt động thành 4 patch tách biệt, single-purpose.
- Chứng minh tái tạo sạch từ pristine tarball → build → test (0C roundtrip + 0E critical).
- Tạo hồ sơ forensic để review/an toàn khi tích hợp sau này vào Remin TerminalPane.

## 3. Scope

- Bao gồm: 4 file nguồn trong cây VTE; test lân cận (0C/0E) được tái tạo + chạy
  độc lập ngoài cây VTE (chúng thuộc Remin, KHÔNG nằm trong patch package — xem §5.6).
- Loại trừ: mọi thay đổi hành vi snapshot, thêm field, viết lại restore, tích hợp
  vào Remin, cài hệ thống, đóng gói binary. Mode này chỉ gói nguồn hiện có.

## 4. Executive Summary

- Đúng **4 file nguồn** khác pristine: `vteterminal.h` (+12), `vte.cc` (+632),
  `vtegtk.cc` (+75), `vteinternal.hh` (+5). Tổng 724 dòng thêm, **0 dòng xóa**.
- Patch series apply sạch lên pristine tarball; tree sau apply **byte-identical**
  với patched tree đang chạy (cmp từng file + full-tree diff không có dòng khác).
- Clean-room build (meson 171 targets, đúng config gốc) PASS; 0C `OK`; 0E
  `ALL TESTS PASSED / READY_FOR_IMPLEMENTATION` (14/14).
- Chu trình revert→reapply→rebuild→rerun PASS: pristine rebuild → test chết vì
  `undefined symbol: vte_terminal_snapshot_restore` (đúng như kỳ vọng); reapply →
  test lại PASS. Chứng minh API chỉ tồn tại nhờ đúng patch này.
- Không phát hiện unrelated/accidental changes (xem §12). Không có blocking issue (§14).
- Vài finding non-blocking cần ghi nhận cho phase tích hợp (uninitialized read
  trong error path, STUB `has_pending_data`, dead code, non-transactional restore — §15).

## 5. Provenance

### 5.1 Nguồn pristine
- `vte-0.76.0.tar.gz` — tarball chính thức GNOME VTE **0.76.0**.
  - SHA256 `2275d5958d89ca1a93488e066ee33987557db36f560a5dfd4101b1a2d4f150a4` (710126 bytes)
- `VTE_SOURCE_REPO = NONE` — thư mục `vte-0.76.0/` và `vte-0.76.0-audit/` **không
  phải git repo** (không có `.git`); không có lịch sử upstream local.
- `VTE_PRISTINE_COMMIT = N/A` (không có git; upstream tag `vte-0.76.0`).
- `VTE_PATCHED_BASE_COMMIT = N/A` (không có git).
- Anchor provenance = **SHA256 tarball** (không phải commit hash).

### 5.2 Remin repo
- `REMIN_REPO = /home/nguyenduccanh/remin` (git; branch `main`; remote
  `git@github.com:dotlinux26/remin.git`; local ahead 1).
- VTE dirs + patch package + doc là file untracked trong Remin repo (chưa commit/đẩy).

### 5.3 Scratch repro repo (forensic, dùng để sinh patch)
- `/tmp/opencode/vte-pkg/vte-0.76.0` — extract từ tarball, `git init -b main`,
  commit `PRISTINE = 1f8a9f33c6ac12c603be7cee4ebb66d62e8dc8d7`
  ("pristine VTE 0.76.0 upstream tarball"). Dùng làm base để sinh 4 patch.

### 5.4 Hash trước/sau (4 file)
| File | Pristine SHA256 | Patched SHA256 |
|------|-----------------|----------------|
| `src/vte/vteterminal.h` | 587cacbc3cf6679ffd011c01c205c06e2a9c27b0ae6759f218c20869489fe1a7 | 3ede094e66240babd6554222103a2947d2d162eaa5d86e939ba4f47cfec0947a |
| `src/vteinternal.hh` | 76bc745e2086536a915d030f6a1dc5c6ef4f2f8cf63eed9e8e826ada99b65603 | 62c9f4930f2f136b035ff4fea22b3d46a62b26cb90b0f48f9d6d55da52c0ace2 |
| `src/vte.cc` | 92315c5b9235aed7469ec14f450800dc80fb3d106ad6336558c611a1e2883f69 | d6d8dea58dd64e973a584dab6c5d1901f47c360e6339521042958f839575b9dc |
| `src/vtegtk.cc` | d4c9c3837643c94251a0bc2747bfe4ab3799f9d62e9a7880ce3c2ef4b58f1dc5 | 0036936bc1db69a3f2d6f7ac2344461aa9d2c06810fc6a9eef2e53d179be7b7a |

### 5.5 Cây diff đầy đủ (pristine-tar vs working patched)
`diff -rq` toàn cây (loại `build/`, `build-snap/`, nested copy stray,
`*.tar.gz`, `.git`): **chỉ duy nhất `.git` của scratch repo xuất hiện → cây
working = pristine + đúng 4 file.** Dotfiles (`.dir-locals.el`,
`.gitattributes`, `.gitlab-ci*`) byte-identical. Stray `vte-0.76.0/vte-0.76.0/`
là bản pristine copy lạc thư mục (artefact backup, không nằm trong patch — §12).

### 5.6 Tests companion
Hai test 0C/0E (`vte_snapshot_roundtrip_test.cpp`, `vte_critical_validation_test.cpp`)
thuộc **repo Remin** (`tests/unit/`). Đưa vào patch package sẽ phá invariant
"exactly current working patched VTE" (cây VTE không được chứa file test). Chúng
được tái tạo/chạy độc lập trong clean-room (§9).

## 6. Patch n of 4

### 6.1 Patch 1/4 — `0001-vte-0.76.0-public-snapshot-api.patch`
- **File**: `src/vte/vteterminal.h` (chèn sau dòng 658; +12 dòng, public header).
- **Classification**: `REQD` (public API contract — phần không thể thiếu).
- **Issue/title**: "vte: add terminal snapshot capture/restore API (public decls)".
- **Commits**: N/A (scratch; phiên bản final = file patched hiện tại).
- **Changes (3 decls)**, tất cả `_VTE_PUBLIC _VTE_CXX_NOEXCEPT _VTE_GNUC_NONNULL(1)`:
  - `void vte_terminal_snapshot_capture(VteTerminal*, GBytes**)`
  - `gboolean vte_terminal_snapshot_restore(VteTerminal*, GBytes*)`
  - `gboolean vte_terminal_snapshot_has_pending_data(VteTerminal*)`
- **Testing**: clean-room build + 0C/0E (locate symbol; GIR/VAPI generated có API).
- **Errors/Precautions**: không có. Không sửa binding GIR/VALA/auto bằng tay.

### 6.2 Patch 2/4 — `0002-vte-0.76.0-internal-snapshot-declarations.patch`
- **File**: `src/vteinternal.hh` (+5 dòng, khai báo phương thức trong `class Terminal`).
- **Classification**: `REQD` (declarations cần thiết) + `DEBUG` (`snapshot_debug()`).
- **Issue/title**: "vte: internal snapshot member declarations".
- **Changes**: khai báo `snapshot_capture`, `snapshot_restore`,
  `snapshot_debug` (debug-only), `snapshot_has_pending_data` (một phần; bản thân
  method `static` — khai báo đi kèm trong cặp struct reader/writer dùng chung).
- **Testing**: build + toàn bộ test.
- **Errors/Precautions**: không có.

### 6.3 Patch 3/4 — `0003-vte-0.76.0-snapshot-serialization-and-restore.patch`
- **File**: `src/vte.cc` (+632 dòng; chèn sau dòng 11356, trước `} // namespace
  terminal`; nằm trong `namespace vte::terminal`). Patch **lớn nhất** (25 KB).
- **Classification**:
  - `REQD`: serializer byte + reader/writer helpers(`SnpReader`/`snp_put_cell*`,
    `snp_parse_*`...), `Terminal::snapshot_capture` (dòng 11631), `Terminal::snapshot_restore`
    (dòng 11734), toàn bộ capture/restore field.
  - `DEBUG`: `SNPSHOT_ASSERT` (×3, VTE_DEBUG-gated), `Terminal::snapshot_debug()`
    (dòng 11961 — dump prototype), 16 calls `g_printerr("SNAP restore: ...")`.
  - `STUB`: `Terminal::snapshot_has_pending_data` (dòng 11980) — **luôn trả TRUE**,
    kèm comment "In a full implementation...".
  - `ACCIDENTAL/DEAD` (báo cáo, không gỡ): `SnpReader::get_bytes()` — không được
    gọi ở đâu (dead); `snp_put_cell` vs `snp_put_cell_with_hyperlink_idx` có hành vi
    giống hệt nhau (duplicate thừa, tên gây hiểu lầm).
  - `UNKNOWN`: 0.
- **Issue/title**: "vte: snapshot serialization/restore implementation".
- **Changes — restore order** (đã validate ở 0E): `set_size` → `set_scrollback_lines`
  → attributes (palette, cursor style/mode, mouse, wrap, insert, autowrap, char
  replacements, hyperlink index, clipboard/selection, focus, icon/title flags) →
  `ring->reset()` → `snp_get_ring_rows` re-freeze ring → `set_visible_rows`/anchor →
  cursor restore. Dữ liệu bất biến theo byte (allocation == exact) — nền tảng so sánh
  byte-for-byte trong 0E.
- **Testing**: 3 tầng — build, 0C roundtrip, 0E 14/14 (bao gồm negative control).
- **Errors/Precautions**:
  - `-Wmaybe-uninitialized` (cactus, cảnh báo hợp lệ): đọc `magic` (dòng 11744-11746)
    và `pal_count` (11776-11778) **chưa init** trong nhánh `g_printerr` khi snapshot
    malformed. Chỉ chạm error path; hardening khi tích hợp (xem §15 N1).
  - No TODO/FIXME/fprintf/printf trong added block.

### 6.4 Patch 4/4 — `0004-vte-0.76.0-public-snapshot-wrappers.patch`
- **File**: `src/vtegtk.cc` (+75 dòng, sau dòng 7303).
- **Classification**: `REQD` (wrapper public C gọi internal, bọc `try/catch` — theo
  convention VTE hiện có).
- **Issue/title**: "vte: public C wrappers for snapshot API".
- **Testing**: build + tests.
- **Errors/Precautions**: apply phát ra 1 whitespace warning
  (`new blank line at EOF`, 1 line adds whitespace errors) — bản sao chính xác của
  working tree hiện tại; cosmetic.

## 7. Reproducibility (exact commands)

```sh
# --- prepare pristine + scratch repo ---
tar xzf vte-0.76.0.tar.gz -C /tmp/opencode/vte-pkg
cd /tmp/opencode/vte-pkg/vte-0.76.0
git init -q -b main
git add -A
git -c user.email=pkg@local -c user.name="P0-F Packaging" commit -qm "pristine VTE 0.76.0 upstream tarball"

# --- generate patches (forensic; from working patched tree) ---
cp <working>/src/vte/vteterminal.h src/vte/vteterminal.h
cp <working>/src/vteinternal.hh src/vteinternal.hh
cp <working>/src/vte.cc src/vte.cc
cp <working>/src/vtegtk.cc src/vtegtk.cc
git diff HEAD -- src/vte/vteterminal.h > 0001...
git diff HEAD -- src/vteinternal.hh   > 0002...
git diff HEAD -- src/vte.cc           > 0003...
git diff HEAD -- src/vtegtk.cc        > 0004...

# --- revert; verify pristine; apply series ---
git checkout -- .
git status --short          # (empty = pristine)
git apply --check 0001...   # + git apply, lặp cho 0002..0004

# --- clean-room configure + build ---
meson setup build-snap --buildtype=release --default-library=shared --prefix=/usr \
  -Dwarning_level=0 -Dgtk3=true -Dgtk4=true -Dgir=true -Dvapi=true -Da11y=true \
  -Dglade=true -Dgnutls=true -Dicu=true -Dfribidi=true -D_systemd=true \
  -Ddocs=false -Ddbg=false
ninja -C build-snap

# --- compile tests against clean build ---
cp <remin>/tests/unit/vte_snapshot_roundtrip_test.cpp .
cp <remin>/tests/unit/vte_critical_validation_test.cpp .
export PKG_CONFIG_PATH=/tmp/opencode/vte-pkg/vte-0.76.0/build-snap/meson-uninstalled:$PKG_CONFIG_PATH
g++ -std=c++20 vte_snapshot_roundtrip_test.cpp -o rt_test \
  $(pkg-config --cflags vte-2.91-gtk4-uninstalled) $(pkg-config --libs vte-2.91-gtk4-uninstalled) \
  -Wl,-rpath,/tmp/opencode/vte-pkg/vte-0.76.0/build-snap/src
g++ -std=c++20 vte_critical_validation_test.cpp -o crit_test \
  $(pkg-config --cflags vte-2.91-gtk4-uninstalled) $(pkg-config --libs vte-2.91-gtk4-uninstalled) \
  -Wl,-rpath,/tmp/opencode/vte-pkg/vte-0.76.0/build-snap/src
export LD_LIBRARY_PATH=/tmp/opencode/vte-pkg/vte-0.76.0/build-snap/src:$LD_LIBRARY_PATH
./rt_test && ./crit_test
```

## 8. Build Results

- Meson options = đúng bộ gốc của patched build đang chạy (trích từ
  `intro-buildoptions.json`): `buildtype=release` (optimization 3 ngầm),
  `default_library=shared`, `prefix=/usr`, `warning_level=0`; project options
  `a11y=true dbg=false docs=false fribidi=true gir=true glade=true gnutls=true
  gtk3=true gtk4=true icu=true vapi=true _systemd=true`.
- Toolchain ghi nhận: `gcc/g++ 13.3.0` (Ubuntu 24.04, qua ccache), `ld.bfd`,
  `meson 1.3.2`, `ninja 1.11.1`, `valac 0.56.16`, `gtk4 4.14.5`, `glib 2.80.0`,
  `gtk3 3.24.41` — khớp bộ dùng để build patched build gốc (intro-compilers cùng
  gcc 13.3.0).
- Build: **171/171 targets PASS** (cả 2 lib gtk3/gtk4, GIR, VAPI, utils).
- Cảnh báo duy nhất: 2× `-Wmaybe-uninitialized` trong error path restore (xem 6.3) —
  KHÔNG chặn, ghi nhận.
- Symbols exports (nm -D): `vte_terminal_snapshot_capture/restore/has_pending_data`
  đều có T-entries. GIR `Vte-2.91.gir` chứa API (8 reference), VAPI chứa 3 — binding
  tự sinh, không sửa tay.
- Không cài đặt gì vào hệ thống (Step 11 local-only).

## 9. Test Results (clean-room)

| Test | Input | Output | Verb |
|---|---|---|---|
| 0C roundtrip | `rt_test` | `snapshot A size=7612 visible=233` / `vis_a.len=233 vis_b.len=233` / `vte_snapshot_roundtrip_test: OK` | PASS |
| 0E critical | `crit_test` | 14 test `========== END ==========` + `ALL TESTS PASSED` + `IMPLEMENTATION_GATE: READY_FOR_IMPLEMENTATION` | PASS (14/14) |

- Sau pristine rebuild (không patch): `crit_test` chết `symbol lookup error:
  undefined symbol: vte_terminal_snapshot_restore` — bằng chứng âm tính đúng kỳ vọng.
- Sau reapply + rebuild: cả hai test lại PASS. Kết luận: kết quả này gắn chặt với đúng
  patch series; không phụ thuộc vào binary cũ của working tree.

## 10. Gating Results — PATCH_GATE

| # | Gate | Kết quả | Bằng chứng |
|---|---|---|---|
| G1 | Pristine xác định | PASS | tarball vte 0.76.0, SHA256 pin §5.1 |
| G2 | Diff chính xác | PASS | đúng 4 file nguồn, 724 insert, 0 delete (§6, diffstat) |
| G3 | Không thay đổi ngoài lề | PASS | full-tree diff sạch (§5.5, §12) |
| G4 | Series apply sạch | PASS | `git apply` OK; 0004 whitespace-warning cosmetic (§6.4) |
| G5 | Clean build | PASS | 171/171 targets (§8) |
| G6 | Clean-room tests | PASS | 0C OK / 0E 14-14 (§9) |
| G7 | Revert→reapply→rebuild→rerun | PASS | pristine→undefined symbol; patched→PASS (§7, §9) |

```text
PATCH_GATE = READY      =>  VERDICT PATCH_PACKAGE = READY
```

## 11. Review of Debug / STUB / Dead Code

- `SNPSHOT_ASSERT` (×3): assert debug-gated, chỉ active khi `VTE_DEBUG`; không ảnh
  hưởng release. Giữ để tái tạo chính xác; gỡ trong phase làm sạch sau integration.
- `snapshot_debug()`: helper dump "Phase 0C prototype to eyeball..." — debug-only,
  cùng dòng chảy.
- 16× `g_printerr("SNAP restore: ...")`: diagnostic restore, ghi ra stderr khi
  restore/parse gặp lỗi — hữu ích khi chạy test, gây ồn nếu malformed input liên tục.
- `snapshot_has_pending_data` STUB: chức năng thật chưa định nghĩa (0C dùng cho
  slot pending; luôn TRUE). Đánh dấu rõ ở dòng 11980 + comment.
- Dead: `SnpReader::get_bytes()` không được gọi; `snp_put_cell_with_hyperlink_idx`
  trùng hành vi `snp_put_cell`. Chỉ báo cáo (không gỡ — bảo toàn "exactly current").

## 12. Unrelated / Accidental Changes Assessment

- Trong patch package: **không có** unrelated change. Đúng 4 file thuộc feature.
- Không sửa GIR/VALA/binding bằng tay; mọi binding được sinh ở build dir.
- Không sửa test, utils, meson config của cây VTE.
- Artefact ngoài patch (thuộc working directory, không gói):
  1. `vte-0.76.0/vte-0.76.0/` — bản pristine copy lạc chỗ (backup artifact), nội dung
     bằng pristine, đúng `src/vte.cc` pristine.
  2. Vị trí dotfile trong `vte-0.76.0-audit/` khác tarball (bị move vào nested dir
     khi copy audit); nội dung byte-identical — artefact của bản copy, không phải thay đổi.
  3. `build/`, `build-snap/` — build artifacts; loại khỏi patch.
  4. `tests/golden/*`, các doc report — thuộc Remin, không thuộc VTE tree.

## 13. System-level changes

- **KHÔNG có** system-wide change từ phase này (Step 11: local-only).
- Quan sát (pre-existing, KHÔNG do phase này): hệ thống `/usr/lib/.../
  libvte-2.91-gtk4.so.0` **đã có** 3 symbol snapshot (do thử nghiệm cài trước đó),
  trong khi header `/usr/include/vte-2.91-gtk4/vte/vte.h` **không khai báo** chúng.
  → Cấm dựa vào system lib; clean-room phải dùng `.pc` `-uninstalled` của local build.

## 14. Blocking Issues

- Không có. VERDICT module không bị chặn.

## 15. Open / Non-blocking Issues

- **N1** (hardening): uninitialized-read `magic`/`pal_count` trong g_printerr error
  path (vte.cc:11744/11776) — UB chỉ chạm khi snapshot malformed; fix khi làm sạch:
  init về 0 hoặc tách nhánh.
- **N2** (STUB): `snapshot_has_pending_data` luôn TRUE — cần định nghĩa thật khi
  tích hợp.
- **N3** (dead): `get_bytes()` chưa dùng; duplicate helper `snp_put_cell_with_hyperlink_idx`.
- **N4** (architecture): restore không transactional — hỏng giữa chừng để lại terminal
  mutation một phần (không crash, chỉ partial). Cân nhắc commit-atomic cho integration.
- **N5** (input): không có upper-bound validation rows/cols → `set_size` cực lớn có
  thể OOM với snapshot malformed; enum (cursor_shape, mouse_tracking_mode, charset)
  không được range-check.
- **N6**: `row.len` cắt về `u16` — placeholder khi cols > 65535.
- **N7**: f64 dùng `memcpy` (giả định IEEE754).
- **N8**: whitespace warning khi apply 0004 (cosmetic).
- **N9**: system lib confl với dev header — chỉ ảnh hưởng CI cũ; clean-room đã né.
- **N10** (format v4): `window_title`, `current_directory_uri`, `current_file_uri`
  chưa vào snapshot — không chặn 0E (test không đụng), là mở rộng tương lai.
- **N11**: chỉ source-patch reproducible; KHÔNG binary-reproducible (không tự nhiên).

## 16. Risks & Limitations

- Provenance gắn tarball hash, không có git commit upstream → khi submit lên GNOME
  cần rebase theo file (4 patch single-file nên dễ).
- Patch 0003 chứa debug/STUB/dead code cố ý để "exactly current" — chấp nhận trade-off
  giữa tái tạo chính xác và clean code; việc dọn được hoãn sang integration phase.
- Test companion nằm ngoài patch package — quy trình tái tạo phải nhắc lấy 2 file
  test từ Remin (đã ghi ở SERIES.md và §7).
- CI/hệ máy khác có thể thiếu đủ deps (vala → vapi, gtk3) — dựa vào config gốc; nếu
  thiếu ta vẫn build được lib gtk4 cần thiết cho test.

## 17. Evidence

- `patches/vte-0.76.0/` — `0001..0004` + `SERIES.md` + `SHA256SUMS` (pinned).
- Scratch repo `/tmp/opencode/vte-pkg` (PRISTINE commit 1f8a9f3; patch generate/
  apply logs trong shell).
- Clean-room build + test outputs (§8, §9) — đầy đủ tên lệnh ở §7.
- Working patched tree `vte-0.76.0/` + pristine `vte-0.76.0-audit/` + tarball.
- P0-D/0E docs: `docs/vte-behavioral-equivalence-audit.md` (READY),
  `docs/vte-phase0e-critical-validation.md` (14/14).

## 18. Version Tracking

- Remin: `git@github.com:dotlinux26/remin.git`, branch `main`, local ahead 1; các
  file phase này **untracked** (chưa commit).
- VTE: không git (N/A); upstream tag 0.76.0; anchor = tarball hash.
- Patch series version: `v1` (2026-09-08); thay đổi nội dung snapshot sẽ tạo bản mới
  và cập nhật SHA256SUMS.

## 19. Next Steps

1. (Sau phase này, KHÔNG đụng trong P0-F) Tích hợp snapshot vào Remin
   `TerminalPane::runtime_capture()` thay ANSI capture — hoàn thiện P0 integration.
2. Submit patch series lên GNOME VTE (upstream proposal) nếu muốn.
3. Làm sạch: fix N1, gỡ STUB/dead code, định nghĩa `has_pending_data` thật, xét
   transactional restore + upper-bounds (N4/N5) — như patch riêng mới, không sửa gói v1.
4. Mở rộng format snapshot (window_title, cwd/file URI) → format v4 (N10).
5. Tái khẳng định card P0-B "CAPTURE FIDELITY FAILING" sau khi runtime capture thật
   chạy trên format v3 → dùng acceptance deterministic marker để xác nhận.

---

*Created 2026-09-08 · P0-F VTE Patch Packaging & Reproducibility · module `UT-VTE-PKG-000000F`*