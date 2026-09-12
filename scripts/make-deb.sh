#!/usr/bin/env bash
# Build the Debian package for Remin from the release build tree.
#
# Usage: scripts/make-deb.sh [out_dir]
#   out_dir  default: repository root (remin_1.0.0_amd64.deb written there)
#
# Uses fakeroot + dpkg-deb. Depends on:
#   - build-release/ (Release build configured with -DREMIN_RESOURCE_DIR=''
#     and -DCMAKE_INSTALL_RPATH='$ORIGIN/../lib/remin')
#   - vte-0.76.0-patched/ from scripts/build-vte.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${1:-$ROOT}"
VER="1.0.0"
ARCH="amd64"
STAGE="$(mktemp -d)"; trap 'rm -rf "$STAGE"' EXIT
ROOTFS="$STAGE/root"

# --- stage the payload -------------------------------------------------------
cmake --install "$ROOT/build-release" --prefix "$ROOTFS/usr" >/dev/null

mkdir -p "$ROOTFS/usr/lib/remin" "$ROOTFS/DEBIAN" "$ROOTFS/usr/share/doc/remin"
for f in libvte-2.91-gtk4.so libvte-2.91-gtk4.so.0; do
  cp -d "$ROOT/vte-0.76.0-patched/build/src/$f" "$ROOTFS/usr/lib/remin/"
done

cat > "$ROOTFS/DEBIAN/control" <<CONTROL
Package: remin
Version: $VER
Section: utils
Priority: optional
Architecture: $ARCH
Depends: libgtkmm-4.0-0, libadwaita-1-0, libgtk-4-1, libgtksourceview-5-0, libglibmm-2.68-1t64, libglib2.0-0t64, libcairo2, libcairomm-1.16-1, libpangomm-2.48-1t64, libpango-1.0-0, libgdk-pixbuf-2.0-0, libsigc++-3.0-0, libssl3t64, libgnutls30t64, libicu74, libpcre2-8-0, liblz4-1, libsystemd0, libfribidi0, libcairo-gobject2, libpangocairo-1.0-0, hicolor-icon-theme
Maintainer: Remin project <maintainers@remin.dev>
Homepage: https://github.com/dotlinux26/remin
Description: Terminal workspace with persistent state
 Remin is a workspace platform for CLI and desktop-oriented tools. It
 gathers terminal, notes and file browsing into a single window whose
 state - tabs, panes, scrollback, working directories - is saved
 continuously and restored on the next launch, so a working session
 survives a reboot or a crash.
CONTROL

cat > "$ROOTFS/usr/share/doc/remin/copyright" <<'EOF'
Remin is released under the terms of its project license.
Upstream: https://github.com/dotlinux26/remin
EOF

find "$ROOTFS" -type d -exec chmod 755 {} +
chmod 755 "$ROOTFS/usr/bin/remin" "$ROOTFS/usr/lib/remin/libvte-2.91-gtk4.so.0"
find "$ROOTFS/usr/share" -type f -exec chmod 644 {} +

# --- post-condition checks before packing ------------------------------------
readelf -d "$ROOTFS/usr/bin/remin" | grep -q 'RUNPATH.*$ORIGIN/../lib/remin' \
  || { echo "!! bad RUNPATH"; exit 1; }
strings "$ROOTFS/usr/bin/remin" | grep -q "/home/" && { echo "!! build-path leak"; exit 1; } || true
cmp -s "$ROOTFS/usr/lib/remin/libvte-2.91-gtk4.so.0" \
  "$ROOT/vte-0.76.0-patched/build/src/libvte-2.91-gtk4.so.0" \
  || { echo "!! patched VTE mismatch"; exit 1; }

# --- pack --------------------------------------------------------------------
mkdir -p "$OUT_DIR"
rm -f "$OUT_DIR/remin_${VER}_$ARCH.deb"
fakeroot dpkg-deb --build "$ROOTFS" "$OUT_DIR/remin_${VER}_$ARCH.deb"
echo "== wrote $OUT_DIR/remin_${VER}_$ARCH.deb"