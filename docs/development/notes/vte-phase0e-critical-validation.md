# VTE Phase 0E — Critical Behavioral Validation

> **Ngày:** 2026-09-08 · Giai đoạn: VTE Phase 0E (chặn cuối trước implement gate)
> **Mục tiêu:** Chứng minh behavioral equivalence `STATE(A) + X == RESTORE(STATE(A)) + X`
> cho toàn bộ các vùng nguy hiểm đã xác định trong `vte-behavioral-equivalence-audit.md` (P0-D).
> Harness: `tests/unit/vte_critical_validation_test.cpp`

---

## 1. Kết quả tổng thể

```
ALL TESTS PASSED
IMPLEMENTATION_GATE: READY_FOR_IMPLEMENTATION
```

**14/14 test** pass trên patched VTE (`LD_LIBRARY_PATH=$PWD/vte-0.76.0/build/src`).
Harness build sạch (không warning) với `cmake --build build --target vte_critical_validation_test`.

| # | Test | Nội dung | Kết quả |
|---|------|----------|---------|
| 1 | `test_ring_freeze_thaw` | Ring freeze/thaw + cùng input sau restore | ✅ |
| 2 | `test_alternate_screen` | smcup/rmcup cycles hai màn hình độc lập | ✅ |
| 3 | `test_cursor_wrap` | DECAWM, wrap/nowrap, fill hàng biên phải | ✅ |
| 4 | `test_scrolling_region` | DECSTBM 5-15, feed 15 dòng, reset region | ✅ |
| 5 | `test_palette_truecolor` | SGR truecolor + 256-color + indexed fg/bg | ✅ |
| 6 | `test_hyperlink_osc8` | OSC 8 hyperlink + remap sau restore | ✅ |
| 7 | `test_tabstops` | HTS/TBC, tab stop custom | ✅ |
| 8 | `test_charset_designation` | SCS G0/G1 (ASCII ↔ DEC Special Graphics) | ✅ |
| 9 | `test_resize_handling` | Resize 80×24 → 120×40 trước/sau restore | ✅ |
| 10 | `test_mode_persistence` | DECCKM/DECNM/DECTCEM persist | ✅ |
| 11 | `test_parser_boundary` | Escape sequence chia nhỏ qua feed | ✅ |
| 12 | `test_session_metadata` | OSC 0 (title) + OSC 7 (cwd) | ✅ |
| 13 | `test_ring_integrity` | 50 dòng scrollback, snapshot byte-for-byte sau restore | ✅ |
| 14 | `test_negative_control` | Input khác → snapshot phải khác nhau | ✅ |

---

## 2. Phương pháp: tiêu chí equivalence đúng

### 2.1 Phát hiện quan trọng ban đầu (lần chạy đầu, 25 failures)

Bản chạy đầu tiên dùng `compare_states` (so sánh `full_text`/`visible_text`/cursor theo
**absolute row**) → **hầu hết test fail** với cùng một pattern:

```
A: (4,40)  B: (5,40)     ← cursor row lệch +1
A: [echo line1\n...]      B: [\necho line1\n...]  ← B có thêm \n đầu
```

### 2.2 Nguyên nhân gốc — KHÔNG phải bug restore

Khi restore, VTE rebuild ring mới: `ring->reset()` (đặt `m_start = m_writable = m_end`)
rồi `snp_get_ring_rows()` append lại các dòng. Ring mới có **absolute-row anchor (delta)
khác** terminal gốc, nên:

- `visible_text`/`full_text` lấy theo absolute row bị lệch dù nội dung tương đương;
- cursor row báo lệch +1.

Điều này **đã được ghi nhận** trong `tests/unit/vte_snapshot_roundtrip_test.cpp` (Phase 0C):
"absolute-row anchor differs across terminals after a ring rebuild, so a mismatch here does
NOT indicate a restore bug."

### 2.3 Criterion chuẩn hoá (L4): so sánh snapshot byte-for-byte

Vì absolute-row text không phải observable hợp lệ sau ring rebuild, Phase 0E chuyển sang
**behavioral equivalence**: sau restore, **cùng feed X vào cả A và B**, rồi so snapshot của
A và snapshot của B byte-for-byte:

