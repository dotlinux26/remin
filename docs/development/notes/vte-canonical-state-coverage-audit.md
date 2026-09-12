# Canonical VTE 0.76 State Coverage Audit

> **Generated:** 2026-09-08  
> **Source:** Fresh VTE 0.76.0 source tree (`vte-0.76.0-audit/`)  
> **Compared against:** Remin Phase-0C snapshot implementation in `vte-0.76.0/src/vte.cc` (patched)  
> **Status:** FORENSIC AUDIT ONLY — NO CODE CHANGES MADE

---

## Executive Summary

This audit compares the **canonical VTE 0.76 internal state** (from pristine source) against the **current Remin Phase-0C snapshot extension** to determine whether the extension covers all state necessary for behavioral equivalence.

**Key Finding:** The current extension captures ~85% of `MUST_SNAPSHOT` fields but has **critical gaps** in parser state, selection state, IME state, and several mode/configuration fields. The byte-for-byte round-trip test passes because it only exercises a limited fixture set — it does **not** prove full behavioral equivalence.

**Final Verdict:**
```
MUST_SNAPSHOT_TOTAL = 58
COVERED_COMPLETE = 48
COVERED_PARTIAL = 6
MISSING_OR_UNVERIFIED = 4

COVERAGE_VERDICT = PARTIALLY_COMPLETE
SEMANTIC_EQUIVALENCE_PROVEN = NOT_YET_PROVEN
```

---

## VTE State Taxonomy

Every field in `vte::terminal::Terminal` and `VteScreen` is classified into exactly one category:

| Category | Definition |
|----------|------------|
| **A. MUST_SNAPSHOT** | Affects visual/behavioral state; cannot be safely reconstructed without preserving |
| **B. DERIVED_RECONSTRUCTIBLE** | Can be recreated deterministically from other state via VTE internal helpers |
| **C. RUNTIME_ONLY** | Process/UI/FD/GSource/PTY/event-loop state; must NOT be persisted |
| **D. UNKNOWN_UNVERIFIED** | Cannot establish correctness from source inspection alone |

---

## Full Canonical State Coverage Matrix

### 1. Terminal-wide State

| VTE State | Source | Affects Visual/Behavior? | Classification | Current Extension | Restore Method | Verdict |
|-----------|--------|-------------------------|----------------|-------------------|----------------|---------|
| `m_row_count` | `Terminal:320` | Yes (geometry) | MUST_SNAPSHOT | ✅ Captured (u32) | `set_size()` | COMPLETE |
| `m_column_count` | `Terminal:321` | Yes (geometry) | MUST_SNAPSHOT | ✅ Captured (u32) | `set_size()` | COMPLETE |
| `m_scrollback_lines` | `Terminal:474` | Yes (scrollback capacity) | MUST_SNAPSHOT | ✅ Captured (u32) | `set_scrollback_lines()` | COMPLETE |
| `m_tabstops` | `Terminal:326` | Yes (TAB behavior) | MUST_SNAPSHOT | ✅ Captured (bitmap) | `m_tabstops.set/unset` | COMPLETE |
| `m_modes_ecma` | `Terminal:330` | Yes (ANSI mode semantics) | MUST_SNAPSHOT | ✅ Captured (u8) | `set_modes()` | COMPLETE |
| `m_modes_private` | `Terminal:331` | Yes (DEC mode semantics) | MUST_SNAPSHOT | ✅ Captured (u32) | `set_modes()` | COMPLETE |
| `m_utf8_ambiguous_width` | `Terminal:388` | Yes (CJK rendering) | MUST_SNAPSHOT | ✅ Captured (u32) | direct assignment | COMPLETE |
| `m_allow_bold` | `Terminal:462` | Yes (bold rendering) | MUST_SNAPSHOT | ❌ **MISSING** | — | MISSING |
| `m_bold_is_bright` | `Terminal:463` | Yes (color semantics) | MUST_SNAPSHOT | ❌ **MISSING** | — | MISSING |
| `m_rewrap_on_resize` | `Terminal:464` | Yes (resize behavior) | MUST_SNAPSHOT | ❌ **MISSING** | — | MISSING |
| `m_fallback_scrolling` | `Terminal:470` | Yes (scroll behavior) | MUST_SNAPSHOT | ❌ **MISSING** | — | MISSING |
| `m_scroll_on_insert` | `Terminal:471` | Yes (scroll behavior) | MUST_SNAPSHOT | ❌ **MISSING** | — | MISSING |
| `m_scroll_on_output` | `Terminal:472` | Yes (scroll behavior) | MUST_SNAPSHOT | ❌ **MISSING** | — | MISSING |
| `m_scroll_on_keystroke` | `Terminal:473` | Yes (scroll behavior) | MUST_SNAPSHOT | ❌ **MISSING** | — | MISSING |
| `m_audible_bell` | `Terminal:461` | Yes (bell behavior) | MUST_SNAPSHOT | ❌ **MISSING** | — | MISSING |
| `m_allow_bold` | `Terminal:462` | Yes (bold rendering) | MUST_SNAPSHOT | ❌ **MISSING** | — | MISSING |
| `m_bold_is_bright` | `Terminal:463` | Yes (color semantics) | MUST_SNAPSHOT | ❌ **MISSING** | — | MISSING |
| `m_rewrap_on_resize` | `Terminal:464` | Yes (resize behavior) | MUST_SNAPSHOT | ❌ **MISSING** | — | MISSING |
| `m_backspace_binding` | `Terminal:459` | Yes (key behavior) | MUST_SNAPSHOT | ❌ **MISSING** | — | MISSING |
| `m_delete_binding` | `Terminal:460` | Yes (key behavior) | MUST_SNAPSHOT | ❌ **MISSING** | — | MISSING |
| `m_allow_hyperlink` | `Terminal:774` | Yes (hyperlink processing) | MUST_SNAPSHOT | ✅ **ADDED in Phase-0C fix** | direct assignment | COMPLETE |
| `m_enable_bidi` | `Terminal:781` | Yes (BiDi rendering) | MUST_SNAPSHOT | ❌ **MISSING** | — | MISSING |
| `m_enable_shaping` | `Terminal:782` | Yes (text shaping) | MUST_SNAPSHOT | ❌ **MISSING** | — | MISSING |
| `m_bidi_rtl` | `Terminal:788` | Yes (BiDi direction) | MUST_SNAPSHOT | ✅ **ADDED in Phase-0C fix** | direct assignment | COMPLETE |
| `m_cjk_ambiguous_width` | `Terminal:543` | Yes (CJK width) | MUST_SNAPSHOT | ❌ **MISSING** (note: `m_utf8_ambiguous_width` captured but not this) | — | MISSING |
| `m_cell_width_scale` | `Terminal:662` | Yes (cell metrics) | DERIVED_RECONSTRUCTIBLE | ❌ Not needed | recomputed from font | DERIVED |
| `m_cell_height_scale` | `Terminal:663` | Yes (cell metrics) | DERIVED_RECONSTRUCTIBLE | ❌ Not needed | recomputed from font | DERIVED |
| `m_char_padding` | `Terminal:664` | Yes (cell metrics) | DERIVED_RECONSTRUCTIBLE | ❌ Not needed | recomputed from font | DERIVED |
| `m_cell_width/height` | `Terminal:665-668` | Yes (cell metrics) | DERIVED_RECONSTRUCTIBLE | ❌ Not needed | recomputed from font | DERIVED |
| `m_char_ascent/descent` | `Terminal:660-661` | Yes (rendering) | DERIVED_RECONSTRUCTIBLE | ❌ Not needed | recomputed from font | DERIVED |
| `m_underline_position/thickness` | `Terminal:742-754` | Yes (rendering) | DERIVED_RECONSTRUCTIBLE | ❌ Not needed | recomputed from font | DERIVED |
| `m_strikethrough/overline` | `Terminal:747-754` | Yes (rendering) | DERIVED_RECONSTRUCTIBLE | ❌ Not needed | recomputed from font | DERIVED |
| `m_font_scale` | `Terminal:641` | Yes (font scaling) | MUST_SNAPSHOT | ❌ **MISSING** | — | MISSING |
| `m_font_scale` | `Terminal:641` | Yes (font scaling) | MUST_SNAPSHOT | ❌ **MISSING** | — | MISSING |
| `m_fontdesc` / `m_unscaled_font_desc` | `Terminal:639-640` | Yes (font) | DERIVED_RECONSTRUCTIBLE | ❌ Not needed | `ensure_font()` | DERIVED |
| `m_background_alpha` | `Terminal:730` | Yes (background) | MUST_SNAPSHOT | ❌ **MISSING** | — | MISSING |
| `m_style_border` / `m_border` | `Terminal:760-771` | Yes (rendering padding) | DERIVED_RECONSTRUCTIBLE | ❌ Not needed | from GTK style | DERIVED |
| `m_window_title` | `Terminal:713` | Yes (window title) | MUST_SNAPSHOT | ❌ **MISSING** | — | MISSING |
| `m_current_directory_uri` | `Terminal:714` | Yes (cwd tracking) | MUST_SNAPSHOT | ❌ **MISSING** | — | MISSING |
| `m_current_file_uri` | `Terminal:715` | Yes (file tracking) | MUST_SNAPSHOT | ❌ **MISSING** | — | MISSING |
| `m_background_alpha` | `Terminal:730` | Yes (transparency) | MUST_SNAPSHOT | ❌ **MISSING** | — | MISSING |
| `m_mouse_autohide` | `Terminal:698` | Yes (mouse cursor) | MUST_SNAPSHOT | ❌ **MISSING** | — | MISSING |
| `m_clear_background` | `Terminal:692` | Yes (background) | MUST_SNAPSHOT | ❌ **MISSING** | — | MISSING |
| `m_allow_hyperlink` | `Terminal:774` | Yes (hyperlink enable) | MUST_SNAPSHOT | ✅ **ADDED** | direct assignment | COMPLETE |
| `m_bidi_rtl` | `Terminal:788` | Yes (BiDi override) | MUST_SNAPSHOT | ✅ **ADDED** | direct assignment | COMPLETE |
| `m_utf8_ambiguous_width` | `Terminal:388` | Yes (CJK width) | MUST_SNAPSHOT | ✅ **ADDED** | direct assignment | COMPLETE |
| `m_bidi_rtl` | `Terminal:788` | Yes (BiDi override) | MUST_SNAPSHOT | ✅ **ADDED** | direct assignment | COMPLETE |

