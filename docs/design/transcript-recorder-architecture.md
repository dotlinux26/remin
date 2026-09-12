# Transcript Recorder - Architecture Investigation (Phase D: D0 + D1 gate)

Status: D0 COMPLETE | D1 DECISION PENDING (2026-09-06)
Authoritative: `docs/design/history-system-spec.md` section3-section14 + `docs/problem-terminal-transcript-capture.md` (P0-B).

> Objective: add a **Remin-owned TerminalTranscriptRecorder** (output history,
> independent of the VTE current screen/scrollback, survives `clear`) **without**
> reimplementing a PTY (D8 out-of-scope) and **without** replacing `vte_terminal_spawn_async`.
> Per user request: do NOT implement the recorder (D2) until D0/D1 are understood.
> If there is no safe tee point within the current integration -> **STOP and report
> the exact constraint**.

---

## 1. D0 - PTY/VTE OUTPUT OWNERSHIP (verified VTE 0.76/0.78 source)

| Question | Conclusion |
|---------|----------|
| Who owns the master PTY fd? | `VtePty` (`vte::base::Pty::m_pty_fd`), opened with `posix_openpt(..., O_NONBLOCK|O_CLOEXEC)` + **TIOCPKT packet mode**. `vte_pty_get_fd()` returns **that same fd** (borrowed, no dup, must not be closed or have flags changed). `vte_terminal_get_pty()` returns the same pty object. |
| Who reads the master fd? | **VTE is always the sole reader.** After `vte_terminal_spawn_async` -> callback `vte_terminal_set_pty()` -> `connect_pty_read()` -> `g_unix_fd_add_full(...)` on the master (G_IO_IN/PRI/HUP/ERR) -> `pty_io_read()` loops `read()` until EAGAIN, pushes into `m_incoming_queue` -> parser -> screen. No output path bypasses this reader. |
| Does VTE have a safe output hook? | **NO.** In GTK4 there is NO signal carrying raw output bytes. `contents-changed` = notification **without payload**, coalesced (idle `emit_pending_signals`), for a11y. `text-inserted/text-scrolled/text-modified` **exist only in GTK3** (`#if _VTE_GTK == 3`) and are not emitted. No `output/received/data`/`log/tee/record` API. |
| Is `get_pty()+get_fd()` plus a separate GIO watch safe? | **NO - illegal dual-reader race.** `get_fd()` returns the exact fd/open-file-description VTE already watches. Reading the master (O_NONBLOCK + TIOCPKT) **consumes bytes** (no peek); two sources in the same main loop contend for `read()`, whichever reads first steals the other's bytes -> corrupts both screen and recorder. This is exactly the race the design forbids. |

### 1a. Where do raw bytes exist?

Only **one** place after reading: `m_incoming_queue`, consumed by `pty_io_read()`
-> parser -> screen buffer. There is no callback/emitter between read and parse. Hence
**no raw output bytes exist outside the VTE parser** in the public 0.76/0.78 API.
Remin's earlier capture used `vte_terminal_get_text_range_format()` = post-parse
snapshot (already decoded, loses escape codes/format, loses content cleared by `clear`) - this is
the root cause of P0-B.

---

## 2. D1 - DECISION GATE

### Requirements from the directive (D0/D1/D8)

The recorder must sit at a **single-reader** tee point around the current integration
(keep `Shell -> OS PTY -> VTE -> TerminalPane`), with no two readers sharing the master.

### D1 conclusion

**There is NO safe tee point while keeping `vte_terminal_spawn_async` unchanged (VTE is the
single reader) and having no output-bytes hook.** The only race-free way to get bytes is to
"become the reader ourselves":

```
create our own VtePty (vte_pty_new_sync / vte_pty_spawn_async - NOT terminal's spawn_async)
+ DO NOT call vte_terminal_set_pty()    (if called -> VTE installs its own reader = dual-reader)
+ own GIO watch on vte_pty_get_fd() -> read (handle TIOCPKT) -> vte_terminal_feed(bytes)
```

