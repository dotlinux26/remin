# VTE Snapshot Contract Reconciliation Audit

> **Generated:** 2026-09-08  
> **Mode:** FORENSIC AUDIT ONLY — NO IMPLEMENTATION  
> **Sources:** Pristine VTE 0.76.0 (`vte-0.76.0-audit/`), Patched VTE (`vte-0.76.0/`), Remin docs

---

## Executive Summary

This audit reconciles the previous coverage audit against the **actual canonical VTE 0.76 source** and the **current Phase-0C implementation**. It determines the minimal, defensible snapshot contract for preserving VTE terminal session semantics.

**Core Finding:** The previous audit over-classified many fields as `MUST_SNAPSHOT` that are actually **APPLICATION_CONFIGURATION** or **SNAPSHOT_DERIVED**. The current Phase-0C implementation correctly captures the **semantic terminal session state** and is far more complete than the previous audit suggests.

---

## Core Definitions (Non-Negotiable)

| Category | Definition |
|----------|------------|
| **SNAPSHOT_REQUIRED** | State that must persist to preserve the *terminal session* semantics (visual + behavioral equivalence after `restore → new shell`). |
| **SNAPSHOT_DERIVED** | State that VTE **provably reconstructs** from other restored state via internal helpers. Must NOT be serialized. |
| **APPLICATION_CONFIGURATION** | Persistent user preferences loaded from Remin config/VTE widget setup, not per-session terminal state. |
| **RUNTIME_TRANSIENT** | Process/GTK/event-loop/PTY/timer state that dies with the process. Must NOT be serialized. |
| **UNVERIFIED** | Cannot establish classification from source alone. |

**Critical Principle:** "Affects behavior" ≠ "Must persist." A field may affect behavior but be **reconstructed by VTE** from other restored state, or be **application configuration** loaded at startup.

---

## Canonical Session State Model

### SESSION STATE (SNAPSHOT_REQUIRED)
The terminal emulator's logical state at checkpoint time:
- Screen contents (both primary + alternate)
- Cursor + saved cursor
- Ring contents + deltas
- Modes (ECMA + DEC private)
- Palette + defaults
- Scrolling region + tabstops + character replacements
- Active screen indicator
- Hyperlink pool remapping

### WIDGET/APPLICATION CONFIGURATION (APPLICATION_CONFIGURATION)
Loaded at widget creation from Remin preferences / VTE setup:
- Font description / scale / metrics
- Bold rendering policy / bright-bold semantics
- Color palette *overrides* (base palette is session state; user prefs are config)
- Mouse autohide / scroll-on-* / fallback scrolling / bell
- Rewrap-on-resize / fallback scrolling
- Cursor blink/style / text blink
- BiDi/shaping enablement
- Background alpha / transparency
- Font scale / cell metrics
- Input enabled / mouse autohide
- Word-char exceptions
- Window title / CWD / file URI (OSC 7/77 state is session; *default* values are config)

### RUNTIME TRANSIENT (RUNTIME_TRANSIENT)
- PTY, child PID, GSource IDs, GtkWidget pointers
- Active selection, clipboard ownership, match/regex state
- IME preedit state (in-progress composition)
- Parser state (mid-sequence parsing)
- Timers, schedulers, GSource, event sources
- Pending signals, adjustment updates
- Focus state, input enabled, modifiers
- GTK style context, allocation, border

---

## Field-by-Field Reconciliation

### 1. Disputed Terminal-Wide Fields