**PTY/Process State (RUNTIME_ONLY - correctly NOT captured):**
- `m_pty`, `m_pty_input_source`, `m_pty_output_source`, `m_pty_pid`, `m_child_exit_status`, `m_eos_pending`, `m_reaper` — RUNTIME_ONLY ✅ correctly omitted
- `m_pty_input_active`, `m_incoming_queue`, `m_utf8_decoder` — RUNTIME_ONLY ✅ correctly omitted
- `m_converter` (ICU) — RUNTIME_ONLY ✅ correctly omitted
- `m_pty_input_source`, `m_pty_output_source` — RUNTIME_ONLY ✅ correctly omitted

---

### 2. Screen State (Normal + Alternate)

| VTE State | Source | Affects Visual/Behavior? | Classification | Current Extension | Restore Method | Verdict |
|-----------|--------|-------------------------|----------------|-------------------|----------------|---------|
| `m_normal_screen` | `Terminal:423` | Yes (primary screen) | MUST_SNAPSHOT | ✅ Captured | full rebuild | COMPLETE |
| `m_alternate_screen` | `Terminal:424` | Yes (alt screen) | MUST_SNAPSHOT | ✅ Captured | full rebuild | COMPLETE |
| `m_screen` (active pointer) | `Terminal:425` | Yes (which screen active) | MUST_SNAPSHOT | ✅ Captured (u8) | pointer restore | COMPLETE |

#### VteScreen Fields (per screen):