BUT that direction **loses all the convenience of VTE's spawn path**:
- must do `vte_pty_set_size` on resize yourself (terminal cannot push size if pty is not attached).
- `vte_terminal_watch_child` **hard-requires** the pty to be set on the terminal (`src/vtegtk.cc:4883`), so you cannot reap the child via the standard API.
- must manage spawn/process-group/EOF/HUP yourself, like a reimplementation.

That is exactly **D8 out-of-scope** (custom PTY lifecycle / process-group / SIGWINCH /
replacing the spawn path) - forbidden by the user.

### Gate: STOP (do not code D2 now)

> Perm D1: "If a safe output tee can be established around the existing VTE
> integration -> implement. If **not** -> **STOP and report the exact architectural
> constraint**."

Per the investigation: **a tee cannot be established around the current integration without violating D8.**
-> Report the constraint. Await decision.

---

## 3. Options (submitted to user)

### (a) Snapshot-based transcript (keep VTE spawn; NO PTY rewrite)

- Capture on `contents-changed` events (no-payload, coalesced, runs on the main
  context - non-blocking) -> call `vte_terminal_get_text_range_format()` and **diff** it
  against the previous one to append only the new output to the transcript; record a `clear`
  marker when the scrollback shrinks/is erased.
- **Pros**: zero PTY risk, keeps `spawn_async`, fully D8-compliant.
- **Cons**: still post-parse (not raw bytes); depends on the VTE screen - this is
  EXACTLY what spec section4 forbids ("must not depend on the VTE current screen") and risks
  reproducing P0-B (lossy decode capture). NOT recommended because it violates the spec core.

### (b) "Own reader -> feed" (the only valid single-reader tee, but exceeds D8)

- As in section2: own VtePty + own reader + `vte_terminal_feed`. Gives clean raw bytes, survives
  `clear`, also resolves P0-B.
- **Cons**: exactly what D8 lists as out-of-scope (PTY lifecycle, watch-child,
  resize, SIGWINCH, EOF/HUP, process-group). Requires **explicit approval** to exceed D8.

### (c) Defer Phase D, do E/F/G first

- Window History + wiring the 3 modes don't depend on the transcript. Return to D after
  E/F/G are done (a new decision may emerge).

---

## 4. Transcript data model (not implemented until a direction is chosen)

Reference (applies whether (a) or (b) is chosen) - spec section3/section15/section7, kept separate:

```text
Per-pane (isolation D5):
  Pane -> TranscriptChunk[] { seq, timestamp_us, kind (output|clear|interrupt), data }

Aggregation only at query/UI:
  Workspace -> Window -> Tab -> Pane -> Transcript[]
```

- Buffering (D4): PTY bytes -> in-memory chunk buffer (e.g. ~64KB) -> flush via
  checkpoint/session pipeline -> SQLite dedicated table (`transcripts_*`), **NOT**
  INSERT per byte, NOT stuffed into `scrollbacks`/`settings` generic store (spec section15/section14).
- Clear semantics (D3): `clear` = screen-state op -> current screen empty, transcript
  still retains: `ls | A | B | clear-event | pwd | /home/user`.
- Restore (D6/D7): restore **current screen + shell context** via `runtime_restore`
  (already exists); transcript is only for **reviewing** history, NOT replayed onto VTE.
- Naming (D2): `TerminalTranscriptRecorder`, **not called** ScrollbackRecorder.

---

## 5. Blocked (blocks D2)

- User decision on direction (a)/(b)/(c).
- If (b): approval to exceed D8 + design of a thin PTY adapter.

---

## 6. Known limitations (from the actual investigation)

- VTE 0.76/0.78 GTK4 **does not expose raw output bytes**; no log/tee/record hook.
- `contents-changed` has no payload and is coalesced -> usable only as a "snapshot trigger", not a byte stream.
- `text-*` signals (GTK3-only) do not exist in GTK4.
- Reading the shared master fd = race that corrupts the screen; absolutely forbidden.