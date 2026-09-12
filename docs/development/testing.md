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

## Test status (v1.0.0)

As of the 1.0.0 release the full suite is green: **22/22 ctest suites pass**
(`build-release`), including:

- `vte_critical_validation_test` — verified spawning under the release
  environment (previously failed under CI).
- `markdown_document_test` — asset assertions updated to the shared
  `~/remin-image/` store + `remin://images/` reference model (the old test
  asserted the superseded per-document `assets/asset-*.png` scheme).

The `markdown_document_test` fix is a test-only change; the Markdown engine
behavior was not altered.