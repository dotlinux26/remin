# VTE 0.76.0 — Remin Terminal Snapshot Patch Series

> Single-purpose, reproducible patch series that adds a terminal snapshot
> capture/restore API to upstream VTE 0.76.0.
>
> Invariant: `pristine VTE 0.76.0 + this series == exact working patched VTE tree`
> (verified byte-for-byte, see `docs/vte-patch-packaging-report.md`).

## Provenance

- **Pristine source**: official upstream tarball `vte-0.76.0.tar.gz`
  - SHA256 `2275d5958d89ca1a93488e066ee33987557db36f560a5dfd4101b1a2d4f150a4`
- **Upstream project**: GNOME VTE, release tag `0.76.0`
  (no local git history; provenance anchored to the tarball hash)
- **Base**: pristine 0.76.0 unmodified

## Application order

```sh
git apply --check 0001-... && git apply 0001-...
git apply --check 0002-... && git apply 0002-...
git apply --check 0003-... && git apply 0003-...
git apply --check 0004-... && git apply 0004-...
```

Alternatively `git apply --check 000[1-4]-... && git apply 000[1-4]-...` (order
is guaranteed by left-to-right filename sorting; patches are non-overlapping).

The last patch emits one whitespace warning (`new blank line at EOF`) — cosmetic
only, matches the working tree exactly.

## The series

| # | Patch file | File modified | Type | Notes |
|---|-----------|---------------|------|-------|
| 0001 | `0001-vte-0.76.0-public-snapshot-api.patch` | `src/vte/vteterminal.h` | REQUIRED | Public API declarations (`vte_terminal_snapshot_capture`, `vte_terminal_snapshot_restore`, `vte_terminal_snapshot_has_pending_data`) |
| 0002 | `0002-vte-0.76.0-internal-snapshot-declarations.patch` | `src/vteinternal.hh` | REQUIRED / DEBUG | Internal `Terminal` declarations; `snapshot_debug()` is a debug-only helper |
| 0003 | `0003-vte-0.76.0-snapshot-serialization-and-restore.patch` | `src/vte.cc` | REQUIRED / DEBUG / STUB / DEAD | Full snapshot implementation (`+632` lines: byte serializer + helpers, capture, restore, debug dump, pending stub). See report §6.3 |
| 0004 | `0004-vte-0.76.0-public-snapshot-wrappers.patch` | `src/vtegtk.cc` | REQUIRED | Public C wrappers with `try/catch` guard |

Layout constraints respected: NO GIR/VALA/other generated or binding sources
are modified; build-only artifacts never enter the patch; the series contains
precisely the 4 source files that differ from pristine.

## Build & verify (clean room)

```sh
tar xzf vte-0.76.0.tar.gz -C /tmp/opencode/vte-pkg
cd /tmp/opencode/vte-pkg/vte-0.76.0
git init -q -b main && git add -A
git -c user.email=pkg@local -c user.name="P0-F Packaging" commit -qm "pristine VTE 0.76.0 upstream tarball"
# apply the 4 patches in order
meson setup build-snap --buildtype=release --default-library=shared --prefix=/usr \
  -Dwarning_level=0 -Dgtk3=true -Dgtk4=true -Dgir=true -Dvapi=true -Da11y=true \
  -Dglade=true -Dgnutls=true -Dicu=true -Dfribidi=true -D_systemd=true \
  -Ddocs=false -Ddbg=false
ninja -C build-snap
```

Then compile/run the companion Remin tests (not part of this package) against
`build-snap/src` — see `docs/vte-patch-packaging-report.md` §9.

## Integrity

`SHA256SUMS` in this directory pins each patch. Verify:

```sh
sha256sum -c SHA256SUMS
```