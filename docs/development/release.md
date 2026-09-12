# Release Checklist

Follow this in order before shipping a release. The full hardening plan that
this checklist operationalizes lives in
[`docs/release-hardening-plan.md`](../release-hardening-plan.md); the linking
and bundling rules for every artifact live in
[`docs/runtime-linking-policy.md`](../runtime-linking-policy.md).

```text
1. Docs cleanup      — restructure goes live (usage/ architecture/ design/
                        patches/ development/ packaging/)
2. VTE docs          — the VTE series becomes a documented technical story
3. README landing    — "Why Remin", Technical Highlights, install links
4. Install docs      — AppImage + .deb + source build documented
5. Desktop assets    — .desktop, AppStream metainfo, icons
6. Debian package    — control/rules/scripts produce remin_<ver>.deb
7. AppImage           — portable build produced and smoke-tested
8. CI                — ci + test + release workflows on GitHub
9. RELEASE notes + SHA256SUMS for all artifacts
10. Tag + publish    — vX.Y.Z tag; attach assets to GitHub Release
```

## Hard gate before tagging

- All 5 ctest suites pass (pre-existing failures documented in
  [testing.md](testing.md)).
- Build is clean (no new warnings introduced; sign-conversion warnings tracked).
- AppImage runs `remin gui` on a fresh machine, restores workspace across
  restart.
- `.deb` installs on a clean container and launches via the desktop entry.
- Generated artifacts are reproducible from a tagged checkout.
- **VTE invariant gate** (policy §8): every produced artifact links the
  Remin-patched VTE — verified with `ldd`/`readelf`/`strings` plus a runtime
  snapshot-symbol probe; RUNPATH is relative (`$ORIGIN/...`), never an
  absolute build path, and no artifact depends on a system `libvte`.

## Release assets

```text
Remin-1.0.0-linux-x86_64.AppImage
remin_1.0.0_amd64.deb
remin-1.0.0.tar.gz
SHA256SUMS
```

## Version bumping

App version lives in `CMakeLists.txt`. Keep the patch series manifest
(`patches/vte-0.76.0/SHA256SUMS`, `VTE_MANIFEST`) pinned separately; a change to
VTE trackers is a coordinate bump, not a Remin version event.