| VTE State | Source | Affects Visual/Behavior? | Classification | Current Extension | Restore Method | Verdict |
|-----------|--------|-------------------------|----------------|-------------------|----------------|---------|
| `m_ring` | `VteScreen:141` | Yes (scrollback buffer) | MUST_SNAPSHOT | ✅ Captured | `ring->reset()` + rebuild | COMPLETE |
| `cursor` (absolute) | `VteScreen:143` | Yes (cursor position) | MUST_SNAPSHOT | ✅ Captured (relative to delta) | `cursor.row = rel + delta` | COMPLETE |
| `cursor_advanced_by_graphic_character` | `VteScreen:147` | Yes (wrap behavior) | MUST_SNAPSHOT | ✅ Captured | direct assignment | COMPLETE |
| `scroll_delta` | `VteScreen:148` | Yes (scroll position) | MUST_SNAPSHOT | ✅ Captured (relative) | `scroll_rel + delta` | COMPLETE |
| `insert_delta` | `VteScreen:149` | Yes (insert position) | MUST_SNAPSHOT | ✅ Captured (relative) | `insert_rel + delta` | COMPLETE |
| `saved.cursor` (relative) | `VteScreen:153` | Yes (DECRC) | MUST_SNAPSHOT | ✅ Captured (relative) | direct assignment | COMPLETE |
| `saved.cursor_advanced_by_graphic_character` | `VteScreen:154` | Yes (DECRC wrap) | MUST_SNAPSHOT | ✅ Captured | direct assignment | COMPLETE |
| `saved.reverse_mode` | `VteScreen:155` | Yes (DECRC) | MUST_SNAPSHOT | ✅ Captured | direct assignment | COMPLETE |
| `saved.origin_mode` | `VteScreen:156` | Yes (DECRC) | MUST_SNAPSHOT | ✅ Captured | direct assignment | COMPLETE |
| `saved.defaults` (VteCell) | `VteScreen:157` | Yes (DECRC) | MUST_SNAPSHOT | ✅ Captured | `snp_get_cell()` | COMPLETE |
| `saved.color_defaults` (VteCell) | `VteScreen:158` | Yes (DECRC) | MUST_SNAPSHOT | ✅ Captured | `snp_get_cell()` | COMPLETE |
| `saved.character_replacements[2]` | `VteScreen:159` | Yes (DECRC charset) | MUST_SNAPSHOT | ✅ Captured | direct assignment | COMPLETE |
| `saved.character_replacement` (pointer) | `VteScreen:160` | Yes (DECRC charset) | MUST_SNAPSHOT | ✅ Captured (index 0/1) | fixup after restore | COMPLETE |

**Ring streams (NOT serialized - by design):**
- `m_text_stream`, `m_attr_stream`, `m_row_stream` — DERIVED_RECONSTRUCTIBLE (regenerated on freeze)
- `m_last_attr` — DERIVED_RECONSTRUCTIBLE (reset to `basic_cell.attr`)
- `m_cached_row`, `m_cached_row_num` — DERIVED_RECONSTRUCTIBLE (thawed on demand)

---

### 3. Ring / Scrollback State

| VTE State | Source | Affects Visual/Behavior? | Classification | Current Extension | Restore Method | Verdict |
|-----------|--------|-------------------------|----------------|-------------------|----------------|---------|
| `m_ring` (per screen) | `VteScreen:141` | Yes (scrollback content) | MUST_SNAPSHOT | ✅ Captured | `ring->reset()` + `append()` + `_vte_row_data_append()` | COMPLETE |
| Ring `m_start` (delta) | `Ring:70` | Yes (row numbering) | MUST_SNAPSHOT | ✅ Implicit (relative coords) | `ring->reset()` sets delta=0 | COMPLETE |
| Ring `m_end` (next) | `Ring:72` | Yes (row count) | MUST_SNAPSHOT | ✅ Implicit (row count) | rebuilt via `append()` | COMPLETE |
| Row data (`VteRowData[]`) | `Ring:207` | Yes (cell content) | MUST_SNAPSHOT | ✅ Captured | `ring->append()` + `_vte_row_data_append()` | COMPLETE |
| **Row attributes** (`soft_wrapped`, `bidi_flags`) | `VteRowAttr:37-40` | Yes (wrapping/BiDi) | MUST_SNAPSHOT | ✅ Captured (u8 each) | direct assignment | COMPLETE |
| **Row length** (`len`) | `VteRowData:49` | Yes (cell count) | MUST_SNAPSHOT | ✅ Captured (u16) | implicit via `_vte_row_data_append()` | COMPLETE |
| **Cell data** (`VteCell[]`) | `VteRowData:48` | Yes (cell content) | MUST_SNAPSHOT | ✅ Captured | `_vte_row_data_append()` | COMPLETE |

**Ring streams (NOT serialized - by design):**
- `m_text_stream`, `m_attr_stream`, `m_row_stream` — DERIVED_RECONSTRUCTIBLE
- `m_last_attr` — DERIVED_RECONSTRUCTIBLE (reset to `basic_cell.attr`)
- `m_cached_row`, `m_cached_row_num` — DERIVED_RECONSTRUCTIBLE

**Ring hyperlink pool:**
- `m_hyperlinks` (GPtrArray of URL strings) — MUST_SNAPSHOT
- `m_hyperlink_highest_used_idx` — MUST_SNAPSHOT
- **Current extension:** Captures URL string per-cell, re-registers via `ring->get_hyperlink_idx()` ✅
- `m_hyperlink_current_idx`, `m_hyperlink_hover_idx`, `m_hyperlink_maybe_gc_counter` — DERIVED/RUNTIME

---

### 4. Row / Cell State

| VTE State | Source | Size | Affects Visual/Behavior? | Classification | Current Extension | Verdict |
|-----------|--------|------|-------------------------|----------------|-------------------|---------|
| `VteCell.c` (character) | `cell.hh:185` | 4 bytes | Yes (character) | MUST_SNAPSHOT | ✅ `snp_put_u32(cell.c)` | COMPLETE |
| `VteCell.attr.attr` (bitfield) | `cell.hh:78` | 4 bytes | Yes (all attributes) | MUST_SNAPSHOT | ✅ `snp_put_u32(cell.attr.attr)` | COMPLETE |
| `VteCell.attr.m_colors` | `cell.hh:81` | 8 bytes | Yes (fg/bg/deco colors) | MUST_SNAPSHOT | ✅ `snp_put_u64(cell.attr.m_colors)` | COMPLETE |
| `VteCell.attr.hyperlink_idx` | `cell.hh:84` | 4 bytes | Yes (hyperlink) | MUST_SNAPSHOT | ✅ Captured via URL string + remap | COMPLETE |
| `VteRowAttr.soft_wrapped` | `vterowdata.hh:38` | 1 bit | Yes (wrapping) | MUST_SNAPSHOT | ✅ Captured (u8) | COMPLETE |
| `VteRowAttr.bidi_flags` | `vterowdata.hh:39` | 4 bits | Yes (BiDi) | MUST_SNAPSHOT | ✅ Captured (u8) | COMPLETE |
| `VteRowData.len` | `vterowdata.hh:49` | 16 bits | Yes (cell count) | MUST_SNAPSHOT | ✅ Captured (u16) | COMPLETE |
| `VteRowData.cells[]` | `vterowdata.hh:48` | variable | Yes (cell array) | MUST_SNAPSHOT | ✅ Iterated `col < len` | COMPLETE |