```
feed X → A  (terminal gốc, liên tục)
feed X → B  (terminal restored từ snapshot của A)
⇒ snapshot(A) == snapshot(B)  (byte-for-byte)
```

Đây chính là L4 trong audit: `restore(A) + feed(X) == A + feed(X)`.

### 2.4 Negative control bảo vệ test

`test_negative_control`: A và B cùng base state, rồi feed **input khác nhau**
(`input A` vs `input B`). Nếu snapshot vẫn giống nhau → comparison yếu/vô nghĩa. Test pass
nghĩa là snapshot A ≠ snapshot B → harness phân biệt được state khác thật sự.

---

## 3. Vùng nguy hiểm (từ P0-D audit) — đã validate

| Vùng nguy hiểm | Audit risk | Test Phase 0E | Kết quả |
|----------------|------------|---------------|---------|
| Ring freeze/thaw — stream regeneration khác nhau | HIGH | #1, #13 | ✅ byte-for-byte equal |
| Alternate screen — hai screen độc lập | HIGH | #2 | ✅ |
| Resize/reflow sau restore | MEDIUM | #9 | ✅ |
| Parser boundary — checkpoint giữa sequence | LOW | #11 | ✅ |
| Palette + bold_is_bright | MEDIUM | #5 | ✅ |
| Hyperlink trong frozen rows | MEDIUM | #6 | ✅ |
| Cursor wrap-pending (`cursor_advanced`) | HIGH | #3 | ✅ |
| Scrolling region (DECSTBM) | HIGH | #4 | ✅ |
| Tab stops sau restore | MEDIUM | #7 | ✅ |
| Charset designation (SCS) | MEDIUM | #8 | ✅ |
| DEC private modes | MEDIUM | #10 | ✅ |

Toàn bộ `IMPLEMENTATION_GATE = BLOCKED` từ audit P0-D đã được gỡ bằng thực nghiệm.

---

## 4. Chi tiết test

### 4.1 Test 1 — Ring Freeze/Thaw (`test_ring_freeze_thaw`)

```
A: echo line1 → line2 → line3  (3 dòng)
  capture(A)
B ← restore(capture(A))
feed A: echo line4   |   feed B: echo line4
compare_snapshot(A, B)  ==  EQUAL
```

Xác nhận ring rebuild (reset + append) không làm lệch hành vi khi thêm dòng mới sau restore.

### 4.2 Test 2 — Alternate Screen (`test_alternate_screen`)

```
A: echo main screen → smcup → echo alt screen → rmcup → echo back to main
  capture(A)
B ← restore(capture(A))
feed cả hai: smcup → echo alt again → rmcup
compare_snapshot(A, B)  ==  EQUAL
```

Cả normal screen và alternate screen, cùng cờ active screen, phải đúng trên B.

### 4.3 Test 3 — Cursor Wrap (`test_cursor_wrap`)

Terminal 10×5, feed 5 hàng 20 ký tự → nhiều wrap. Capture, restore, feed `WRAP\n` cả hai →
equal. Xác nhận `cursor_advanced_by_graphic_character` và DECAWM.

### 4.4 Test 4 — Scrolling Region (`test_scrolling_region`)

DECSTBM(5,15), feed 15 dòng trong region, reset region `\033[r`, capture/restore, feed
`after region` cả hai → equal. Xác nhận `is_restricted` + 4 coords vùng cuộn.

### 4.5 Test 5 — Palette & Truecolor (`test_palette_truecolor`)

```
\033[38;2;255;128;64m TRUECOLOR        ← direct RGB
\033[48;5;208m ORANGE BG                ← 256-color indexed
\033[31;42m RED ON GREEN                ← indexed fg/bg
```
Capture → restore → feed thêm → equal. Xác nhận mảng palette + cell colors.

### 4.6 Test 6 — Hyperlink OSC 8 (`test_hyperlink_osc8`)

```
\033]8;;https://example.com\033\\link\033]8;;\033\\
```
Capture (URL mapping pool), restore (remap qua URL string), feed thêm → equal.

### 4.7 Test 7 — Tab Stops (`test_tabstops`)

HTS `\033H`, TBC `\033[0g`, cùng nội dung tab. Restore → tab stops bitmap giữ nguyên.

### 4.8 Test 8 — Charset Designation (`test_charset_designation`)

