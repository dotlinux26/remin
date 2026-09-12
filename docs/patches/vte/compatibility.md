# Compatibility

## Round-trip guarantees

The snapshot interface is proven to round-trip byte-for-byte:

- capture VTE A → store → restore into VTE B → recapture ⇒ identical bytes
  (verified end-to-end via the SQLite BLOB path).
- live scrollback round-trips **exactly** at the textual level:

| Property               | Verdict                                          |
|------------------------|--------------------------------------------------|
| textual scrollback     | EXACT round-trip                                 |
| semantic content       | preserved                                        |
| colors / formatting    | NOT preserved (documented, intentional)          |

`clear`, terminal control sequences, and screen-state operations do not touch
the snapshot; the snapshot is the canonical state at capture time.

## Consumers

The interface is exercised two ways:

1. **In-tree (Remin)** — `TerminalPane.runtime_capture()/runtime_restore()`
   and the DB blob path (`vte_snapshot_db_roundtrip_test`).
2. **External consumer** (`vte_snapshot_consumer_test`) — build + link against
   the **public** `<vte/vte.h>` API only. This proves the extension is usable
   by third parties without internal headers.

## Linking requirement

The **system** `/usr/lib` libvte may already carry snapshot symbols from earlier
experiments while the system headers lack the declarations. Always build/link
Remin against the patched build:

```bash
PKG_CONFIG_PATH=vte-0.76.0-patched/build/meson-uninstalled
LD_LIBRARY_PATH=vte-0.76.0-patched/build/src
```

Do **not** link the system libvte for snapshot builds.

## Known limitations (non-blocking)

- `snapshot_has_pending_data()` is a **stub** returning `TRUE` — no dirty
  tracking yet.
- Restore is **non-transactional**: a malformed snapshot aborts with an error
  rather than rolling back a half-applied state; capture is exact and restore
  validates magic/version/counts up front.
- No upper bounds enforced on `rows`/`cols` in the deserializer.
- Uninitialized `magic`/`pal_count` can be read in the `g_printerr()` error
  path (harmless in practice, worth cleaning up).
- `cwd`, file-URI, and window title are **not** part of the format yet;
  Remin keeps those fields separately in workspace state.

## Supported versions

| VTE        | Status          |
|------------|-----------------|
| 0.76.0     | active series   |
| newer      | [rebase guide](upstream-delta.md) |