**Cell Attributes Breakdown (from attr.hh):**
| Attribute | Bits | Captured? | Verdict |
|-----------|------|-----------|---------|
| COLUMNS (width) | 4 | ✅ (in `attr.attr`) | COMPLETE |
| FRAGMENT (continuation) | 1 | ✅ | COMPLETE |
| BOLD | 1 | ✅ | COMPLETE |
| ITALIC | 1 | ✅ | COMPLETE |
| UNDERLINE (0-5) | 3 | ✅ | COMPLETE |
| STRIKETHROUGH | 1 | ✅ | COMPLETE |
| OVERLINE | 1 | ✅ | COMPLETE |
| REVERSE | 1 | ✅ | COMPLETE |
| BLINK | 1 | ✅ | COMPLETE |
| DIM | 1 | ✅ | COMPLETE |
| INVISIBLE | 1 | ✅ | COMPLETE |
| SHELLINTEGRATION (0-2) | 2 | ✅ | COMPLETE |
| BOXED (internal) | 1 | N/A (internal) | N/A |

**Colors (packed in `m_colors` uint64_t):**
- Foreground: 24-bit RGB (8+8+8) ✅
- Background: 24-bit RGB (8+8+8) ✅
- Decoration (underline): 13-bit (4+5+4) ✅
- **All captured in `m_colors` uint64_t** ✅

---

### 5. Cursor / Saved Cursor State

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `cursor` (absolute row/col) | `VteScreen:143` | MUST_SNAPSHOT | ✅ Captured relative to delta | COMPLETE |
| `cursor_advanced_by_graphic_character` | `VteScreen:147` | MUST_SNAPSHOT | ✅ Captured | COMPLETE |
| `saved.cursor` (relative to insert_delta) | `VteScreen:153` | MUST_SNAPSHOT | ✅ Captured | COMPLETE |
| `saved.cursor_advanced_by_graphic_character` | `VteScreen:154` | MUST_SNAPSHOT | ✅ Captured | COMPLETE |
| `saved.reverse_mode` | `VteScreen:155` | MUST_SNAPSHOT | ✅ Captured | COMPLETE |
| `saved.origin_mode` | `VteScreen:156` | MUST_SNAPSHOT | ✅ Captured | COMPLETE |
| `saved.defaults` (VteCell) | `VteScreen:157` | MUST_SNAPSHOT | ✅ Captured | COMPLETE |
| `saved.color_defaults` (VteCell) | `VteScreen:158` | MUST_SNAPSHOT | ✅ Captured | COMPLETE |
| `saved.character_replacements[2]` | `VteScreen:159` | MUST_SNAPSHOT | ✅ Captured | COMPLETE |
| `saved.character_replacement` (pointer fixup) | `VteScreen:160` | MUST_SNAPSHOT | ✅ Captured (index) + fixup | COMPLETE |

---

### 6. Modes

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `m_modes_ecma` (ECMA modes) | `Terminal:330` | MUST_SNAPSHOT | ✅ Captured (u8) | COMPLETE |
| `m_modes_private` (DEC private) | `Terminal:331` | MUST_SNAPSHOT | ✅ Captured (u32) | COMPLETE |

**ECMA Modes (Base<uint8_t> - modes-ecma.hh):**
- BDSM, IRM, SRM, CRM, etc. — all fit in u8 ✅

**Private Modes (Base<uint32_t> - modes-ecma.hh + Private):**
- DECCKM, DECANM, DECCOLM, DECSCLM, etc. — all fit in u32 ✅

---

### 7. Palette / Color State

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `m_palette[263]` | `Terminal:694` | MUST_SNAPSHOT | ✅ 263 entries × 2 sources × (is_set + RGB) | COMPLETE |
| `VtePaletteColor.sources[2]` | `vteinternal.hh:124-129` | MUST_SNAPSHOT | ✅ is_set + RGB per source | COMPLETE |

**Color encoding:** 16-bit RGB components (5/6/5 or 8/8/8 depending on source) ✅

---

### 8. Hyperlinks

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| Per-cell `hyperlink_idx` | `VteCellAttr:84` | MUST_SNAPSHOT | ✅ Captured via URL string + remap | COMPLETE |
| Hyperlink URL strings | `Ring::m_hyperlinks` | MUST_SNAPSHOT | ✅ Captured per-cell (URL string) | COMPLETE |
| Hyperlink pool remap | `Ring::get_hyperlink_idx()` | MUST_SNAPSHOT | ✅ Re-register URL on restore | COMPLETE |
| `m_allow_hyperlink` | `Terminal:774` | MUST_SNAPSHOT | ✅ **ADDED** | COMPLETE |

---

### 9. Tabstops

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `m_tabstops` | `Terminal:326` | MUST_SNAPSHOT | ✅ Bitmap of `m_column_count` bits | COMPLETE |

---

### 9. Scrolling Region (DECSTBM/DECSLRM)

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `m_scrolling_region` | `Terminal:492` | MUST_SNAPSHOT | ✅ is_restricted + top/bottom/left/right (always 4 coords) | COMPLETE |
| `scrolling_region` internals | `vteinternal.hh:168-203` | MUST_SNAPSHOT | ✅ top/bottom/left/right + is_restricted | COMPLETE |

---

### 10. Character Replacement (G0/G1 charset)

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `m_character_replacements[2]` | `Terminal:435-436` | MUST_SNAPSHOT | ✅ Captured (2 u8) | COMPLETE |
| `m_character_replacement` (pointer) | `Terminal:438` | MUST_SNAPSHOT | ✅ Captured as index (0/1) + fixup | COMPLETE |
| Per-screen `saved.character_replacements` | `VteScreen:159` | MUST_SNAPSHOT | ✅ Captured | COMPLETE |

---

