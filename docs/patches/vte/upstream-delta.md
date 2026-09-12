# Upstream Delta

Remin currently maintains the snapshot patch series against **VTE 0.76.0**.

The series intentionally touches only the components required for terminal
snapshot/restore — no parser, rendering, or input behavior is altered. This
keeps the rebase surface small when upstream moves.

## When rebasing onto a newer VTE version

```text
1. inspect upstream terminal state structures
2. re-evaluate serialized fields
3. re-run snapshot/restore behavioral tests
4. regenerate patch series
5. update SHA256SUMS
6. update the target version directory
```

### 1. Inspect upstream state structures

State structures move around between VTE releases (`VteScreen`, `Ring`,
`VteRowData`, `VteCell`, the mode containers). Start by diffing the members the
serializer reads against the target version's structure layout, then adjust the
read/write functions in `0003` accordingly.

### 2. Re-evaluate serialized fields

Each field in [snapshot-format.md](snapshot-format.md) should be re-checked:
does the field still exist upstream? Has its meaning changed? Are there new
state fields that matter for behavioral continuity (for example, new terminal
modes, new representation of row attributes)?

### 3. Re-run snapshot/restore behavioral tests

The [testing gates](testing.md) are the source of truth:

- byte-for-byte capture → restore → recapture
- textual/semantic round-trip of live scrollback
- external-consumer build against public headers only

### 4. Regenerate patch series

Re-derive the four patches cleanly from the pristine target tree so the series
stays self-contained, then verify `pristine + series`sanity the same way the
original was verified.

### 5. Update SHA256SUMS

`SHA256SUMS` pins every file of `patches/vte-<version>/`; `VTE_MANIFEST`
pins the same series. Both must be regenerated.

### 6. Update the target version directory

Add the new version alongside the old:

```text
patches/
├── vte-0.76.0/
├── vte-0.78.x/
└── …
```

Keeping historic series around makes upgrades reversible and gives consumers a
per-version contract. Update `docs/patches/vte/README.md` and the manifest
pins to point at the new series as the active one.