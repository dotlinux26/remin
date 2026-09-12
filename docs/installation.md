# Installation

Remin targets Linux (GTK4 + VTE, Wayland and X11). Three distribution methods
are provided; AppImage is the recommended portable option.

## AppImage (recommended)

```bash
chmod +x Remin-1.0.0-linux-x86_64.AppImage
./Remin-1.0.0-linux-x86_64.AppImage
```

The AppImage bundles the runtime, terminal backend, and Remin itself — no
system packages to install.

## Debian / Ubuntu package

```bash
sudo apt install ./remin_1.0.0_amd64.deb
remin gui
```

The package installs `/usr/bin/remin`, a desktop entry, icons, and AppStream
metadata (see [Debian packaging](packaging/debian.md)).

## Installing from source

See [Building](development/build.md) for full requirements and commands.

Requirements: a C++20 compiler, CMake ≥ 3.24, GTK/VTE development packages
(`gtkmm-4.0`, `vte-2.91-gtk4`, `libadwaita-1`, `librsvg2-dev`), and the
patched VTE build described under
[the VTE extension](patches/vte/README.md).

## Runtime requirements

- Linux with a 64-bit CPU
- A working `forkpty()` (standard on Linux)
- A Wayland or X11 display

## Checking the installation

```bash
remin gui
```

The app creates its data directory automatically; see
[Configuration](configuration.md) for where things live.