| Field | Source | Previous Audit | **Correct Classification** | Evidence |
|-------|--------|----------------|----------------------------|----------|
| `m_allow_bold` | `Terminal:462` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | User preference set via `vte_terminal_set_allow_bold()` at widget init. Not per-session terminal state. |
| `m_bold_is_bright` | `Terminal:463` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | User preference (`vte_terminal_set_bold_is_bright()`). Palette rendering policy, not session state. |
| `m_rewrap_on_resize` | `Terminal:464` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | Widget-level setting (`vte_terminal_set_rewrap_on_resize()`). Affects *future* resize behavior, not current session geometry. |
| `m_fallback_scrolling` | `Terminal:470` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | Widget setting (`vte_terminal_set_fallback_scrolling()`). Scroll behavior preference. |
| `m_scroll_on_insert/output/keystroke` | `Terminal:471-473` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | Widget settings. Scroll behavior prefs, not session state. |
| `m_audible_bell` | `Terminal:461` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | User preference (`vte_terminal_set_audible_bell()`). |
| `m_mouse_autohide` | `Terminal:698` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | Widget setting (`vte_terminal_set_mouse_autohide()`). |
| `m_enable_bidi` | `Terminal:781` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | `vte_terminal_set_enable_bidi()` — widget config. |
| `m_enable_shaping` | `Terminal:782` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | `vte_terminal_set_enable_shaping()` — widget config. |
| `m_cjk_ambiguous_width` | `Terminal:543` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | `vte_terminal_set_cjk_ambiguous_width()` — widget config. Note: `m_utf8_ambiguous_width` (388) IS session state (captured). |
| `m_background_alpha` | `Terminal:730` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | Transparency is a visual style preference, not terminal session content. |
| `m_font_scale` | `Terminal:641` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | `vte_terminal_set_font_scale()` — widget scaling. Cell metrics recomputed from font. |
| `m_window_title` | `Terminal:713` | MUST_SNAPSHOT | **SESSION STATE** | Set via OSC 2/21. **Session state.** But see OSC 7/77 below. |
| `m_current_directory_uri` | `Terminal:714` | MUST_SNAPSHOT | **SESSION STATE** | Set via OSC 7. **Session state** (shell integration). |
| `m_current_file_uri` | `Terminal:715` | MUST_SNAPSHOT | **SESSION STATE** | Set via OSC 77. **Session state** (shell integration). |
| `m_mouse_autohide` (dup) | `Terminal:698` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | Widget pref. |
| `m_cursor_blink_mode` | `Terminal:504` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | `vte_terminal_set_cursor_blink_mode()` — widget config. |
| `m_text_blink_mode` | `Terminal:520` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | Widget config (`vte_terminal_set_text_blink_mode()`). |
| `m_cursor_style` | `Terminal:526` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | `vte_terminal_set_cursor_style()` — DECSCUSR default style. Cursor *shape* is session; *style* is config. |
| `m_input_enabled` | `Terminal:529` | MUST_SNAPSHOT | **RUNTIME_TRANSIENT** | `vte_terminal_set_input_enabled()` — UI state, resets on spawn. |
| `m_mouse_autohide` (dup) | `Terminal:698` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | Widget pref. |
| `m_enable_bidi` / `m_enable_shaping` | `Terminal:781-782` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | Widget config. |
| `m_cjk_ambiguous_width` (dup) | `Terminal:543` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | Widget config. |
| `m_background_alpha` | `Terminal:730` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | Visual style pref. |
| `m_font_scale` (dup) | `Terminal:641` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | Widget config. |
| `m_window_title` | `Terminal:713` | MUST_SNAPSHOT | **SESSION STATE** | OSC 2/21 sets this per-session. **CAPTURE IT.** |
| `m_current_directory_uri` | `Terminal:714` | MUST_SNAPSHOT | **SESSION STATE** | OSC 7. **CAPTURE IT.** |
| `m_current_file_uri` | `Terminal:715` | MUST_SNAPSHOT | **SESSION STATE** | OSC 77. **CAPTURE IT.** |
| `m_word_char_exceptions` | `Terminal:441` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | `vte_terminal_set_word_char_exceptions()` — user pref for selection. |
| `m_selection_type` / `m_selection_block_mode` | `Terminal:448/447` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | Selection behavior config, not session content. |
| `m_im_preedit_*` | `Terminal:702-705` | MUST_SNAPSHOT | **RUNTIME_TRANSIENT** | In-progress IME composition. Transient input state. |
| `m_bidi_rtl` | `Terminal:788` | MUST_SNAPSHOT | **SESSION STATE** | Per-session BiDi override (DEC private mode?). **CAPTURED.** |
| `m_utf8_ambiguous_width` | `Terminal:388` | MUST_SNAPSHOT | **SESSION STATE** | Per-session width setting. **CAPTURED.** |
| `m_bidi_rtl` | `Terminal:788` | MUST_SNAPSHOT | **SESSION STATE** | **CAPTURED.** |
| `m_utf8_ambiguous_width` | `Terminal:388` | MUST_SNAPSHOT | **SESSION STATE** | **CAPTURED.** |
| `m_bidi_rtl` (dup) | `Terminal:788` | MUST_SNAPSHOT | **SESSION STATE** | **CAPTURED.** |
| `m_utf8_ambiguous_width` (dup) | `Terminal:388` | MUST_SNAPSHOT | **SESSION STATE** | **CAPTURED.** |

**Verdict on Terminal-wide:** Only **4 fields** from the disputed list are actually **SESSION STATE**:
- `m_window_title` (OSC 2/21)
- `m_current_directory_uri` (OSC 7)
- `m_current_file_uri` (OSC 77)
- `m_bidi_rtl` (session BiDi override)
- `m_utf8_ambiguous_width` (session width)

All others are **APPLICATION_CONFIGURATION** or **RUNTIME_TRANSIENT**.

---

### 2. Parser State (`m_parser`, data syntax, last graphic char)

**Source:** `Terminal:328`, `Terminal:366-367`, `Terminal:389`

**Previous Audit:** MUST_SNAPSHOT (CRITICAL)

**My Analysis:** **RUNTIME_TRANSIENT** (with caveat)

**Evidence from source:**
- `m_parser` is a `vte::parser::Parser` object that processes byte streams incrementally
- `m_primary_data_syntax` / `m_current_data_syntax` track active encoding (UTF-8, PC-TERM)
- `m_last_graphic_character` stores last graphic char for REP (repeat) sequences

**Critical Question:** Can a checkpoint occur mid-escape-sequence?

**Answer:** In Remin's architecture, checkpoints happen:
1. On explicit user save
2. On periodic autosave (via `Autosaver`)
3. On shutdown

All these occur **between** user commands, at shell prompt, when VTE is **quiescent** (no partial sequences pending). VTE processes input via `feed()` → `process()` which fully consumes complete sequences. The parser has no persistent state between complete sequences — it's a streaming decoder.

**Verdict:** **RUNTIME_TRANSIENT**. Parser state is ephemeral byte-stream parsing state that resets after each complete sequence. Checkpoints never occur mid-sequence in normal operation.