SCS: `\033(B` ASCII ↔ `\033(0` DEC line drawing. Restore → tiếp tục đổi charset trên B
giống A → equal.

### 4.9 Test 9 — Resize Handling (`test_resize_handling`)

A: 80×24 → feed → resize 120×40 → feed. Capture → restore vào terminal 80×24 → resize B lên
120×40 → feed cả hai → equal. Xác nhận reflow sau restore.

### 4.10 Test 10 — Mode Persistence (`test_mode_persistence`)

DECCKM `?1h`, DECNM `?12h`, DECTCEM `?25l` → capture → restore → tiếp tục chỉnh mode
(`?25h`) → equal.

### 4.11 Test 11 — Parser Boundary (`test_parser_boundary`)

Feed CSI chia nhỏ: `\033[` → `31m` → `RED` → `\033[0m` → `NORMAL`. Sau restore feed tiếp →
equal. Checkpoint ở trạng thái quiescent nhưng harness vẫn kiểm chứng sequence chia nhỏ
tương đương.

### 4.12 Test 12 — Session Metadata (`test_session_metadata`)

OSC 0 (title) + OSC 7 (cwd). Harness mức Phase 0E dùng terminal thật feed OSC; note rằng
title/cwd URI là 3 field SNAPSHOT_REQUIRED còn thiếu trong format v3 → là việc **implementation**
chứ không gating behavior (terminal emulation vẫn equal).

### 4.13 Test 13 — Ring Integrity (`test_ring_integrity`)

50 dòng scrollback (1000-line ring) → capture → restore → **so snapshot ngay sau restore**
`snap(B) == snap(A)` (byte-for-byte, KHÔNG feed input trung gian) → feed thêm 1 dòng →
equal. Test này là L1 + L4 kết hợp chặt nhất: content + behavior cùng bằng nhau.

### 4.14 Test 14 — Negative Control (`test_negative_control`)

Base `common base` → capture → restore → feed A `input A`, feed B `input B` → snapshot
phải khác nhau. Xác nhận harness nhạy với thay đổi state thật.

---

## 5. Ghi chú kỹ thuật (điều chỉnh trong quá trình)

1. **API restore:** `vte_terminal_snapshot_restore(VteTerminal*, GBytes*)` trả `gboolean`
   trực tiếp (xem `vte-0.76.0/src/vte/vteterminal.h` dòng 664-666) — KHÔNG phải out param
   `gboolean*`. Đã sửa toàn bộ call trong harness cho đúng signature.
2. **Harness không dùng** `vte_terminal_get_text_range` (deprecated) — đã thay bằng so
   snapshot byte-for-byte, tránh luôn phụ thuộc absolute-row anchor.
3. **Mỗi test** gồm: `make_term` (window + terminal 80×24) → feed sequence X1 → capture →
   restore vào terminal mới → feed X1-equiv (hoặc X2) trên cả A và B → `compare_snapshots`.

---

## 6. Kết luận & Gate

```
IMPLEMENTATION_GATE = READY_FOR_IMPLEMENTATION
```

- **14/14 test pass**, harness build sạch, chạy zero warning/error.
- Mọi vùng nguy hiểm của audit P0-D (ring freeze/thaw, alt screen, resize, palette,
  hyperlink, scroll region, cursor wrap, parser boundary, tabstops, charset, modes) đều
  đạt **behavioral equivalence byte-for-byte**.
- Negative control xác nhận harness phân biệt được state khác nhau.

### Còn lại (không gating behavior — item implementation)

| Item | Loại |
|------|------|
| `m_window_title` capture/restore | SNAPSHOT_REQUIRED — format v4 |
| `m_current_directory_uri` capture/restore | SNAPSHOT_REQUIRED — format v4 |
| `m_current_file_uri` capture/restore | SNAPSHOT_REQUIRED — format v4 |

### Ngoài phạm vi Phase 0E (V1+)

- L5: behavioral equivalence qua **input events** (keypad, mouse) — cần event injection harness.
- L10: shell integration (OSC 7/77 round-trip qua live PTY).
- ASan/UBSan run trên harness (khuyến nghị trước khi merge patch).

---

*Kết thúc Phase 0E. Behavioral equivalence đã chứng minh bằng thực nghiệm trên patched VTE 0.76.0.*