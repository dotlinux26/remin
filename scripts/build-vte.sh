#!/bin/bash
# Build the clean-room patched VTE dependency for Remin.
# Reproduces: pristine VTE 0.76.0 + patches/vte-0.76.0/*.patch -> libvte with
# the Remin snapshot API. Out-of-tree, build-only (no system install).
#
# Usage:
#   ./scripts/build-vte.sh             # build into vte-0.76.0-patched/
#
# The script validates every reproducibility gate and prints a manifest
# summary. It NEVER installs into the system; Remin must consume the artifacts
# via PKG_CONFIG_PATH / LD_LIBRARY_PATH (see docs/vte-p0g-reproducibility.md).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"
TARBALL="$ROOT/third_party/vte/vte-0.76.0.tar.gz"
PATCHES="$ROOT/patches/vte-0.76.0"
PRIS_TREEDIR="$ROOT/vte-0.76.0-pristine"
PATCHED_TREEDIR="$ROOT/vte-0.76.0-patched"
BUILD_DIR="$PATCHED_TREEDIR/build"

PRIS_TAR_SHA="2275d5958d89ca1a93488e066ee33987557db36f560a5dfd4101b1a2d4f150a4"

PATCH_0001="$PATCHES/0001-vte-0.76.0-public-snapshot-api.patch"
PATCH_0002="$PATCHES/0002-vte-0.76.0-internal-snapshot-declarations.patch"
PATCH_0003="$PATCHES/0003-vte-0.76.0-snapshot-serialization-and-restore.patch"
PATCH_0004="$PATCHES/0004-vte-0.76.0-public-snapshot-wrappers.patch"

log() { printf '\n[P0-G] %s\n' "$*"; }
die() { printf 'ERROR: %s\n' "$*"; exit 1; }

# ---------------------------------------------------------------------------
log "1/7 Verify pristine source provenance"
[[ -f "$TARBALL" ]] || die "missing $TARBALL"
ACT=$(sha256sum "$TARBALL" | awk '{print $1}')
[[ "$ACT" == "$PRIS_TAR_SHA" ]] || die "tarball SHA256 mismatch: $ACT"
log "  tarball SHA256 OK ($ACT)"

# (Re)create pristine git tree from the tarball.
rm -rf "$PRIS_TREEDIR"
mkdir -p "$PRIS_TREEDIR"
tar xzf "$TARBALL" -C "$PRIS_TREEDIR" --strip-components=1
git -C "$PRIS_TREEDIR" init -q -b main
git -C "$PRIS_TREEDIR" add -A
git -C "$PRIS_TREEDIR" -c user.email=pkg@local -c user.name="P0-G" commit -qm "pristine VTE 0.76.0"
[[ -z "$(git -C "$PRIS_TREEDIR" status --porcelain)" ]] || die "pristine tree not clean"
log "  pristine tree created & clean (commit $(git -C "$PRIS_TREEDIR" rev-parse --short HEAD))"

# ---------------------------------------------------------------------------
log "2/7 Verify patch series checksum"
( cd "$PATCHES" && sha256sum -c SHA256SUMS >/dev/null ) || die "patch SHA256SUMS mismatch"

# ---------------------------------------------------------------------------
log "3/7 Clone pristine -> patched working tree"
rm -rf "$PATCHED_TREEDIR"
git clone -q "$PRIS_TREEDIR" "$PATCHED_TREEDIR"
[[ -z "$(git -C "$PATCHED_TREEDIR" status --porcelain)" ]] || die "clone not pristine"

# ---------------------------------------------------------------------------
log "4/7 Apply patch series"
for p in "$PATCH_0001" "$PATCH_0002" "$PATCH_0003" "$PATCH_0004"; do
  git -C "$PATCHED_TREEDIR" apply --check "$p"
done
git -C "$PATCHED_TREEDIR" apply "$PATCH_0001" "$PATCH_0002" "$PATCH_0003" "$PATCH_0004"
log "  applied (note: 0004 emits one cosmetic 'blank line at EOF' warning)"

# Footprint gate: exactly 4 files, 724 insertions, 0 deletions, unchanged.
expected_numstat="632	0	src/vte.cc
12	0	src/vte/vteterminal.h
75	0	src/vtegtk.cc
5	0	src/vteinternal.hh"
got_numstat="$(git -C "$PATCHED_TREEDIR" diff --numstat)"
[[ "$got_numstat" == "$expected_numstat" ]] || die "patch footprint mismatch"
total_ins="$(git -C "$PATCHED_TREEDIR" diff --numstat | awk '{s+=$1} END{print s}')"
total_del="$(git -C "$PATCHED_TREEDIR" diff --numstat | awk '{s+=$2} END{print s}')"
[[ "$total_ins" == "724" && "$total_del" == "0" ]] || die "patch total mismatch"
log "  patch footprint matches P0-F (4 files, 724 insertions, 0 deletions)"

# ---------------------------------------------------------------------------
log "5/7 Fresh meson configure + build"
rm -rf "$BUILD_DIR"
meson setup "$BUILD_DIR" "$PATCHED_TREEDIR" \
  --buildtype=release --default-library=shared --prefix=/usr \
  -Dwarning_level=0 -Dgtk3=true -Dgtk4=true -Dgir=true -Dvapi=true -Da11y=true \
  -Dglade=true -Dgnutls=true -Dicu=true -Dfribidi=true -D_systemd=true \
  -Ddocs=false -Ddbg=false >/tmp/vte-g-meson-setup.log 2>&1 || \
  die "meson setup failed (see /tmp/vte-g-meson-setup.log)"
ninja -C "$BUILD_DIR" >/tmp/vte-g-ninja.log 2>&1 || \
  die "ninja build failed (see /tmp/vte-g-ninja.log)"

# ---------------------------------------------------------------------------
log "6/7 Verify exported API symbols + header exposure"
SO="$BUILD_DIR/src/libvte-2.91-gtk4.so.0"
symbols=$(nm -D --defined-only "$SO" || true)
for sym in vte_terminal_snapshot_capture vte_terminal_snapshot_restore vte_terminal_snapshot_has_pending_data; do
  grep -q "$sym" <<<"$symbols" || die "missing symbol $sym"
done
HDR="$PATCHED_TREEDIR/src/vte/vteterminal.h"
grep -q "vte_terminal_snapshot_capture" "$HDR" || die "header missing capture decl"
log "  symbols + header API verified"

# ---------------------------------------------------------------------------
log "7/7 Done"
cat <<EOF

VTE_MANIFEST
  vte_version          = 0.76.0
  patch_series_version = 1
  patch_sha256         = $(sha256sum "$PATCH_0001" | awk '{print $1}')
  source_sha256        = $PRIS_TAR_SHA
  patched_tree         = $PATCHED_TREEDIR
  build_dir            = $BUILD_DIR
  amd64_lib            = $SO

Remin must consume this dependency via:
  PKG_CONFIG_PATH=$BUILD_DIR/meson-uninstalled
  LD_LIBRARY_PATH=$BUILD_DIR/src
(instead of the system /usr/lib libvte)
EOF
