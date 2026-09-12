# Testing

All suites run through CTest.

```bash
cmake --build build -j
cd build && ctest --output-on-failure
```

## Suites

Five ctest suites cover, roughly:

- **core** — workspace model, pane tree, history, persistence policy, autosave,
  workspace lock
- **storage** — SQLite, serialization round-trips
- **terminal** — cwd detection, VTE snapshot round-trips
- **markdown** — parser/AST, document engine, HTML, layout
- **golden** — window identity & persistence acceptance
  (`tests/golden/GOLDEN_ACCEPTANCE_CHECKLIST.md`)

## Snapshot-specific tests

- `vte_snapshot_roundtrip_test` — capture → restore → recapture byte-fidelity
- `vte_snapshot_db_roundtrip_test` — the SQLite BLOB path, end to end
- `vte_snapshot_consumer_test` — builds against public `<vte/vte.h>` only
- `fidelity_gate_test` — textual/semantic scrollback round-trip

See [VTE testing](../patches/vte/testing.md) for the full background.

## Known pre-existing failures (out of scope)

Current builds have two pre-existing failures unrelated to feature code:

- `vte_critical_validation_test` — fails to spawn under the test environment
  (permission denied)
- `markdown_document_test` — 4 asset-number/file-name assertions

These predate the v1.0.0rc work and are tracked separately.