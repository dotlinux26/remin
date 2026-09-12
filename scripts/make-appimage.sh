#!/usr/bin/env bash
# Build a portable AppImage of Remin.
#
# Usage:
#   scripts/make-appimage.sh [install_tree] [out_file]
#
#   install_tree  a staged install root whose usr/ layout is the final payload
#                 (usr/bin/remin, usr/share/remin/resources, usr/lib/remin/...).
#                 If omitted, a fresh tree is staged from the release build.
#   out_file      default: Remin-1.0.0-linux-x86_64.AppImage
#
# Requirements: a Release build in build-release/, the patched VTE build from
# scripts/build-vte.sh, curl, tar, and FUSE (or the --appimage-extract path).
#
# All downloaded tooling goes to $REMIN_APPIMAGE_CACHE (default ~/.cache/remin-appimage).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CACHE="${REMIN_APPIMAGE_CACHE:-$HOME/.cache/remin-appimage}"
mkdir -p "$CACHE"

VER="1.0.0"
ARCH="x86_64"
OUT="${2:-"$ROOT/Remin-$VER-linux-$ARCH.AppImage"}"
STAGE_DIR="${1:-}"

linuxdeploy="${CACHE}/linuxdeploy-x86_64.AppImage"
gtk_plugin="${CACHE}/linuxdeploy-plugin-gtk.sh"
appimagetool="${CACHE}/appimagetool-x86_64.AppImage"

download() { # url dest
  if [ ! -f "$2" ]; then
    echo "== downloading $1"
    curl -L --fail --retry 3 -o "$2.part" "$1"
    mv "$2.part" "$2"
    chmod +x "$2" 2>/dev/null || true
  fi
}

if [ ! -x "$linuxdeploy" ]; then
  download "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" "$linuxdeploy"
fi
if [ ! -x "$gtk_plugin" ]; then
  download "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh" "$gtk_plugin"
fi
if [ ! -x "$appimagetool" ]; then
  download "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage" "$appimagetool"
fi

# --- stage the payload -------------------------------------------------------
if [ -z "$STAGE_DIR" ]; then
  STAGE_DIR="${CACHE}/appdir-stage"
  rm -rf "$STAGE_DIR"
  mkdir -p "$STAGE_DIR"
  export PKG_CONFIG_PATH="$ROOT/vte-0.76.0-patched/build/meson-uninstalled"
  cmake -B "$ROOT/build-release" -DREMIN_RESOURCE_DIR='' -DCMAKE_INSTALL_RPATH='$ORIGIN/../lib/remin' >/dev/null
  cmake --build "$ROOT/build-release" -j"$(nproc)" >/dev/null
  cmake --install "$ROOT/build-release" --prefix "$STAGE_DIR/usr" >/dev/null
  # Private patched VTE runtime (never resolved from the system).
  mkdir -p "$STAGE_DIR/usr/lib/remin"
  for f in libvte-2.91-gtk4.so libvte-2.91-gtk4.so.0; do
    cp -d "$ROOT/vte-0.76.0-patched/build/src/$f" "$STAGE_DIR/usr/lib/remin/"
  done
  chmod 755 "$STAGE_DIR/usr/lib/remin/libvte-2.91-gtk4.so.0"
fi
[ -x "$STAGE_DIR/usr/bin/remin" ] || { echo "no $STAGE_DIR/usr/bin/remin"; exit 1; }

APP_DIR="${CACHE}/AppDir"
rm -rf "$APP_DIR"
mkdir -p "$APP_DIR"
cp -a "$STAGE_DIR"/usr "$APP_DIR/" 

# --- assemble ----------------------------------------------------------------
export QT_XCB_NO_MITSHM=1
# Extract-and-run avoids needing FUSE on the build host.
LDS="--appimage-extract-and-run" "$linuxdeploy" \
  --appdir "$APP_DIR" \
  --executable "$APP_DIR/usr/bin/remin" \
  --desktop-file "$APP_DIR/usr/share/applications/remin.desktop" \
  --icon-file "$APP_DIR/usr/share/icons/hicolor/scalable/apps/remin.svg" \
  --plugin gtk

# --- VTE identity enforcement ------------------------------------------------
# linuxdeploy/plugin-gtk drops a foreign libvte into usr/lib (from its bundled
# gtk.ext2) that is NOT our reproducible patched build. Replace it byte-for-byte
# with OUR patched build so the binary's \$ORIGIN/../lib resolution never hits a
# system/foreign VTE.
patched_vte="$ROOT/vte-0.76.0-patched/build/src/libvte-2.91-gtk4.so.0"
[ -f "$patched_vte" ] || { echo "patched VTE missing (run scripts/build-vte.sh)"; exit 1; }
install -m755 "$patched_vte" "$APP_DIR/usr/lib/libvte-2.91-gtk4.so.0"
rm -rf "$APP_DIR/usr/lib/remin"

# --- package (appimagetool directly; linuxdeploy must not touch the libs again)
rm -f "$OUT"
"$appimagetool" --appimage-extract-and-run "$APP_DIR" "$OUT" >/dev/null

# --- VTE gate (post-package) --------------------------------------------------
# The resolved copy under <payload>/usr/lib must be byte-for-byte our patched
# build — never a system/foreign libvte.
pvte="$ROOT/vte-0.76.0-patched/build/src/libvte-2.91-gtk4.so.0"
gate_dir="$(mktemp -d)"
( cd "$gate_dir" && "$OUT" --appimage-extract >/dev/null 2>&1 )
if cmp -s "$gate_dir/squashfs-root/usr/lib/libvte-2.91-gtk4.so.0" "$pvte"; then
  echo "== VTE gate OK: bundled libvte == patched build ($(stat -c%s "$pvte") bytes)"
else
  rm -rf "$gate_dir"
  echo "!! VTE gate FAILED: AppImage does not carry the patched VTE build" >&2
  exit 1
fi
rm -rf "$gate_dir"

echo "== wrote $OUT ($(du -h "$OUT" | cut -f1))"