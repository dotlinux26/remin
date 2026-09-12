# Testing

The snapshot extension is held to **reproducible, byte-for-byte** gates. Every
claim in this documentation is backed by a check that either runs in CI or is
reproducible from a clean checkout.

## 1. Round-trip fidelity

Live VTE flow, end to end:

```text
VTE A  →  capture  →  ANSI/control  →  capture  →  new VTE  →  feed  →  compare
```

- capture → restore → recapture must be **byte-for-byte identical**
- textual scrollback round-trip is **EXACT**
- color/format is NOT preserved (recorded, intentional)

Reference test: `vte_snapshot_db_roundtrip_test` (14/14 PASS) — VTE A →
capture → SqliteStorage BLOB store/load → restore VTE B → recapture equals A
byte-for-byte.

## 2. Behavioral tests

Snapshot/restore behavioral checks re-run during a version rebase: state-level
continuity (see [restoration-semantics.md](restoration-semantics.md)) after
restore + supported post-restore actions.

## 3. External consumer

`vte_snapshot_consumer_test` compiles and links against the **public**
`<vte/vte.h>` only (no internal headers). It proves the extension is usable by
third-party consumers, not just Remin.

## 4. Reproducibility gates

`scripts/build-vte.sh` reproduces the whole chain from a pristine checkout and
verifies the invariant:

```text
pristine 0.76.0 + patch series  ==  working patched tree   (byte-for-byte)
```

`VTE_MANIFEST` pins `vte_version=0.76.0`, `patch_series=1`, and the SHA256
hashes of all six series files, so the exact inputs are locked.

Negative and regression proofs already recorded:

- pristine rebuild ⇒ `undefined symbol` (proves the series really adds the API)
- revert → reapply → rebuild → rerun ⇒ PASS (series is self-contained)
- clean-room build: 171 targets, tests 0C/0E all PASS

## 5. Remin's own suite

All 5 Remin ctest suites must pass. The workspace persistence pipeline adds
golden acceptance checks (window identity, checkpoint generations,
restart-no-window-growth) captured in
`tests/golden/GOLDEN_ACCEPTANCE_CHECKLIST.md`.