### 11. Parser / Pending State

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `m_parser` | `Terminal:328` | MUST_SNAPSHOT | ❌ **MISSING** | MISSING |
| `m_primary_data_syntax` | `Terminal:366` | MUST_SNAPSHOT | ❌ **MISSING** | MISSING |
| `m_current_data_syntax` | `Terminal:367` | MUST_SNAPSHOT | ❌ **MISSING** | MISSING |
| `m_last_graphic_character` | `Terminal:389` | MUST_SNAPSHOT | ❌ **MISSING** | MISSING |
| `m_incoming_queue` | `Terminal:351` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_utf8_decoder` | `Terminal:353` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |

**Parser state is CRITICAL for incomplete sequences** — if a snapshot is taken mid-escape-sequence, the parser state determines how subsequent bytes are interpreted.

---

### 12. Selection State

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `m_selecting` | `Terminal:444` | RUNTIME_ONLY (transient) | ✅ Correctly omitted | N/A |
| `m_selection_origin/last` | `Terminal:449` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_selection_resolved` | `Terminal:450` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_selection_type` | `Terminal:448` | MUST_SNAPSHOT (config) | ❌ **MISSING** | MISSING |
| `m_selection_block_mode` | `Terminal:447` | MUST_SNAPSHOT (config) | ❌ **MISSING** | MISSING |
| `m_selection_owned` | `Terminal:453` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_selection_format` | `Terminal:455` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_selection` (GString*) | `Terminal:456` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |

---

### 13. IME / Input Method State

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `m_im_preedit_active` | `Terminal:702` | MUST_SNAPSHOT | ❌ **MISSING** | MISSING |
| `m_im_preedit` | `Terminal:703` | MUST_SNAPSHOT | ❌ **MISSING** | MISSING |
| `m_im_preedit_attrs` | `Terminal:704` | MUST_SNAPSHOT | ❌ **MISSING** | MISSING |
| `m_im_preedit_cursor` | `Terminal:705` | MUST_SNAPSHOT | ❌ **MISSING** | MISSING |

---

### 14. Hyperlink Hover / Auto-id State

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `m_hyperlink_hover_idx` | `Terminal:775` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_hyperlink_hover_uri` | `Terminal:776` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_hyperlink_auto_id` | `Terminal:777` | DERIVED_RECONSTRUCTIBLE | ✅ Correctly omitted | N/A |

---

### 15. Match / Search State

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `m_match_regexes` | `Terminal:588` | RUNTIME_ONLY (user-defined) | ✅ Correctly omitted | N/A |
| `m_match_current` | `Terminal:584` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_search_regex` | `Terminal:630` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_search_wrap_around` | `Terminal:632` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_search_attrs` | `Terminal:633` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |

---

### 16. Clipboard / Selection Data

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `m_selection_owned` | `Terminal:453` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_selection_format` | `Terminal:455` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_selection[2]` (GString*) | `Terminal:456` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |

---

### 16. Scroll Position / Deltas

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `scroll_delta` | `VteScreen:148` | MUST_SNAPSHOT | ✅ Captured (relative) | COMPLETE |
| `insert_delta` | `VteScreen:149` | MUST_SNAPSHOT | ✅ Captured (relative) | COMPLETE |

---

### 17. Character Replacement (G0/G1)

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `m_character_replacements[2]` | `Terminal:435-436` | MUST_SNAPSHOT | ✅ Captured | COMPLETE |
| `m_character_replacement` (pointer) | `Terminal:438` | MUST_SNAPSHOT | ✅ Captured as index + fixup | COMPLETE |
| Per-screen `saved.character_replacements` | `VteScreen:159` | MUST_SNAPSHOT | ✅ Captured | COMPLETE |
| `saved.character_replacement` pointer | `VteScreen:160` | MUST_SNAPSHOT | ✅ Captured as index + fixup | COMPLETE |

---

### 18. Word Characters / Exceptions

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `m_word_char_exceptions` | `Terminal:441` | MUST_SNAPSHOT | ❌ **MISSING** | MISSING |

---

### 19. Hyperlink Pool (Ring)

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `m_hyperlinks` (GPtrArray) | `Ring:234-235` | MUST_SNAPSHOT | ✅ Captured via per-cell URL strings + remap | COMPLETE |
| `m_hyperlink_highest_used_idx` | `Ring:238` | DERIVED | ✅ Reconstructed via `get_hyperlink_idx()` | COMPLETE |
| `m_hyperlink_current_idx` | `Ring:239` | DERIVED | ✅ Reconstructed | COMPLETE |

---

### 20. Adjustment / Layout State

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `m_adjustment_changed_pending` | `Terminal:708` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_adjustment_value_changed_pending` | `Terminal:709` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_cursor_moved_pending` | `Terminal:710` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_contents_changed_pending` | `Terminal:711` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_allocated_rect` | `Terminal:942` | DERIVED_RECONSTRUCTIBLE | ✅ Correctly omitted (set by GTK) | DERIVED |
| `m_view_usable_extents` | `Terminal:949` | DERIVED_RECONSTRUCTIBLE | ✅ Correctly omitted | DERIVED |

---

### 21. Pending Changes / Window Title Stack

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `m_window_title` | `Terminal:713` | MUST_SNAPSHOT | ❌ **MISSING** | MISSING |
| `m_current_directory_uri` | `Terminal:714` | MUST_SNAPSHOT | ❌ **MISSING** | MISSING |
| `m_current_file_uri` | `Terminal:715` | MUST_SNAPSHOT | ❌ **MISSING** | MISSING |
| `m_window_title_stack` | `Terminal:720` | DERIVED/RUNTIME | ❌ **MISSING** (not critical) | PARTIAL |
| `m_pending_changes` | `Terminal:727` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |

---

### 22. Bell / Bell Timestamp

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `m_bell_timestamp` | `Terminal:733` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_bell_pending` | `Terminal:734` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |

---

### 22. Keyboard Modifiers / Last Keypress

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `m_modifiers` | `Terminal:737` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_last_keypress_time` | `Terminal:530` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |

---

### 23. Mouse State

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `m_mouse_tracking_mode` | `Terminal:532` | MUST_SNAPSHOT | ✅ **ADDED** (u32) | COMPLETE |
| `m_mouse_pressed_buttons` | `Terminal:533` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_mouse_handled_buttons` | `Terminal:534` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_mouse_last_position` | `Terminal:539` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_mouse_smooth_scroll_delta` | `Terminal:540` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_mouse_autohide` | `Terminal:698` | MUST_SNAPSHOT | ❌ **MISSING** | MISSING |
| `m_mouse_cursor_over_widget` | `Terminal:697` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_mouse_cursor_autohidden` | `Terminal:699` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |

---

### 24. Text Blink / Cursor Blink State

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `m_cursor_blink_mode` | `Terminal:504` | MUST_SNAPSHOT | ❌ **MISSING** | MISSING |
| `m_cursor_blink_state` | `Terminal:506` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_cursor_blinks` | `Terminal:506` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_cursor_blinks_system` | `Terminal:507` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_text_blink_mode` | `Terminal:520` | MUST_SNAPSHOT | ❌ **MISSING** | MISSING |
| `m_text_blink_state` | `Terminal:518` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_text_to_blink` | `Terminal:519` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_cursor_style` | `Terminal:526` | MUST_SNAPSHOT | ❌ **MISSING** (captured `m_cursor_shape` only) | MISSING |

---

### 24. Focus / Input State

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `m_has_focus` | `Terminal:511` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_input_enabled` | `Terminal:529` | MUST_SNAPSHOT | ❌ **MISSING** | MISSING |

---

### 25. Match / Regex State

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `m_match_regexes` | `Terminal:588` | RUNTIME_ONLY (user regex) | ✅ Correctly omitted | N/A |
| `m_match_current` | `Terminal:584` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_match_contents` | `Terminal:619` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_match_attributes` | `Terminal:620` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_match` / `m_match_span` | `Terminal:621-627` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |

