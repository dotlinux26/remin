# Integration — how Remin drives the snapshot

Remin is the reference consumer of the VTE extension. Every terminal pane in
the workspace persists through the snapshot, both on autosave and on shutdown,
and is rebuilt on the next launch.

## Capture side

`TerminalPane` calls `vte_terminal_snapshot_capture()` and stores the resulting
`GBytes` as `std::vector<uint8_t> snapshot_data` on the pane.

The snapshot bytes are persisted in **two** places, with the database as the
authoritative store:

```text
SQLite terminal_snapshots(pane_id, content BLOB, version, updated_at)
                        ↕ mirror
JSON workspace state   snapshot_data (base64 inline)
```

## Restore side

Restore of a pane follows a strict order in `TerminalPane`:

1. **Configure the size** — rows/cols come from the workspace state.
2. **`vte_terminal_snapshot_restore(snapshot_data)`** — rebuild terminal state.
3. **CR-LF display feed** — `vte_terminal_feed("\r\n")`, display-only, never
   written to the child. This moves the freshly spawned shell's prompt onto a
   new line so the restored prompt and the new prompt don't glue together.
4. **Spawn the shell once** — the child process starts inside the restored
   terminal.

## Per-pane state kept alongside

The pane snapshot is stored **with the pane identity**, so the workspace can
pair each restored snapshot with its canonical command history, working
directory, and split geometry.

## Reference pointers

- `src/gui/terminal/terminal_pane.cpp` — `runtime_capture()` /
  `runtime_restore()`
- `src/storage/sqlite_storage.cpp` — `terminal_snapshots(pane_id, content BLOB,
  version=1, updated_at)` + `sqlite3_bind_blob`
- `tests/unit/vte_snapshot_db_roundtrip_test.cpp` — blob round-trip proof:
  VTE A → capture → BLOB store/load → restore VTE B → recapture byte-for-byte
  identical

The workspace persistence pipeline that consumes this is described in
[`docs/design/workspace-persistence-pipeline.md`](../design/workspace-persistence-pipeline.md).