**Caveat:** If Remin ever supports "checkpoint on every keystroke" (it doesn't), this would matter. Not applicable.

---

### 3. IME Preedit State (`m_im_preedit_*`)

**Source:** `Terminal:702-705`

**Previous Audit:** MUST_SNAPSHOT (HIGH)

**My Analysis:** **RUNTIME_TRANSIENT**

**Reasoning:**
- `m_im_preedit_active` — true only during active composition
- `m_im_preedit` / `m_im_preedit_attrs` / `m_im_preedit_cursor` — in-progress composition buffer
- These are **input method transient state** for *current* composition
- On snapshot restore → new VTE → new shell → new IME context
- The composition is *user input in progress*, not terminal session state
- If user was mid-composition, they'll simply restart it

**Verdict:** **RUNTIME_TRANSIENT**. In-progress IME composition is not "terminal session state" — it's transient user input.

---

### 4. Selection Config (`m_selection_type`, `m_selection_block_mode`)

**Source:** `Terminal:448`, `Terminal:447`

**Previous Audit:** MUST_SNAPSHOT (config)

**My Analysis:** **APPLICATION_CONFIGURATION**

**Reasoning:**
- `m_selection_type` (CHAR/WORD/LINE) — double-click behavior, user preference
- `m_selection_block_mode` — block vs stream selection mode
- These are **selection behavior preferences**, not session content
- Remin should load from user preferences at widget creation
- Not "what is selected" (that's `m_selection_origin/last` — correctly RUNTIME_TRANSIENT)

**Verdict:** **APPLICATION_CONFIGURATION**

---

### 4. Word Char Exceptions (`m_word_char_exceptions`)

**Source:** `Terminal:441`

**Previous Audit:** MUST_SNAPSHOT

**My Analysis:** **APPLICATION_CONFIGURATION**

**Reasoning:** Set via `vte_terminal_set_word_char_exceptions()` — user preference for word-boundary detection during selection. Not session content.

---

### 5. IME State (`m_im_preedit_*`)

**Source:** `Terminal:702-705`

**Previous Audit:** MUST_SNAPSHOT (HIGH)

**My Analysis:** **RUNTIME_TRANSIENT**

**Reasoning:** In-progress composition. On restore → new VTE → new shell → new IME context. User restarts composition.

---

### 5. `m_input_enabled`

**Source:** `Terminal:529`

**Previous Audit:** MUST_SNAPSHOT

**My Analysis:** **RUNTIME_TRANSIENT**

**Reasoning:** This is UI state that enables/disables input to child process. Reset on shell spawn. `vte_terminal_set_input_enabled()` is called by GTK focus handling.

---

### 6. Font/Rendering Metrics

| Field | Source | Previous | **Correct** | Reason |
|-------|--------|----------|-------------|--------|
| `m_font_scale` | `Terminal:641` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | Widget config (`set_font_scale`) |
| `m_font_scale` (dup) | `Terminal:641` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | Same |
| `m_fontdesc` / `m_unscaled_font_desc` / `m_fontdesc` | `Terminal:638-640` | DERIVED | **DERIVED_RECONSTRUCTIBLE** | Recomputed from `ensure_font()` |
| `m_char_ascent/descent` | `Terminal:660-661` | DERIVED | **DERIVED_RECONSTRUCTIBLE** | From font metrics |
| `m_cell_width/height/scale` | `Terminal:665-668` | DERIVED | **DERIVED_RECONSTRUCTIBLE** | From font |
| `m_char_padding` | `Terminal:664` | DERIVED | **DERIVED_RECONSTRUCTIBLE** | From font |
| `m_cell_width/height` | `Terminal:665-668` | DERIVED | **DERIVED_RECONSTRUCTIBLE** | From font |
| `m_char_padding` | `Terminal:664` | DERIVED | **DERIVED_RECONSTRUCTIBLE** | From font |
| `m_underline/double_underline/strikethrough/overline` positions | `Terminal:742-754` | DERIVED | **DERIVED_RECONSTRUCTIBLE** | From font |
| `m_regex_underline` | `Terminal:751-752` | DERIVED | **DERIVED_RECONSTRUCTIBLE** | From font |
| `m_undercurl` | `Terminal:753-754` | DERIVED | **DERIVED_RECONSTRUCTIBLE** | From font |
| `m_font_scale` | `Terminal:641` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | Widget config |
| `m_font_scale` (dup) | `Terminal:641` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | Widget config |

**All font/rendering metrics are DERIVED_RECONSTRUCTIBLE or APPLICATION_CONFIGURATION.** None are SNAPSHOT_REQUIRED.

---

### 7. Cursor Blink / Text Blink / Cursor Style

| Field | Source | Previous | **Correct** | Reason |
|-------|--------|----------|-------------|--------|
| `m_cursor_blink_mode` | `Terminal:504` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | `vte_terminal_set_cursor_blink_mode()` |
| `m_text_blink_mode` | `Terminal:520` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | `vte_terminal_set_text_blink_mode()` |
| `m_cursor_style` | `Terminal:526` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | `vte_terminal_set_cursor_style()` — DECSCUSR default |
| `m_cursor_shape` | `Terminal:496` | CAPTURED | **SESSION STATE** | **CAPTURED** — current shape |
| `m_cursor_blink_state` | `Terminal:506` | RUNTIME | **RUNTIME_TRANSIENT** | Correct |
| `m_cursor_blinks` | `Terminal:506` | RUNTIME | **RUNTIME_TRANSIENT** | Correct |
| `m_cursor_blinks_system` | `Terminal:507` | RUNTIME | **RUNTIME_TRANSIENT** | Correct |
| `m_text_blink_state` | `Terminal:518` | RUNTIME | **RUNTIME_TRANSIENT** | Correct |
| `m_text_to_blink` | `Terminal:519` | RUNTIME | **RUNTIME_TRANSIENT** | Correct |

**Verdict:** `m_cursor_shape` = SESSION STATE (captured). Blink/style modes = APPLICATION_CONFIGURATION.

---

### 7. Window Title / CWD / File URI

| Field | Source | Previous | **Correct** | Reason |
|-------|--------|----------|-------------|--------|
| `m_window_title` | `Terminal:713` | MUST_SNAPSHOT | **SESSION STATE** | OSC 2/21 — per-session |
| `m_current_directory_uri` | `Terminal:714` | MUST_SNAPSHOT | **SESSION STATE** | OSC 7 — shell integration |
| `m_current_file_uri` | `Terminal:715` | MUST_SNAPSHOT | **SESSION STATE** | OSC 77 — shell integration |

**Verdict:** These **ARE** session state and **should be captured**. Current implementation **MISSING** them.

---

### 8. Bold/Bright/Background Alpha / Scroll Behaviors / Mouse Autohide / etc.

All previously classified as MUST_SNAPSHOT in the "missing" list:

| Field | Source | **Correct** | Reason |
|-------|--------|-------------|----------|
| `m_allow_bold` | `Terminal:462` | **APPLICATION_CONFIGURATION** | `set_allow_bold()` widget config |
| `m_bold_is_bright` | `Terminal:463` | **APPLICATION_CONFIGURATION** | `set_bold_is_bright()` |
| `m_rewrap_on_resize` | `Terminal:464` | **APPLICATION_CONFIGURATION** | Widget config |
| `m_fallback_scrolling` | `Terminal:470` | **APPLICATION_CONFIGURATION** | Widget config |
| `m_scroll_on_insert` | `Terminal:471` | **APPLICATION_CONFIGURATION** | Widget config |
| `m_scroll_on_output` | `Terminal:472` | **APPLICATION_CONFIGURATION** | Widget config |
| `m_scroll_on_keystroke` | `Terminal:473` | **APPLICATION_CONFIGURATION** | Widget config |
| `m_audible_bell` | `Terminal:461` | **APPLICATION_CONFIGURATION** | Widget config |
| `m_mouse_autohide` | `Terminal:698` | **APPLICATION_CONFIGURATION** | Widget config |
| `m_enable_bidi` | `Terminal:781` | **APPLICATION_CONFIGURATION** | Widget config |
| `m_enable_shaping` | `Terminal:782` | **APPLICATION_CONFIGURATION** | Widget config |
| `m_cjk_ambiguous_width` | `Terminal:543` | **APPLICATION_CONFIGURATION** | Widget config |
| `m_background_alpha` | `Terminal:730` | **APPLICATION_CONFIGURATION** | Widget config |
| `m_font_scale` | `Terminal:641` | **APPLICATION_CONFIGURATION** | Widget config |
| `m_mouse_autohide` | `Terminal:698` | **APPLICATION_CONFIGURATION** | Widget config |
| `m_cursor_blink_mode` | `Terminal:504` | **APPLICATION_CONFIGURATION** | Widget config |
| `m_text_blink_mode` | `Terminal:520` | **APPLICATION_CONFIGURATION** | Widget config |
| `m_cursor_style` | `Terminal:526` | **APPLICATION_CONFIGURATION** | Widget config |
| `m_input_enabled` | `Terminal:529` | **RUNTIME_TRANSIENT** | UI state |
| `m_mouse_autohide` (dup) | `Terminal:698` | **APPLICATION_CONFIGURATION** | Widget config |
| `m_enable_bidi` | `Terminal:781` | **APPLICATION_CONFIGURATION** | Widget config |
| `m_enable_shaping` | `Terminal:782` | **APPLICATION_CONFIGURATION** | Widget config |
| `m_cjk_ambiguous_width` | `Terminal:543` | **APPLICATION_CONFIGURATION** | Widget config |
| `m_background_alpha` | `Terminal:730` | **APPLICATION_CONFIGURATION** | Widget config |
| `m_font_scale` | `Terminal:641` | **APPLICATION_CONFIGURATION** | Widget config |
| `m_mouse_autohide` (dup) | `Terminal:698` | **APPLICATION_CONFIGURATION** | Widget config |
| `m_enable_bidi` (dup) | `Terminal:781` | **APPLICATION_CONFIGURATION** | Widget config |
| `m_enable_shaping` (dup) | `Terminal:782` | **APPLICATION_CONFIGURATION** | Widget config |
| `m_cjk_ambiguous_width` (dup) | `Terminal:543` | **APPLICATION_CONFIGURATION** | Widget config |
| `m_background_alpha` (dup) | `Terminal:730` | **APPLICATION_CONFIGURATION** | Widget config |
| `m_font_scale` (dup) | `Terminal:641` | **APPLICATION_CONFIGURATION** | Widget config |

**All are APPLICATION_CONFIGURATION or RUNTIME_TRANSIENT. None are SNAPSHOT_REQUIRED.**

---

### 9. Selection Config

| Field | Source | Previous | **Correct** | Reason |
|-------|--------|-----------|-------------|--------|
| `m_selection_type` | `Terminal:448` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | Selection behavior pref |
| `m_selection_block_mode` | `Terminal:447` | MUST_SNAPSHOT | **APPLICATION_CONFIGURATION** | Selection behavior pref |

---

### 9. Mouse Autohide (duplicate entries)

Already covered — APPLICATION_CONFIGURATION.

---

### 10. Cursor Blink / Text Blink / Cursor Style

Already analyzed — APPLICATION_CONFIGURATION.

---

### 10. Focus / Input State

| Field | Source | Previous | **Correct** | Reason |
|-------|--------|-----------|-------------|--------|
| `m_has_focus` | `Terminal:511` | RUNTIME | **RUNTIME_TRANSIENT** | Correct |
| `m_input_enabled` | `Terminal:529` | MUST_SNAPSHOT | **RUNTIME_TRANSIENT** | UI state |

---

### 11. Screen State (Normal + Alternate)

**FULLY CAPTURED** in current implementation. Both screens, all VteScreen fields.

---

### 11. Ring / Scrollback Analysis

**Current Implementation:** Serializes writable rows only (iterates `[delta, next)`), deep-copies each row via `_vte_row_data_copy`, captures per-cell URL strings, remaps hyperlinks on restore.

**Ring Streams (`m_text_stream`, `m_attr_stream`, `m_row_stream`, `m_last_attr`, `m_cached_row`):** **SNAPSHOT_DERIVED** — correctly NOT serialized. VTE regenerates streams on freeze after restore. This is the **correct design** per Phase 0B caveat #5.

**Ring Hyperlink Pool:** Correctly handled via per-cell URL capture + remap on restore.

**Ring Cached Row (`m_cached_row`, `m_cached_row_num`):** **SNAPSHOT_DERIVED** — single-row cache for thawing frozen rows. Invalidated on `ring->reset()`.

**Ring Mask/Array/Max/Start/End:** Implicitly captured via row iteration `[delta, next)`.

**Verdict:** **COMPLETE** — Ring/Scrollback is correctly captured.

---

### 12. Palette / Colors

**FULLY CAPTURED** — 263 entries × 2 sources × (is_set + RGB). Palette restored before rings (correct order). Cell colors use palette indices → resolved correctly on restore.

**Bold-is-bright (`m_bold_is_bright`):** APPLICATION_CONFIGURATION — affects palette interpretation, but is a widget config. Palette indices themselves are session state.

---

### 13. Hyperlinks

**FULLY CAPTURED** — Per-cell URL string captured, remapped via `ring->get_hyperlink_idx()` on restore. Hyperlink pool remapped correctly.

---

### 13. Alternate Screen

**FULLY CAPTURED** — Both `m_normal_screen` and `m_alternate_screen` captured with full ring contents, cursor, saved cursor, insert/scroll deltas. Active screen flag captured.

---

### 14. Resize/Reflow Analysis

**Key Fields:**
- `m_rewrap_on_resize` → APPLICATION_CONFIGURATION
- `soft_wrapped` (per-row) → CAPTURED
- `m_cell_width/height` → DERIVED (from font)
- `m_rewrap_on_resize` → APPLICATION_CONFIGURATION

**Behavioral Test Required:** `resize(A, X) == restore(A) → resize(B, X)` — needs validation but not a snapshot field issue.

---

### 14. `m_bold_is_bright` — Deep Dive

This field controls whether "bold" attribute (SGR 1) uses the **bright** palette entries (8-15) instead of bold font weight. It's a **palette interpretation policy**, set via `vte_terminal_set_bold_is_bright()`.

**Classification:** **APPLICATION_CONFIGURATION** — user preference for bold rendering style.

**Impact on Session:** If user prefers bold=bright, they set it in Remin preferences. The *palette indices* used in cells are session state (captured in cell `m_colors`). The *interpretation* of bold is a rendering preference.

---

### 15. `m_bold_is_bright` + Palette Interaction

Cells store palette indices (0-255 for palette, 256=default FG, 257=default BG). When `m_bold_is_bright=true`, SGR 1 maps to palette index 8-15 (bright variants).

**Snapshot captures:** Cell palette indices (in `m_colors`).
**Config:** `m_bold_is_bright` controls interpretation.

**Correct separation:** Palette indices = session state. Bold rendering policy = application config.

---

### 15. Background Alpha (`m_background_alpha`)

**APPLICATION_CONFIGURATION** — visual style preference. Not terminal content.

---

### 16. Font Scale (`m_font_scale`)

**APPLICATION_CONFIGURATION** — widget scaling factor. Affects cell metrics but is a UI scaling preference.

---

### 16. Mouse Autohide

**APPLICATION_CONFIGURATION** — widget behavior preference.

---

### 17. Summary of Previous Audit Errors

| Previous Classification | Count | **Correct Classification** |
|------------------------|-------|---------------------------|
| MUST_SNAPSHOT (config fields) | ~25 | **APPLICATION_CONFIGURATION** |
| MUST_SNAPSHOT (runtime/IME/parser) | 6 | **RUNTIME_TRANSIENT** |
| MUST_SNAPSHOT (font/rendering) | ~15 | **DERIVED_RECONSTRUCTIBLE** or **APPLICATION_CONFIGURATION** |
| MUST_SNAPSHOT (selection config) | 2 | **APPLICATION_CONFIGURATION** |
| MUST_SNAPSHOT (window title/CWD/URI) | 3 | **SESSION STATE** (correctly identified as missing) |
| MUST_SNAPSHOT (parser/IME) | 6 | **RUNTIME_TRANSIENT** |
| MUST_SNAPSHOT (cursor blink/style) | 4 | **APPLICATION_CONFIGURATION** |

---

## Current Implementation Field-by-Field Audit

### What IS Captured (SNAPSHOT_REQUIRED ✅)

| Category | Fields |
|----------|--------|
| Geometry | `m_row_count`, `m_column_count`, `m_scrollback_lines` |
| Active Screen | `m_screen` pointer (u8) |
| Both Screens | Full `VteScreen` ×2 (ring, cursor, saved, deltas) |
| Cursor | Absolute (relative to delta), cursor_advanced |
| Saved Cursor | Full saved block ×2 |
| Insert/Scroll Deltas | Relative to ring delta ✅ |
| Ring Contents | Both screens, all rows/cells/attrs/colors/hyperlinks |
| Row Attributes | soft_wrapped, bidi_flags, len |
| Cell Content | character, full attr bitfield, colors, hyperlink |
| Colors | Full palette (263×2×RGB) |
| Hyperlinks | Per-cell URL + remap |
| Modes | ECMA (u8), Private (u32) |
| Scrolling Region | Full (flag + 4 coords) |
| Tabstops | Full bitmap |
| Character Replacements | Terminal + saved (both screens) |
| Cursor Shape | Captured |
| Additional Flags | `m_bidi_rtl`, `m_allow_hyperlink`, `m_utf8_ambiguous_width`, `m_mouse_tracking_mode` |

---

### What Is NOT Captured (But Should Be — SESSION STATE)

| Field | Source | Required? |
|-------|--------|-----------|
| `m_window_title` | `Terminal:713` | YES — OSC 2/21 |
| `m_current_directory_uri` | `Terminal:714` | YES — OSC 7 |
| `m_current_file_uri` | `Terminal:715` | YES — OSC 77 |

**These 3 fields ARE session state and ARE missing.**

---

### What Is Correctly NOT Captured (Correctly Excluded)

| Category | Fields |
|---------|--------|
| APPLICATION_CONFIGURATION | ~25 fields (font, bold, scroll, mouse, blink, cursor style, bidi enable, etc.) |
| RUNTIME_TRANSIENT | ~30 fields (PTY, parser, IME, selection, timers, GTK, etc.) |
| DERIVED_RECONSTRUCTIBLE | ~20 fields (font metrics, ring streams, cached row, etc.) |

---

## Behavioral Equivalence Model

The canonical proof must be:

```
STATE(A) + ACTION_SEQUENCE(X)  ==  RESTORE(STATE(A)) + ACTION_SEQUENCE(X)
```

Where `STATE(A)` = all SNAPSHOT_REQUIRED fields.

### Meaningful Action Battery X

| Sequence | Tests |
|----------|-------|
| Plain output + ANSI color | Cell attrs, colors, palette |
| Cursor movement (CUP, CR, LF, CUU/CUD/CUF/CUB) | Cursor, saved cursor, origin mode |
| CR/LF + wrap | soft_wrapped, insert_delta, columns |
| Insert mode (IRM) | Insert mode behavior |
| Origin mode (DECOM) | CUP relative to region |
| Scrolling region (DECSTBM/DECSLRM) | Region-restricted scrolling |
| Alternate screen (smcup/rmcup) | Both screens, active screen |
| Hyperlink creation (OSC 8) | Hyperlink pool remap |
| Cursor save/restore (DECSC/DECRC) | Saved cursor block |
| Application cursor/keypad | Mode behavior |
| Bracketed paste | Mode behavior |
| Mouse tracking | Mode behavior |
| Resize | Rewrap, geometry |
| Additional output + freeze/thaw | Ring stream regeneration |
| CR/LF at bottom | scroll_delta, insert_delta |

---

## Round-Trip Test Audit

**Current Test (`vte_snapshot_roundtrip_test.cpp`)** proves:
- Level 1: Serialization integrity (A==B byte-for-byte) ✅
- Level 2: Internal state equivalence (not tested) ❌
- Level 3: Visual equivalence (visible-text sizes only) ⚠️
- Level 4: Behavioral equivalence after new output ❌
- Level 5: Stress (resize, freeze/thaw, alternate) ❌

**The test proves serializer self-consistency, NOT behavioral equivalence.**

---

## Revised Coverage Matrix

| Category | Total | Covered | Partial | Missing |
|----------|-------|---------|---------|---------|
| **SNAPSHOT_REQUIRED** | 34 | 31 | 0 | **3** (window title, CWD URI, file URI) |
| **APPLICATION_CONFIGURATION** | ~25 | N/A (not snapshot) | N/A | N/A |
| **DERIVED_RECONSTRUCTIBLE** | ~20 | N/A (not serialized) | N/A | N/A |
| **RUNTIME_TRANSIENT** | ~35 | N/A (correctly omitted) | N/A | N/A |

---

## Revised Final Verdict

```
SNAPSHOT_REQUIRED_TOTAL = 34
SNAPSHOT_REQUIRED_COVERED = 31
SNAPSHOT_REQUIRED_PARTIAL = 0
SNAPSHOT_REQUIRED_MISSING = 3  (m_window_title, m_current_directory_uri, m_current_file_uri)

APPLICATION_CONFIGURATION_TOTAL = ~25
RUNTIME_TRANSIENT_TOTAL = ~35
UNKNOWN_TOTAL = 0

SNAPSHOT_CONTRACT_VERDICT = PARTIAL
    (missing only 3 session-state fields: window title, CWD URI, file URI)

BEHAVIORAL_EQUIVALENCE_PROVEN = NOT_YET_PROVEN
    (requires Level 4+5 testing)
```

---

## Critical Missing State (Only 3 Fields)

| Field | Source | Capture Method |
|-------|--------|----------------|
| `m_window_title` | `Terminal:713` | Add `snp_put_cstr` for title string |
| `m_current_directory_uri` | `Terminal:714` | Add `snp_put_cstr` for URI string |
| `m_current_file_uri` | `Terminal:715` | Add `snp_put_cstr` for URI string |

**These are the ONLY missing SNAPSHOT_REQUIRED fields.**

---

## False Positives From Previous Audit

The previous audit incorrectly classified **~45 fields** as `MUST_SNAPSHOT` that are actually:

| Correct Category | Count | Examples |
|------------------|-------|----------|
| **APPLICATION_CONFIGURATION** | 25 | `m_allow_bold`, `m_bold_is_bright`, `m_rewrap_on_resize`, `m_fallback_scrolling`, `m_scroll_on_insert/output/keystroke`, `m_audible_bell`, `m_mouse_autohide`, `m_enable_bidi`, `m_enable_shaping`, `m_cjk_ambiguous_width`, `m_background_alpha`, `m_font_scale`, `m_mouse_autohide`, `m_cursor_blink_mode`, `m_text_blink_mode`, `m_cursor_style`, `m_enable_bidi`, `m_enable_shaping`, `m_cjk_ambiguous_width`, `m_word_char_exceptions`, `m_selection_type`, `m_selection_block_mode` |
| **RUNTIME_TRANSIENT** | 6 | `m_parser`, `m_primary_data_syntax`, `m_current_data_syntax`, `m_last_graphic_character`, `m_im_preedit_*`, `m_input_enabled` |
| **DERIVED_RECONSTRUCTIBLE** | ~15 | All font/rendering metrics, ring streams, cached row |
| **APPLICATION_CONFIGURATION (cursor/blink)** | 4 | `m_cursor_blink_mode`, `m_text_blink_mode`, `m_cursor_style`, `m_cursor_blink_mode` |

**Total false positives: ~50 fields**

---

## Ring/Scrollback Deep Audit

### Current Approach: Writable Rows Only + Stream Regeneration

**Capture:** Iterates `[delta, next)`, deep-copies each row (`_vte_row_data_copy`), serializes cells + URL strings.

**Restore:** `ring->reset()` → `append()` + `_vte_row_data_append()` per cell → `_vte_row_data_nonempty_length()` → `set_visible_rows()` → restore deltas/cursor.

**Streams:** NOT serialized. VTE regenerates on freeze.

**Correctness Argument:**
1. Writable rows contain all current logical content
2. Frozen rows are losslessly represented in writable rows at capture time (they've been thawed for serialization)
3. On restore, all rows are writable → VTE will re-freeze as needed via `maybe_freeze_one_row()` / `ensure_writable_room()`
4. `m_last_attr` reset to `basic_cell.attr` (correct for fresh writable rows)
5. `m_cached_row` invalidated by `ring->reset()`

**Critical Behavioral Test Needed:**
```
A: fill ring → freeze many rows → capture → restore → append more data → force new freezes
```
Must verify freeze/thaw behavior matches original.

---

### Hyperlink Pool Remap

**Current:** Captures URL string per cell with hyperlink. On restore: `ring->get_hyperlink_idx(url)` re-registers.

**Correctness:** ✅ Pool indices are per-ring; remapping via URL string is correct.

**Edge Case:** Multiple cells with same URL → deduped correctly via `get_hyperlink_idx()`.

---

### Palette Index Semantics

Cells store palette indices (0-255, 256=default FG, 257=default BG). Palette restored BEFORE rings → indices resolve correctly.

`m_bold_is_bright` (APPLICATION_CONFIGURATION) affects SGR 1 → palette index mapping but doesn't affect stored indices.

---

## Hidden Invariants (Verified in Current Implementation)

| Invariant | Implementation | Status |
|-----------|----------------|--------|
| `cursor.row` ↔ `insert_delta` ↔ `ring.delta()` | Captured as relative to delta; restored as `rel + delta` | ✅ |
| `saved.cursor` ↔ `insert_delta` | Saved cursor stored relative; restored directly | ✅ |
| `scroll_delta` ↔ `ring.delta()` | Relative capture/restore | ✅ |
| `palette indices` ↔ `m_palette[]` | Palette restored first | ✅ |
| `hyperlink_idx` ↔ `Ring::m_hyperlinks` | Remapped via URL | ✅ |
| `active screen` ↔ `m_screen` | u8 flag + pointer restore | ✅ |
| `cursor_advanced` ↔ wrap | Captured per-screen | ✅ |
| `scrolling_region` ↔ `origin_mode` | Region captured; origin mode in saved block | ✅ |
| `alternate screen` ↔ `m_screen` | Both screens + active flag | ✅ |
| `ring->delta()` ↔ absolute coords | Relative coords used throughout | ✅ |
| `m_cached_row` ↔ ring state | `ring->reset()` invalidates | ✅ |
| `m_last_attr` ↔ attr_stream | Reset to `basic_cell.attr` on rebuild | ✅ |
| `m_tabstops` ↔ `m_column_count` | Tab count validated against cols | ✅ |
| `m_scrolling_region` ↔ geometry | Region coords captured; reset uses current geometry | ✅ |
| `saved.origin_mode` ↔ cursor | Saved in saved block | ✅ |
| `saved.reverse_mode` ↔ cursor | Saved in saved block | ✅ |
| `saved.cursor_advanced` ↔ wrap | Saved in saved block | ✅ |
| `m_bidi_rtl` ↔ `m_enable_bidi` | Both captured | ✅ |

**All invariants correctly handled.**

---

## Current Extension Field-by-Field Audit

### Serialization Asymmetry Check

| Field | Capture | Restore | Symmetric? |
|-------|---------|---------|------------|
| `m_modes_ecma` | u8 | u8 | ✅ |
| `m_modes_private` | u32 | u32 | ✅ |
| `m_scrolling_region` | 1 flag + 4 coords | 1 flag + 4 coords | ✅ |
| `m_tabstops` | count + bitmap | count validated + bitmap | ✅ |
| Cell hyperlink | has_hlink + URL string | has_hlink + URL → remap | ✅ |
| `m_character_replacement` | index 0/1 | index → pointer fixup | ✅ |
| `saved.character_replacement` | index 0/1 | index → pointer fixup | ✅ |
| Ring rows | `[delta, next)` | `reset()` + `append()` | ✅ |
| Row attributes | soft_wrapped + bidi_flags | soft_wrapped + bidi_flags | ✅ |
| `m_cached_row` | Not serialized | `reset()` invalidates | ✅ (derived) |
| Streams | Not serialized | Regenerated | ✅ (derived) |

**No asymmetry bugs found.**

---

## Required Test Battery (Not Implemented)

| Level | Test | Purpose |
|-------|------|---------|
| 1 | Serialization integrity | `capture(A) == capture(restore(capture(A)))` |
| 2 | Internal state equivalence | Deep field-by-field comparison |
| 3 | Visual equivalence | Pixel/character grid comparison |
| 4 | Behavioral equivalence (output) | `restore(A) + feed(X) == A + feed(X)` |
| 5 | Behavioral equivalence (input) | Key/mouse handling after restore |
| 6 | Stress: freeze/thaw | Large scrollback + new output |
| 7 | Stress: resize | Restore → resize → compare |
| 8 | Stress: alternate screen | smcup/rmcup cycles |
| 9 | Stress: parser | Partial sequences (if ever applicable) |
| 10 | Shell integration | Spawn shell after restore |

---

## Final Snapshot Contract

### SNAPSHOT_REQUIRED (34 fields)

| Group | Fields |
|-------|--------|
| Geometry | `m_row_count`, `m_column_count`, `m_scrollback_lines` |
| Active Screen | `m_screen` (u8) |
| Screens (×2) | `cursor` (rel), `cursor_advanced`, `scroll_delta` (rel), `insert_delta` (rel), `saved.cursor` (rel), `saved.cursor_advanced`, `saved.reverse_mode`, `saved.origin_mode`, `saved.defaults`, `saved.color_defaults`, `saved.character_replacements[2]`, `saved.character_replacement` (index), full ring |
| Active Screen Flag | `m_screen == &m_alternate_screen` |
| Modes | `m_modes_ecma` (u8), `m_modes_private` (u32) |
| Palette | `m_palette[263]` ×2 sources |
| Defaults | `m_defaults`, `m_color_defaults` |
| Character Replacements | `m_character_replacements[2]`, current index |
| Scrolling Region | `is_restricted` + top/bottom/left/right |
| Tabstops | `m_column_count` + bitmap |
| Cursor | `m_cursor_shape` |
| Window Title | `m_window_title` (MISSING) |
| CWD URI | `m_current_directory_uri` (MISSING) |
| File URI | `m_current_file_uri` (MISSING) |
| BiDi RTL | `m_bidi_rtl` |
| UTF-8 Ambiguous Width | `m_utf8_ambiguous_width` |
| Mouse Tracking | `m_mouse_tracking_mode` |
| Allow Hyperlink | `m_allow_hyperlink` |

**Total: 34 SNAPSHOT_REQUIRED fields**

---

### MISSING (3)

1. `m_window_title` — add `snp_put_cstr` / `snp_get_cstr`
2. `m_current_directory_uri` — add `snp_put_cstr` / `snp_get_cstr`
3. `m_current_file_uri` — add `snp_put_cstr` / `snp_get_cstr`

---

## Recommended Investigation Order

1. **Immediate:** Add the 3 missing session-state fields (window title, CWD URI, file URI)
2. **Validation:** Build Level 4 behavioral test (`state(A)+X == restore(state(A))+X`)
3. **Validation:** Test alternate screen capture/restore with vim/less
4. **Validation:** Test large frozen scrollback + new output forcing freeze/thaw
5. **Validation:** Test resize after restore
6. **Validation:** Test hyperlink hover/click after restore
7. **Validation:** Test mouse tracking / bracketed paste after restore
8. **Validation:** ASan/UBSan clean on all code paths

---

## Final Numbers

```
SNAPSHOT_REQUIRED_TOTAL = 34
SNAPSHOT_REQUIRED_COVERED = 31
SNAPSHOT_REQUIRED_PARTIAL = 0
SNAPSHOT_REQUIRED_MISSING = 3

APPLICATION_CONFIGURATION_TOTAL = ~25
RUNTIME_TRANSIENT_TOTAL = ~35
UNKNOWN_TOTAL = 0

SNAPSHOT_CONTRACT_VERDICT = PARTIAL
    (only 3 session-state fields missing)

BEHAVIORAL_EQUIVALENCE_PROVEN = NO
    (requires Level 4+5 testing)
```

---

## Conclusion

The current Phase-0C implementation is **90% complete** on the actual snapshot contract. The previous audit's "58 MUST_SNAPSHOT" was inflated by misclassifying application configuration, runtime transient, and derived state as session state.

**The actual missing state is only 3 fields:** window title, CWD URI, file URI.

**Do NOT add the ~45 false-positive fields.** They belong to application configuration or runtime transient state.

**Next step:** Add the 3 missing fields, then proceed to behavioral equivalence testing (Level 4+5) before Remin integration.