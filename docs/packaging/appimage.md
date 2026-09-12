# Packaging — AppImage

The AppImage is the **recommended portable** distribution: a single executable
that works on any modern 64-bit Linux desktop without installing system
dependencies.

## What it contains

- the `remin` binary
- the Remin VTE runtime (the **patched** libvteterminal — never the stock
  system lib)
- GTK/gtkmm/adwaita runtime libraries (FUSE-less self-extract convention)
- resources: styles, icons, `.desktop`

## Building it

The helper assembles an AppDir from the CMake install tree and packages it
with linuxdeploy-style tooling; the goal is reproducibility from a tagged
checkout:

```bash
./scripts/make-appimage.sh --version 1.0.0
# → Remin-1.0.0-linux-x86_64.AppImage
```

The image resolves the patched VTE at runtime from inside the AppDir, so
stock VTE never shadows the snapshot-capable library. This fully bundled
runtime follows the [Runtime & Linking Policy](../runtime-linking-policy.md).

## Smoke test (do before every release)

On a clean VM or container:

```bash
chmod +x Remin-1.0.0-linux-x86_64.AppImage
./Remin-1.0.0-linux-x86_64.AppImage
```

Create a split + run output, quit, relaunch — the workspace (incl. scrollback
snapshot) must come back.

## Artifacts

```text
Remin-1.0.0-linux-x86_64.AppImage
SHA256SUMS
```