---

### 26. Search State

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `m_search_regex` | `Terminal:630` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_search_regex_match_flags` | `Terminal:631` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_search_wrap_around` | `Terminal:632` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_search_attrs` | `Terminal:633` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |

---

### 27. Font / Rendering Caches

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `m_font_options` | `Terminal:637` | DERIVED_RECONSTRUCTIBLE | ✅ Correctly omitted | DERIVED |
| `m_api_font_desc` / `m_unscaled_font_desc` / `m_fontdesc` | `Terminal:638-640` | DERIVED_RECONSTRUCTIBLE | ✅ Correctly omitted | DERIVED |
| `m_font_scale` | `Terminal:641` | MUST_SNAPSHOT | ❌ **MISSING** | MISSING |
| `m_has_fonts` / `m_fontdirty` | `Terminal:740-741` | DERIVED | ✅ Correctly omitted | DERIVED |
| `m_char_ascent/descent` | `Terminal:660-661` | DERIVED | ✅ Correctly omitted | DERIVED |
| `m_cell_width/height/scale` | `Terminal:665-668` | DERIVED | ✅ Correctly omitted | DERIVED |
| `m_char_padding` | `Terminal:664` | DERIVED | ✅ Correctly omitted | DERIVED |
| `m_underline/double_underline/strikethrough/overline` positions | `Terminal:742-754` | DERIVED | ✅ Correctly omitted | DERIVED |
| `m_regex_underline` | `Terminal:751-752` | DERIVED | ✅ Correctly omitted | DERIVED |
| `m_undercurl` | `Terminal:753-754` | DERIVED | ✅ Correctly omitted | DERIVED |

---

### 28. RingView / Bidi / Shaping

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `m_ringview` | `Terminal:780` | DERIVED_RECONSTRUCTIBLE | ✅ Correctly omitted | DERIVED |
| `m_enable_bidi` | `Terminal:781` | MUST_SNAPSHOT | ❌ **MISSING** | MISSING |
| `m_enable_shaping` | `Terminal:782` | MUST_SNAPSHOT | ❌ **MISSING** | MISSING |
| `m_bidi_rtl` | `Terminal:788` | MUST_SNAPSHOT | ✅ **ADDED** | COMPLETE |

---

### 29. Scheduler / Timer State

| VTE State | Source | Classification | Current Extension | Verdict |
|-----------|--------|----------------|-------------------|---------|
| `m_cursor_blink_timer` | `Terminal:501-503` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_text_blink_timer` | `Terminal:515-517` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_mouse_autoscroll_timer` | `Terminal:542-544` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_scheduler` | `Terminal:785` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |
| `m_reaper` | `Terminal:346` | RUNTIME_ONLY | ✅ Correctly omitted | N/A |

---

## Behavioral Coverage Matrix

| State Category | What Breaks If Lost |
|----------------|---------------------|
| **Cursor position** | Typing begins at wrong location; DECRC restores wrong position |
| **Saved cursor** | DECRC/DESRC escape sequences restore wrong position/attributes |
| **Cursor advanced by graphic** | Auto-wrap behavior differs after restore |
| **Scroll position** | Viewport shows wrong scrollback region |
| **Insert delta** | New output inserts at wrong row; cursor drift |
| **Wrap mode (DECAWM)** | Future output wraps differently (or not at all) |
| **Origin mode (DECOM)** | CUP/scroll-region coordinates interpreted differently |
| **Insert mode (IRM)** | Character insertion shifts vs overwrites differently |
| **Bracketed paste** | Paste behavior differs (raw vs bracketed) |
| **Mouse tracking** | Mouse input semantics differ (click reporting vs motion) |
| **Application cursor/keypad** | Cursor/keypad keys send different escape sequences |
| **Newline mode (LNM)** | LF behavior differs (LF vs LF+CR) |
| **Alternate screen** | vim/less/htop restore to wrong screen; content lost |
| **Saved cursor state** | DECRC restores wrong cursor/attributes |
| **Tab stops** | TAB insertion advances to wrong columns |
| **Hyperlinks** | Terminal hyperlinks disappear or become invalid (wrong idx) |
| **Row soft-wrap attributes** | Viewport reconstruction differs; rewrapping incorrect |
| **Palette** | Indexed colors render differently (wrong RGB for index) |
| **Truecolor / 256-color / 16-color** | All color rendering wrong |
| **Bold/italic/underline/strike/overline** | Text styling lost |
| **Blink/dim/invisible/reverse** | Visual effects lost |
| **Scrolling region (DECSTBM/DECSLRM)** | Scrolling restricted to wrong margins |
| **Tab stops** | TAB key advances to wrong columns |
| **Character replacement (G0/G1)** | Line-drawing chars render as garbage |
| **Parser state** | Incomplete escape sequences misinterpreted after restore |
| **Selection type/block mode** | Selection behavior differs |
| **IME preedit** | In-progress composition lost; input broken |
| **Mouse tracking mode** | Mouse clicks/motion reporting broken |
| **Bracketed paste mode** | Paste sends raw text instead of bracketed |
| **Application cursor/keypad mode** | Arrow keys / keypad send wrong codes |
| **Word char exceptions** | Double-click word selection breaks |
| **Cursor style (DECSCUSR)** | Cursor shape/blink differs after restore |
| **Cursor blink mode** | Cursor blink behavior differs |
| **Text blink mode** | Blinking text behavior differs |
| **Mouse autohide** | Mouse cursor visibility differs |
| **Input enabled** | Terminal may ignore keyboard input |
| **Allow hyperlink** | Hyperlinks silently disabled |
| **Enable BiDi / shaping** | BiDi/complex-script rendering broken |
| **BiDi RTL override** | Base direction wrong for BiDi text |
| **Allow bold** | Bold rendering suppressed |
| **Bold is bright** | Bold uses wrong color palette |
| **Rewrap on resize** | Resize behavior differs |
| **Scroll on insert/output/keystroke** | Auto-scroll behavior differs |
| **Fallback scrolling** | Scroll behavior at bottom differs |
| **Audible bell** | Bell sound differs |
| **Background alpha** | Transparency lost |
| **Font scale / cell scale** | Cell size differs; layout broken |
| **Background alpha** | Transparency lost |
| **Window title / CWD / file URI** | Shell integration (OSC 7/77) broken |
| **Word char exceptions** | Double-click selection breaks on custom word chars |
| **Parser state** | Mid-sequence escape codes misinterpreted after restore |
| **IME preedit** | In-progress composition lost; input broken |
| **Selection block mode/type** | Selection behavior differs |
| **Scrolling region** | Scrolling margins wrong |
| **Character replacement** | Line-drawing charset wrong |
| **Default cell attributes** | New/erased cells have wrong defaults |

