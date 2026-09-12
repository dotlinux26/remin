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

- All 5 ctest suites pass.
- Build is clean (no new warnings introduced; sign-conversion warnings tracked).
- AppImage runs `remin gui` on a fresh machine, restores workspace across
  restart.
- `.deb` installs on a clean container and launches via the desktop entry.
- Generated artifacts are reproducible from a tagged checkout.
- **VTE invariant gate** (policy §8): every produced artifact links the
  Remin-patched VTE — verified with `ldd`/`readelf`/`strings` plus a runtime
  snapshot-symbol probe; RUNPATH is relative (`$ORIGIN/...`), never an
  absolute build path, and no artifact depends on a system `libvte`.
- **Resource dir gate**: release binaries are built with
  `-DREMIN_RESOURCE_DIR=''` so resources resolve at runtime via
  `<exe_dir>/../share/remin/resources` (`src/gui/resources.cpp`); a packaged
  binary must boot with zero "failed to load icons/theme" warnings from outside
  the install tree.

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

## Reproducible artifact pipeline (as performed for 1.0.0)

Prerequisites: `scripts/build-vte.sh` has produced `vte-0.76.0-patched/` and the
Release build tree is configured once:

```bash
export PKG_CONFIG_PATH="$PWD/vte-0.76.0-patched/build/meson-uninstalled"
cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DREMIN_RESOURCE_DIR='' \
  -DCMAKE_INSTALL_RPATH='$ORIGIN/../lib/remin' \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build-release -j"$(nproc)"
ctest --test-dir build-release   # 22/22 must pass (needs $LD_LIBRARY_PATH patched VTE)
```

### .deb (via `scripts/make-deb.sh`)

Stage the payload and force-pack with fakeroot:

```bash
STAGE="$(mktemp -d)"; STAGE="$STAGE/root"
cmake --install build-release --prefix "$STAGE/usr"
mkdir -p "$STAGE/usr/lib/remin"
cp -d vte-0.76.0-patched/build/src/libvte-2.91-gtk4.so{,.0} "$STAGE/usr/lib/remin/"
# chmod 755 the .so, 644 the share files, 755 the binary
# write DEBIAN/control (Depends: see the 1.0.0 control stanza), DEBIAN conffiles
fakeroot dpkg-deb --build "$STAGE" remin_1.0.0_amd64.deb
```

Byte-for-byte checks after the build:

```bash
readelf -d stage/usr/bin/remin            # RUNPATH $ORIGIN/../lib/remin, NOT /home/...
ldd stage/usr/bin/remin                   # resolves libvte from stage/usr/lib/remin
cmp -s stage/usr/lib/remin/libvte-2.91-gtk4.so.0 vte-0.76.0-patched/build/src/libvte-2.91-gtk4.so.0
strings stage/usr/bin/remin | grep -c "/home/"   # must be 0
dpkg-deb -c remin_1.0.0_amd64.deb         # tree + perms
dpkg-deb -I remin_1.0.0_amd64.deb         # control/stanza
appstreamcli validate <metainfo> && desktop-file-validate <desktop>
```

Smoke: run the staged binary from its installed layout with a scratch
`$XDG_DATA_HOME` on a display; expect zero resource warnings and a
`terminal_snapshots` BLOB + checkpoint row after exit.

### AppImage (via `scripts/make-appimage.sh`)

The script pins linuxdeploy (`continuous`, upstream AppImage releases) and
`linuxdeploy-plugin-gtk` (`master`), assembles an AppDir, **enforces VTE
identity by overwriting any foreign `usr/lib/libvte-*.so.0` with the patched
build**, packages with appimagetool, then runs its own VTE gate (extracts the
payload and `cmp`s the bundled lib to `vte-0.76.0-patched`).

```bash
scripts/make-appimage.sh            # -> Remin-1.0.0-linux-x86_64.AppImage
```

Tools are cached in `~/.cache/remin-appimage`; record their hashes in the
release notes:

```text
linuxdeploy-x86_64.AppImage  (continuous, 2026-09-12)
linuxdeploy-plugin-gtk.sh    (master,    2026-09-12)
appimagetool-x86_64.AppImage a6d71e2b6cd66f8e8d16c37ad164658985e0cf5fcaa950c90a482890cb9d13e0
```

Smoke: `./Remin-1.0.0-linux-x86_64.AppImage --appimage-extract-and-run gui`
on a scratch `$XDG_DATA_HOME` — expect zero resource warnings and a persisted
VTE snapshot BLOB on exit.

### Final audit + STOP condition

Produce `SHA256SUMS` for the existing artifacts only (no signatures yet —
signing is out of scope until credentials exist). Print the audit table
(artifact / size / sha256 / VTE gate / resource gate / smoke). **Do not tag
`v1.0.0`, do not push tags, and do not create a GitHub Release until the audit
gates pass.** The release work (version bump, packaging scripts, docs) is
committed on `main`; only the tag/Release step waits.

### macOS (documented pipeline only — no macOS host available)

Build with the Homebrew SDK (arm64 first; x86_64 later; `universal2` when both
toolchains are available) and package a dylib-bundled `.app`; sign with a
Developer ID application certificate, enable the hardened runtime, and
notarize. There is no macOS builder in the 1.0.0 environment, so this step is
deferred.