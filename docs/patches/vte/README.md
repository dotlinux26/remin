# Remin VTE Extension

Remin requires persistence of terminal emulator state across application
restarts.

Instead of replacing VTE's terminal engine or introducing a PTY proxy, Remin
extends VTE with a minimal snapshot/restore interface:

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

## The problem

VTE 0.76 ships a rich public API for *operating* a terminal, but **no public
snapshot/restore API for the full internal terminal state**. Most terminal
apps that want "state" across restarts have to replay output text, re-feed PTY
traffic, or roll their own emulator. The Remin snapshot interface fills exactly
that gap.

Remin does **not**:

- implement its own terminal emulator
- proxy PTY traffic
- replay terminal output
- reconstruct terminal state from visible text
- replace VTE's parser/rendering engine

## The patch series

`patches/vte-0.76.0/` is a small, versioned, self-contained patch series against
pristine VTE 0.76.0:

```text
patches/
└── vte-0.76.0/
    ├── 0001-vte-0.76.0-public-snapshot-api.patch
    ├── 0002-vte-0.76.0-internal-snapshot-declarations.patch
    ├── 0003-vte-0.76.0-snapshot-serialization-and-restore.patch
    ├── 0004-vte-0.76.0-public-snapshot-wrappers.patch
    ├── SERIES.md
    └── SHA256SUMS
```

The series is kept **separate from the Remin application tree** on purpose so
the integration can be inspected, reproduced, and reused independently.
Reproducibility is byte-for-byte: pristine 0.76.0 + the series == the working
patched tree, pinned by `SHA256SUMS` and rebuilt end-to-end by
`scripts/build-vte.sh` (see [testing](testing.md)).

## Where to go next

- [Architecture](architecture.md) — how the interface fits into VTE
- [Snapshot format](snapshot-format.md) — the logical state that is preserved
- [Restoration semantics](restoration-semantics.md) — what “restore” means
- [Integration](integration.md) — how Remin drives capture/restore
- [Upstream delta](upstream-delta.md) — rebasing onto a newer VTE
- [Compatibility](compatibility.md) — round-trips, consumers, caveats
- [Testing](testing.md) — fidelity and reproducibility gates

## Who may find this useful?

This patch series may be useful for terminal applications that need to preserve
terminal emulator state across process restarts without:

- replaying terminal output
- recording and re-feeding PTY traffic
- maintaining a custom terminal emulator
- depending on terminal-specific text reconstruction

Remin is the reference consumer of this interface, but the patch series is kept
separate so the approach can be studied and reused independently.