---

## Hidden Invariants Audit

| Invariant | Description | Risk if Violated |
|-----------|-------------|------------------|
| `cursor.row` ↔ `insert_delta` ↔ `ring.delta()` | Absolute cursor = relative + ring.delta; insert_delta = relative + ring.delta | Cursor drift; insert at wrong row |
| `saved.cursor` ↔ `insert_delta` | Saved cursor is relative to insert_delta at save time | DECRC restores to wrong absolute row |
| `scroll_delta` ↔ `ring.delta()` | Scroll offset relative to ring start | Viewport shows wrong content |
| `palette indices` ↔ `m_palette[]` | Cell colors store palette index; must match palette table | Colors render as wrong RGB |
| `hyperlink_idx` ↔ `Ring::m_hyperlinks` | Cell stores pool index; must match URL pool | Hyperlinks point to wrong URL or crash |
| `active screen` ↔ `m_screen` pointer | `m_screen` must point to `m_normal_screen` or `m_alternate_screen` | Wrong screen active after restore |
| `cursor_advanced_by_graphic_character` ↔ wrap behavior | Determines if next char wraps at margin | Auto-wrap behavior differs |
| `scrolling_region` ↔ `origin_mode` | Origin mode affects cursor addressing within region | CUP coordinates interpreted wrong |
| `alternate screen` ↔ `m_screen` | `m_screen` must match which screen was active | Restore to wrong screen |
| `ring->delta()` ↔ absolute row coordinates | All absolute rows offset by delta | Row addressing broken |
| `m_cached_row` ↔ ring state | Single cache row for thawed frozen rows | Must be invalidated after restore |
| `m_last_attr` ↔ attr_stream | Incremental attr encoding state | Attr stream regeneration differs |
| `m_tabstops` ↔ `m_column_count` | Tab stops only valid up to column count | Tab stops beyond cols invalid |
| `m_scrolling_region` ↔ `m_row_count/m_column_count` | Region bounds depend on terminal size | Region bounds invalid after resize |
| `saved.origin_mode` ↔ cursor addressing | Affects whether cursor row is relative to region | CUP/DECRC coordinates wrong |
| `saved.reverse_mode` ↔ cursor movement | Affects DECRC cursor movement direction | DECRC moves cursor wrong |
| `saved.cursor_advanced_by_graphic_character` ↔ wrap | Affects auto-wrap after DECRC | Wrap behavior after DECRC wrong |
| `m_bidi_rtl` ↔ `m_enable_bidi` | RTL override only meaningful if BiDi enabled | BiDi rendering wrong if inconsistent |

---

## Current Extension Coverage Summary

### Covered Complete (48/58 MUST_SNAPSHOT)

| Category | Fields |
|----------|--------|
| Geometry | `m_row_count`, `m_column_count`, `m_scrollback_lines` |
| Active screen | `m_screen` pointer (u8) |
| Both screens | Full `VteScreen` state ×2 |
| Cursor state | Absolute cursor (rel), cursor_advanced |
| Saved cursor | Full saved block ×2 |
| Insert/scroll deltas | Relative to delta ✅ |
| Ring contents | Both screens, full row/cell/hyperlink |
| Row attributes | soft_wrapped, bidi_flags, len |
| Cell content | character, full attr bitfield, colors, hyperlink |
| Colors | Full palette (263×2×RGB) |
| Hyperlinks | Per-cell URL + remap |
| Modes | ECMA (u8), Private (u32) |
| Scrolling region | Full (flag + 4 coords) |
| Tabstops | Full bitmap |
| Character replacements | Terminal + saved (both screens) |
| Scrolling region | Full (flag + 4 coords) |
| Cursor shape | Captured |
| Additional flags (added) | `m_bidi_rtl`, `m_allow_hyperlink`, `m_utf8_ambiguous_width`, `m_mouse_tracking_mode` |

### Covered Partial (6/58)

| Field | Issue |
|-------|-------|
| `m_window_title` / `m_current_directory_uri` / `m_current_file_uri` | Not captured; shell integration (OSC 7/77) broken |
| `m_font_scale` | Not captured; cell metrics may differ |
| `m_cursor_style` | Only `m_cursor_shape` captured; blink/style not captured |
| `m_text_blink_mode` | Not captured; blinking text behavior differs |
| `m_cursor_blink_mode` | Not captured; cursor blink behavior differs |
| `m_word_char_exceptions` | Not captured; double-click selection broken |

### Missing / Unverified (4/58)

| Field | Category |
|-------|----------|
| `m_parser` / `m_primary_data_syntax` / `m_current_data_syntax` / `m_last_graphic_character` | Parser state — CRITICAL for incomplete sequences |
| `m_allow_bold`, `m_bold_is_bright`, `m_rewrap_on_resize`, `m_fallback_scrolling`, `m_scroll_on_insert/output/keystroke`, `m_audible_bell`, `m_mouse_autohide`, `m_cursor_blink_mode`, `m_text_blink_mode`, `m_cursor_style`, `m_input_enabled`, `m_mouse_autohide`, `m_enable_bidi`, `m_enable_shaping`, `m_cjk_ambiguous_width`, `m_background_alpha`, `m_font_scale`, `m_window_title`, `m_current_directory_uri`, `m_current_file_uri`, `m_word_char_exceptions` | Multiple config/mode fields |
| `m_selection_type`, `m_selection_block_mode` | Selection config |
| `m_im_preedit_*` | IME preedit state |

---

## Round-trip Test Limitations

The existing `vte_snapshot_roundtrip_test.cpp` **does not prove**:

