# Building Remin

## Prerequisites

- Linux (64-bit), g++ (C++20) or another conforming C++20 compiler
- CMake ≥ 3.24
- OpenSSL development headers
- The GUI stack: GTK 4, gtkmm 4, VTE (see below), libadwaita, gtksourceview,
  and (optional) md4c.
- A **patched VTE build** — the snapshot/restore interface is not in stock
  VTE. See [the VTE extension](../patches/vte/README.md).

Vendored dependencies (no network): SQLite amalgamation and nlohmann/json live
under `third_party/`.

## One-shot setup

```bash
./scripts/setup_dev.sh      # installs dependency packages for the distro
./scripts/install_deps.sh   # distro-agnostic dependency helper
```

## Patched VTE first

```bash
./scripts/build-vte.sh      # pristine 0.76.0 → apply series → build → verify
```

Always point the build at the patched VTE tree (never the system lib):

```bash
export PKG_CONFIG_PATH=vte-0.76.0-patched/build/meson-uninstalled
export LD_LIBRARY_PATH=vte-0.76.0-patched/build/src
```

## Configure & build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

| Option | Default | Meaning |
|--------|---------|---------|
| `REMIN_BUILD_GUI`   | ON  | build the GUI frontend (GTK/VTE) |
| `REMIN_BUILD_TESTS` | ON  | build tests (`ctest`) |
| `REMIN_ENABLE_SANITIZERS` | OFF | ASan/UBSan build |

Standard build flavours:

```bash
build/          # Release
build-debug/    # RelWithDebInfo
build-asan/     # ASan + UBSan
```

## Requirements summary (reference)

- gtkmm 4.10.0 / GTK 4.14.5
- VTE 0.76 (patched)
- md4c 0.4.8
- Wayland + X11 supported; windowing stack comes from the distro.

## Install

```bash
cmake --install build --prefix /usr/local
```

Installs the binary, resources, icons, and the `.desktop` entry
(see [packaging](../packaging/debian.md)).