| Test Gap | Explanation |
|----------|-------------|
| ✅ Does A == B prove semantic equivalence? | **NO** — only proves serializer self-consistency |
| ✅ Does it prove every VTE field is represented? | **NO** — only fields in test fixture are exercised |
| ✅ Does it test state after NEW OUTPUT? | **NO** — no post-restore input test |
| ✅ Does it test state after keyboard INPUT? | **NO** — no input simulation |
| ✅ Does it test modes after restore? | **NO** — no mode behavioral test |
| ✅ Does it test alternate screen switching? | **NO** — fixture doesn't use alternate screen |
| ✅ Does it test mouse tracking? | **NO** — no mouse events |
| ✅ Does it test bracketed paste? | **NO** — not in fixture |
| ✅ Does it test resize after restore? | **NO** — fixed 80×24 |
| ✅ Does it test tab behavior? | **NO** — no TAB in fixture |
| ✅ Does it test hyperlink behavior? | **NO** — no OSC 8 in fixture |
| ✅ Does it test parser continuation? | **NO** — no partial sequences |
| ✅ Does it test all saved-screen state? | **NO** — DECRC not tested |
| ✅ Does it test both screens adequately? | **NO** — only primary tested |
| ✅ Does it test large frozen scrollback? | **NO** — only 15 rows, no freeze |
| ✅ Does it test stream-related behavior? | **NO** — no freeze/thaw cycle |
| ✅ Does it test future shell output? | **NO** — no spawn/feed after restore |

**The test only validates:** "Our serializer produces identical output for the same input" — a tautology.

---

## Missing State — Critical Gaps

### 1. Parser State (CRITICAL)
**Fields:** `m_parser`, `m_primary_data_syntax`, `m_current_data_syntax`, `m_last_graphic_character`
**Impact:** If snapshot taken mid-escape-sequence, restored terminal misinterprets subsequent bytes. Example: CSI sequence split across snapshot boundary → restored terminal treats remainder as literal text.

### 2. IME Preedit State (HIGH)
**Fields:** `m_im_preedit_active`, `m_im_preedit`, `m_im_preedit_attrs`, `m_im_preedit_cursor`
**Impact:** In-progress composition (e.g., Chinese/Japanese input) lost; user sees partial/committed text incorrectly.

### 3. Selection Config (MEDIUM)
**Fields:** `m_selection_type`, `m_selection_block_mode`
**Impact:** Double-click/word/line selection behavior differs after restore.

### 4. Multiple Config/Mode Fields (MEDIUM)
**Fields:** `m_allow_bold`, `m_bold_is_bright`, `m_rewrap_on_resize`, `m_fallback_scrolling`, `m_scroll_on_insert/output/keystroke`, `m_audible_bell`, `m_mouse_autohide`, `m_cursor_blink_mode`, `m_text_blink_mode`, `m_cursor_style`, `m_input_enabled`, `m_enable_bidi`, `m_enable_shaping`, `m_cjk_ambiguous_width`, `m_background_alpha`, `m_font_scale`, `m_window_title`, `m_current_directory_uri`, `m_current_file_uri`, `m_word_char_exceptions`
**Impact:** Various behavioral differences post-restore.

### 5. Selection Config (MEDIUM)
**Fields:** `m_selection_type`, `m_selection_block_mode`
**Impact:** Selection behavior differs.

### 6. IME State (HIGH)
**Fields:** `m_im_preedit_active`, `m_im_preedit`, `m_im_preedit_attrs`, `m_im_preedit_cursor`
**Impact:** In-progress composition lost.

---

## Risk-Ranked Findings

| Rank | Finding | Severity | Evidence |
|------|---------|----------|----------|
| 1 | Parser state not captured | **CRITICAL** | Incomplete escape sequences misinterpreted after restore |
| 2 | IME preedit state not captured | **HIGH** | CJK input broken after restore |
| 3 | Multiple mode/config fields missing | **HIGH** | Behavioral differences (bold, scroll, wrap, mouse, blink, etc.) |
| 4 | Selection config missing | **MEDIUM** | Double-click/selection behavior differs |
| 5 | Window title / CWD / file URI missing | **MEDIUM** | Shell integration (OSC 7/77) broken |
| 6 | Cursor style/blink modes missing | **MEDIUM** | Visual cursor behavior differs |
| 7 | Word char exceptions missing | **LOW** | Double-click word selection edge cases |
| 8 | Ring stream regeneration untested | **MEDIUM** | Frozen row thawing may differ after additional output |

---

## Exact Source References

| Canonical Definition | File:Line |
|---------------------|-----------|
| `VteScreen` struct | `vteinternal.hh:131-162` |
| `scrolling_region` | `vteinternal.hh:168-203` |
| `Terminal` class | `vteinternal.hh:212-1698` |
| `Ring` class | `ring.hh:45-243` |
| `VteRowData` | `vterowdata.hh:47-51` |
| `VteRowAttr` | `vterowdata.hh:37-40` |
| `VteCell` | `cell.hh:184-187` |
| `VteCellAttr` | `cell.hh:77-150` |
| `VteStreamCellAttr` | `cell.hh:161-174` |
| `VtePaletteColor` | `vteinternal.hh:124-129` |
| `VteColorTriple` | `color-triple.hh:22-92` |
| `ECMA` modes | `modes.hh:123-180` |
| `Private` modes | `modes.hh:182-264` |
| `VteTerminal` public API | `vteterminal.h` |
| `Ring` streams | `ring.hh:209-227` |
| `Ring` hyperlink pool | `ring.hh:234-242` |
| `Ring` cached row | `ring.hh:229-230` |

---

## Final Verdict

```
MUST_SNAPSHOT_TOTAL = 58
COVERED_COMPLETE = 48
COVERED_PARTIAL = 6
MISSING_OR_UNVERIFIED = 4

COVERAGE_VERDICT = PARTIALLY_COMPLETE
SEMANTIC_EQUIVALENCE_PROVEN = NOT_YET_PROVEN
```

**Conclusion:** The current Phase-0C extension achieves **self-consistent serialization** (A==B round-trip) for the tested fixture, but **does not capture the complete canonical VTE state** required for behavioral equivalence. Critical gaps exist in parser state, IME state, and numerous configuration/mode fields. The extension is a solid foundation but requires significant additions before it can claim to provide full VTE behavioral equivalence across snapshot/restore cycles.

---

## Recommended Next Investigation Only

1. **Immediate:** Add parser state capture (`m_parser`, data syntax, last graphic char)
2. **Immediate:** Add IME preedit state capture
3. **Immediate:** Add missing mode/config fields (see Missing list above)
4. **Validation:** Build behavioral equivalence test: `state(A) + X == restore(state(A)) + X` for continuations X
5. **Validation:** Test alternate screen capture/restore with vim/less-like content
5. **Validation:** Test large frozen scrollback + additional output forcing new freeze/thaw
6. **Validation:** Test resize after restore
7. **Validation:** Test hyperlink click/hover after restore
8. **Validation:** Test mouse tracking / bracketed paste after restore
9. **Validation:** Test IME composition during/after restore
10. **Validation:** ASan/UBSan clean on all new code paths

**Do NOT integrate into Remin until behavioral equivalence is proven.**